#include "ringbuffer.h"
#include <assert.h>
#include <whist/utils/fec.h>

#define MAX_RING_BUFFER_SIZE 500
#define MAX_VIDEO_PACKETS 500
#define MAX_AUDIO_PACKETS 3
#define MAX_UNORDERED_PACKETS 10

#define MAX_PACKETS (get_num_fec_packets(max(MAX_VIDEO_PACKETS, MAX_AUDIO_PACKETS), MAX_FEC_RATIO))

void reset_ring_buffer(RingBuffer* ring_buffer);
void init_frame(RingBuffer* ring_buffer, int id, int num_original_indices, int num_fec_indices);

void reset_ring_buffer(RingBuffer* ring_buffer) {
    /*
        Reset the ring buffer, making it forget about all of the packets that it has received.
        This will bring it to the state that it was originally initialized into
    */

    // Note that we do not wipe currently_rendering_frame,
    // since someone else might still be using it
    for (int i = 0; i < ring_buffer->ring_buffer_size; i++) {
        FrameData* frame_data = &ring_buffer->receiving_frames[i];
        if (frame_data->id != -1) {
            reset_frame(ring_buffer, frame_data);
        }
    }
    ring_buffer->max_id = -1;
    ring_buffer->frames_received = 0;
}

void reset_bitrate_stat_members(RingBuffer* ring_buffer) {
    /*
        Reset all accumulators used for calculating bitrate stats to 0.

        NOTE: This is separate from `reset_ring_buffer` because the
        `calculate_statistics` function takes care of resting these
        members when necessary.
    */

    ring_buffer->num_packets_nacked = 0;
    ring_buffer->num_packets_received = 0;
    ring_buffer->num_frames_rendered = 0;
    ring_buffer->total_delay_gradient = 0.0;
    ring_buffer->total_delay_gradient_squared = 0.0;
    ring_buffer->num_gradient_frames_tracked = 0;
}

RingBuffer* init_ring_buffer(WhistPacketType type, int ring_buffer_size, NackPacketFn nack_packet) {
    /*
        Initialize the ring buffer; malloc space for all the frames and set their IDs to -1.

        Arguments:
            type (WhistPacketType): audio or video
            ring_buffer_size (int): Desired size of the ring buffer. If the size exceeds
       MAX_RING_BUFFER_SIZE, no ring buffer is returned.

        Returns:
            (RingBuffer*): pointer to the created ring buffer
            */
    if (ring_buffer_size > MAX_RING_BUFFER_SIZE) {
        LOG_ERROR("Requested ring buffer size %d too large - ensure size is at most %d",
                  ring_buffer_size, MAX_RING_BUFFER_SIZE);
        return NULL;
    }
    RingBuffer* ring_buffer = safe_malloc(sizeof(RingBuffer));
    if (!ring_buffer) {
        return NULL;
    }

    ring_buffer->type = type;
    ring_buffer->ring_buffer_size = ring_buffer_size;
    ring_buffer->receiving_frames = safe_malloc(ring_buffer_size * sizeof(FrameData));
    ring_buffer->nack_packet = nack_packet;
    if (!ring_buffer->receiving_frames) {
        return NULL;
    }

    // Mark all the frames as uninitialized
    for (int i = 0; i < ring_buffer_size; i++) {
        FrameData* frame_data = &ring_buffer->receiving_frames[i];
        frame_data->id = -1;
    }

    // determine largest frame size
    ring_buffer->largest_frame_size =
        ring_buffer->type == PACKET_VIDEO ? LARGEST_VIDEOFRAME_SIZE : LARGEST_AUDIOFRAME_SIZE;

    ring_buffer->packet_buffer_allocator = create_block_allocator(ring_buffer->largest_frame_size);
    ring_buffer->currently_rendering_id = -1;
    ring_buffer->last_rendered_id = -1;
    ring_buffer->last_missing_frame_nack = -1;

    // set all additional metadata for frames and ring buffer
    reset_ring_buffer(ring_buffer);
    reset_bitrate_stat_members(ring_buffer);

    return ring_buffer;
}

