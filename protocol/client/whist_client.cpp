/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file main.c
 * @brief This file contains the main code that runs a Whist client on a
 *        Windows, MacOS or Linux Ubuntu computer.
============================
Usage
============================

Follow main() to see a Whist video streaming client being created and creating
its threads.
*/

/*
============================
Includes
============================
*/

#if defined(_MSC_VER)
#pragma warning(disable : 4815)  // Disable MSVC warning about 0-size array in wcmsg
#endif

#include <whist/core/whist.h>

extern "C" {
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>

#include "network.h"
#include <whist/core/whist_string.h>
#include <whist/network/network.h>
#include <whist/utils/aes.h>
#include <whist/utils/clock.h>
#include <whist/utils/os_utils.h>
#include <whist/utils/sysinfo.h>
#include <whist/logging/logging.h>
#include <whist/logging/log_statistic.h>
#include <whist/logging/error_monitor.h>
#include "whist_client.h"
#include "audio.h"
#include "client_utils.h"
#include "handle_frontend_events.h"
#include "sdl_utils.h"
#include "handle_server_message.h"
#include "video.h"
#include "sync_packets.h"
#include <whist/utils/color.h>
#include "renderer.h"
#include <whist/debug/debug_console.h>
#include "whist/debug/plotter.h"
#include "whist/utils/command_line.h"
#include "frontend/virtual/interface.h"

#if OS_IS(OS_MACOS)
#include <mach-o/dyld.h>
#include <whist/utils/mac_utils.h>
#endif  // macOS
}

// N.B.: Please don't put globals here, since main.c won't be included when the testing suite is
// used instead

// Global state variables
extern volatile char binary_aes_private_key[16];
extern volatile char hex_aes_private_key[33];

static char* server_ip;
extern "C" {
extern bool using_stun;

// Window resizing state
extern WhistMutex window_resize_mutex;  // protects pending_resize_message
extern WhistTimer window_resize_timer;
}

// Whether a pinch is currently active - set in handle_frontend_events.c
extern std::atomic<bool> active_pinch;

extern std::atomic<bool> client_exiting;

// Used to check if we need to call filepicker from main thread
extern std::atomic<bool> upload_initiated;

// Mouse motion state
extern MouseMotionAccumulation mouse_state;
extern volatile bool pending_resize_message;

// The state of the client, i.e. whether it's connected to a server or not
extern std::atomic<bool> connected;

// Defines
#define APP_PATH_MAXLEN 1023

// Command-line options.

COMMAND_LINE_STRING_OPTION(server_ip, 0, "server-ip", IP_MAXLEN, "Set the server IP to connect to.")

static bool wait_dynamic_arguments;
COMMAND_LINE_BOOL_OPTION(wait_dynamic_arguments, 'r', "dynamic-arguments",
                         "Wait for dynamic arguments to be passed in via the frontend. SDL uses "
                         "`key` or `key?value` via stdin.")

static void sync_keyboard_state(WhistFrontend* frontend) {
    /*
        Synchronize the keyboard state of the host machine with
        that of the server by grabbing the host keyboard state and
        sending a packet to the server.
    */

    // Set keyboard state initialized to null
    WhistClientMessage wcmsg;
    memset(&wcmsg, 0, sizeof(wcmsg));

    wcmsg.type = MESSAGE_KEYBOARD_STATE;

    int num_keys;
    const uint8_t* key_state;
    int mod_state;

    whist_frontend_get_keyboard_state(frontend, &key_state, &num_keys, &mod_state);

    wcmsg.keyboard_state.num_keycodes = (short)min(num_keys, KEYCODE_UPPERBOUND);

    // Copy keyboard state, but using scancodes of the keys in the current keyboard layout.
    // Must convert to/from the name of the key so SDL returns the scancode for the key in the
    // current layout rather than the scancode for the physical key.
    for (int i = 0; i < wcmsg.keyboard_state.num_keycodes; i++) {
        if (key_state[i]) {
            if (0 <= i && i < (int)sizeof(wcmsg.keyboard_state.state)) {
                wcmsg.keyboard_state.state[i] = 1;
            }
        }
    }

    // Handle keys and state not tracked by key_state.
    wcmsg.keyboard_state.state[FK_LGUI] = !!(mod_state & MOD_LGUI);
    wcmsg.keyboard_state.state[FK_RGUI] = !!(mod_state & MOD_RGUI);
    wcmsg.keyboard_state.caps_lock = !!(mod_state & MOD_CAPS);
    wcmsg.keyboard_state.num_lock = !!(mod_state & MOD_NUM);
    wcmsg.keyboard_state.active_pinch = active_pinch;

    // Grabs the keyboard layout as well
    wcmsg.keyboard_state.layout = get_keyboard_layout();

    send_wcmsg(&wcmsg);
}

