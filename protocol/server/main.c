/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file main.c
 * @brief This file contains the main code that runs a Whist server on a
 *        Windows or Linux Ubuntu computer.
============================
Usage
============================

Follow main() to see a Whist video streaming server being created and creating
its threads.
*/

/*
============================
Includes
============================
*/

#include "state.h"
#include "parse_args.h"
#include "handle_client_message.h"
#include "dbus.h"
#include "whist/debug/plotter.h"
#include "whist/utils/command_line.h"
#include "whist/utils/clock.h"
#include "gpu_commands.h"

/*
============================
Defines
============================
*/

#if OS_IS(OS_WIN32)
#define DEFAULT_INPUT_TYPE WHIST_INPUT_DEVICE_WIN32
#define DEFAULT_CAPTURE_TYPE NVIDIA_DEVICE
#else
#define DEFAULT_INPUT_TYPE WHIST_INPUT_DEVICE_UINPUT
#define DEFAULT_CAPTURE_TYPE NVX11_DEVICE
#endif

/*
============================
Globals
============================
*/

// TODO: Remove ugly global state
WhistServerState server_state;

// TODO: Remove, needed by udp.c temporarily
volatile bool connected = false;

static bool enable_gpu_command_streaming = false;
COMMAND_LINE_BOOL_OPTION(enable_gpu_command_streaming, 0, "enable-gpu-command-streaming",
                         "Set whether to enable GPU command(Skia) streaming")

/*
============================
Private Functions
============================
*/

void graceful_exit(WhistServerState* state);
#if OS_IS(OS_LINUX)
int xioerror_handler(Display* d);
void sig_handler(int sig_num);
#endif
int main(int argc, char* argv[]);

/*
============================
Private Function Implementations
============================
*/

void graceful_exit(WhistServerState* state) {
    /*
        Quit clients gracefully and allow server to exit.
    */

    // Mark server as an exiting state
    state->exiting = true;

    // If the client happens to be active, send a quit message
    ClientLock* client_lock = client_active_trylock(state->client);
    if (client_lock != NULL) {
        // Broadcast client quit message
        WhistServerMessage wsmsg_quit = {0};
        wsmsg_quit.type = SMESSAGE_QUIT;
        if (send_packet(&state->client->udp_context, PACKET_MESSAGE, &wsmsg_quit,
                        sizeof(WhistServerMessage), 1, false) < 0) {
            LOG_ERROR("Failed to send QUIT message");
        } else {
            LOG_INFO("Send QUIT message");
        }
        client_active_unlock(client_lock);
    }
}

#if OS_IS(OS_LINUX)
int xioerror_handler(Display* d) {
    /*
        When X display is destroyed, intercept XIOError in order to
        quit clients.

        For use as arg for XSetIOErrorHandler - this is fatal and thus
        any program exit handling that would normally be expected to
        be handled in another thread must be explicitly handled here.
        Right now, we handle:
            * SendContainerDestroyMessage
            * quitClients
    */

    graceful_exit(&server_state);

    return 0;
}

void sig_handler(int sig_num) {
    /*
        When the server receives a SIGTERM or SIGINT, gracefully exit.
    */
    if (sig_num == SIGTERM || sig_num == SIGINT) {
        graceful_exit(&server_state);
    }
}
#endif

// Gets all pending Whist UDP messages
static void get_whist_udp_client_messages(WhistServerState* state) {
    WhistClientMessage wcmsg;
    size_t wcmsg_size;
    // If received a UDP message
    if (try_get_next_message_udp(state->client, &wcmsg, &wcmsg_size) == 0 && wcmsg_size != 0) {
        double time_before_handling, time_after_handling;
        if (PLOT_SERVER_UDP_MESSAGE_HANDING) {
            time_before_handling = get_timestamp_sec();
        }

        handle_client_message(state, &wcmsg);

        if (PLOT_SERVER_UDP_MESSAGE_HANDING) {
            time_after_handling = get_timestamp_sec();
            char buf[30];
            snprintf(buf, sizeof(buf), "handling of wcmsg_%d", (int)wcmsg.type);
            whist_plotter_insert_sample(
                buf, get_timestamp_sec(),
                (time_after_handling - time_before_handling) * MS_IN_SECOND);
        }
    }
}