FrameData* get_frame_at_id(RingBuffer* ring_buffer, int id) {
    /*
        Retrieve the frame in ring_buffer of ID id. Currently, does not check that the ID of the
       retrieved frame is actually the desired ID.

        Arguments:
            ring_buffer (RingBuffer*): the ring buffer holding the frame
            id (int): desired frame ID

        Returns:
            (FrameData*): Pointer to FrameData at the correct index
            */
    return &ring_buffer->receiving_frames[id % ring_buffer->ring_buffer_size];
}

void init_frame(RingBuffer* ring_buffer, int id, int num_original_indices, int num_fec_indices) {
    /*
        Initialize a frame with num_indices indices and ID id. This allocates the frame buffer and
        the arrays we use for metadata.

        Arguments:
            ring_buffer (RingBuffer*): ring buffer containing the frame
            id (int): desired frame ID
            num_indices (int): number of indices in the frame
    */

    FrameData* frame_data = get_frame_at_id(ring_buffer, id);

    // Confirm that the frame is uninitialized
    FATAL_ASSERT(frame_data->id == -1);

    // Initialize new framedata
    memset(frame_data, 0, sizeof(*frame_data));
    frame_data->id = id;
    frame_data->packet_buffer = allocate_block(ring_buffer->packet_buffer_allocator);
    frame_data->original_packets_received = 0;
    frame_data->fec_packets_received = 0;
    frame_data->num_original_packets = num_original_indices;
    frame_data->num_fec_packets = num_fec_indices;
    frame_data->received_indices = safe_malloc(MAX_PACKETS * sizeof(bool));
    frame_data->num_times_index_nacked = safe_malloc(MAX_PACKETS * sizeof(int));
    memset(frame_data->received_indices, 0, MAX_PACKETS * sizeof(bool));
    memset(frame_data->num_times_index_nacked, 0, MAX_PACKETS * sizeof(int));
    frame_data->last_nacked_index = -1;
    frame_data->num_times_nacked = 0;
    frame_data->type = ring_buffer->type;
    frame_data->recovery_mode = false;
    start_timer(&frame_data->frame_creation_timer);
    start_timer(&frame_data->last_nacked_timer);

    // Initialize FEC-related things, if we need to
    if (num_fec_indices > 0) {
        frame_data->fec_decoder =
            create_fec_decoder(num_original_indices, num_fec_indices, MAX_PAYLOAD_SIZE);
        frame_data->fec_frame_buffer = allocate_block(ring_buffer->packet_buffer_allocator);
        frame_data->successful_fec_recovery = false;
    }
}

void reset_frame(RingBuffer* ring_buffer, FrameData* frame_data) {
    /*
       Reset the frame's frame buffer and its metadata. Useful for when we're skipping frames and
       don't want to leave stale frames in the buffer.

        Arguments:
            ring_buffer (RingBuffer*): ring buffer containing the frame
            frame_data (FrameData*): frame to reset
    */

    if (frame_data->id == -1) {
        LOG_FATAL("Tried to call reset_frame on a frame that's already reset!");
    }

    // Free the frame's data
    free_block(ring_buffer->packet_buffer_allocator, frame_data->packet_buffer);
    free(frame_data->received_indices);
    free(frame_data->num_times_index_nacked);

    // Free FEC-related data
    if (frame_data->num_fec_packets > 0) {
        destroy_fec_decoder(frame_data->fec_decoder);
        free_block(ring_buffer->packet_buffer_allocator, frame_data->fec_frame_buffer);
    }

    // Mark as uninitialized
    memset(frame_data, 0, sizeof(*frame_data));
    frame_data->id = -1;
    frame_data->frame_buffer = NULL;
    frame_data->frame_buffer_size = 0;
}