static void initiate_file_upload(WhistFrontend* frontend) {
    /*
        Pull up system file dialog and set selection as transfering file
    */

    const char* ns_picked_file_path = whist_frontend_get_chosen_file(frontend);
    if (ns_picked_file_path) {
        file_synchronizer_set_file_reading_basic_metadata(ns_picked_file_path,
                                                          FILE_TRANSFER_SERVER_UPLOAD, NULL);
        file_synchronizer_end_type_group(FILE_TRANSFER_SERVER_UPLOAD);
        LOG_INFO("Upload has been initiated");
    } else {
        LOG_INFO("No file selected");
        WhistClientMessage wcmsg;
        memset(&wcmsg, 0, sizeof(wcmsg));
        wcmsg.type = MESSAGE_FILE_UPLOAD_CANCEL;
        send_wcmsg(&wcmsg);
    }
    upload_initiated = false;
}

#define MAX_INIT_CONNECTION_ATTEMPTS (6)

/**
 * @brief Initialize the connection to the server, retrying as many times as desired.
 *
 * @returns WHIST_SUCCESS if the connection was successful, WHIST_ERROR_UNKNOWN otherwise.
 *
 * @note This function is responsible for all retry logic -- the caller can assume that if this
 *       function fails, no amount of retries will succeed.
 */
static WhistStatus initialize_connection(void) {
    // Connection attempt loop
    for (int retry_attempt = 0; retry_attempt < MAX_INIT_CONNECTION_ATTEMPTS && !client_exiting;
         ++retry_attempt) {
        WhistTimer handshake_time;
        start_timer(&handshake_time);
        if (connect_to_server(server_ip, using_stun) == 0) {
            // Success -- log time to metrics and developer logs
            double connect_to_server_time = get_timer(&handshake_time);
            LOG_INFO("Server connection took %f ms", connect_to_server_time);
            LOG_METRIC("\"HANDSHAKE_CONNECT_TO_SERVER_TIME\" : %f", connect_to_server_time);
            return WHIST_SUCCESS;
        }

        LOG_WARNING("Failed to connect to server, retrying...");
        // TODO: maybe we want something better than sleeping 1 second?
        whist_sleep(1000);
    }

    return WHIST_ERROR_UNKNOWN;
}

/**
 * @brief Loop through the main Whist client loop until we either exit or the server disconnects.
 *
 * @param frontend      The frontend to target for UI updates.
 * @param renderer      The video/audio renderers to pump.
 *
 * @returns             The appropriate WHIST_EXIT_CODE based on how we exited.
 * @todo                Use a more sensible return type/scheme.
 */
static WhistExitCode run_main_loop(WhistFrontend* frontend, WhistRenderer* renderer) {
    LOG_INFO("Entering main event loop...");

    WhistTimer keyboard_sync_timer, monitor_change_timer, cpu_usage_statistics_timer;
    start_timer(&keyboard_sync_timer);
    start_timer(&monitor_change_timer);
    start_timer(&cpu_usage_statistics_timer);

    while (connected && !client_exiting) {
        // Update any pending SDL tasks
        sdl_update_pending_tasks(frontend);

        // Log cpu usage once per second. Only enable this when LOG_CPU_USAGE flag is set
        // because getting cpu usage statistics is expensive.

        double cpu_timer_time_elapsed = 0;
        if (LOG_CPU_USAGE &&
            ((cpu_timer_time_elapsed = get_timer(&cpu_usage_statistics_timer)) > 1)) {
            double cpu_usage = get_cpu_usage(cpu_timer_time_elapsed);
            if (cpu_usage != -1) {
                log_double_statistic(CLIENT_CPU_USAGE, cpu_usage);
            }
            start_timer(&cpu_usage_statistics_timer);
        }

        // This might hang for a long time
        // The 50ms timeout is chosen to match other checks in this
        // loop, though when video is running it will almost always
        // be interrupted before it reaches the timeout.
        if (!handle_frontend_events(frontend, 50)) {
            // unable to handle event
            return WHIST_EXIT_FAILURE;
        }

        if (get_timer(&keyboard_sync_timer) * MS_IN_SECOND > 50.0) {
            sync_keyboard_state(frontend);
            start_timer(&keyboard_sync_timer);
        }

        if (get_timer(&monitor_change_timer) * MS_IN_SECOND > 10) {
            static int cached_display_index = -1;
            int current_display_index;
            if (whist_frontend_get_window_display_index(frontend, 0, &current_display_index) ==
                WHIST_SUCCESS) {
                if (cached_display_index != current_display_index) {
                    if (cached_display_index) {
                        // Update DPI to new monitor
                        send_message_dimensions(frontend);
                    }
                    cached_display_index = current_display_index;
                }
            } else {
                LOG_ERROR("Failed to get display index");
            }

            start_timer(&monitor_change_timer);
        }

        // Check if file upload has been initiated and initiated selection dialog if so
        if (upload_initiated) {
            initiate_file_upload(frontend);
        }
    }

    return WHIST_EXIT_SUCCESS;
}

