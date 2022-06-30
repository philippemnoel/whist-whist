#ifndef RINGBUFFER_H
#define RINGBUFFER_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file ringbuffer.h
 * @brief This file contains the code to create a ring buffer and use it for handling received
audio/video.
============================
Usage
============================
Initialize a ring buffer for audio or video using init_ring_buffer. When new packets arrive, call
receive_packet to process the packet and modify or create ring buffer entries as needed. To nack a
packet, call nack_single_packet.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/fec/fec.h>
#include <whist/network/network_algorithm.h>
#include "whist/utils/linked_list.h"

/*
============================
Defines
============================
*/

#define PACKET_LOSS_DURATION_IN_SEC 1

/**
 * @brief FrameData struct containing content and metadata of encoded frames.
 * @details This is used to handle reconstruction of encoded frames from UDP packets. It contains
 * metadata to keep track of what packets we have received and nacked for, as well as a buffer for
 * holding the concatenated UDP packets.
 *
 * TODO: Pull this into RingBuffer.c, and stop exposing it to the client
 */
typedef struct FrameData {
    int num_original_packets;
    int num_fec_packets;
    int prev_frame_num_duplicate_packets;
    int id;
    int original_packets_received;
    int fec_packets_received;
    int duplicate_packets_received;
    int nack_packets_received;
    int unnecessary_nack_packets_received;
    bool received_indices[MAX_PACKETS];
    WhistTimer last_nacked_timer[MAX_PACKETS];
    char* packet_buffer;

    // When the FrameData is being rendered,
    // this is the data that's being rendered
    char* frame_buffer;
    int frame_buffer_size;

    // FEC logic
    char* fec_frame_buffer;
    FECDecoder* fec_decoder;
    bool successful_fec_recovery;

    // Nack logic
    uint8_t num_entire_frame_nacked;
    int entire_frame_nacked_id;
    WhistTimer last_frame_nack_timer;
    uint8_t num_times_index_nacked[MAX_PACKETS];
    WhistTimer last_nonnack_packet_timer;
    WhistTimer frame_creation_timer;
} FrameData;

// Handler that gets called when the ring buffer wants to nack for a packet
typedef void (*NackPacketFn)(SocketContext* socket_context, WhistPacketType frame_type, int id,
                             int index);
typedef void (*StreamResetFn)(SocketContext* socket_context, WhistPacketType frame_type,
                              int last_failed_id);

/**
 * @brief	RingBuffer struct for abstracting away frame reconstruction and frame retrieval.
 * @details This is used by client/audio.c and client/video.c to keep track of frames as the client
 * receives packets. It handles inserting new packets into the ring buffer and nacking for missing
 * packets.
 */
typedef struct RingBuffer {
    int ring_buffer_size;
    FrameData* receiving_frames;
    WhistPacketType type;
    int largest_frame_size;

    // networking interface
    SocketContext* socket_context;
    NackPacketFn nack_packet;
    StreamResetFn request_stream_reset;

    BlockAllocator* packet_buffer_allocator;  // unused if audio

    int currently_rendering_id;
    FrameData currently_rendering_frame;

    int num_packets_nacked;

    // Data ranges for frames
    int frames_received;
    int min_id;
    int max_id;

    // a maintained value of ready(fully received) frames inside ringbuffer,
    // not including the currently rendering frame
    int num_pending_ready_frames;

    // Nack variables

    // The next ID that should be rendered, marks
    // the lowest packet ID we're interested in nacking about
    int last_rendered_id;
    int most_recent_reset_id;
    WhistTimer last_stream_reset_request_timer;
    int last_stream_reset_request_id;

    // Nacking bandwidth tracker
    WhistTimer burst_timer;
    WhistTimer avg_timer;
    int burst_counter;
    int avg_counter;
    bool last_nack_possibility;

    // NACKing statistics
    int num_nacks_received;
    int num_original_packets_received;
    int num_unnecessary_original_packets_received;
    int num_unnecessary_nacks_received;
    int num_times_nacking_saturated;
    WhistTimer last_nack_statistics_timer;
} RingBuffer;

/*
============================
Public Functions
============================
*/