// Get a pointer to a framebuffer for that id, if such a framebuffer is possible to construct
char* get_framebuffer(RingBuffer* ring_buffer, FrameData* current_frame) {
    if (current_frame->num_fec_packets > 0) {
        if (current_frame->successful_fec_recovery) {
            return current_frame->fec_frame_buffer;
        } else {
            return NULL;
        }
    } else {
        if (current_frame->original_packets_received == current_frame->num_original_packets) {
            return current_frame->packet_buffer;
        } else {
            return NULL;
        }
    }
}

bool is_ready_to_render(RingBuffer* ring_buffer, int id) {
    FrameData* current_frame = get_frame_at_id(ring_buffer, id);
    // A frame ID ready to render, if the ID exists in the ringbuffer,
    if (current_frame->id != id) {
        return false;
    }
    // and if getting a framebuffer out of it is possible
    return get_framebuffer(ring_buffer, current_frame) != NULL;
}

FrameData* set_rendering(RingBuffer* ring_buffer, int id) {
    /*
        Indicate that the frame with ID id is currently rendering and free the frame buffer for the
        previously rendering frame. Ownership of the frame buffer for the rendering frame is
        transferred to ring_buffer->currently_rendering_frame, allowing us to fully wipe the ring
        buffer's receiving_frames array if we fall too behind.

        Arguments:
            ring_buffer (RingBuffer*): ring buffer metadata to change
            id (int): ID of the frame we are currently rendering.
    */

    if (id <= ring_buffer->last_rendered_id) {
        LOG_FATAL("Tried to call set_rendering on an ID %d <= the last rendered ID %d", id,
                  ring_buffer->last_rendered_id);
    }

    // Set first, so that last_rendered_id is updated
    ring_buffer->last_rendered_id = id;

    if (ring_buffer->currently_rendering_id != -1) {
        // Reset the now unwanted currently rendering frame
        reset_frame(ring_buffer, &ring_buffer->currently_rendering_frame);
    }

    // Move Frame ID "id", from the ring buffer and into currently_rendering_frame
    FATAL_ASSERT(is_ready_to_render(ring_buffer, id));

    // Move frame from current_frame, to currently_rendering_frame
    FrameData* current_frame = get_frame_at_id(ring_buffer, id);
    ring_buffer->currently_rendering_id = id;
    ring_buffer->currently_rendering_frame = *current_frame;

    // Invalidate the current_frame, without deallocating its data with reset_frame,
    // Since currently_rendering_frame now owns that data
    memset(current_frame, 0, sizeof(*current_frame));
    current_frame->id = -1;

    // Set the framebuffer pointer of the currently rendering frame
    ring_buffer->currently_rendering_frame.frame_buffer =
        get_framebuffer(ring_buffer, &ring_buffer->currently_rendering_frame);

    // Track for statistics
    ring_buffer->num_frames_rendered++;

    // Return the currently rendering frame
    return &ring_buffer->currently_rendering_frame;
}

