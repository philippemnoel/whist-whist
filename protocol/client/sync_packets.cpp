/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
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
#if defined(_MSC_VER)
#pragma warning(disable : 4815)  // Disable MSVC warning about 0-size array in wcmsg
#endif

#include <whist/core/whist.h>

extern "C" {
#include <whist/utils/clock.h>
#include <whist/network/network.h>
#include <whist/logging/log_statistic.h>
#include <whist/logging/logging.h>

#include "handle_server_message.h"
#include "network.h"
#include "audio.h"
#include "video.h"
#include "sync_packets.h"
#include "client_utils.h"
#include "whist/debug/plotter.h"
}

// Updater variables
extern "C" {
extern SocketContext packet_udp_context;
}
extern SocketContext packet_tcp_context;
extern std::atomic<bool>
    connected;  // The state of the client, i.e. whether it's connected to a server or not

// Threads
static WhistThread sync_udp_packets_thread;
static WhistThread sync_tcp_packets_thread;
static bool run_sync_packets_threads;

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
static int multithreaded_sync_udp_packets(void* opaque) {
    /*
        Send, receive, and process UDP packets - dimension messages, audio/video packets.
    */

    whist_set_thread_priority(WHIST_THREAD_PRIORITY_REALTIME);

    WhistRenderer* whist_renderer = (WhistRenderer*)opaque;
    SocketContext* udp_context = &packet_udp_context;

    WhistTimer statistics_timer;

    // For now, manually make ring buffers for audio and video
    // TODO: Make udp.c do this automatically
    // This represents 4 seconds worth of 60FPS video,
    // Or 2.5 seconds worth of 128kbps audio.
    // If stream resets fail to recover the stream in that time interval,
    // the connection is considered lost.
    udp_register_ring_buffer(udp_context, PACKET_VIDEO, LARGEST_VIDEOFRAME_SIZE, 256);
    udp_register_ring_buffer(udp_context, PACKET_AUDIO, LARGEST_AUDIOFRAME_SIZE, 256);
    udp_register_ring_buffer(udp_context, PACKET_GPU, LARGEST_GPUFRAME_SIZE, 256);

    WhistPacket* last_whist_packet[NUM_PACKET_TYPES] = {0};

    while (run_sync_packets_threads) {
        if (PLOT_CLIENT_UDP_SOCKET_RECV_QUEUE) {
            double current_time = get_timestamp_sec();
            int socket_queue_len = udp_get_socket_queue_len(udp_context->context);

            whist_plotter_insert_sample("udp_socket_queue", current_time,
                                        socket_queue_len / 1024.0);
        }

        // Update the UDP socket
        start_timer(&statistics_timer);
        // Disconnect if the UDP connection was lost
        if (!socket_update(udp_context)) {
            for (int i = 0; i < NUM_PACKET_TYPES; i++) {
                if (last_whist_packet[i] != NULL) {
                    free_packet(udp_context, last_whist_packet[i]);
                }
            }
            // TODO: Remove global
            if (connected) {
                LOG_WARNING("UDP Connection Lost!");
                connected = false;
            }
            whist_sleep(1);
            continue;
        }
        log_double_statistic(NETWORK_READ_PACKET_UDP, get_timer(&statistics_timer) * MS_IN_SECOND);

        // Handle any messages we've received
        WhistPacket* message_packet = (WhistPacket*)get_packet(udp_context, PACKET_MESSAGE);
        if (message_packet) {
            handle_server_message((WhistServerMessage*)message_packet->data,
                                  message_packet->payload_size, NULL);
            free_packet(udp_context, message_packet);
        }

        // Loop over both VIDEO and AUDIO
        static const WhistPacketType packet_types[] = {PACKET_VIDEO, PACKET_AUDIO, PACKET_GPU};
        for (int i = 0; i < (int)ARRAY_LENGTH(packet_types); i++) {
            WhistPacketType packet_type = packet_types[i];
            // If the renderer wants the frame of that type,
            // Knowing how many frames are pending a render...
            if (renderer_wants_frame(whist_renderer, packet_type,
                                     udp_get_num_pending_frames(udp_context, packet_type))) {
                // If the renderer wants a new frame, it must be done with the old frame, so we can
                // free it now
                // TODO: Make the renderer memcpy so this logic don't have to be weird
                if (last_whist_packet[packet_type] != NULL) {
                    free_packet(udp_context, last_whist_packet[packet_type]);
                }
                // Now, we try to get the packet from UDP,
                // And pass it to the renderer if one exists
                WhistPacket* whist_packet = (WhistPacket*)get_packet(udp_context, packet_type);
                if (whist_packet) {
                    renderer_receive_frame(whist_renderer, packet_type, whist_packet->data,
                                           whist_packet->payload_size);
                    // Store the pointer so we can free it later,
                    // While still keeping it alive for the renderer to render it
                    last_whist_packet[packet_type] = whist_packet;
                }
            }
        }
    }

    return 0;
}

