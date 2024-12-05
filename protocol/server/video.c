/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file video.c
 * @brief This file contains all code that interacts directly with processing
 *        video on the server.
============================
Usage
============================

multithreaded_send_video() is called on its own thread and loops repeatedly to capture and send
video.
*/

/*
============================
Includes
============================
*/

#include <whist/core/platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#if OS_IS(OS_WIN32)
#include <process.h>
#include <shlwapi.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <signal.h>
#include <unistd.h>
#endif

#include <whist/video/transfercapture.h>
#include <whist/video/capture/capture.h>
#include <whist/video/codec/encode.h>
#include <whist/utils/avpacket_buffer.h>
#include <whist/logging/log_statistic.h>
#include <whist/network/network_algorithm.h>
#include <whist/network/throttle.h>
#include <whist/utils/linked_list.h>
#include "whist/core/features.h"
#include "client.h"
#include "network.h"
#include "video.h"
#include "state.h"
#include <whist/utils/window_info.h>

#if OS_IS(OS_WIN32)
#pragma comment(lib, "ws2_32.lib")
#endif
// Linux shouldn't have this

#define USE_GPU 0
#define USE_MONITOR 0
#define SAVE_VIDEO_OUTPUT 0

// VBV Buffer size in seconds / Burst ratio. Setting it to a very low number as recomended for Ultra
// low latency applications
// https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-video-encoder-api-prog-guide/#recommended-nvenc-settings
// Please note that this number will be multiplied by BURST_BITRATE_RATIO to get the VBV size in sec
#define VBV_IN_SEC_BY_BURST_BITRATE_RATIO 0.2

static WhistSemaphore consumer;
static WhistSemaphore producer;
// send_populated_frames/send_empty_frame will populate one of the frame_buf's, and then wait
// While multithreaded_send_video_packets is working to send the other frame_buf over the network
static char encoded_frame_buf[2][LARGEST_VIDEOFRAME_SIZE];
static bool run_multithreaded_send_video_packets;
static int send_frame_id;
static int currently_sending_index;
static NetworkSettings network_settings;
/*
============================
Private Functions
============================
*/

static int32_t multithreaded_encoder_factory(void* opaque);
static int32_t multithreaded_destroy_encoder(void* opaque);

/*
============================
Private Function Implementations
============================
*/

static int32_t multithreaded_encoder_factory(void* opaque) {
    WhistServerState* state = (WhistServerState*)opaque;

    state->encoder_factory_result =
        create_video_encoder(state->encoder_factory_server_w, state->encoder_factory_server_h,
                             state->encoder_factory_bitrate, state->encoder_factory_vbv_size,
                             state->encoder_factory_codec_type);
    if (state->encoder_factory_result == NULL) {
        LOG_FATAL("Could not create an encoder, giving up!");
    }
    state->encoder_finished = true;
    return 0;
}

static int32_t multithreaded_destroy_encoder(void* opaque) {
    VideoEncoder* encoder = (VideoEncoder*)opaque;
    destroy_video_encoder(encoder);
    return 0;
}

/**
 * @brief                           Sends the populated video frames to the
 *                                  client
 *
 * @param state						server state
 * @param statistics_timer          Pointer to statistics timer used for logging
 * @param server_frame_timer        Pointer to server_frame_timer
 * @param device                    CaptureDevice pointer
 * @param encoder                   VideoEncoder pointer
 * @param id                        Pointer to frame id
 * @param client_input_timestamp    Estimated client timestamp at which user input is sent
 * @param server_timestamp          Server timestamp at which this frame is captured
 */
