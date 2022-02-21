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

#include <whist/video/transfercapture.h>
#include <whist/video/capture/capture.h>
#include <whist/video/codec/encode.h>
#include <whist/utils/avpacket_buffer.h>
#include <whist/logging/log_statistic.h>
#include <whist/network/network_algorithm.h>
#include <whist/network/throttle.h>
#include "whist/core/features.h"
#include "client.h"
#include "network.h"
#include "video.h"
#include "state.h"
#include "server_statistic.h"

#ifdef _WIN32
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
#define VBV_IN_SEC_BY_BURST_BITRATE_RATIO 0.1

static WhistSemaphore consumer;
static WhistSemaphore producer;
// send_populated_frames/send_empty_frame will populate one of the frame_buf's, and then wait
// While multithreaded_send_video_packets is working to send the other frame_buf over the network
static char encoded_frame_buf[2][LARGEST_VIDEOFRAME_SIZE];
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
    whist_server_state* state = (whist_server_state*)opaque;

    state->encoder_factory_result =
        create_video_encoder(state->encoder_factory_server_w, state->encoder_factory_server_h,
                             state->encoder_factory_client_w, state->encoder_factory_client_h,
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
 * @brief                   Creates a new CaptureDevice
 *
 * @param state				The whist server state
 * @param statistics_timer  Pointer to statistics timer used for logging
 * @param device            CaptureDevice pointer
 * @param rdevice           CaptureDevice pointer
 * @param encoder           VideoEncoder pointer
 * @param true_width        True width of client screen
 * @param true_height       True height of client screen
 * @return                  On success, 0. On failure, -1.
 */
static int32_t create_new_device(whist_server_state* state, WhistTimer* statistics_timer,
                                 CaptureDevice** device, CaptureDevice* rdevice,
                                 VideoEncoder** encoder, uint32_t true_width,
                                 uint32_t true_height) {
    start_timer(statistics_timer);
    *device = rdevice;
    if (create_capture_device(*device, true_width, true_height, state->client_dpi) < 0) {
        LOG_WARNING("Failed to create capture device");
        *device = NULL;
        state->update_device = true;

        whist_sleep(100);
        return -1;
    }

    LOG_INFO("Created a new Capture Device of dimensions %dx%d with DPI %d", (*device)->width,
             (*device)->height, state->client_dpi);

    // If an encoder is pending, while capture_device is updating, then we should wait
    // for it to be created
    while (state->pending_encoder) {
        if (state->encoder_finished) {
            *encoder = state->encoder_factory_result;
            state->pending_encoder = false;
            break;
        }
        whist_sleep(1);
    }

    // Next, we should update our ffmpeg encoder
    state->update_encoder = true;

    log_double_statistic(VIDEO_CAPTURE_CREATE_TIME, get_timer(statistics_timer) * MS_IN_SECOND);

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
static void send_populated_frames(whist_server_state* state, WhistTimer* statistics_timer,
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
    frame->corner_color = device->corner_color;
    frame->server_timestamp = server_timestamp;
    frame->client_input_timestamp = client_input_timestamp;

    static uint32_t last_cursor_hash = 0;

    start_timer(statistics_timer);
    WhistCursorInfo* current_cursor = get_current_cursor();
    log_double_statistic(VIDEO_GET_CURSOR_TIME, get_timer(statistics_timer) * MS_IN_SECOND);

    // On I-frames or new cursors, we want to pack the new cursor into the frame
    if ((VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type) ||
         current_cursor->hash != last_cursor_hash) &&
        current_cursor) {
        set_frame_cursor_info(frame, current_cursor);
        last_cursor_hash = current_cursor->hash;
    } else {
        set_frame_cursor_info(frame, NULL);
    }

    if (current_cursor) {
        free(current_cursor);
    }

    // Client needs to know about frame type to find recovery points.
    frame->frame_type = encoder->frame_type;

    frame->frame_id = id;

    frame->videodata_length = encoder->encoded_frame_size;

    write_avpackets_to_buffer(encoder->num_packets, encoder->packets,
                              (void*)get_frame_videodata(frame));
    whist_wait_semaphore(consumer);
    send_frame_id = id;
    currently_sending_index = 1 - currently_sending_index;

    if (VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type) || LOG_VIDEO) {
        LOG_INFO("Sent video packet %d (Size: %d) %s", id, encoder->encoded_frame_size,
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
 * @brief           Attempts to capture the screen. Afterwards sets update_device
 *                  to true
 *
 * @param state		the Whist server state
 * @param device    CaptureDevice pointer
 * @param encoder   VideoEncoder pointer
 */
static void retry_capture_screen(whist_server_state* state, CaptureDevice* device,
                                 VideoEncoder* encoder) {
    LOG_WARNING("Failed to capture screen");
    // The Nvidia Encoder must be wrapped in the lifetime of the capture device
    if (encoder != NULL && encoder->active_encoder == NVIDIA_ENCODER) {
        multithreaded_destroy_encoder(encoder);
        encoder = NULL;
    }
    destroy_capture_device(device);
    device = NULL;
    state->update_device = true;

    whist_sleep(100);
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
static void update_current_device(whist_server_state* state, WhistTimer* statistics_timer,
                                  CaptureDevice* device, VideoEncoder* encoder, uint32_t true_width,
                                  uint32_t true_height) {
    state->update_device = false;
    start_timer(statistics_timer);

    LOG_INFO("Received an update capture device request to dimensions %dx%d with DPI %d",
             true_width, true_height, state->client_dpi);

    // If a device already exists, we should reconfigure or destroy it
    if (device != NULL) {
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
    } else {
        LOG_INFO("No capture device exists yet, creating a new one.");
    }
    log_double_statistic(VIDEO_CAPTURE_UPDATE_TIME, get_timer(statistics_timer) * MS_IN_SECOND);
}

/**
 * @brief                         Sends an empty frame to the client
 *
 * @param state		the Whist server state
 * @param id        Pointer to the frame id
 */
static void send_empty_frame(whist_server_state* state, int id) {
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
static VideoEncoder* update_video_encoder(whist_server_state* state, VideoEncoder* encoder,
                                          CaptureDevice* device, int bitrate, CodecType codec,
                                          int fps, int vbv_size) {
    // If this is a new update encoder request, log it
    if (!state->pending_encoder) {
        LOG_INFO("Update encoder request received, will update the encoder now!");
    }

    // TODO: Make the encode take in a variable FPS
    if (fps != MAX_FPS) {
        LOG_ERROR("Setting FPS to anything but %d is not supported yet!", MAX_FPS);
    }

    // First, try to simply reconfigure the encoder to
    // handle the update_encoder event
    if (encoder != NULL) {
        // TODO: Use requested_video_fps as well
        if (reconfigure_encoder(encoder, device->width, device->height, bitrate, vbv_size, codec)) {
            // If we could update the encoder in-place, then we're done updating the encoder
            LOG_INFO("Reconfigured Encoder to %dx%d using Bitrate: %d, and Codec %d", device->width,
                     device->height, bitrate, (int)codec);
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

            // TODO: Use requested_video_fps
            LOG_INFO(
                "Creating a new Encoder of dimensions %dx%d using Bitrate: %d, and "
                "Codec %d",
                device->width, device->height, bitrate, (int)codec);
            state->encoder_finished = false;
            state->encoder_factory_server_w = device->width;
            state->encoder_factory_server_h = device->height;
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
    whist_server_state* state = (whist_server_state*)opaque;
    add_thread_to_client_active_dependents();
    bool assuming_client_active = false;
    WhistTimer statistics_timer;
    while (!state->exiting) {
        update_client_active_status(&state->client, &assuming_client_active);
        if (!assuming_client_active) {
            whist_sleep(1);
            continue;
        }
        whist_wait_semaphore(producer);
        start_timer(&statistics_timer);
        VideoFrame* frame = (VideoFrame*)encoded_frame_buf[currently_sending_index];
        int id = send_frame_id;
        if (state->client.is_active && !state->exiting) {
            send_packet(&state->client.udp_context, PACKET_VIDEO, frame,
                        get_total_frame_size(frame), id,
                        VIDEO_FRAME_TYPE_IS_RECOVERY_POINT(frame->frame_type));
        }
        whist_post_semaphore(consumer);
        log_double_statistic(VIDEO_SEND_TIME, get_timer(&statistics_timer) * MS_IN_SECOND);
        int index = 0;
        udp_reset_duplicate_packet_counter(&state->client.udp_context, PACKET_VIDEO);
        // If requested by the client, keep sending duplicate packets to saturate the network
        // bandwidth, till a new frame is available.
        while (state->client.is_active && !state->exiting && whist_semaphore_value(producer) == 0 &&
               network_settings.saturate_bandwidth) {
            update_client_active_status(&state->client, &assuming_client_active);
            if (!assuming_client_active) {
                break;
            }
            // Re-send all indices of this video frame in a round-robin manner
            udp_resend_packet(&state->client.udp_context, PACKET_VIDEO, id, index++);
            int num_indices = udp_get_num_indices(&state->client.udp_context, PACKET_VIDEO, id);
            if (num_indices < 0) {
                break;
            } else if (num_indices == index) {
                index = 0;
            }
        }
    }
    return 0;
}

/*
============================
Public Function Implementations
============================
*/

int32_t multithreaded_send_video(void* opaque) {
    whist_server_state* state = (whist_server_state*)opaque;

    whist_set_thread_priority(WHIST_THREAD_PRIORITY_REALTIME);

    whist_sleep(500);

#if defined(_WIN32)
    // set Windows DPI
    SetThreadDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
#endif

// When SAVE_VIDEO_OUTPUT is enabled the encoded video output is stored as per the filepath passed
// to fopen(). This is primarily used for debugging the protocol testing framework, where the client
// also runs in cloud. The saved h264 file can be transferred to the laptop and can be converted to
// mp4 using the ffmpeg command below.
// "ffmpeg -i output.h264 -c copy output.mp4"
// The mp4 output can be played in VLC media player (or any other player of your choice)
#if SAVE_VIDEO_OUTPUT
    FILE* fp = fopen("/var/log/whist/output.h264", "wb");
#endif

    // Capture Device
    CaptureDevice rdevice;
    CaptureDevice* device = NULL;

    init_cursors();

    VideoEncoder* encoder = NULL;

    WhistTimer world_timer;
    start_timer(&world_timer);

    WhistTimer statistics_timer;

    int id = 1;
    state->update_device = true;

    int start_frame_id = id;
    WhistTimer start_frame_timer;
    start_timer(&start_frame_timer);

    state->pending_encoder = false;
    state->encoder_finished = false;

    add_thread_to_client_active_dependents();

    NetworkSettings last_network_settings = {0};

    // Create producer-consumer semaphore pair with queue size of 1
    producer = whist_create_semaphore(0);
    consumer = whist_create_semaphore(1);
    WhistThread video_send_packets = whist_create_thread(multithreaded_send_video_packets,
                                                         "multithreaded_send_video_packets", state);

    int consecutive_identical_frames = 0;
    bool assuming_client_active = false;

    // Wait till client dimensions are available, so that we know the capture resolution
    while (!state->exiting &&
           (state->client_width == -1 || state->client_height == -1 || state->client_dpi == -1)) {
        whist_sleep(1);
    }

    // If we're exiting, then no need to do anything
    if (!state->exiting) {
        udp_handle_network_settings(
            state->client.udp_context.context,
            get_default_network_settings(state->client_width, state->client_height,
                                         state->client_dpi));
    }

    while (!state->exiting) {
        update_client_active_status(&state->client, &assuming_client_active);

        if (!assuming_client_active || !state->client.is_active) {
            whist_sleep(1);
            state->stream_needs_restart = true;
            continue;
        }

        static WhistTimer send_video_loop_timer;
        start_timer(&send_video_loop_timer);

        // Update device with new parameters

        // YUV pixel format requires the width to be a multiple of 4 and the height to be a
        // multiple of 2 (see `bRoundFrameSize` in NvFBC.h). By default, the dimensions will be
        // implicitly rounded up, but for some reason it looks better if we explicitly set the
        // size. Also for some reason it actually rounds the width to a multiple of 8.
        int true_width = state->client_width + 7 - ((state->client_width + 7) % 8);
        int true_height = state->client_height + 1 - ((state->client_height + 1) % 2);

        // If we got an update device request, we should update the device
        if (state->update_device) {
            update_current_device(state, &statistics_timer, device, encoder, true_width,
                                  true_height);
            state->stream_needs_restart = true;
        }

        // If no device is set, we need to create one
        if (device == NULL) {
            if (create_new_device(state, &statistics_timer, &device, &rdevice, &encoder, true_width,
                                  true_height) < 0) {
                continue;
            }
            state->stream_needs_restart = true;
        }

        network_settings = udp_get_network_settings(&state->client.udp_context);

        int video_bitrate =
            (network_settings.bitrate - (NUM_PREV_AUDIO_FRAMES_RESEND + 1) * AUDIO_BITRATE) *
            (1.0 - network_settings.video_fec_ratio);
        FATAL_ASSERT(video_bitrate > 0);
        CodecType video_codec = network_settings.desired_codec;
        // TODO: Use video_fps instead of max_fps, also see update_video_encoder when doing this
        int video_fps = network_settings.fps;

        if (memcmp(&network_settings, &last_network_settings, sizeof(NetworkSettings)) != 0) {
            // Mark to update the encode, if the network settings have been updated
            state->update_encoder = true;
            last_network_settings = network_settings;
        }

        // Update encoder with new parameters
        if (state->update_encoder) {
            start_timer(&statistics_timer);
            double burst_bitrate_ratio =
                (double)network_settings.burst_bitrate / network_settings.bitrate;
            int vbv_size =
                (VBV_IN_SEC_BY_BURST_BITRATE_RATIO * video_bitrate * burst_bitrate_ratio);
            encoder = update_video_encoder(state, encoder, device, video_bitrate, video_codec,
                                           video_fps, vbv_size);
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
            state->client.is_active ? udp_get_client_input_timestamp(&state->client.udp_context)
                                    : 0;

        // Check if the udp connection's stream has been reset
        bool pending_stream_reset =
            state->client.is_active &&
            get_pending_stream_reset(&state->client.udp_context, PACKET_VIDEO);
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
                log_double_statistic(VIDEO_FPS_SKIPPED_IN_CAPTURE, 1.0);
#if LOG_VIDEO
                LOG_INFO("Missed Frames! %d frames passed since last capture", accumulated_frames);
#endif
            }
            // If capture screen failed, we should try again
            if (accumulated_frames < 0) {
                retry_capture_screen(state, device, encoder);
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
                               !state->stream_needs_restart;
        // Lower the min_fps to DISABLED_ENCODER_FPS when the encoder is disabled
        int min_fps = disable_encoder ? DISABLED_ENCODER_FPS : MIN_FPS;

        // Reset the same regularly at every AVG_FPS_DURATION, to prevent any overcompensation in
        // the current fps due to a past low fps (which could occur due to any unpredictable
        // situation, such as network throttling)
        if (get_timer(&start_frame_timer) > AVG_FPS_DURATION) {
            LOG_INFO("Reset encoder FPS timer");
            start_timer(&start_frame_timer);
            start_frame_id = id;
        }

        // This outer loop potentially runs 10s of thousands of times per second, every ~1usec

        // Send a frame if we have a real frame to send, or we need to keep up with min_fps
        if (state->client.is_active &&
            (accumulated_frames > 0 || state->stream_needs_restart ||
             get_timer(&start_frame_timer) > (double)(id - start_frame_id) / min_fps)) {
            // This loop only runs ~1/current_fps times per second, every 16-100ms

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
                if (transfer_capture(device, encoder) != 0) {
                    // if there was a failure
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

                    // TODO: demote this log message once this is stable.
                    LOG_INFO("LTR action for frame ID %d: { %s, %d }", id,
                             video_frame_type_string(ltr_action.frame_type),
                             ltr_action.long_term_frame_index);

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
                    if (encoder->encoded_frame_size > (int)MAX_VIDEOFRAME_DATA_SIZE) {
                        // Please make MAX_VIDEOFRAME_DATA_SIZE larger if this error happens
                        LOG_ERROR("Frame of size %d bytes is too large! Dropping Frame.",
                                  encoder->encoded_frame_size);
                        continue;
                    } else {
#if SAVE_VIDEO_OUTPUT
                        for (int i = 0; i < encoder->num_packets; i++) {
                            fwrite(encoder->packets[i].data, encoder->packets[i].size, 1, fp);
                        }
#endif
                        send_populated_frames(state, &statistics_timer, &server_frame_timer, device,
                                              encoder, id, client_input_timestamp,
                                              server_timestamp);

                        log_double_statistic(VIDEO_FPS_SENT, 1.0);
                        log_double_statistic(VIDEO_FRAME_SIZE, encoder->encoded_frame_size);
                        log_double_statistic(VIDEO_FRAME_PROCESSING_TIME,
                                             get_timer(&server_frame_timer) * 1000);
                    }
                }
            }
        } else {
            whist_usleep(100);  // Sleep for 0.1ms before trying again.
        }
    }
#if SAVE_VIDEO_OUTPUT
    fclose(fp);
#endif
    whist_post_semaphore(producer);  // Post this to unblock the video_send_packets thread
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
