/**
 * Copyright Fractal Computers, Inc. 2020
 * @file server_main.c
 * @brief This file contains the main code that runs a Fractal server on a
Windows or Linux Ubuntu computer.
============================
Usage
============================

Follow main() to see a Fractal video streaming server being created and creating
its threads.
*/

/*
============================
Includes
============================
*/

#if defined(_WIN32)
#define _CRT_SECURE_NO_WARNINGS
#endif
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#include <shlwapi.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
#include <time.h>

#include <fractal/utils/window_name.h>
#include <fractal/core/fractalgetopt.h>
#include <fractal/core/fractal.h>
#include <fractal/input/input.h>
#include <fractal/utils/error_monitor.h>
#include <fractal/video/transfercapture.h>
#include "client.h"
#include "handle_client_message.h"
#include "server_parse_args.h"
#include "server_network.h"
#include "server_video.h"
#include "server_audio.h"

#ifdef _WIN32
#include <fractal/utils/windows_utils.h>
#endif

#ifdef _WIN32
#pragma comment(lib, "ws2_32.lib")
#endif
// Linux shouldn't have this

extern Client clients[MAX_NUM_CLIENTS];

char binary_aes_private_key[16];
char hex_aes_private_key[33];

// This variables should stay as arrays - we call sizeof() on them
char identifier[FRACTAL_IDENTIFIER_MAXLEN + 1];

volatile int connection_id;
volatile bool exiting;

int sample_rate = -1;

volatile double max_mbps;
InputDevice* input_device = NULL;

static FractalMutex packet_mutex;

volatile bool wants_iframe;
volatile bool update_encoder;

bool client_joined_after_window_name_broadcast = false;
// This variable should always be an array - we call sizeof()
static char cur_window_name[WINDOW_NAME_MAXLEN + 1] = {0};

/*
============================
Private Functions
============================
*/

void graceful_exit();
#ifdef __linux__
int xioerror_handler(Display* d);
void sig_handler(int sig_num);
#endif
int main(int argc, char* argv[]);

/*
============================
Private Function Implementations
============================
*/

void graceful_exit() {
    /*
        Quit clients gracefully and allow server to exit.
    */

    exiting = true;

    //  Quit all clients. This means that there is a possibility
    //  of the quitClients() pipeline happening more than once
    //  because this error handler can be called multiple times.

    // POSSIBLY below locks are not necessary if we're quitting everything and dying anyway?

    // Broadcast client quit message
    FractalServerMessage fmsg_response = {0};
    fmsg_response.type = SMESSAGE_QUIT;
    read_lock(&is_active_rwlock);
    if (broadcast_udp_packet(PACKET_MESSAGE, (uint8_t*)&fmsg_response, sizeof(FractalServerMessage),
                             1, STARTING_BURST_BITRATE, NULL, NULL) != 0) {
        LOG_WARNING("Could not send Quit Message");
    }
    read_unlock(&is_active_rwlock);

    // Kick all clients
    write_lock(&is_active_rwlock);
    fractal_lock_mutex(state_lock);
    if (quit_clients() != 0) {
        LOG_ERROR("Failed to quit clients.");
    }
    fractal_unlock_mutex(state_lock);
    write_unlock(&is_active_rwlock);
}

#ifdef __linux__
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

    graceful_exit();

    return 0;
}

void sig_handler(int sig_num) {
    /*
        When the server receives a SIGTERM, gracefully exit.
    */

    if (sig_num == SIGTERM) {
        graceful_exit();
    }
}
#endif

int main(int argc, char* argv[]) {
    fractal_init_multithreading();
    init_logger();

    int ret = server_parse_args(argc, argv);
    if (ret == -1) {
        // invalid usage
        return -1;
    } else if (ret == 1) {
        // --help or --version
        return 0;
    }

    LOG_INFO("Server protocol started.");

    // Initialize the error monitor, and tell it we are the server.
    error_monitor_initialize(false);

    init_networking();

#if defined(_WIN32)
    // set Windows DPI
    SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
#endif

    srand((unsigned int)time(NULL));
    connection_id = rand();

    LOG_INFO("Fractal server revision %s", fractal_git_revision());

// initialize the windows socket library if this is a windows client
#ifdef _WIN32
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        LOG_FATAL("Failed to initialize Winsock with error code: %d.", WSAGetLastError());
    }
