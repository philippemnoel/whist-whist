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

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>

#include <whist/core/whist.h>
#include <whist/core/whist_string.h>
#include <whist/network/network.h>
#include <whist/utils/aes.h>
#include <whist/utils/clock.h>
#include <whist/utils/os_utils.h>
#include <whist/utils/sysinfo.h>
#include <whist/logging/logging.h>
#include <whist/logging/log_statistic.h>
#include <whist/logging/error_monitor.h>
#include <whist/file/file_upload.h>
#include "whist_client.h"
#include "audio.h"
#include "client_utils.h"
#include "network.h"
#include "handle_frontend_events.h"
#include "sdl_utils.h"
#include "handle_server_message.h"
#include "video.h"
#include "sync_packets.h"
#include <whist/utils/color.h>
#include "renderer.h"
#include <whist/debug/debug_console.h>
#include "whist/utils/command_line.h"

#if OS_IS(OS_MACOS)
#include <mach-o/dyld.h>
#include <whist/utils/mac_utils.h>
#endif  // macOS

// N.B.: Please don't put globals here, since main.c won't be included when the testing suite is
// used instead

// Global state variables
extern volatile char binary_aes_private_key[16];
extern volatile char hex_aes_private_key[33];

static char* server_ip;
extern bool using_stun;

// Mouse motion state
extern MouseMotionAccumulation mouse_state;

// Whether a pinch is currently active - set in handle_frontend_events.c
extern bool active_pinch;

// Window resizing state
extern WhistMutex window_resize_mutex;  // protects pending_resize_message
extern WhistTimer window_resize_timer;
extern volatile bool pending_resize_message;

// The state of the client, i.e. whether it's connected to a server or not
extern volatile bool connected;

extern volatile bool client_exiting;

static char* new_tab_urls;

// Used to check if we need to call filepicker from main thread
extern bool upload_initiated;

// Defines
#define APP_PATH_MAXLEN 1023

// Command-line options.

COMMAND_LINE_STRING_OPTION(new_tab_urls, 'x', "new-tab-url", MAX_URL_LENGTH* MAX_NEW_TAB_URLS,
                           "URL to open in new tab.")
COMMAND_LINE_STRING_OPTION(server_ip, 0, "server-ip", IP_MAXLEN, "Set the server IP to connect to.")

static void sync_keyboard_state(WhistFrontend* frontend) {
    /*
        Synchronize the keyboard state of the host machine with
        that of the server by grabbing the host keyboard state and
        sending a packet to the server.
    */

    // Set keyboard state initialized to null
    WhistClientMessage wcmsg = {0};

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
    wcmsg.keyboard_state.state[FK_LGUI] = !!(mod_state & KMOD_LGUI);
    wcmsg.keyboard_state.state[FK_RGUI] = !!(mod_state & KMOD_RGUI);
    wcmsg.keyboard_state.caps_lock = !!(mod_state & KMOD_CAPS);
    wcmsg.keyboard_state.num_lock = !!(mod_state & KMOD_NUM);
    wcmsg.keyboard_state.active_pinch = active_pinch;

    // Grabs the keyboard layout as well
    wcmsg.keyboard_state.layout = get_keyboard_layout();

    send_wcmsg(&wcmsg);
}

