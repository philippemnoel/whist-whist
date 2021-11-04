/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file sync_packets.c
 * @brief This file contains code for high-level communication with the server
============================
Usage
============================
Use multithreaded_sync_udp_packets to send and receive UDP messages to and from the server, such as
audio and video packets. Use multithreaded_sync_tcp_packets to send and receive TCP messages, mostly
clipboard packets.
*/

/*
============================
Includes
============================
*/
#include <fractal/core/fractal.h>
#include <fractal/utils/clock.h>
#include <fractal/network/network.h>
#include <fractal/logging/log_statistic.h>
#include <fractal/logging/logging.h>
#include "handle_server_message.h"
#include "network.h"
#include "audio.h"
#include "video.h"
#include "sync_packets.h"
#include "client_utils.h"

// Updater variables
bool tried_to_update_dimension;
bool updater_initialized;
clock last_tcp_check_timer;
extern SocketContext packet_tcp_context;
volatile bool run_sync_udp_packets;
volatile bool run_sync_tcp_packets;
bool connected = true;
extern bool using_stun;
// Ping variables
clock last_ping_timer;
volatile int last_ping_id;
volatile int last_pong_id;
volatile int ping_failures;
clock latency_timer;
// TCP ping variables
clock last_tcp_ping_timer;
volatile int last_tcp_ping_id;
volatile int last_tcp_pong_id;
clock tcp_latency_timer;
extern volatile double latency;
// MBPS variables
extern volatile int client_max_bitrate;
extern volatile int max_burst_bitrate;
extern volatile bool update_bitrate;
// dimension variables
extern volatile int server_width;
extern volatile int server_height;
extern volatile CodecType server_codec_type;
extern volatile CodecType output_codec_type;
extern volatile int output_width;
extern volatile int output_height;

/*
============================
Private Functions
============================
*/
void init_updater();
void destroy_updater();
void try_update_bitrate();
void update_ping();
void update_tcp_ping();
void update_initial_dimensions();

/*
============================
Private Function Implementations
============================
*/
// TODO: better name
void init_updater() {
    /*
        Initialize client update handler.
        Anything that will be continuously be called (within `update()`)
        that changes program state should be initialized in here.
    */

    tried_to_update_dimension = false;

    start_timer(&last_tcp_check_timer);
    start_timer(&latency_timer);
    start_timer(&tcp_latency_timer);

    // we initialize latency here because on macOS, latency would not initialize properly in
    // its declaration above. We start at 25ms before the first ping.
    latency = 25.0 / 1000.0;
    last_ping_id = 1;
    ping_failures = -2;

    last_tcp_ping_id = 0;

    init_clipboard_synchronizer(true);

    updater_initialized = true;
}

void update_ping() {
    /*
       Check if we should send more pings, disconnect, etc. If no valid pong has been received for
       600ms, we mark that as a ping failure. If we successfully received a pong and it has been
       500ms since the last ping, we send the next ping. Otherwise, if we haven't yet received a
       pong and it has been 210 ms, resend the ping.
    */

    // If it's been 1 second since the last ping, we should warn
    if (get_timer(last_ping_timer) > 1.0) {
        LOG_WARNING("No ping sent or pong received in over a second");
    }

    // If we're waiting for a ping, and it's been 600ms, then that ping will be
    // noted as failed
    if (last_ping_id != last_pong_id && get_timer(latency_timer) > 0.6) {
        LOG_WARNING("Ping received no response: %d", last_ping_id);
        // Keep track of failures, and exit if too many failures
        last_pong_id = last_ping_id;
        ping_failures++;
        if (ping_failures == 3) {
            // we make this a LOG_WARNING so it doesn't clog up Sentry, as this
            // error happens periodically but we have recovery systems in place
            // for streaming interruption/connection loss
            LOG_WARNING("Server disconnected: 3 consecutive ping failures.");
            connected = false;
        }
    }

    // if we've received the last ping, send another
    if (last_ping_id == last_pong_id && get_timer(last_ping_timer) > 0.5) {
        send_ping(last_ping_id + 1);
        start_timer(&latency_timer);
    }

    // if we haven't received the last ping, send the same ping
    if (last_ping_id != last_pong_id && get_timer(last_ping_timer) > 0.21) {
        send_ping(last_ping_id);
    }
}