int receive_packet(RingBuffer* ring_buffer, WhistPacket* packet) {
    /*
        Process a WhistPacket and add it to the ring buffer. If the packet belongs to an existing
        frame, copy its data into the frame; if it belongs to a new frame, initialize the frame and
        copy data. Nack for missing packets (of the packet's frame) and missing frames (before the
        current frame).

        Arguments:
            ring_buffer (RingBuffer*): ring buffer to place the packet in
            packet (WhistPacket*): UDP packet for either audio or video

        Returns:
            (int): 1 if we overwrote a valid frame, 0 on success, -1 on failure
    */

    // Sanity check the packet's metadata
    FATAL_ASSERT(0 <= packet->index && packet->index < packet->num_indices);
    FATAL_ASSERT(packet->num_indices <= MAX_PACKETS);
    FATAL_ASSERT(packet->num_fec_indices < packet->num_indices);

    FrameData* frame_data = get_frame_at_id(ring_buffer, packet->id);

    ring_buffer->num_packets_received++;

    // TODO: Get rid of confusing 'num_overwritten_frames' return variable
    int num_overwritten_frames = 0;

    // If packet->id != frame_data->id, handle the situation
    if (packet->id < frame_data->id) {
        // This packet must be from a very stale frame,
        // because the current ringbuffer occupant already contains packets with a newer ID in it
        LOG_WARNING("Very stale packet (ID %d) received, current ringbuffer occupant's ID %d",
                    packet->id, frame_data->id);
        return -1;
    } else if (packet->id <= ring_buffer->currently_rendering_id) {
        // This packet won't help us render any new packets,
        // So we can safely just ignore it
        return 0;
    } else if (packet->id > frame_data->id) {
        // This packet is newer than the resident,
        // so it's time to overwrite the resident if such a resident exists
        if (frame_data->id != -1) {
            num_overwritten_frames = 1;
            if (frame_data->id > ring_buffer->currently_rendering_id) {
                // We have received a packet which will overwrite a frame that needs to be rendered
                // in the future. In other words, the ring buffer is full, so we should wipe the
                // whole ring buffer.
                LOG_WARNING(
                    "We received a packet with Frame ID %d, that is trying to overwrite Frame ID "
                    "%d!\n"
                    "But we can't overwrite that frame, since our renderer has only gotten to ID "
                    "%d!\n"
                    "Resetting the entire ringbuffer...",
                    packet->id, frame_data->id, ring_buffer->currently_rendering_id);
                num_overwritten_frames = packet->id - ring_buffer->currently_rendering_id - 1;
                reset_ring_buffer(ring_buffer);
            } else {
                // Here, the frame is older than where our renderer is,
                // So we can just reset the undesired frame
                LOG_ERROR(
                    "Trying to allocate Frame ID %d, but Frame ID %d has not been destroyed yet!",
                    packet->id, frame_data->id);
                reset_frame(ring_buffer, frame_data);
            }
        }

        // Initialize the frame now, so that it can hold the packet we just received
        int num_original_packets = packet->num_indices - packet->num_fec_indices;
        init_frame(ring_buffer, packet->id, num_original_packets, packet->num_fec_indices);

        // Update the ringbuffer's max id, with this new frame's ID
        ring_buffer->max_id = max(ring_buffer->max_id, frame_data->id);
    }

    // Now, the frame_data should be ready to accept the packet
    FATAL_ASSERT(packet->id == frame_data->id);

    // Verify that the packet metadata matches frame_data metadata
    FATAL_ASSERT(frame_data->num_fec_packets == packet->num_fec_indices);
    FATAL_ASSERT(frame_data->num_original_packets + frame_data->num_fec_packets ==
                 packet->num_indices);

    // LOG the the nacking situation
    // TODO: log video vs audio
    if (packet->is_a_nack) {
        if (!frame_data->received_indices[packet->index]) {
            LOG_INFO("NACK for ID %d, Index %d received!", packet->id, packet->index);
        } else {
            LOG_INFO("NACK for ID %d, Index %d received, but didn't need it.", packet->id,
                     packet->index);
        }
    } else {
        // Reset timer since the last time we received a non-nack packet
        start_timer(&frame_data->last_nonnack_packet_timer);
        if (frame_data->num_times_index_nacked[packet->index] > 0) {
            LOG_INFO("Received original ID %d, Index %d, but we had NACK'ed for it.", packet->id,
                     packet->index);
        }
    }

    // If we have already received this packet anyway, just drop this packet
    if (frame_data->received_indices[packet->index]) {
        // The only way it should possible to receive a packet twice, is if nacking got involved
        if (frame_data->num_times_index_nacked[packet->index] == 0) {
            LOG_ERROR(
                "We received a packet (ID %d / index %d) twice, but we had never nacked for it?",
                packet->id, packet->index);
            return -1;
        }
        return 0;
    }

    // Remember whether or not this frame was ready to render
    bool was_already_ready = is_ready_to_render(ring_buffer, packet->id);

    // Track whether the index we received is one of the N original packets,
    // or one of the M FEC packets
    frame_data->received_indices[packet->index] = true;
    if (packet->index < frame_data->num_original_packets) {
        frame_data->original_packets_received++;
        FATAL_ASSERT(frame_data->original_packets_received <= frame_data->num_original_packets);
    } else {
        frame_data->fec_packets_received++;
    }

    // Copy the packet's payload into the correct location in frame_data's frame buffer
    int buffer_offset = packet->index * MAX_PAYLOAD_SIZE;
    if (buffer_offset + packet->payload_size >= ring_buffer->largest_frame_size) {
        LOG_ERROR("Packet payload too large for frame buffer! Dropping the packet...");
        return -1;
    }
    memcpy(frame_data->packet_buffer + buffer_offset, packet->data, packet->payload_size);

    // If this frame isn't an fec frame, the frame_buffer_size is just the sum of the payload sizes
    if (frame_data->num_fec_packets == 0) {
        frame_data->frame_buffer_size += packet->payload_size;
    }

    // If this is an FEC frame, and we haven't yet decoded the frame successfully,
    // Try decoding the FEC frame
    if (frame_data->num_fec_packets > 0 && !frame_data->successful_fec_recovery) {
        // Register this packet into the FEC decoder
        fec_decoder_register_buffer(frame_data->fec_decoder, packet->index,
                                    frame_data->packet_buffer + buffer_offset,
                                    packet->payload_size);

        // Using the newly registered packet, try to decode the frame using FEC
        int frame_size =
            fec_get_decoded_buffer(frame_data->fec_decoder, frame_data->fec_frame_buffer);

        // If we were able to successfully decode the frame, mark it as such!
        if (frame_size >= 0) {
            if (frame_data->original_packets_received < frame_data->num_original_packets) {
                LOG_INFO("Successfully recovered %d/%d Packet %d, using %d FEC packets",
                         frame_data->original_packets_received, frame_data->num_original_packets,
                         frame_data->id, frame_data->fec_packets_received);
            }
            // Save the frame buffer size of the fec frame,
            // And mark the fec recovery as succeeded
            frame_data->frame_buffer_size = frame_size;
            frame_data->successful_fec_recovery = true;
        }
    }

    static timestamp_us prev_recvside_ts = 0;
    static timestamp_us prev_sendside_ts = 0;

    if (is_ready_to_render(ring_buffer, packet->id) && !was_already_ready) {
        ring_buffer->frames_received++;
        // Update bitrate delay gradient statistics
        VideoFrame* video_frame;
        if (frame_data->successful_fec_recovery) {
            video_frame = (VideoFrame*)frame_data->fec_frame_buffer;
        } else {
            video_frame = (VideoFrame*)frame_data->packet_buffer;
        }
        if (!prev_recvside_ts || !prev_sendside_ts) {
            prev_recvside_ts = current_time_us();
            prev_sendside_ts = video_frame->server_timestamp;
        } else {
            // Delay gradient: difference in time first packet sent and last packet received for
            // frame
            int delay_gradient = (current_time_us() - prev_recvside_ts) -
                                 (video_frame->server_timestamp - prev_sendside_ts);
            ring_buffer->total_delay_gradient += delay_gradient;
            ring_buffer->total_delay_gradient_squared += delay_gradient * delay_gradient;
            ring_buffer->num_gradient_frames_tracked++;
        }
    }

    return num_overwritten_frames;
}