static void send_populated_frames(WhistServerState* state, WhistTimer* statistics_timer,
                                  WhistTimer* server_frame_timer, CaptureDevice* device,
                                  VideoEncoder* encoder, int id,
                                  timestamp_us client_input_timestamp,
                                  timestamp_us server_timestamp) {
    // transfer the capture of the latest frame from the device to
    // the encoder,
    // This function will try to CUDA/OpenGL optimize the transfer by
    // only passing a GPU reference rather than copy to/from the CPU

    // Create frame struct with compressed frame data and
    // metadata

    VideoFrame* frame = (VideoFrame*)encoded_frame_buf[1 - currently_sending_index];
    frame->width = encoder->out_width;
    frame->height = encoder->out_height;
    frame->codec_type = encoder->codec_type;
    frame->is_empty_frame = false;
    frame->is_window_visible = true;
    memcpy(frame->window_data, device->window_data, sizeof(frame->window_data));
    frame->corner_color = device->corner_color;
    frame->server_timestamp = server_timestamp;
    frame->client_input_timestamp = client_input_timestamp;

    start_timer(statistics_timer);
    WhistCursorInfo* current_cursor = whist_cursor_capture();
    FATAL_ASSERT(current_cursor != NULL);
    log_double_statistic(VIDEO_GET_CURSOR_TIME, get_timer(statistics_timer) * MS_IN_SECOND);

    // Client needs to know about frame type to find recovery points.
    frame->frame_type = encoder->frame_type;

    frame->frame_id = id;

    frame->videodata_length = (int)encoder->encoded_frame_size;

    // The cursor cache is reset on recovery points, since we can't
    // guaranteed that all previous cursors have been received.
    if (VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type)) {
        whist_cursor_cache_clear(state->cursor_cache);
        state->last_cursor_hash = 0;
    }

    // Store the cursor info in the frame struct
    if (current_cursor->hash == state->last_cursor_hash) {
        // Cursor has not changed.
        set_frame_cursor_info(frame, NULL);
    } else {
        // Cursor has changed, we need to send the new one.
        if (current_cursor->type == WHIST_CURSOR_PNG) {
            // If a PNG is being used,
            // Make sure to cache it
            const WhistCursorInfo* cached_cursor =
                whist_cursor_cache_check(state->cursor_cache, current_cursor->hash);
            if (cached_cursor) {
                // Use cached cursor.
                set_frame_cursor_info(frame, cached_cursor);
            } else {
                // Send new cursor.
                whist_cursor_cache_add(state->cursor_cache, current_cursor);
                set_frame_cursor_info(frame, current_cursor);
            }
        } else {
            set_frame_cursor_info(frame, current_cursor);
        }
        state->last_cursor_hash = current_cursor->hash;
    }
    free(current_cursor);

    // Write frame data to the frame struct
    write_avpackets_to_buffer(encoder->num_packets, encoder->packets, get_frame_videodata(frame));
    whist_wait_semaphore(consumer);
    send_frame_id = id;
    currently_sending_index = 1 - currently_sending_index;

    if (VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type) || LOG_VIDEO) {
        LOG_INFO("Sent video packet %d (Size: %zu) %s", id, encoder->encoded_frame_size,
                 video_frame_type_string(frame->frame_type));
    }

    whist_post_semaphore(producer);
    timestamp_us time_to_transmit =
        ((uint64_t)get_total_frame_size(frame) * BITS_IN_BYTE * US_IN_SECOND) /
        network_settings.burst_bitrate;
    // If the time to transmit this frame is more than one frame duration, then sleep for remaining
    // time to reduce the latency of next frame. If we capture the next frame early anyways network
    // throttler will make us wait thus increasing its latency(time from capture to render).
    if (time_to_transmit > AVG_FRAME_DURATION_IN_US) {
        whist_usleep((uint32_t)(time_to_transmit - AVG_FRAME_DURATION_IN_US));
    }
}

/**
 * @brief                  Updates dimensions of capture device and sets
 *                         update_encoder to true
 *
 * @param state			   The server state
 * @param statistics_timer Pointer to the timer used for statistics logging
 * @param device           CaptureDevice pointer
 * @param encoder          VideoEncoder pointer
 * @param true_width       True width of client screen
 * @param true_height      True height of client screen
 */