void update_tcp_ping() {
    /*
       If no valid TCP pong has been received or sending a TCP ping is failing, then
       send a TCP reconnection request to the server. This is agnostic of whether
       the lost connection was caused by the client or the server.
    */

    // If it's been 4 seconds since the last ping, we should warn
    if (get_timer(last_tcp_ping_timer) > 4.0) {
        LOG_WARNING("No TCP ping sent or pong received in over a second");
    }

    // If we're waiting for a ping, and it's been 1s, then that ping will be
    // noted as failed
    if (last_tcp_ping_id != last_tcp_pong_id && get_timer(tcp_latency_timer) > 1.0) {
        LOG_WARNING("TCP ping received no response: %d", last_tcp_ping_id);

        // Only if we successfully recover the TCP connection should we continue
        //     as if the ping was successful.
        if (send_tcp_reconnect_message(using_stun) == 0) {
            last_tcp_pong_id = last_tcp_ping_id;
        }
    }

    // if we've received the last ping, send another
    if (last_tcp_ping_id == last_tcp_pong_id && get_timer(last_tcp_ping_timer) > 2.0) {
        send_tcp_ping(last_tcp_ping_id + 1);
        start_timer(&tcp_latency_timer);
    }
}

void update_initial_dimensions() {
    /*
        Send the initial client width/height to the server. This should only run once.
    */
    if (!tried_to_update_dimension &&
        (server_width != output_width || server_height != output_height ||
         server_codec_type != output_codec_type)) {
        send_message_dimensions();
        tried_to_update_dimension = true;
    }
}

void try_update_bitrate() {
    /*
        Tell the server to update the bitrate of its video if needed.
    */
    if (update_bitrate) {
        update_bitrate = false;
        FractalClientMessage fcmsg = {0};
        fcmsg.type = MESSAGE_MBPS;
        fcmsg.bitrate_data.bitrate = client_max_bitrate;
        fcmsg.bitrate_data.burst_bitrate = max_burst_bitrate;
        fcmsg.bitrate_data.fec_packet_ratio = FEC_PACKET_RATIO;
        LOG_INFO("Asking for server MBPS to be %f/%f/%f",
                 fcmsg.bitrate_data.bitrate / 1024.0 / 1024.0,
                 fcmsg.bitrate_data.burst_bitrate / 1024.0 / 1024.0,
                 fcmsg.bitrate_data.fec_packet_ratio);
        send_fcmsg(&fcmsg);
    }
}

void destroy_updater() {
    /*
        Destroy the client update handler - currently, just the clipboard
    */
    updater_initialized = false;
    destroy_clipboard_synchronizer();
}

/*
============================
Public Function Implementations
============================
*/
// This function polls for UDP packets from the server
// NOTE: This contains a very sensitive hotpath,
// as recvp will potentially receive tens of thousands packets per second.
// The total execution time of inner for loop must not take longer than 0.01ms-0.1ms
// i.e., this function should not take any more than 10,000 assembly instructions per loop.
// Please do not put any for loops, and do not make any non-trivial system calls.
// Please label any functions in the hotpath with the following lines:
// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int multithreaded_sync_udp_packets(void* opaque) {
    /*
        Send, receive, and process UDP packets - dimension messages, bitrate messages, nack
       messages, pings, audio and video packets.
    */
    SocketContext socket_context = *(SocketContext*)opaque;
    /****
    Timers
    ****/

    clock recvfrom_timer;
    clock update_video_timer;
    clock update_audio_timer;
    clock video_timer;
    clock audio_timer;
    clock message_timer;

    /****
    End Timers
    ****/

    double lastrecv = 0.0;

    clock last_ack;
    start_timer(&last_ack);

    init_updater();

    while (run_sync_udp_packets) {
        if (get_timer(last_ack) > 5.0) {
            ack(&socket_context);
            start_timer(&last_ack);
        }
        if (!updater_initialized) {
            LOG_ERROR("Tried to update, but updater not initialized!");
        }
        update_initial_dimensions();
        try_update_bitrate();
        update_ping();
        // Video and Audio should be updated at least every 5ms
        // We will do it here, after receiving each packet or if the last recvp
        // timed out

        start_timer(&update_video_timer);
        update_video();
        log_double_statistic("update_video time (ms)",
                             get_timer(update_video_timer) * MS_IN_SECOND);

        start_timer(&update_audio_timer);
        update_audio();
        log_double_statistic("update_audio time (ms)",
                             get_timer(update_audio_timer) * MS_IN_SECOND);

        // Time the following recvfrom code
        start_timer(&recvfrom_timer);
        FractalPacket* packet;
        packet = read_packet(&socket_context, true);

        double recvfrom_short_time = get_timer(recvfrom_timer);

        // Total amount of time spent in recvfrom / decrypt_packet
        log_double_statistic("recvfrom_time (ms)", recvfrom_short_time * MS_IN_SECOND);
        // Total amount of cumulative time spend in recvfrom, since the last
        // time recv_size was > 0
        lastrecv += recvfrom_short_time;

        if (packet) {
            // Log if it's been a while since the last packet was received
            if (lastrecv > 50.0 / MS_IN_SECOND) {
                LOG_WARNING(
                    "Took more than 50ms to receive something!! Took %fms "
                    "total!",
                    lastrecv * MS_IN_SECOND);
            }
            lastrecv = 0.0;

            // LOG_INFO("Recv wait time: %f", get_timer(recvfrom_timer));

            // Check packet type and then redirect packet to the proper packet
            // handler
            switch (packet->type) {
                case PACKET_VIDEO:
                    // Video packet
                    start_timer(&video_timer);
                    receive_video(packet);
                    log_double_statistic("receive_video time (ms)",
                                         get_timer(video_timer) * MS_IN_SECOND);
                    break;
                case PACKET_AUDIO:
                    // Audio packet
                    start_timer(&audio_timer);
                    receive_audio(packet);
                    log_double_statistic("receive_audio time (ms)",
                                         get_timer(audio_timer) * MS_IN_SECOND);
                    break;
                case PACKET_MESSAGE:
                    // A FractalServerMessage for other information
                    start_timer(&message_timer);
                    handle_server_message((FractalServerMessage*)packet->data,
                                          (size_t)packet->payload_size);
                    log_double_statistic("handle_server_message time (ms)",
                                         get_timer(message_timer) * MS_IN_SECOND);
                    break;
                default:
                    LOG_WARNING("Unknown Packet");
                    break;
            }

            free_packet(&socket_context, packet);
        }
    }

    if (lastrecv > 20.0 / MS_IN_SECOND) {
        LOG_INFO("Took more than 20ms to receive something!! Took %fms total!",
                 lastrecv * MS_IN_SECOND);
    }

    SDL_Delay(50);

    destroy_updater();

    return 0;
}