void nack_single_packet(RingBuffer* ring_buffer, int id, int index) {
    ring_buffer->num_packets_nacked++;
    // If a nacking function was passed in, use it
    if (ring_buffer->nack_packet) {
        ring_buffer->nack_packet(ring_buffer->type, id, index);
    }
}

void nack_bitarray_packets(RingBuffer* ring_buffer, int id, int start_index, BitArray* bit_arr) {
    /*
        Nack the packets at ID id and start_index start_index.

        Arguments:
            ring_buffer (RingBuffer*): the ring buffer waiting for the packet
            id (int): Frame ID of the packet
            start_index (int): index of the first packet to be NACKed
            bit_arr (BitArray): the bit array with the indexes of the packets to NACK.
    */

    LOG_INFO("NACKing with bit array for Packets with ID %d, Starting Index %d", id, start_index);
    WhistClientMessage fcmsg = {0};
    fcmsg.type = MESSAGE_BITARRAY_NACK;
    fcmsg.bitarray_nack.type = ring_buffer->type;
    fcmsg.bitarray_nack.id = id;
    fcmsg.bitarray_nack.index = start_index;
    memset(fcmsg.bitarray_nack.ba_raw, 0, BITS_TO_CHARS(bit_arr->numBits));
    memcpy(fcmsg.bitarray_nack.ba_raw, bit_array_get_bits(bit_arr),
           BITS_TO_CHARS(bit_arr->numBits));
    fcmsg.bitarray_nack.numBits = bit_arr->numBits;

    for (unsigned int i = 0; i < bit_arr->numBits; i++) {
        if (bit_array_test_bit(bit_arr, i)) {
            ring_buffer->num_packets_nacked++;
        }
    }

    // send_fcmsg(&fcmsg);
}