static void update_current_device(WhistServerState* state, WhistTimer* statistics_timer,
                                  CaptureDevice* device, VideoEncoder* encoder, uint32_t true_width,
                                  uint32_t true_height) {
    state->update_device = false;
    start_timer(statistics_timer);

    LOG_INFO("Received an update capture device request to dimensions %dx%d with DPI %d",
             true_width, true_height, state->client_dpi);

    if (reconfigure_capture_device(device, true_width, true_height, state->client_dpi)) {
        // Reconfigured the capture device!
        // No need to recreate it, the device has now been updated
        LOG_INFO("Successfully reconfigured the capture device");
        // We should also update the encoder since the device has been reconfigured
        state->update_encoder = true;
    } else {
        // Destroying the old capture device so that a new one can be recreated below
        LOG_FATAL(
            "Failed to reconfigure the capture device! We probably have a memory "
            "leak!");
        // "Destroying and recreating the capture device instead!");

        // For the time being, we have disabled the reconfigure functionality because
        // of some weirdness happening in vkCreateDevice()
    }

    log_double_statistic(VIDEO_CAPTURE_UPDATE_TIME, get_timer(statistics_timer) * MS_IN_SECOND);
}

/**
 * @brief                         Sends an empty frame to the client
 *
 * @param state		the Whist server state
 * @param id        Pointer to the frame id
 */
static void send_empty_frame(WhistServerState* state, int id) {
    // If we don't have a new frame to send, let's just send an empty one
    VideoFrame* frame = (VideoFrame*)encoded_frame_buf[1 - currently_sending_index];
    memset(frame, 0, sizeof(*frame));
    frame->is_empty_frame = true;
    // This signals that the screen hasn't changed, so don't bother rendering
    // this frame and just keep showing the last one.
    frame->is_window_visible = !state->stop_streaming;
    // We don't need to fill out the rest of the fields of the VideoFrame because
    // is_empty_frame is true, so it will just be ignored by the client.

    whist_wait_semaphore(consumer);
    // Increase the size of empty size frames during saturate bandwidth to prevent sending lot of
    // small packets. Lot of small packets means that we might hit the packet count limit on certain
    // networks.
    if (network_settings.saturate_bandwidth) {
        frame->videodata_length = MAX_PAYLOAD_SIZE - sizeof(VideoFrame);
    }
    send_frame_id = id;
    currently_sending_index = 1 - currently_sending_index;
    whist_post_semaphore(producer);
}

/**
 * @brief           Updates the encoder upon request. Note that this function
 *                  _returns_ the updated encoder, due to the encoder factory.
 *
 * @param state		The Whist server state
 * @param encoder   The previous VideoEncoder
 * @param device    The CaptureDevice
 * @param bitrate   The bitrate to encode at
 * @param codec     The codec to use
 * @param fps       The FPS to use
 *
 * @returns         The new encoder
 */