/**
 * @brief Initialize the renderer and synchronizer threads which must exist before connection.
 *
 * @param frontend      The frontend to target for UI updates.
 * @param renderer      A pointer to be filled in with a reference to the video/audio renderer.
 */
static void pre_connection_setup(WhistFrontend* frontend, WhistRenderer** renderer) {
    FATAL_ASSERT(renderer != NULL);
    int initial_width = 0, initial_height = 0;
    whist_frontend_get_window_pixel_size(frontend, 0, &initial_width, &initial_height);
    *renderer = init_renderer(frontend, initial_width, initial_height);
    init_clipboard_synchronizer(true);
    init_file_synchronizer(FILE_TRANSFER_DEFAULT);
}

/**
 * @brief Initialize synchronization threads and state which must be created after a connection is
 *        started.
 *
 * @param frontend      The frontend to target for UI updates.
 * @param renderer      A reference to the video/audio renderer.
 */
static void on_connection_setup(WhistFrontend* frontend, WhistRenderer* renderer) {
    start_timer(&window_resize_timer);
    window_resize_mutex = whist_create_mutex();

    // Create tcp/udp handlers and give them the renderer so they can route packets properly
    init_packet_synchronizers(frontend, renderer);

    // Sometimes, resize events aren't generated during startup, so we manually call this to
    // initialize internal values to actual dimensions
    sdl_renderer_resize_window(frontend, -1, -1);
    send_message_dimensions(frontend);
}

/**
 * @brief Clean up the renderer, synchronization threads, and state which must be destroyed after a
 *        connection is closed.
 *
 * @param renderer      A reference to the video/audio renderer.
 */
static void post_connection_cleanup(WhistRenderer* renderer) {
    // End communication with server
    destroy_packet_synchronizers();

    // Destroy the renderer, which may have been viewing into the packet buffer
    destroy_renderer(renderer);

    // Destroy networking peripherals
    destroy_file_synchronizer();
    destroy_clipboard_synchronizer();

    // Close the connections, destroying the packet buffers
    close_connections();
}

#if OS_IS(OS_LINUX) || OS_IS(OS_MACOS)
// signal handler for graceful quit of program
void sig_handler(int sig_num) {
    /*
        When the client receives a SIGTERM or SIGINT, gracefully exit.
    */
    if (sig_num == SIGTERM || sig_num == SIGINT) {
        client_exiting = true;
    }
}
#endif

int whist_client_main(int argc, const char* argv[]) {
#if USING_MLOCK
    // Override macOS system malloc with our mimalloc
    init_whist_mlock();
#endif
    int ret = client_parse_args(argc, argv);
    if (ret == -1) {
        // invalid usage
        return WHIST_EXIT_CLI;
    } else if (ret == 1) {
        // --help or --version
        return WHIST_EXIT_SUCCESS;
    }

    whist_init_subsystems();

    whist_set_thread_priority(WHIST_THREAD_PRIORITY_REALTIME);

    if (CLIENT_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT) {
        // init as stream plotter, by passing a filename on init
        whist_plotter_init(CLIENT_SIDE_DEFAULT_EXPORT_FILE);
    }

    // (internally, only happens for debug builds)
    init_debug_console();
    whist_init_statistic_logger(STATISTICS_FREQUENCY_IN_SEC);

    srand(rand() * (unsigned int)time(NULL) + rand());

    LOG_INFO("Client protocol started...");

#if CLIENT_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT && (OS_IS(OS_LINUX) || OS_IS(OS_MACOS))
    // if CLIENT_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT enabled, insert handler
    // for grace quit for ctrl-c and kill, so that no plotter data will be lost on quit
    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        LOG_FATAL("Establishing SIGTERM signal handler failed.");
    }
    if (sigaction(SIGINT, &sa, NULL) == -1) {
        LOG_FATAL("Establishing SIGINT signal handler failed.");
    }