// The max number of times we can nack a packet: limited to 2 times right now so that we don't get
// stuck on a packet that never arrives
#define MAX_PACKET_NACKS 2
// Maximum amount of mbps that can be used by nacking
// This is calculated per 100ms interval
#define MAX_NACK_AVG_MBPS 2200000
// Maximum burst mbps that can be used by nacking
// This is calculated per 5ms interval
#define MAX_NACK_BURST_MBPS 4800000

int nack_missing_packets_up_to_index(RingBuffer* ring_buffer, FrameData* frame_data, int end_index,
                                     int max_packets_to_nack) {
    /*
        Nack up to 1 missing packet up to index

        Arguments:
            ring_buffer (RingBuffer*): ring buffer missing packets
            frame_data (FrameData*): frame missing packets
            end_index (int): the index up to which to nack missing packets
            max_packets_to_nack (int): Most amount of packets we want to nack for

        Returns:
            The number of packets Nacked
    */

    // Note that an invalid last_nacked_index is set to -1, correctly starting us at 0
    int start_index = frame_data->last_nacked_index + 1;

    // Something really large, static because this only gets called from one thread
    static char nack_log_buffer[1024 * 32];
    int log_len = 0;
    log_len += snprintf(nack_log_buffer + log_len, sizeof(nack_log_buffer) - log_len,
                        "NACKing Frame ID %d, Indices ", frame_data->id);

    int num_packets_nacked = 0;
    for (int i = start_index; i <= end_index && num_packets_nacked < max_packets_to_nack; i++) {
        if (!frame_data->received_indices[i] &&
            frame_data->num_times_index_nacked[i] < MAX_PACKET_NACKS) {
            nack_single_packet(ring_buffer, frame_data->id, i);
            log_len += snprintf(nack_log_buffer + log_len, sizeof(nack_log_buffer) - log_len,
                                "%s%d", num_packets_nacked == 0 ? "" : ", ", i);
            frame_data->num_times_index_nacked[i]++;
            frame_data->last_nacked_index = i;
            num_packets_nacked++;
        }
    }

    if (num_packets_nacked > 0) {
        LOG_INFO("%s", nack_log_buffer);
    }

    return num_packets_nacked;
}