static VideoEncoder* update_video_encoder(WhistServerState* state, VideoEncoder* encoder,
                                          CaptureDevice* device, int bitrate, CodecType codec,
                                          int vbv_size) {
    // If this is a new update encoder request, log it
    if (!state->pending_encoder) {
        LOG_INFO("Update encoder request received, will update the encoder now!");
    }

    CaptureDeviceInfos* infos = &device->infos;
    // First, try to simply reconfigure the encoder to
    // handle the update_encoder event
    if (encoder != NULL) {
        if (reconfigure_encoder(encoder, infos->width, infos->height, bitrate, vbv_size, codec)) {
            // If we could update the encoder in-place, then we're done updating the encoder
            LOG_INFO("Reconfigured Encoder to %dx%d using Bitrate: %d, and Codec %d", infos->width,
                     infos->height, bitrate, (int)codec);
            state->update_encoder = false;
        } else {
            // TODO: Make LOG_ERROR after ffmpeg reconfiguration is implemented
            LOG_INFO("Reconfiguration failed! Creating a new encoder!");
        }
    }

    // If reconfiguration didn't happen, we still need to update the encoder
    if (state->update_encoder) {
        // Otherwise, this capture device must use an external encoder,
        // so we should start making it in our encoder factory
        if (state->pending_encoder) {
            if (state->encoder_finished) {
                // Once encoder_finished, we'll destroy the old one that we've been using,
                // and replace it with the result of multithreaded_encoder_factory
                if (encoder) {
                    WhistThread encoder_destroy_thread = whist_create_thread(
                        multithreaded_destroy_encoder, "multithreaded_destroy_encoder", encoder);
                    whist_detach_thread(encoder_destroy_thread);
                }
                encoder = state->encoder_factory_result;
                state->pending_encoder = false;
                state->update_encoder = false;
            }
        } else {
            // Starting making new encoder. This will set pending_encoder=true, but won't
            // actually update it yet, we'll still use the old one for a bit

            LOG_INFO(
                "Creating a new Encoder of dimensions %dx%d using Bitrate: %d, and "
                "Codec %d",
                infos->width, infos->height, bitrate, (int)codec);
            state->encoder_finished = false;
            state->encoder_factory_server_w = infos->width;
            state->encoder_factory_server_h = infos->height;
            state->encoder_factory_client_w = (int)state->client_width;
            state->encoder_factory_client_h = (int)state->client_height;
            state->encoder_factory_codec_type = codec;
            state->encoder_factory_bitrate = bitrate;
            state->encoder_factory_vbv_size = vbv_size;

            // If using nvidia, then we must destroy the existing encoder first
            // We can't have two nvidia encoders active or the 2nd attempt to
            // create one will fail
            // If using ffmpeg, if the dimensions don't match, then we also need to destroy
            // the old encoder, since we'll no longer be able to pass captured frames into
            // it
            // For now, we'll just always destroy the encoder right here
            if (encoder != NULL && encoder->active_encoder == NVIDIA_ENCODER) {
                destroy_video_encoder(encoder);
                encoder = NULL;
            }

            if (encoder == NULL) {
                // Run on this thread bc we have to wait for it anyway since encoder == NULL
                multithreaded_encoder_factory(state);
                encoder = state->encoder_factory_result;
                state->pending_encoder = false;
                state->update_encoder = false;
            } else {
                WhistThread encoder_creator_thread = whist_create_thread(
                    multithreaded_encoder_factory, "multithreaded_encoder_factory", state);
                whist_detach_thread(encoder_creator_thread);
                state->pending_encoder = true;
            }
        }
    }
    return encoder;
}

// Video packet sending over UDP is done a separate thread to maximally utilize the available
// bandwidth, without dropping frames.
static int32_t multithreaded_send_video_packets(void* opaque) {
    WhistServerState* state = (WhistServerState*)opaque;
    // Information to resend the previous frame
    int last_id = -1;
    int previous_connection_id = -1;
    int packet_sent = -1;
    int index = 0;
    while (true) {
        // TODO: Move to UDP's update_socket, and remove the sleep-loop
        // Till a new frame, send any nack or duplicate packets as required
        if (whist_semaphore_value(producer) == 0 && packet_sent != -1) {
            bool did_work = false;

            ClientLock* client_lock = client_active_trylock(state->client);
            if (client_lock != NULL) {
                // If there are nack packets to send, then don't send duplicate packets
                if (udp_handle_pending_nacks(state->client->udp_context.context)) {
                    did_work = true;
                } else if (network_settings.saturate_bandwidth &&
                           state->client->connection_id == previous_connection_id) {
                    // TODO: Make network saturation work, even when connection_id is new
                    // If requested by the client, keep sending duplicate packets to saturate the
                    // network bandwidth, till a new frame is available. Re-send all indices of this
                    // video frame in a round-robin manner
                    udp_resend_packet(&state->client->udp_context, PACKET_VIDEO, last_id, index);
                    index++;
                    int num_indices =
                        udp_get_num_indices(&state->client->udp_context, PACKET_VIDEO, last_id);
                    if (num_indices < 0) {
                        // Something wrong happened
                        LOG_ERROR("udp_get_num_indices returned %d", num_indices);
                        index = 0;
                        did_work = false;
                    } else if (num_indices == index) {
                        index = 0;
                        did_work = true;
                    }
                }

                client_active_unlock(client_lock);
            }

            if (!did_work) {
                // Sleep for 0.1ms before checking again.
                whist_usleep(100);
            }
            continue;
        }

        // Wait for us to receive a video frame
        whist_wait_semaphore(producer);
        // Exit if this was an exit state
        if (!run_multithreaded_send_video_packets) {
            break;
        }

        // Consume the video frame
        WhistTimer statistics_timer;
        start_timer(&statistics_timer);
        VideoFrame* frame = (VideoFrame*)encoded_frame_buf[currently_sending_index];
        ClientLock* client_lock = client_active_trylock(state->client);
        if (client_lock != NULL) {
            packet_sent = send_packet(&state->client->udp_context, PACKET_VIDEO, frame,
                                      get_total_frame_size(frame), send_frame_id,
                                      VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type));
            if (packet_sent != 0) {
                LOG_WARNING("Failed to send the video packet!");
            }
            previous_connection_id = state->client->connection_id;
            last_id = send_frame_id;
            client_active_unlock(client_lock);
        }
        // Mark the video frame as sent
        whist_post_semaphore(consumer);
        log_double_statistic(VIDEO_SEND_TIME, get_timer(&statistics_timer) * MS_IN_SECOND);
        udp_reset_duplicate_packet_counter(&state->client->udp_context, PACKET_VIDEO);
        // Variables for network saturation resending
        index = 0;
    }

    // Post this to unblock any `multithreaded_send_video()` semaphore waits
    whist_post_semaphore(consumer);
    return 0;
}