static void create_and_send_tcp_wcmsg(WhistClientMessageType message_type, char* payload) {
    /*
        Create and send a TCP wcmsg according to the given payload, and then
        deallocate once finished.

        Arguments:
            message_type (WhistClientMessageType): the type of the TCP message to be sent
            payload (char*): the payload of the TCP message
    */

    int data_size = 0;
    char* copy_location = NULL;
    int type_size = 0;

    switch (message_type) {
        case CMESSAGE_CLIPBOARD: {
            data_size = ((ClipboardData*)payload)->size;
            type_size = sizeof(ClipboardData);
            break;
        }
        case CMESSAGE_FILE_METADATA: {
            data_size = (int)((FileMetadata*)payload)->filename_len;
            type_size = sizeof(FileMetadata);
            break;
        }
        case CMESSAGE_FILE_DATA: {
            data_size = (int)((FileData*)payload)->size;
            type_size = sizeof(FileData);
            break;
        }
        default: {
            LOG_ERROR("Not a valid server wcmsg type");
            return;
        }
    }

    // Alloc wcmsg
    WhistClientMessage* wcmsg_tcp =
        (WhistClientMessage*)allocate_region(sizeof(WhistClientMessage) + data_size);

    switch (message_type) {
        case CMESSAGE_CLIPBOARD: {
            copy_location = (char*)&wcmsg_tcp->clipboard;
            break;
        }
        case CMESSAGE_FILE_METADATA: {
            copy_location = (char*)&wcmsg_tcp->file_metadata;
            break;
        }
        case CMESSAGE_FILE_DATA: {
            copy_location = (char*)&wcmsg_tcp->file;
            break;
        }
        default: {
            LOG_FATAL("Invalid wcmsg type! %d", (int)message_type);
        }
    }

    // Build wcmsg
    // Init header to 0 to prevent sending uninitialized packets over the network
    memset(wcmsg_tcp, 0, sizeof(*wcmsg_tcp));
    wcmsg_tcp->type = message_type;
    memcpy(copy_location, payload, type_size + data_size);

    // Send wcmsg
    send_wcmsg(wcmsg_tcp);

    // Free wcmsg
    deallocate_region(wcmsg_tcp);
}

static void send_complete_file_drop_message(FileTransferType transfer_type) {
    /*
        Create and send a file drop complete message

        Arguments:
            transfer_type (FileTransferType): group end type
    */

    LOG_INFO("send_complete_file_drop_message");

    // Alloc wcmsg
    WhistClientMessage wcmsg;
    memset(&wcmsg, 0, sizeof(wcmsg));

    // Build wcmsg
    wcmsg.type = CMESSAGE_FILE_GROUP_END;
    wcmsg.file_group_end.transfer_type = transfer_type;

    // Send wcmsg
    send_wcmsg(&wcmsg);
}