int multithreaded_sync_tcp_packets(void* opaque) {
    /*
        Thread to send and receive all TCP packets (clipboard and file)

        Arguments:
            opaque (void*): any arg to be passed to thread

        Return:
            (int): 0 on success
    */

    UNUSED(opaque);
    LOG_INFO("sync_tcp_packets running on Thread %p", SDL_GetThreadID(NULL));

    // TODO: compartmentalize each part into its own function
    while (run_sync_tcp_packets) {
        // RECEIVE TCP PACKET HANDLER
        // Check if TCP connection is active
        // last_tcp_check_timer indicates the last successful TCP check, or, if we've not had a
        // successful TCP check for at least a second, the time since the last LOG_ERROR indicating
        // lost TCP connection

        update_tcp_ping();

        int result = ack(&packet_tcp_context);
        if (result < 0) {
            // If the TCP checks are unsuccessful for 1 second, we should LOG_ERROR and restart the
            // check timer
            if (get_timer(last_tcp_check_timer) > 1000.0 / MS_IN_SECOND) {
                // we make this a LOG_WARNING so it doesn't clog up Sentry, as this
                // error happens periodically but we have recovery systems in place
                // for streaming interruption/connection loss
                LOG_WARNING("Lost TCP Connection (Error: %d)", get_last_network_error());
                start_timer(&last_tcp_check_timer);
            }
            continue;
        }

        // Update the time since the last successful TCP check
        start_timer(&last_tcp_check_timer);

        // Receive tcp buffer, if a full packet has been received
        FractalPacket* tcp_packet = read_packet(&packet_tcp_context, true);
        if (tcp_packet) {
            handle_server_message((FractalServerMessage*)tcp_packet->data,
                                  (size_t)tcp_packet->payload_size);
            free_packet(&packet_tcp_context, tcp_packet);
        }

        // SEND TCP PACKET HANDLERS:

        // GET CLIPBOARD HANDLER
        ClipboardData* clipboard_chunk = clipboard_synchronizer_get_next_clipboard_chunk();
        if (clipboard_chunk) {
            // Alloc an fcmsg
            FractalClientMessage* fcmsg_clipboard = allocate_region(
                sizeof(FractalClientMessage) + sizeof(ClipboardData) + clipboard_chunk->size);
            // Build the fcmsg
            // Init metadata to 0 to prevent sending uninitialized packets over the network
            memset(fcmsg_clipboard, 0, sizeof(*fcmsg_clipboard));
            fcmsg_clipboard->type = CMESSAGE_CLIPBOARD;
            memcpy(&fcmsg_clipboard->clipboard, clipboard_chunk,
                   sizeof(ClipboardData) + clipboard_chunk->size);
            // Send the fcmsg
            send_fcmsg(fcmsg_clipboard);
            // Free the fcmsg
            deallocate_region(fcmsg_clipboard);
            // Free the clipboard chunk
            deallocate_region(clipboard_chunk);
        }

        // Wait until 25 ms have elapsed since we started interacting with the TCP socket, unless
        //    the clipboard is actively being written or read
        if (!is_clipboard_synchronizing() &&
            get_timer(last_tcp_check_timer) < 25.0 / MS_IN_SECOND) {
            SDL_Delay(max((int)(25.0 - MS_IN_SECOND * get_timer(last_tcp_check_timer)), 1));
        }
    }

    return 0;
}