/*
============================
Public Function Implementations
============================
*/

int32_t multithreaded_send_video(void* opaque) {
    WhistServerState* state = (WhistServerState*)opaque;

    whist_set_thread_priority(WHIST_THREAD_PRIORITY_REALTIME);

    whist_sleep(500);

#if OS_IS(OS_WIN32)
    // set Windows DPI
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
#endif

    // When SAVE_VIDEO_OUTPUT is enabled the encoded video output is stored as per the filepath
    // passed to fopen(). This is primarily used for debugging the protocol testing framework, where
    // the client also runs in cloud. The saved h264 file can be transferred to the laptop and can
    // be converted to mp4 using the ffmpeg command below. "ffmpeg -i output.h264 -c copy
    // output.mp4" The mp4 output can be played in VLC media player (or any other player of your
    // choice)
    FILE* fp;
    if (SAVE_VIDEO_OUTPUT) {
        fp = fopen("/var/log/whist/output.h264", "wb");
    }

    CaptureDevice* device = &state->capture_device;

    whist_cursor_capture_init();

    VideoEncoder* encoder = NULL;

    WhistTimer world_timer;
    start_timer(&world_timer);

    WhistTimer statistics_timer;

    int id = 1;
    state->update_device = true;

    int start_frame_id = id;
    WhistTimer start_frame_timer;
    start_timer(&start_frame_timer);
    WhistTimer last_frame_timer;
    start_timer(&last_frame_timer);

    state->pending_encoder = false;
    state->encoder_finished = false;

    NetworkSettings last_network_settings = {0};

    // Create producer-consumer semaphore pair with queue size of 1
    producer = whist_create_semaphore(0);
    consumer = whist_create_semaphore(1);
    run_multithreaded_send_video_packets = true;
    WhistThread video_send_packets = whist_create_thread(multithreaded_send_video_packets,
                                                         "multithreaded_send_video_packets", state);

    int consecutive_identical_frames = 0;

    // Wait for the client to lock
    int previous_connection_id = -1;
    bool initialized_network_settings = false;
    ClientLock* client_lock = client_active_lock(state->client);

    // The video loop
    while (client_lock != NULL) {
        // Refresh the client activation lock, to let the client (re/de)activate if it's trying to
        client_active_unlock(client_lock);
        client_lock = client_active_lock(state->client);
        if (client_lock == NULL) {
            break;
        }

        // If a new connection occured, restart the stream
        if (previous_connection_id != state->client->connection_id) {
            state->stream_needs_restart = true;
            initialized_network_settings = false;
            previous_connection_id = state->client->connection_id;
        }

        // Wait till client dimensions are available, so that we know the capture resolution
        // TODO: Make this information part of the init handshake, so we don't have to busy-loop for
        // this information
        if (!initialized_network_settings) {
            if (state->client_width == -1 || state->client_height == -1 ||
                state->client_dpi == -1) {
                whist_sleep(1);
                continue;
            } else {
                // Update the network settings based on the above information
                udp_handle_network_settings(
                    state->client->udp_context.context,
                    get_default_network_settings(state->client_width, state->client_height,
                                                 state->client_dpi));
                initialized_network_settings = true;
            }
        }

        // Update device with new parameters

        // Don't round the width or height explictly. Both X11 and nvidia capture will provide ARGB
        // output with non-rounded exact width and height. But the encoder will implicitly round off
        // the width and height to the nearest even number. On the client-side,
        //  - Chromium client will crop off the extra rounded pixels before rendering.
        //  - SDL client will do the rounding before sending this resolution to server. So the
        //  capture resolution and encoder resolution will always be same for SDL client.
        int true_width = state->client_width;
        int true_height = state->client_height;

        // If we got an update device request, we should update the device
        if (state->update_device) {
            update_current_device(state, &statistics_timer, device, encoder, true_width,
                                  true_height);
            state->stream_needs_restart = true;
        }

        network_settings = udp_get_network_settings(&state->client->udp_context);

        int video_bitrate =
            network_settings.video_bitrate * (1.0 - network_settings.video_fec_ratio);
        FATAL_ASSERT(video_bitrate > 0);
        CodecType video_codec = network_settings.desired_codec;

        if (memcmp(&network_settings, &last_network_settings, sizeof(NetworkSettings)) != 0) {
            // Mark to update the encode, if the network settings have been updated
            state->update_encoder = true;
            last_network_settings = network_settings;
        }

        // Update encoder with new parameters
        if (state->update_encoder) {
            start_timer(&statistics_timer);
            double burst_bitrate_ratio =
                (double)network_settings.burst_bitrate / network_settings.video_bitrate;
            int vbv_size =
                (VBV_IN_SEC_BY_BURST_BITRATE_RATIO * video_bitrate * burst_bitrate_ratio);
            encoder =
                update_video_encoder(state, encoder, device, video_bitrate, video_codec, vbv_size);
            log_double_statistic(VIDEO_ENCODER_UPDATE_TIME,
                                 get_timer(&statistics_timer) * MS_IN_SECOND);
        }

        if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
            // If any frame acks have been received, tell the frame type
            // decision logic about them.
            if (state->update_frame_ack) {
                ltr_mark_frame_received(state->ltr_context, state->frame_ack_id);
                state->update_frame_ack = false;
            }
        }

        // Get this timestamp before we capture the screen,
        // To measure the full pre-capture to post-render E2E latency
        timestamp_us server_timestamp = current_time_us();
        timestamp_us client_input_timestamp =
            udp_get_client_input_timestamp(&state->client->udp_context);

        // Check if the udp connection's stream has been reset
        bool pending_stream_reset =
            get_pending_stream_reset(&state->client->udp_context, PACKET_VIDEO);
        if (pending_stream_reset) {
            state->stream_needs_recovery = true;
        }

        // SENDING LOGIC:
        // first, we call capture_screen, which returns how many frames have passed since the last
        // call to capture_screen, If we are using Nvidia, the captured frame is also
        // hardware-encoded. Otherwise, we pass the most recent frame to our encoder and call
        // video_encoder_encode_frame on it. Then, we send encoded frames to the client. If the
        // frames we encode + send are a subset of the frames we capture, that means we are dropping
        // frames, which is suboptimal and should be investigated.

        // Accumulated_frames is equal to how many frames have passed since the
        // last call to CaptureScreen
        int accumulated_frames = 0;
        if ((!state->stop_streaming || state->stream_needs_restart)) {
            start_timer(&statistics_timer);
            accumulated_frames = capture_screen(device);
            if (accumulated_frames > 1) {
                log_double_statistic(VIDEO_FRAMES_SKIPPED_IN_CAPTURE, (accumulated_frames - 1));
                if (LOG_VIDEO) {
                    LOG_INFO("Missed Frames! %d frames passed since last capture",
                             accumulated_frames);
                }
            }
            // If capture screen failed, we should try again
            if (accumulated_frames < 0) {
                continue;
            }
            // Immediately bring consecutives to 0, when a new frame is captured
            if (accumulated_frames > 0) {
                consecutive_identical_frames = 0;
                log_double_statistic(VIDEO_CAPTURE_SCREEN_TIME,
                                     get_timer(&statistics_timer) * MS_IN_SECOND);
            }
        }

        WhistTimer server_frame_timer;
        start_timer(&server_frame_timer);

        // Disable the encoder when we've sent enough identical frames,
        // And no iframe is being requested at this time.
        // When the encoder is disabled, we only wake the client CPU,
        // DISABLED_ENCODER_FPS times per second, for just a usec at a time.
        bool disable_encoder = consecutive_identical_frames > CONSECUTIVE_IDENTICAL_FRAMES &&
                               !state->stream_needs_restart && !state->stream_needs_recovery;
        // Lower the min_fps to DISABLED_ENCODER_FPS when the encoder is disabled
        int min_fps = disable_encoder ? DISABLED_ENCODER_FPS : MIN_FPS;

        // Reset the same regularly at every AVG_FPS_DURATION, to prevent any overcompensation in
        // the current fps due to a past low fps (which could occur due to any unpredictable
        // situation, such as network throttling)
        if (get_timer(&start_frame_timer) > AVG_FPS_DURATION) {
            start_timer(&start_frame_timer);
            start_frame_id = id;
        }

        // This outer loop potentially runs 10s of thousands of times per second, every ~1usec

        // Send a frame if we have a real frame to send, or we need to keep up with min_fps
        if ((accumulated_frames > 0 || state->stream_needs_restart ||
             (get_timer(&start_frame_timer) > (double)(id - start_frame_id) / min_fps &&
              get_timer(&last_frame_timer) > 1.0 / min_fps))) {
            // This loop only runs ~1/current_fps times per second, every 16-100ms
            start_timer(&last_frame_timer);

            if (accumulated_frames == 0) {
                // Slowly increment while receiving identical frames
                consecutive_identical_frames++;
            }
            if (accumulated_frames > 1) {
                LOG_INFO("Accumulated Frames: %d", accumulated_frames);
            }

            // Increment the Frame ID so that each frame we send gets its own unique ID.
            // The decoder will ensure to decode frames in the order of these IDs,
            // _Or_ skip to the next I-Frame.
            id++;

            if (disable_encoder) {
                // Send an empty frame
                send_empty_frame(state, id);
            } else {
                // transfer the capture of the latest frame from the device to
                // the encoder,
                // This function will try to CUDA/OpenGL optimize the transfer by
                // only passing a GPU reference rather than copy to/from the CPU
                start_timer(&statistics_timer);
                if (transfer_capture(device, encoder, &state->stream_needs_restart) != 0) {
                    // If there was a failure, exit
                    LOG_ERROR("transfer_capture failed! Exiting!");
                    state->exiting = true;
                    break;
                }
                log_double_statistic(VIDEO_CAPTURE_TRANSFER_TIME,
                                     get_timer(&statistics_timer) * MS_IN_SECOND);

                VideoFrameType frame_type;
                if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
                    if (state->stream_needs_restart || state->stream_needs_recovery) {
                        if (state->stream_needs_restart) {
                            ltr_force_intra(state->ltr_context);
                        } else {
                            ltr_mark_stream_broken(state->ltr_context);
                        }
                        state->stream_needs_restart = false;
                        state->stream_needs_recovery = false;
                    }

                    LTRAction ltr_action;
                    ltr_get_next_action(state->ltr_context, &ltr_action, id);

                    if (LOG_LONG_TERM_REFERENCE_FRAMES) {
                        LOG_INFO("LTR action for frame ID %d: { %s, %d }", id,
                                 video_frame_type_string(ltr_action.frame_type),
                                 ltr_action.long_term_frame_index);
                    }

                    video_encoder_set_ltr_action(encoder, &ltr_action);
                    frame_type = ltr_action.frame_type;
                } else {
                    if (state->stream_needs_restart || state->stream_needs_recovery) {
                        video_encoder_set_iframe(encoder);
                        frame_type = VIDEO_FRAME_TYPE_INTRA;
                    } else {
                        frame_type = VIDEO_FRAME_TYPE_NORMAL;
                    }
                    state->stream_needs_restart = false;
                    state->stream_needs_recovery = false;
                }

                start_timer(&statistics_timer);

                int res = video_encoder_encode(encoder);
                if (res < 0) {
                    // bad boy error
                    LOG_ERROR("Error encoding video frame!");
                    state->exiting = true;
                    break;
                } else if (res > 0) {
                    // filter graph is empty
                    LOG_ERROR("video_encoder_encode filter graph failed! Exiting!");
                    state->exiting = true;
                    break;
                }
                if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
                    // Ensure that the encoder actually generated the
                    // frame type we expected.  If it didn't then
                    // something has gone horribly wrong.
                    FATAL_ASSERT(encoder->frame_type == frame_type);
                }
                log_double_statistic(VIDEO_ENCODE_TIME,
                                     get_timer(&statistics_timer) * MS_IN_SECOND);

                if (encoder->encoded_frame_size != 0) {
                    if (encoder->encoded_frame_size > MAX_VIDEOFRAME_DATA_SIZE) {
                        // Please make MAX_VIDEOFRAME_DATA_SIZE larger if this error happens
                        LOG_ERROR("Frame of size %zu bytes is too large! Dropping Frame.",
                                  encoder->encoded_frame_size);
                        continue;
                    } else {
                        if (SAVE_VIDEO_OUTPUT) {
                            for (int i = 0; i < encoder->num_packets; i++) {
                                fwrite(encoder->packets[i]->data, encoder->packets[i]->size, 1, fp);
                            }
                            fflush(fp);
                        }
                        send_populated_frames(state, &statistics_timer, &server_frame_timer, device,
                                              encoder, id, client_input_timestamp,
                                              server_timestamp);

                        log_double_statistic(VIDEO_FPS_SENT, 1.0);
                        log_double_statistic(VIDEO_FRAME_SIZE, encoder->encoded_frame_size);
                        log_double_statistic(VIDEO_FRAME_PROCESSING_TIME,
                                             get_timer(&server_frame_timer) * MS_IN_SECOND);
                        if (VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(encoder->frame_type))
                            log_double_statistic(VIDEO_NUM_RECOVERY_FRAMES, 1.0);
                    }
                }
            }
        } else {
            whist_usleep(100);  // Sleep for 0.1ms before trying again.
        }
    }

    // If we're holding a client lock, unlock it
    if (client_lock != NULL) {
        client_active_unlock(client_lock);
    }

    whist_cursor_capture_destroy();

    if (SAVE_VIDEO_OUTPUT) {
        fclose(fp);
    }
    // Post this to unblock `multithreaded_send_video_packets()` semaphore waits
    run_multithreaded_send_video_packets = false;
    whist_post_semaphore(producer);
    whist_wait_thread(video_send_packets, NULL);
    whist_destroy_semaphore(consumer);
    whist_destroy_semaphore(producer);
    // The Nvidia Encoder must be wrapped in the lifetime of the capture device,
    // So we destroy the encoder first
    if (encoder) {
        multithreaded_destroy_encoder(encoder);
        encoder = NULL;
    }
    if (device) {
        destroy_capture_device(device);
        device = NULL;
    }

    return 0;
}