// Gets all pending Whist TCP messages
static bool get_whist_tcp_client_messages(WhistServerState* state) {
    bool ret = false;

    WhistPacket* tcp_packet = NULL;
    try_get_next_message_tcp(state->client, &tcp_packet);
    // If we get a TCP client message, handle it
    if (tcp_packet) {
        WhistClientMessage* wcmsg = (WhistClientMessage*)tcp_packet->data;
        LOG_INFO("TCP Packet type: %d", wcmsg->type);
        handle_client_message(state, wcmsg);
        free_packet(&state->client->tcp_context, tcp_packet);
        ret = true;
    }

    return ret;
}

static void create_and_send_tcp_wmsg(WhistServerMessageType message_type, char* payload) {
    /*
        Create and send a TCP wmsg according to the given payload, and then
        deallocate once finished.

        Arguments:
            message_type (WhistServerMessageType): the type of the TCP message to be sent
            payload (char*): the payload of the TCP message
    */

    int data_size = 0;
    char* copy_location = NULL;
    int type_size = 0;

    switch (message_type) {
        case SMESSAGE_CLIPBOARD: {
            data_size = ((ClipboardData*)payload)->size;
            type_size = sizeof(ClipboardData);
            break;
        }
        case SMESSAGE_FILE_METADATA: {
            data_size = (int)((FileMetadata*)payload)->filename_len;
            type_size = sizeof(FileMetadata);
            break;
        }
        case SMESSAGE_FILE_DATA: {
            data_size = (int)((FileData*)payload)->size;
            type_size = sizeof(FileData);
            break;
        }
        default: {
            LOG_ERROR("Not a valid server wmsg type");
            return;
        }
    }

    // Alloc wmsg
    WhistServerMessage* wmsg_tcp = allocate_region(sizeof(WhistServerMessage) + data_size);

    switch (message_type) {
        case SMESSAGE_CLIPBOARD: {
            copy_location = (char*)&wmsg_tcp->clipboard;
            break;
        }
        case SMESSAGE_FILE_METADATA: {
            copy_location = (char*)&wmsg_tcp->file_metadata;
            break;
        }
        case SMESSAGE_FILE_DATA: {
            copy_location = (char*)&wmsg_tcp->file;
            break;
        }
        default: {
            // This is unreachable code, only here for consistency's sake
            deallocate_region(wmsg_tcp);
            LOG_ERROR("Not a valid server wmsg type");
            return;
        }
    }

    // Build wmsg
    // Init header to 0 to prevent sending uninitialized packets over the network
    memset(wmsg_tcp, 0, sizeof(*wmsg_tcp));
    wmsg_tcp->type = message_type;
    memcpy(copy_location, payload, type_size + data_size);
    // Send wmsg
    if (broadcast_tcp_packet(server_state.client, PACKET_MESSAGE, (uint8_t*)wmsg_tcp,
                             sizeof(WhistServerMessage) + data_size) < 0) {
        LOG_WARNING("Failed to broadcast server message of type %d.", message_type);
    }
    // Free wmsg
    deallocate_region(wmsg_tcp);
}

static void send_complete_file_drop_message(FileTransferType transfer_type) {
    /*
        Create and send a file drop complete message

        Arguments:
            transfer_type (FileTransferType): group end type
    */

    // Alloc and build wsmsg
    WhistServerMessage wsmsg = {
        .type = SMESSAGE_FILE_GROUP_END,
        .file_group_end.transfer_type = transfer_type,
    };

    // Send wsmsg
    if (broadcast_tcp_packet(server_state.client, PACKET_MESSAGE, (uint8_t*)(&wsmsg),
                             sizeof(WhistServerMessage)) < 0) {
        LOG_WARNING("Failed to broadcast server message of type SMESSAGE_FILE_GROUP_END.");
    }
}