/**
 * @brief                           Initializes a ring buffer of the specified size and type.
 *
 * @param type                      Either an audio or video ring buffer.
 *
 * @param max_frame_size            The largest a frame can get
 *
 * @param ring_buffer_size          The desired size of the ring buffer
 *
 * @param socket_context            A temporary parameter to call the lambda functions
 *                                  without std::bind
 *                                  TODO: Remove
 *
 * @param nack_packet               A lambda function that will be called when the ring buffer
 *                                  wants to nack for something. NULL will disable nacking.
 *
 * @param request_stream_reset      A temporary lambda function to make the refactor work
 *                                  NULL to disable
 *                                  TODO: Remove
 *
 * @returns                         A pointer to the newly created and now empty ring buffer
 */
RingBuffer* init_ring_buffer(WhistPacketType type, int max_frame_size, int ring_buffer_size,
                             SocketContext* socket_context, NackPacketFn nack_packet,
                             StreamResetFn request_stream_reset);

/**
 * @brief Add a packet to the ring buffer, and initialize the corresponding frame if necessary. Also
 * nacks for missing packets or frames.
 *
 * @param ring_buffer Ring buffer to place packet into
 *
 * @param segment The segment of an audio or video WhistPacket
 *
 * @returns True on success, False on failure. False implies that the ringbuffer overflowed.
 */
bool ring_buffer_receive_segment(RingBuffer* ring_buffer, WhistSegment* segment);

/**
 * @brief Retrives the frame at the given ID in the ring buffer.
 *
 * @param ring_buffer Ring buffer to access
 *
 * @param id ID of desired frame.
 *
 * @returns Pointer to the FrameData at ID id in ring_buffer.
 */
FrameData* get_frame_at_id(RingBuffer* ring_buffer, int id);

/**
 * @brief Calculates the packet loss ratio of non-rendered frames in the last 250ms.
 *
 * @param ring_buffer Ring buffer to access
 *
 * @param latency UDP round trip time
 *
 * @returns Packet loss ratio of non-rendered frames in the last 250ms.
 */
double get_packet_loss_ratio(RingBuffer* ring_buffer, double latency);

/**
 * @brief       Indicate that the frame with ID id is currently rendering, and free the frame buffer
 *              for the previously rendered frame. The ring buffer will not write to the currently
 *              rendering frame.
 *
 * @param ring_buffer Ring buffer containing the frame
 *
 * @param id ID of the frame we want to render
 *
 * @returns true if the frame is ready, false if it's not
 */
bool is_ready_to_render(RingBuffer* ring_buffer, int id);

/**
 * @brief       Indicate that the frame with ID id is currently rendering, and free the frame buffer
 *              for the previously rendered frame. The ring buffer will not write to the currently
 *              rendering frame.
 *
 * @param ring_buffer Ring buffer containing the frame
 *
 * @param id ID of the frame we are currently rendering.
 *
 * @returns      A new pointer to the frame buffer that's now being rendered.
 *               This pointer will be invalidated on the next call to set_rendering.
 *               Use ->frame_buffer to read the contents of the captured frame
 */
FrameData* set_rendering(RingBuffer* ring_buffer, int id);

/**
 * @brief                          Skip the ring buffer to ID id,
 *                                 dropping all packets prior to that id
 *
 * @param ring_buffer              Ring buffer to use
 * @param id                       The ID to skip to
 */
void reset_stream(RingBuffer* ring_buffer, int id);

/**
 * @brief                          Try nacking or requesting a stream reset
 *
 * @param ring_buffer              Ring buffer to use
 * @param latency                  The latency, used in figuring out when to nack / stream reset
 * @param max_unordered_packets    The max number of packets that can arrive out-of-order
 * @param network_settings         NetworkSettings structure containing the current bitrate and
 *                                 burst bitrate
 * @param current_time             Current time as returned by start_timer()
 *
 * @note                           This will call the lambda nack_packet and request_stream_reset
 *                                 TODO: Just make this return an array of nacked packets,
 *                                       or a stream reset request with last failed ID
 */
void try_recovering_missing_packets_or_frames(RingBuffer* ring_buffer, double latency,
                                              int max_unordered_packets,
                                              NetworkSettings* network_settings,
                                              WhistTimer* current_time);

/**
 * @brief Destroy the ringbuffer and all associated memory
 *
 * @param ring_buffer Ring buffer to destroy
 */
void destroy_ring_buffer(RingBuffer* ring_buffer);

/**
 * @brief                          Get the number of fully received frames inside ring buffer, that
 *                                 is waiting for render.
 *
 * @param ring_buffer              Ring buffer to use
 * @returns                        Num of fully received frames inside ring buffer
 * @note                           This value by definition doesn't include the currently rendering
 * frame.
 */
int get_num_pending_ready_frames(RingBuffer* ring_buffer);

#endif