static void handle_single_icon_launch_client_app(int argc, const char* argv[]) {
    // This function handles someone clicking the protocol icon as a means of starting Whist by
    // instead launching the client app
    // If argc == 1 (no args passed), then check if client app path exists
    // and try to launch.
    //     This should be done first because `execl` won't cleanup any allocated resources.
    // Mac apps also sometimes pass an argument like -psn_0_2126343 to the executable.
#if OS_IN(OS_WIN32 | OS_MACOS)
    if (argc == 1 || (argc == 2 && !strncmp(argv[1], "-psn_", 5))) {
        // hopefully the app path is not more than 1024 chars long
        char client_app_path[APP_PATH_MAXLEN + 1];
        memset(client_app_path, 0, APP_PATH_MAXLEN + 1);

#if OS_IS(OS_WIN32)
        const char* relative_client_app_path = "/../../Whist.exe";
        char dir_split_char = '\\';
        size_t protocol_path_len;

#elif OS_IS(OS_MACOS)
        // This executable is located at
        //    Whist.app/Contents/MacOS/WhistClient
        // We want to reference client app at Whist.app/Contents/MacOS/WhistLauncher
        const char* relative_client_app_path = "/WhistLauncher";
        char dir_split_char = '/';
        int protocol_path_len;
#endif

        int relative_client_app_path_len = (int)strlen(relative_client_app_path);
        if (relative_client_app_path_len < APP_PATH_MAXLEN + 1) {
#if OS_IS(OS_WIN32)
            int max_protocol_path_len = APP_PATH_MAXLEN + 1 - relative_client_app_path_len - 1;
            // Get the path of the current executable
            int path_read_size = GetModuleFileNameA(NULL, client_app_path, max_protocol_path_len);
            if (path_read_size > 0 && path_read_size < max_protocol_path_len) {
#elif OS_IS(OS_MACOS)
            uint32_t max_protocol_path_len =
                (uint32_t)(APP_PATH_MAXLEN + 1 - relative_client_app_path_len - 1);
            // Get the path of the current executable
            if (_NSGetExecutablePath(client_app_path, &max_protocol_path_len) == 0) {
#endif
                // Get directory from executable path
                char* last_dir_slash_ptr = strrchr(client_app_path, dir_split_char);
                if (last_dir_slash_ptr) {
                    *last_dir_slash_ptr = '\0';
                }

                // Get the relative path to the client app from the current executable location
                protocol_path_len = strlen(client_app_path);
                if (safe_strncpy(client_app_path + protocol_path_len, relative_client_app_path,
                                 relative_client_app_path_len + 1)) {
                    LOG_INFO("Client app path: %s", client_app_path);
#if OS_IS(OS_WIN32)
                    // If `_execl` fails, then the program proceeds, else defers to client app
                    if (_execl(client_app_path, "Whist.exe", NULL) < 0) {
                        LOG_INFO("_execl errno: %d errstr: %s", errno, strerror(errno));
                    }
#elif OS_IS(OS_MACOS)
                    // If `execl` fails, then the program proceeds, else defers to client app
                    if (execl(client_app_path, "Whist", NULL) < 0) {
                        LOG_INFO("execl errno: %d errstr: %s", errno, strerror(errno));
                    }
#endif
                }
            }
        }
    }
#endif

    // END OF CHECKING IF IN PROD MODE AND TRYING TO LAUNCH CLIENT APP IF NO ARGS
}

static void initiate_file_upload(void) {
    /*
        Pull up system file dialog and set selection as transfering file
    */

    const char* ns_picked_file_path = whist_file_upload_get_picked_file();
    if (ns_picked_file_path) {
        file_synchronizer_set_file_reading_basic_metadata(ns_picked_file_path,
                                                          FILE_TRANSFER_SERVER_UPLOAD, NULL);
        file_synchronizer_end_type_group(FILE_TRANSFER_SERVER_UPLOAD);
        LOG_INFO("Upload has been initiated");
    } else {
        LOG_INFO("No file selected");
        WhistClientMessage wcmsg = {0};
        wcmsg.type = MESSAGE_FILE_UPLOAD_CANCEL;
        send_wcmsg(&wcmsg);
    }
    upload_initiated = false;
}

static void send_new_tab_urls_if_needed(WhistFrontend* frontend) {
    // Send any new URL to the server
    if (new_tab_urls) {
        LOG_INFO("Sending message to open URL in new tab %s", new_tab_urls);
        const size_t urls_length = strlen((const char*)new_tab_urls);
        const size_t wcmsg_size = sizeof(WhistClientMessage) + urls_length + 1;
        WhistClientMessage* wcmsg = safe_malloc(wcmsg_size);
        memset(wcmsg, 0, sizeof(*wcmsg));
        wcmsg->type = MESSAGE_OPEN_URL;
        memcpy(&wcmsg->urls_to_open, (const char*)new_tab_urls, urls_length + 1);
        send_wcmsg(wcmsg);
        free(wcmsg);

        free((void*)new_tab_urls);
        new_tab_urls = NULL;

        // Unmimimize the window if needed
        if (!whist_frontend_is_window_visible(frontend)) {
            whist_frontend_restore_window(frontend);
        }
    }
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

    WhistTimer keyboard_sync_timer, monitor_change_timer, new_tab_urls_timer,
        cpu_usage_statistics_timer;
    start_timer(&keyboard_sync_timer);
    start_timer(&monitor_change_timer);
    start_timer(&new_tab_urls_timer);
    start_timer(&cpu_usage_statistics_timer);

    while (connected && !client_exiting) {
        // This should be called BEFORE the call to read_piped_arguments,
        // otherwise one URL may get lost.
        send_new_tab_urls_if_needed(frontend);

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

        if (get_timer(&new_tab_urls_timer) * MS_IN_SECOND > 50.0) {
            int piped_args_ret = read_piped_arguments(true);
            switch (piped_args_ret) {
                case -2: {
                    // Fatal reading pipe or similar
                    LOG_ERROR("Failed to read piped arguments -- exiting");
                    return WHIST_EXIT_FAILURE;
                }
                case -1: {
                    // Invalid arguments
                    LOG_ERROR("Invalid piped arguments -- exiting");
                    return WHIST_EXIT_CLI;
                }
                case 1: {
                    // Arguments prompt graceful exit
                    LOG_INFO("Piped argument prompts graceful exit");
                    client_exiting = true;
                    return WHIST_EXIT_SUCCESS;
                    break;
                }
                default: {
                    // Success, so nothing to do
                    break;
                }
            }
            start_timer(&new_tab_urls_timer);
        }

        if (get_timer(&keyboard_sync_timer) * MS_IN_SECOND > 50.0) {
            sync_keyboard_state(frontend);
            start_timer(&keyboard_sync_timer);
        }

        if (get_timer(&monitor_change_timer) * MS_IN_SECOND > 10) {
            static int cached_display_index = -1;
            int current_display_index;
            if (whist_frontend_get_window_display_index(frontend, &current_display_index) ==
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
            initiate_file_upload();
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
    whist_frontend_get_window_pixel_size(frontend, &initial_width, &initial_height);
    *renderer = init_renderer(frontend, initial_width, initial_height);
    init_clipboard_synchronizer(true);
    init_file_synchronizer(FILE_TRANSFER_CLIENT_DOWNLOAD);
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
    init_packet_synchronizers(renderer);

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

int whist_client_main(int argc, const char* argv[]) {
    int ret = client_parse_args(argc, argv);
    if (ret == -1) {
        // invalid usage
        return WHIST_EXIT_CLI;
    } else if (ret == 1) {
        // --help or --version
        return WHIST_EXIT_SUCCESS;
    }

    whist_init_subsystems();
    // (internally, only happend for debug builds)
    init_debug_console();
    whist_init_statistic_logger(STATISTICS_FREQUENCY_IN_SEC);
    handle_single_icon_launch_client_app(argc, argv);

    srand(rand() * (unsigned int)time(NULL) + rand());

    LOG_INFO("Client protocol started...");

    // Initialize logger error monitor
    whist_error_monitor_initialize(true);

    print_system_info();
    LOG_INFO("Whist client revision %s", whist_git_revision());

    client_exiting = false;
    WhistExitCode exit_code = WHIST_EXIT_SUCCESS;

    WhistFrontend* frontend = create_frontend();

    // Read in any piped arguments. If the arguments are bad, then skip to the destruction phase
    switch (read_piped_arguments(false)) {
        case -2: {
            // Fatal reading pipe or similar
            LOG_ERROR("Failed to read piped arguments -- exiting");
            exit_code = WHIST_EXIT_FAILURE;
            break;
        }
        case -1: {
            // Invalid arguments
            LOG_ERROR("Invalid piped arguments -- exiting");
            exit_code = WHIST_EXIT_CLI;
            break;
        }
        case 1: {
            // Arguments prompt graceful exit
            LOG_INFO("Piped argument prompts graceful exit");
            exit_code = WHIST_EXIT_SUCCESS;
            client_exiting = true;
            break;
        }
        default: {
            // Success, so nothing to do
            break;
        }
    }

    bool failed_to_connect = false;

    while (!client_exiting && (exit_code == WHIST_EXIT_SUCCESS)) {
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
        if (client_exiting || (exit_code != WHIST_EXIT_SUCCESS)) {
            // We actually exited the main loop, so signal the server to quit
            LOG_INFO("Disconnecting from server...");
            send_server_quit_messages(3);
        } else {
            // We exited due to a disconnect
            LOG_INFO("Reconnecting to server...");
        }

        post_connection_cleanup(renderer);
        connected = false;
    }

    switch (exit_code) {
        case WHIST_EXIT_SUCCESS: {
            break;
        }
        case WHIST_EXIT_FAILURE: {
            LOG_ERROR("Failure in main loop! Exiting with code WHIST_EXIT_FAILURE");
            break;
        }
        case WHIST_EXIT_CLI: {
            // If we're in prod or staging, CLI errors are serious errors since we should always
            // be passing the correct arguments, so we log them as errors to report to Sentry.
            const char* environment = get_error_monitor_environment();
            if (strcmp(environment, "prod") == 0 || strcmp(environment, "staging") == 0) {
                LOG_ERROR("Failure in main loop! Exiting with code WHIST_EXIT_CLI");
            } else {
                // In dev or localdev, CLI errors will happen a lot as engineers develop, so we only
                // log them as warnings
                LOG_WARNING("Failure in main loop! Exiting with code WHIST_EXIT_CLI");
            }
            break;
        }
        default: {
            LOG_ERROR("Failure in main loop! Unhandled exit with unknown exit code: %d", exit_code);
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

    // Destroy any resources being used by the client
    LOG_INFO("Closing Client...");

    destroy_frontend(frontend);

    LOG_INFO("Client frontend has exited...");

    destroy_statistic_logger();

    destroy_logger();

    LOG_INFO("Logger has exited...");

    // We must call this after destroying the logger so that all
    // error monitor breadcrumbs and events can finish being reported
    // before we close the error monitor.
    whist_error_monitor_shutdown();

    return exit_code;
}