static int multithreaded_sync_tcp_packets(void* opaque) {
    /*
        Thread to send and receive all TCP packets (clipboard and file)

        Arguments:
            opaque (void*): any arg to be passed to thread

        Return:
            (int): 0 on success
    */
    WhistServerState* state = (WhistServerState*)opaque;

    LOG_INFO("multithreaded_sync_tcp_packets running on Thread %lu", whist_get_thread_id(NULL));

    init_clipboard_synchronizer(false);
    init_file_synchronizer((FILE_TRANSFER_SERVER_DROP | FILE_TRANSFER_SERVER_UPLOAD));

    // Hold a client active lock
    ClientLock* client_lock = client_active_lock(state->client);

    // TCP Message handler loop
    while (client_lock != NULL) {
        // Refresh the client activation lock, to let the client (re/de)activate if it's trying to
        client_active_unlock(client_lock);
        client_lock = client_active_lock(state->client);
        if (client_lock == NULL) {
            break;
        }

        // RECEIVE TCP PACKET HANDLER
        bool data_transferred = get_whist_tcp_client_messages(state);

        // SEND TCP PACKET HANDLERS:

        // GET CLIPBOARD HANDLER
        // If the clipboard has a new available chunk, we should send it over to the
        // client
        ClipboardData* clipboard_chunk = pull_clipboard_chunk();
        if (clipboard_chunk) {
            LOG_INFO("Received clipboard trigger. Broadcasting clipboard message.");
            create_and_send_tcp_wmsg(SMESSAGE_CLIPBOARD, (char*)clipboard_chunk);
            data_transferred = true;
            // Free clipboard chunk
            deallocate_region(clipboard_chunk);
        }

        // READ FILE CHUNK HANDLER
        FileData* file_chunk;
        FileMetadata* file_metadata;
        FileGroupEnd file_group_end;
        // Iterate through all file indexes and try to read next chunk to send
        LinkedList* transferring_files = file_synchronizer_get_transferring_files();
        // Iterate through all file indexes and try to read next chunk to send
        linked_list_for_each(transferring_files, TransferringFile, transferring_file) {
            if (file_synchronizer_handle_type_group_end(transferring_file, &file_group_end)) {
                // Returns true when the TransferringFile was a type group end indicator
                send_complete_file_drop_message(file_group_end.transfer_type);
                continue;
            }

            file_synchronizer_read_next_file_chunk(transferring_file, &file_chunk);
            if (file_chunk == NULL) {
                // If chunk cannot be read, then try opening the file
                file_synchronizer_open_file_for_reading(transferring_file, &file_metadata);
                if (file_metadata == NULL) {
                    continue;
                }

                create_and_send_tcp_wmsg(SMESSAGE_FILE_METADATA, (char*)file_metadata);
                data_transferred = true;
                // Free file chunk
                deallocate_region(file_metadata);
                continue;
            }

            create_and_send_tcp_wmsg(SMESSAGE_FILE_DATA, (char*)file_chunk);
            data_transferred = true;
            // Free file chunk
            deallocate_region(file_chunk);
        }

        // Sleep for a small duration to avoid busy looping when idle
        // 100us is just a low enough number to not affect the latency significantly. And it is also
        // reduces CPU usage significantly.

        // TODO: This should be set to a TCP socket timeout, not a manual sleep
        if (!data_transferred) whist_usleep(100);
    }

    // If we're holding a client lock, unlock it
    if (client_lock != NULL) {
        client_active_unlock(client_lock);
    }
    destroy_clipboard_synchronizer();
    destroy_file_synchronizer();

    return 0;
}

static void whist_server_state_init(WhistServerState* state, whist_server_config* config) {
    CaptureDeviceType capture_type = DEFAULT_CAPTURE_TYPE;
    void* capture_data = getenv("DISPLAY");

    FATAL_ASSERT(capture_data);

    memset(state, 0, sizeof(*state));
    state->config = config;
    state->client_os = WHIST_UNKNOWN_OS;
    state->input_device = NULL;

    // Set width and height to -1. Video thread will start once correct dimensions are received from
    // client
    state->client_width = -1;
    state->client_height = -1;
    state->client_dpi = -1;
    state->update_device = true;

    // Mark initial update encoder
    state->update_encoder = true;

    srand((unsigned int)time(NULL));
    server_state.connection_id = rand();

    if (create_capture_device(&state->capture_device, capture_type, capture_data, 1024, 800, 96) <
        0) {
        LOG_FATAL("unable to create capture device");
    }

    server_state.input_device = create_input_device(DEFAULT_INPUT_TYPE, state->capture_device.impl);
    if (!server_state.input_device) {
        LOG_FATAL("Failed to create input device.");
    }

    server_state.client = init_client();
}