#endif

    input_device = create_input_device();
    if (!input_device) {
        LOG_FATAL("Failed to create input device for playback.");
    }

#ifdef _WIN32
    if (!init_desktop(input_device, "winlogonpassword")) {
        LOG_FATAL("Could not winlogon!");
    }
#endif

    if (init_clients() != 0) {
        LOG_FATAL("Failed to initialize client objects.");
    }

#ifdef __linux__
    signal(SIGTERM, sig_handler);
    XSetIOErrorHandler(xioerror_handler);
#endif

    clock startup_time;
    start_timer(&startup_time);

    max_mbps = STARTING_BITRATE;
    wants_iframe = false;
    update_encoder = false;
    exiting = false;

    FractalThread manage_clients_thread =
        fractal_create_thread(multithreaded_manage_clients, "MultithreadedManageClients", NULL);
    fractal_sleep(500);

    FractalThread send_video_thread =
        fractal_create_thread(multithreaded_send_video, "multithreaded_send_video", NULL);
    FractalThread send_audio_thread =
        fractal_create_thread(multithreaded_send_audio, "multithreaded_send_audio", NULL);
    LOG_INFO("Sending video and audio...");

    clock totaltime;
    start_timer(&totaltime);

    clock last_exit_check;
    start_timer(&last_exit_check);

    clock last_ping_check;
    start_timer(&last_ping_check);

    LOG_INFO("Receiving packets...");

    init_clipboard_synchronizer(false);
    init_window_name_getter();

    clock ack_timer;
    start_timer(&ack_timer);

    clock window_name_timer;
    start_timer(&window_name_timer);

    while (!exiting) {
        if (get_timer(ack_timer) > 5) {
            if (get_using_stun()) {
                // Broadcast ack
                read_lock(&is_active_rwlock);
                if (broadcast_ack() != 0) {
                    LOG_ERROR("Failed to broadcast acks.");
                }
                read_unlock(&is_active_rwlock);
            }
            start_timer(&ack_timer);
        }

        // If they clipboard as updated, we should send it over to the
        // client
        ClipboardData* clipboard = clipboard_synchronizer_get_new_clipboard();
        if (clipboard) {
            LOG_INFO("Received clipboard trigger. Broadcasting clipboard message.");
            // Alloc fmsg
            FractalServerMessage* fmsg_response =
                allocate_region(sizeof(FractalServerMessage) + clipboard->size);
            // Build fmsg
            memset(fmsg_response, 0, sizeof(*fmsg_response));
            fmsg_response->type = SMESSAGE_CLIPBOARD;
            memcpy(&fmsg_response->clipboard, clipboard, sizeof(ClipboardData) + clipboard->size);
            // Send fmsg
            read_lock(&is_active_rwlock);
            if (broadcast_tcp_packet(PACKET_MESSAGE, (uint8_t*)fmsg_response,
                                     sizeof(FractalServerMessage) + clipboard->size) < 0) {
                LOG_WARNING("Failed to broadcast clipboard message.");
            }
            read_unlock(&is_active_rwlock);
            // Free fmsg
            deallocate_region(fmsg_response);
            // Free clipboard
            free_clipboard(clipboard);
        }

        if (get_timer(window_name_timer) > 0.1) {  // poll window name every 100ms
            char name[WINDOW_NAME_MAXLEN + 1];
            if (get_focused_window_name(name) == 0) {
                if (client_joined_after_window_name_broadcast ||
                    (num_active_clients > 0 && strcmp(name, cur_window_name) != 0)) {
                    LOG_INFO("Window title changed. Broadcasting window title message.");
                    size_t fsmsg_size = sizeof(FractalServerMessage) + sizeof(name);
                    FractalServerMessage* fmsg_response = safe_malloc(fsmsg_size);
                    memset(fmsg_response, 0, sizeof(*fmsg_response));
                    fmsg_response->type = SMESSAGE_WINDOW_TITLE;
                    memcpy(&fmsg_response->window_title, name, sizeof(name));
                    read_lock(&is_active_rwlock);
                    if (broadcast_tcp_packet(PACKET_MESSAGE, (uint8_t*)fmsg_response,
                                             (int)fsmsg_size) < 0) {
                        LOG_WARNING("Failed to broadcast window title message.");
                    } else {
                        LOG_INFO("Sent window title message!");
                        safe_strncpy(cur_window_name, name, sizeof(cur_window_name));
                        client_joined_after_window_name_broadcast = false;
                    }
                    read_unlock(&is_active_rwlock);
                    free(fmsg_response);
                }
            }
            start_timer(&window_name_timer);
        }

        if (get_timer(last_ping_check) > 20.0) {
            for (;;) {
                read_lock(&is_active_rwlock);
                bool exists, should_reap = false;
                if (exists_timed_out_client(CLIENT_PING_TIMEOUT_SEC, &exists) != 0) {
                    LOG_ERROR("Failed to find if a client has timed out.");
                } else {
                    should_reap = exists;
                }
                read_unlock(&is_active_rwlock);
                if (should_reap) {
                    write_lock(&is_active_rwlock);
                    if (reap_timed_out_clients(CLIENT_PING_TIMEOUT_SEC) != 0) {
                        LOG_ERROR("Failed to reap timed out clients.");
                    }
                    write_unlock(&is_active_rwlock);
                }
                break;
            }
            start_timer(&last_ping_check);
        }

        read_lock(&is_active_rwlock);

        for (int id = 0; id < MAX_NUM_CLIENTS; id++) {
            if (!clients[id].is_active) continue;

            // Get packet!
            FractalPacket* tcp_packet = NULL;
            FractalClientMessage* fmsg = NULL;
            FractalClientMessage local_fcmsg;
            size_t fcmsg_size;
            if (try_get_next_message_tcp(id, &tcp_packet) != 0 || tcp_packet == NULL) {
                // On no TCP
                if (try_get_next_message_udp(id, &local_fcmsg, &fcmsg_size) != 0 ||
                    fcmsg_size == 0) {
                    // On no UDP
                    continue;
                }
                // On UDP
                fmsg = &local_fcmsg;
            } else {
                // On TCP
                fmsg = (FractalClientMessage*)tcp_packet->data;
            }

            // HANDLE FRACTAL CLIENT MESSAGE
            fractal_lock_mutex(state_lock);
            bool is_controlling = clients[id].is_controlling;
            fractal_unlock_mutex(state_lock);
            if (handle_client_message(fmsg, id, is_controlling) != 0) {
                LOG_ERROR(
                    "Failed to handle message from client. "
                    "(ID: %d)",
                    id);
            } else {
                // if (handleSpectatorMessage(fmsg, id) != 0) {
                //     LOG_ERROR("Failed to handle message from spectator");
                // }
            }

            // Free the tcp packet if we received one
            if (tcp_packet) {
                free_tcp_packet(tcp_packet);
            }
        }
        read_unlock(&is_active_rwlock);
    }

    destroy_input_device(input_device);
    destroy_clipboard_synchronizer();
    destroy_window_name_getter();

    fractal_wait_thread(send_video_thread, NULL);
    fractal_wait_thread(send_audio_thread, NULL);
    fractal_wait_thread(manage_clients_thread, NULL);

    fractal_destroy_mutex(packet_mutex);

    write_lock(&is_active_rwlock);
    fractal_lock_mutex(state_lock);
    if (quit_clients() != 0) {
        LOG_ERROR("Failed to quit clients.");
    }
    fractal_unlock_mutex(state_lock);
    write_unlock(&is_active_rwlock);

#ifdef _WIN32
    WSACleanup();
#endif

    destroy_logger();
    error_monitor_shutdown();
    destroy_clients();

    return 0;
}
