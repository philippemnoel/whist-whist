#ifndef RINGBUFFER_H
#define RINGBUFFER_H
/**
 * Copyright 2021 Whist Technologies, Inc.
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

#include <whist/core/whist.h>

/**
 * @brief FrameData struct containing content and metadata of encoded frames.
 * @details This is used to handle reconstruction of encoded frames from UDP packets. It contains
 * metadata to keep track of what packets we have received and nacked for, as well as a buffer for
 * holding the concatenated UDP packets.
 */
typedef struct FrameData {
    WhistPacketType type;
    int num_packets;
    int id;
    int packets_received;
    int frame_size;
    bool* received_indices;
    char* frame_buffer;

    // Nack logic

    // Whether or not we're in "recovery mode"
    bool recovery_mode;
    int* nacked_indices;
    int num_times_nacked;
    int last_nacked_index;
    clock last_nacked_timer;
    clock last_nonnack_packet_timer;
    clock frame_creation_timer;
} FrameData;

// Handler that gets called when the ring buffer wants to nack for a packet
typedef void (*NackPacketFn)(WhistPacketType frame_type, int id, int index);

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
    int largest_num_packets;
    NackPacketFn nack_packet;

    BlockAllocator* frame_buffer_allocator;  // unused if audio

    int currently_rendering_id;
    FrameData currently_rendering_frame;

    // *** START FOR BITRATE STAT CALCULATIONS ***
    int num_packets_nacked;
    int num_packets_received;
    int num_frames_skipped;
    int num_frames_rendered;
    // *** DONE FOR BITRATE STAT CALCULATIONS ***
    int frames_received;
    int max_id;

    // Nack variables

    // The next ID that should be rendered, marks
    // the lowest packet ID we're interested in nacking about
    int last_rendered_id;
    int last_missing_frame_nack;
} RingBuffer;

/**
 * @brief Initializes a ring buffer of the specified size and type.
 *
 * @param type Either an audio or video ring buffer.
 *
 * @param ring_buffer_size The desired size of the ring buffer
 *
 * @param nack_packet A lambda function that will be called when the ring buffer wants to nack.
 *                    NULL will disable nacking.
 *
 * @returns A pointer to the newly created ring buffer. All frames in the new ring buffer have ID
 * -1.
 */
RingBuffer* init_ring_buffer(WhistPacketType type, int ring_buffer_size,
                             NackPacketFn nack_packet);

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
 * @brief Add a packet to the ring buffer, and initialize the corresponding frame if necessary. Also
 * nacks for missing packets or frames.
 *
 * @param ring_buffer Ring buffer to place packet into
 *
 * @param packet An audio or video UDP packet
 *
 * @returns 0 on success, -1 on failure
 */
int receive_packet(RingBuffer* ring_buffer, WhistPacket* packet);

/**
 * @brief Free the ring buffer and all its constituent frames.
 *
 * @param ring_buffer Ring buffer to destroy
 */
void destroy_ring_buffer(RingBuffer* ring_buffer);

/**
 * @brief If any packets are still missing, and it's been too long, try nacking for them.
 *        Ideally, this gets called quite rapidly, it has internal timers to throttle nacks.
 *        The more rapidly the better, just need to balance CPU usage, 5-10ms should be fine.
 *
 * @param ring_buffer The ring buffer to try nacking with
 *
 * @param latency The round-trip latency of the connection. Helpful with nacking logic
 */
void try_nacking(RingBuffer* ring_buffer, double latency);

/**
 * @brief Reset the frame, both metadata and frame buffer.
 *
 * @param ring_buffer Ring buffer containing the frame.
 * @param frame_data Frame to "clear" from the ring buffer.
 */
void reset_frame(RingBuffer* ring_buffer, FrameData* frame_data);

/**
 * @brief       Indicate that the frame with ID id is currently rendering, and free the frame buffer
 *              for the previously rendered frame. The ring buffer will not write to the currently
 *              rendering frame.
 *
 * @param ring_buffer Ring buffer containing the frame
 *
 * @param id ID of the frame we are currently rendering.
 */
void set_rendering(RingBuffer* ring_buffer, int id);

#endif