#define SYNC_TCP_LOOP_TARGET_PERIOD_MS 25.0
static int multithreaded_sync_tcp_packets(void* opaque) {
    /*
        Thread to send and receive all TCP packets (clipboard and file)

        Arguments:
            opaque (void*): any arg to be passed to thread

        Return:
            (int): 0 on success
    */
    WhistFrontend* frontend = (WhistFrontend*)opaque;
    SocketContext* tcp_context = &packet_tcp_context;

    WhistTimer last_loop_start;
    WhistTimer statistics_timer;
    bool successful_read_or_pull = false;

    while (run_sync_packets_threads) {
        start_timer(&last_loop_start);

        if (!socket_update(tcp_context)) {
            // TODO: Remove global
            connected = false;
            whist_sleep(1);
            continue;
        }

        successful_read_or_pull = false;

        TIME_RUN(WhistPacket* packet = (WhistPacket*)get_packet(tcp_context, PACKET_MESSAGE),
                 NETWORK_READ_PACKET_TCP, statistics_timer);

        if (packet) {
            TIME_RUN(handle_server_message((WhistServerMessage*)packet->data,
                                           (size_t)packet->payload_size, frontend),
                     SERVER_HANDLE_MESSAGE_TCP, statistics_timer);
            free_packet(tcp_context, packet);
        }

        // PULL CLIPBOARD HANDLER
        ClipboardData* clipboard_chunk = pull_clipboard_chunk();
        if (clipboard_chunk) {
            create_and_send_tcp_wcmsg(CMESSAGE_CLIPBOARD, (char*)clipboard_chunk);
            deallocate_region(clipboard_chunk);

            successful_read_or_pull = true;
        }

        // READ FILE HANDLER
        FileData* file_chunk;
        FileMetadata* file_metadata;
        FileGroupEnd file_group_end;
        // Iterate through all file indexes and try to read next chunk to send
        LinkedList* transferring_files = file_synchronizer_get_transferring_files();
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

                create_and_send_tcp_wcmsg(CMESSAGE_FILE_METADATA, (char*)file_metadata);
                // Free file chunk
                deallocate_region(file_metadata);
            } else {
                // If we successfully read a chunk, then send it to the server.
                create_and_send_tcp_wcmsg(CMESSAGE_FILE_DATA, (char*)file_chunk);
                // Free file chunk
                deallocate_region(file_chunk);
            }

            successful_read_or_pull = true;
        }

        // We want to continue pumping pull_clipboard_chunk and
        //     file_synchronizer_read_next_file_chunk
        //     without sleeping if either of them are currently pulling/reading chunks. Otherwise,
        //     sleep to target one loop every 25 ms.
        if (!successful_read_or_pull &&
            get_timer(&last_loop_start) * MS_IN_SECOND < SYNC_TCP_LOOP_TARGET_PERIOD_MS) {
            whist_sleep(max(1, (int)(SYNC_TCP_LOOP_TARGET_PERIOD_MS -
                                     get_timer(&last_loop_start) * MS_IN_SECOND)));
        }
    }
    return 0;
}

/*
============================
Public Function Implementations
============================
*/

void init_packet_synchronizers(WhistFrontend* frontend, WhistRenderer* whist_renderer) {
    /*
        Initialize the packet synchronizer threads for UDP and TCP.
    */
    if (run_sync_packets_threads == true) {
        LOG_FATAL("Packet synchronizers are already running!");
    }
    run_sync_packets_threads = true;
    sync_udp_packets_thread = whist_create_thread(multithreaded_sync_udp_packets,
                                                  "multithreaded_sync_udp_packets", whist_renderer);
    sync_tcp_packets_thread = whist_create_thread(multithreaded_sync_tcp_packets,
                                                  "multithreaded_sync_tcp_packets", frontend);
}

void destroy_packet_synchronizers(void) {
    /*
        Destroy and wait on the packet synchronizer threads for UDP and TCP.
    */
    if (run_sync_packets_threads == false) {
        LOG_ERROR("Packet synchronizers have not been initialized!");
        return;
    }
    run_sync_packets_threads = false;
    whist_wait_thread(sync_tcp_packets_thread, NULL);
    whist_wait_thread(sync_udp_packets_thread, NULL);
}