bool try_nacking(RingBuffer* ring_buffer, double latency) {
    if (ring_buffer->max_id == -1) {
        // Don't nack if we haven't received anything yet
        // Return true, our nacking vacuously succeeded
        return true;
    }
    if (ring_buffer->last_rendered_id == -1) {
        ring_buffer->last_rendered_id = ring_buffer->max_id - 1;
    }

    static bool first_call = true;
    static clock burst_timer;
    static clock avg_timer;
    static int burst_counter;
    static int avg_counter;

    double burst_interval = 5.0 / MS_IN_SECOND;
    double avg_interval = 100.0 / MS_IN_SECOND;

    if (first_call || get_timer(burst_timer) > burst_interval) {
        burst_counter = 0;
        start_timer(&burst_timer);
    }
    if (first_call || get_timer(avg_timer) > avg_interval) {
        avg_counter = 0;
        start_timer(&avg_timer);
        first_call = false;
    }

    // MAX_MBPS * interval / MAX_PAYLOAD_SIZE is the amount of nack payloads allowed in each
    // interval The XYZ_counter is the amount of packets we've already sent in that interval We
    // subtract the two, to get the max nacks that we're allowed to send at this point in time We
    // min to take the stricter restriction of either burst or average
    int max_nacks_remaining =
        MAX_NACK_BURST_MBPS * burst_interval / MAX_PAYLOAD_SIZE - burst_counter;
    int avg_nacks_remaining = MAX_NACK_AVG_MBPS * avg_interval / MAX_PAYLOAD_SIZE - avg_counter;
    int max_nacks = (int)min(max_nacks_remaining, avg_nacks_remaining);
    // Note how the order-of-ops ensures arithmetic is done with double's for higher accuracy

    static bool last_nack_possibility = true;
    if (max_nacks <= 0) {
        // We can't nack, so just exit. Also takes care of negative case from above calculation.
        if (last_nack_possibility) {
            LOG_INFO("Can't nack anymore! Hit NACK bitrate limit. Try increasing NACK bitrate?");
            last_nack_possibility = false;
        }
        // Nacking has failed when avg_nacks has been saturated.
        // If max_nacks has been saturated, that's just burst bitrate distribution
        return avg_nacks_remaining > 0;
    } else {
        if (!last_nack_possibility) {
            LOG_INFO("NACKing is possible again.");
            last_nack_possibility = true;
        }
    }

    // Track how many nacks we've made this call, to keep it under max_nacks
    int num_packets_nacked = 0;

    // last_missing_frame_nack is strictly increasing so it doesn't need to be throttled
    // Non recovery mode last_packet index is strictly increasing so it doesn't need to be throttled
    // Recovery mode cycles through trying to nack, and we throttle to ~latency,
    // longer during consecutive cycles

    // Nack all the packets we might want to nack about, from oldest to newest,
    // up to max_nacks times
    for (int id = ring_buffer->last_rendered_id + 1;
         id <= ring_buffer->max_id && num_packets_nacked < max_nacks; id++) {
        FrameData* frame_data = get_frame_at_id(ring_buffer, id);
        // If this frame doesn't exist, skip it
        if (frame_data->id != id) {
            // If we've received nothing from a frame before max_id,
            // let's try nacking for index 0 of it
            if (ring_buffer->last_missing_frame_nack < id) {
                // Nack the first set of indices of the missing frame
                // Frames 10-15 packets in size, we'd like to recover in one RTT,
                // But we don't want to try to recover 200 packet frames so quickly
                for (int index = 0; index <= 20; index++) {
                    nack_single_packet(ring_buffer, id, index);
                    num_packets_nacked++;
                }
                LOG_INFO("NACKing for missing Frame ID %d", id);
                ring_buffer->last_missing_frame_nack = id;
            }
            continue;
        }
        // If this frame has been entirely received, there's nothing to nack for
        if (is_ready_to_render(ring_buffer, id)) {
            continue;
        }

        // =======
        // Go through the frame looking for packets to nack
        // =======

        // Get the last index we received
        int last_packet_received = 0;
        for (int i = frame_data->num_original_packets - 1; i >= 0; i--) {
            if (frame_data->received_indices[i]) {
                last_packet_received = i;
                break;
            }
        }

        // If too much time has passed since the last packet received,
        // we swap into *recovery mode*, since something is probably wrong with this packet
        if ((id < ring_buffer->max_id ||
             get_timer(frame_data->last_nonnack_packet_timer) > 0.2 * latency) &&
            !frame_data->recovery_mode) {
#if LOG_NACKING
            LOG_INFO("Too long since last non-nack packet from ID %d. Entering recovery mode...",
                     id);
#endif
            frame_data->recovery_mode = true;
        }

        // Track packets nacked this frame
        int packets_nacked_this_frame = 0;

        if (!frame_data->recovery_mode) {
            // During *normal nacking mode*,
            // we nack for packets that are more than MAX_UNORDERED_PACKETS "out of order"
            // E.g., if we get 1 2 3 4 5 9, and MAX_UNORDERED_PACKETS = 3, then
            // 5 is considered to be too far from 9 to still be unreceived simply due to UDP
            // reordering.
            packets_nacked_this_frame += nack_missing_packets_up_to_index(
                ring_buffer, frame_data, last_packet_received - MAX_UNORDERED_PACKETS,
                max_nacks - num_packets_nacked);
#if LOG_NACKING
            if (packets_nacked_this_frame > 0) {
                LOG_INFO("~~ Frame ID %d Nacked for %d out-of-order packets", id,
                         packets_nacked_this_frame);
            }
#endif
        } else {
            // *Recovery mode* means something is wrong with the network,
            // and we should keep trying to nack for those missing packets.
            // On the first round, we finish up the work that the *normal nacking mode* did,
            // i.e. we nack for everything after last_packet_received - MAX_UNORDERED_PACKETS.
            // After an additional 1.2 * latency, we send another round of nacks
            if (get_timer(frame_data->last_nacked_timer) >
                1.2 * latency * frame_data->num_times_nacked) {
#if LOG_NACKING
                LOG_INFO("Attempting to recover Frame ID %d, %d/%d indices received.", id,
                         frame_data->packets_received, frame_data->num_packets);
#endif
                packets_nacked_this_frame = nack_missing_packets_up_to_index(
                    ring_buffer, frame_data, frame_data->num_original_packets - 1,
                    max_nacks - num_packets_nacked);
                if (frame_data->last_nacked_index == frame_data->num_original_packets - 1) {
                    frame_data->last_nacked_index = -1;
                    start_timer(&frame_data->last_nacked_timer);

                    frame_data->num_times_nacked++;
#if LOG_NACKING
                    LOG_INFO("- Done with Nacking cycle #%d, restarting cycle",
                             frame_data->num_times_nacked);
#endif
                }
#if LOG_NACKING
                LOG_INFO("~~ Frame ID %d Nacked for %d packets in recovery mode", id,
                         packets_nacked_this_frame);
#endif
            }
        }

        // Add to total
        num_packets_nacked += packets_nacked_this_frame;
    }

#if LOG_NACKING
    if (num_packets_nacked > 0) {
        LOG_INFO("Nacked %d/%d packets this Nacking round", num_packets_nacked, max_nacks);
    }
#endif

    // Update the counters to track max nack bitrate
    burst_counter += num_packets_nacked;
    avg_counter += num_packets_nacked;

    // Nacking succeeded
    return true;
}

void destroy_ring_buffer(RingBuffer* ring_buffer) {
    /*
        Destroy ring_buffer: free all frames and any malloc'ed data, then free the ring buffer

        Arguments:
            ring_buffer (RingBuffer*): ring buffer to destroy
    */

    // First, wipe the ring buffer
    reset_ring_buffer(ring_buffer);
    // If a frame was held by the renderer then also clear it.
    // (The renderer has already been destroyed when we get here.)
    if (ring_buffer->currently_rendering_id != -1)
        reset_frame(ring_buffer, &ring_buffer->currently_rendering_frame);
    // free received_frames
    free(ring_buffer->receiving_frames);
    // Destroy the allocator.
    destroy_block_allocator(ring_buffer->packet_buffer_allocator);
    // free ring_buffer
    free(ring_buffer);
}