int main(int argc, char* argv[]) {
    whist_server_config config = {0};

    switch (server_parse_args(&config, argc, argv)) {
        case -1:
            // invalid usage
            return -1;
        case 1:
            // --help or --version
            return 0;
        default:
            break;
    }

    whist_init_subsystems();

    if (SERVER_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT) {
        // init as stream plotter, by passing a filename on init
        whist_plotter_init(SERVER_SIDE_DEFAULT_EXPORT_FILE);
    }

    whist_init_statistic_logger(STATISTICS_FREQUENCY_IN_SEC);

    whist_server_state_init(&server_state, &config);

    // Initialize the error monitor, and tell it we are the server.
    whist_error_monitor_initialize(false);

    LOG_INFO("Whist server revision %s", whist_git_revision());
    LOG_INFO("Server protocol started.");

#if OS_IS(OS_WIN32)
    // set Windows DPI
    SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
#endif

#if OS_IS(OS_LINUX)
    struct sigaction sa = {0};
    sa.sa_handler = sig_handler;
    if (sigaction(SIGTERM, &sa, NULL) == -1) {
        LOG_FATAL("Establishing SIGTERM signal handler failed.");
    }

    // if SERVER_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT enabled, insert handler
    // for grace quit for ctrl-c as well, so that no plotter data will be lost
    // on quit
    // exported automatically on quit
    if (SERVER_SIDE_PLOTTER_START_SAMPLING_BY_DEFAULT && sigaction(SIGINT, &sa, NULL) == -1) {
        LOG_FATAL("Establishing SIGINT signal handler failed.");
    }
    XSetIOErrorHandler(xioerror_handler);
#endif

    WhistTimer startup_time;
    start_timer(&startup_time);

    server_state.stop_streaming = false;
    server_state.update_encoder = false;
    server_state.exiting = false;

    server_state.ltr_context = ltr_create();
    server_state.stream_needs_restart = false;
    server_state.stream_needs_recovery = false;
    server_state.update_frame_ack = false;

    server_state.cursor_cache = whist_cursor_cache_create(CURSOR_CACHE_ENTRIES, false);
    server_state.last_cursor_hash = 0;

    WhistThread send_video_thread =
        whist_create_thread(multithreaded_send_video, "multithreaded_send_video", &server_state);
    WhistThread send_audio_thread =
        whist_create_thread(multithreaded_send_audio, "multithreaded_send_audio", &server_state);

    DBusHandlerContext* dbus_handler = whist_create_dbus_handler(&server_state);

    WhistThread sync_tcp_packets_thread = whist_create_thread(
        multithreaded_sync_tcp_packets, "multithreaded_sync_tcp_packets", &server_state);

    WhistThread gpu_command_receiver_thread = NULL;
    if (enable_gpu_command_streaming) {
        gpu_command_receiver_thread =
            whist_create_thread(multithreaded_gpu_command_receiver,
                                "multithreaded_gpu_command_receiver", &server_state);
    }

    LOG_INFO("Sending video, audio, and D-Bus handler...");

    WhistTimer totaltime;
    start_timer(&totaltime);

    LOG_INFO("Receiving packets...");

    init_window_info_getter();

    WhistTimer window_fullscreen_timer;
    start_timer(&window_fullscreen_timer);

    WhistTimer cpu_usage_statistics_timer;
    start_timer(&cpu_usage_statistics_timer);

#if !OS_IS(OS_WIN32)
    WhistTimer downloaded_file_timer;
    start_timer(&downloaded_file_timer);

    WhistTimer uploaded_file_timer;
    start_timer(&uploaded_file_timer);
#endif  // Not Windows

    // Tracking how long we're willing to attempt to connect
    WhistTimer connection_attempt_timer;
    start_timer(&connection_attempt_timer);

    // Whether or not the client connected at least once
    bool client_connected_once = false;

    while (!server_state.exiting) {
        // Client management code

        // If the client isn't active, activate the client
        if (!server_state.client->is_active) {
            // If begin_time_to_exit seconds pass, and no connection has succeeded,
            if (config.begin_time_to_exit != -1 &&
                get_timer(&connection_attempt_timer) > config.begin_time_to_exit) {
                // Just exit
                server_state.exiting = true;
                break;
            }
            // Otherwise, try to connect
            if (connect_client(&server_state)) {
                // Mark the client as activated
                activate_client(server_state.client);
                client_connected_once = true;
            } else {
                LOG_INFO("No client found.");
            }
            continue;
        }

        // If the client is deactivating for any reason,
        if (server_state.client->is_deactivating) {
            // Wait for the client to deactivate (Which waits for every clientlock to be unlocked)
            deactivate_client(server_state.client);
            // Disconnect the client
            // TODO: Make this a function

            // Client and server share file transfer indexes when sending files, so
            //     when a client disconnects, we need to reset the transferring files
            //     to make sure that if a client reconnects that the indices are fresh.
            reset_all_transferring_files();

            // Destroy the udp/tcp socket contexts
            destroy_socket_context(&server_state.client->udp_context);
            destroy_socket_context(&server_state.client->tcp_context);

            // Restart the timer for the next connection attempt
            start_timer(&connection_attempt_timer);
            continue;
        }

        // UDP client messages, and various periodic updates

        // Get UDP messages
        get_whist_udp_client_messages(&server_state);

        // Log cpu usage once per second. Only enable this when LOG_CPU_USAGE flag is set because
        // getting cpu usage statistics is expensive.
        // TODO : Do NOT enable this, as it is bottlenecking UDP receive. Change expensive 'top'
        // based logic to simple '/proc/stat' logic for CPU usage.
        double cpu_timer_time_elapsed = 0;
        if (LOG_CPU_USAGE &&
            ((cpu_timer_time_elapsed = get_timer(&cpu_usage_statistics_timer)) > 1)) {
            double cpu_usage = get_cpu_usage(cpu_timer_time_elapsed);
            if (cpu_usage != -1) {
                log_double_statistic(SERVER_CPU_USAGE, cpu_usage);
            }
            start_timer(&cpu_usage_statistics_timer);
        }

        if (get_timer(&window_fullscreen_timer) > 50.0 / MS_IN_SECOND) {
            // This is the cached fullscreen state. We only send state change events
            // to the client if the fullscreen value has changed.
            // TODO: Move static variable into client variable, so that it can clear on reactivation
            static bool cur_fullscreen = false;
            bool fullscreen = is_focused_window_fullscreen();
            if (fullscreen != cur_fullscreen) {
                if (fullscreen) {
                    LOG_INFO("Window is now fullscreen. Broadcasting fullscreen message.");
                } else {
                    LOG_INFO("Window is no longer fullscreen. Broadcasting fullscreen message.");
                }
                WhistServerMessage wsmsg = {0};
                wsmsg.type = SMESSAGE_FULLSCREEN;
                wsmsg.fullscreen = (int)fullscreen;
                if (broadcast_tcp_packet(server_state.client, PACKET_MESSAGE, &wsmsg,
                                         sizeof(WhistServerMessage)) == 0) {
                    LOG_INFO("Sent fullscreen message!");
                    cur_fullscreen = fullscreen;
                } else {
                    LOG_ERROR("Failed to broadcast fullscreen message.");
                }
            }
            start_timer(&window_fullscreen_timer);
        }

#if !OS_IS(OS_WIN32)
#define FILE_UPLOAD_TRIGGER_FILE "/home/whist/.teleport/uploaded-file"
        if (get_timer(&uploaded_file_timer) > 50.0 / MS_IN_SECOND) {
            if (!access(FILE_UPLOAD_TRIGGER_FILE, R_OK)) {
                // If trigger file exists, request upload from client then delete the file
                WhistServerMessage wsmsg = {0};
                wsmsg.type = SMESSAGE_INITIATE_UPLOAD;
                if (broadcast_tcp_packet(server_state.client, PACKET_MESSAGE, &wsmsg,
                                         sizeof(WhistServerMessage)) == 0) {
                    LOG_INFO("Sent initiate upload message!");
                } else {
                    LOG_ERROR("Failed to broadcast initiate upload message.");
                }
                remove(FILE_UPLOAD_TRIGGER_FILE);
            }
            start_timer(&uploaded_file_timer);
        }
#endif  // Not Windows
    }

    if (!client_connected_once) {
        // Log to sentry that the client never ended up connecting
        LOG_ERROR("Client did not connect!");
    }

    // If the client is still active,
    if (server_state.client->is_active) {
        // Begin deactivating the client, if not done already
        start_deactivating_client(server_state.client);
        // Wait for the client to deactivate (Which waits for every clientlock to be unlocked)
        deactivate_client(server_state.client);
    }

    // Mark the client as permanently deactivated, which lets the threads reap
    permanently_deactivate_client(server_state.client);

    destroy_input_device(server_state.input_device);
    server_state.input_device = NULL;

    destroy_window_info_getter();

    whist_wait_thread(send_video_thread, NULL);
    whist_wait_thread(send_audio_thread, NULL);
    whist_wait_thread(sync_tcp_packets_thread, NULL);
    if (gpu_command_receiver_thread) {
        whist_wait_thread(gpu_command_receiver_thread, NULL);
    }
    whist_destroy_dbus_handler(dbus_handler);

    ltr_destroy(server_state.ltr_context);
    whist_cursor_cache_destroy(server_state.cursor_cache);

    LOG_INFO("Protocol has shutdown gracefully");

    destroy_statistic_logger();
    whist_plotter_destroy();  // it's safe to call, regardless initalized or not
    destroy_logger();
    whist_error_monitor_shutdown();

    // This is safe to call here because all other threads have been waited and destroyed
    destroy_client(server_state.client);

    return 0;
}