#endif

    // Initialize logger error monitor
    whist_error_monitor_initialize(true);

    LOG_INFO("Whist client revision %s", whist_git_revision());
    WhistThread system_info_thread = print_system_info();

    client_exiting = false;
    WhistExitCode exit_code = WHIST_EXIT_SUCCESS;

    WhistFrontend* frontend = create_frontend();

    if (wait_dynamic_arguments) {
        ret = client_handle_dynamic_args(frontend);
        if (ret == -1) {
            LOG_ERROR("Failed to handle dynamic arguments in this environment -- exiting");
            exit_code = WHIST_EXIT_FAILURE;
        } else if (ret == 1) {
            LOG_INFO("Dynamic arguments prompt graceful exit");
            exit_code = WHIST_EXIT_SUCCESS;
            client_exiting = true;
        }
    }

#define CLIENT_SHOULD_CONTINUE() (!client_exiting && (exit_code == WHIST_EXIT_SUCCESS))

    if (CLIENT_SHOULD_CONTINUE() && !client_args_valid()) {
        LOG_ERROR("Invalid client arguments -- exiting");
        exit_code = WHIST_EXIT_CLI;
    }

    bool failed_to_connect = false;

    while (CLIENT_SHOULD_CONTINUE()) {
        WhistRenderer* renderer;
        pre_connection_setup(frontend, &renderer);

        if (initialize_connection() != WHIST_SUCCESS) {
            failed_to_connect = true;
            break;
        }
        connected = true;
        on_connection_setup(frontend, renderer);

        // TODO: Maybe return something more informative than exit_code, like a WhistStatus
        exit_code = run_main_loop(frontend, renderer);
        if (!CLIENT_SHOULD_CONTINUE()) {
            // We actually exited the main loop, so signal the server to quit
            LOG_INFO("Disconnecting from server...");
            send_server_quit_messages(3);
        } else {
            // We exited due to a disconnect
            LOG_WARNING("Connection lost: Reconnecting to server...");
            whist_frontend_send_error_notification(frontend, WHIST_DISCONNECT_ERROR);
        }

        post_connection_cleanup(renderer);
        connected = false;
    }

    switch (exit_code) {
        case WHIST_EXIT_SUCCESS: {
            break;
        }
        case WHIST_EXIT_FAILURE: {
            LOG_ERROR("Exiting with code WHIST_EXIT_FAILURE");
            break;
        }
        case WHIST_EXIT_CLI: {
            // If we're in prod or staging, CLI errors are serious errors since we should always
            // be passing the correct arguments, so we log them as errors to report to Sentry.
            const char* environment = get_error_monitor_environment();
            if (environment &&
                (strcmp(environment, "prod") == 0 || strcmp(environment, "staging") == 0)) {
                LOG_ERROR("Exiting with code WHIST_EXIT_CLI");
            } else {
                // In dev or localdev, CLI errors will happen a lot as engineers develop, so we only
                // log them as warnings
                LOG_WARNING("Exiting with code WHIST_EXIT_CLI");
            }
            break;
        }
        default: {
            LOG_ERROR("Unhandled exit with unknown exit code: %d", exit_code);
            break;
        }
    }

    if (failed_to_connect) {
        // we make this a LOG_WARNING so it doesn't clog up Sentry, as this
        // error happens periodically but we have recovery systems in place
        // for streaming interruption/connection loss
        LOG_WARNING("Failed to connect after %d attempts!", MAX_INIT_CONNECTION_ATTEMPTS);
        exit_code = WHIST_EXIT_FAILURE;
    }

    // Wait on system info thread before destroying logger
    whist_wait_thread(system_info_thread, NULL);

    // Destroy any resources being used by the client
    LOG_INFO("Closing Client...");

    destroy_frontend(frontend);

    LOG_INFO("Client frontend has exited...");

    destroy_statistic_logger();

    // This is safe to call even if the plotter has not been initialized.
    whist_plotter_destroy();

    destroy_logger();

    LOG_INFO("Logger has exited...");

    // We must call this after destroying the logger so that all
    // error monitor breadcrumbs and events can finish being reported
    // before we close the error monitor.
    whist_error_monitor_shutdown();

    return exit_code;
}
