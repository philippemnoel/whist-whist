/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file encode.c
 * @brief This file contains the code to create and destroy Encoders and use
 *        them to encode captured screens.
============================
Usage
============================
Video is encoded to H264 via either a hardware encoder (currently, we use NVidia GPUs, so we use
NVENC) or a software encoder. H265 is also supported but not currently used. Since NVidia allows us
to both capture and encode the screen, most of the functions will be called in server/main.c with an
empty dummy encoder. For encoders, create an H264 encoder via create_video_encode, and use
it to encode frames via video_encoder_encode. Write the encoded output via
video_encoder_write_buffer, and when finished, destroy the encoder using destroy_video_encoder.
*/

/*
============================
Includes
============================
*/
#include "encode.h"

#include <libavfilter/avfilter.h>
#include <stdio.h>
#include <stdlib.h>

#include "whist/core/error_codes.h"
#include "whist/core/features.h"
/*
============================
Private Functions
============================
*/

static void transfer_nvidia_data(VideoEncoder *encoder);
static int transfer_ffmpeg_data(VideoEncoder *encoder);

/*
============================
Private Function Implementations
============================
*/

static AVPacket *alloc_packet(VideoEncoder *encoder, int index) {
    AVPacket *pkt = encoder->packets[index];
    if (pkt) {
        av_packet_unref(pkt);
    } else {
        pkt = av_packet_alloc();
        FATAL_ASSERT(pkt);
        encoder->packets[index] = pkt;
    }
    return pkt;
}

static void transfer_nvidia_data(VideoEncoder *encoder) {
    /*
        Set encoder metadata according to nvidia_encoder members and tell the encoder there is only
       one packet, containing the encoded frame data from the nvidia encoder.

        Arguments:
            encoder (VideoEncoder*): encoder to use
    */
    if (encoder->active_encoder != NVIDIA_ENCODER) {
        LOG_ERROR("Encoder is not using nvidia, but we are trying to call transfer_nvidia_data!");
        return;
    }
    NvidiaEncoder *nvidia_encoder = encoder->nvidia_encoders[encoder->active_encoder_idx];
    // Set meta data
    encoder->frame_type = nvidia_encoder->frame_type;
    encoder->out_width = nvidia_encoder->width;
    encoder->out_height = nvidia_encoder->height;

    AVPacket *pkt;
    if (encoder->bsf) {
        // Apply output filtering.
        int err;

        pkt = av_packet_alloc();
        FATAL_ASSERT(pkt);

        // Bitstream filters require a refcounted packet, so we can't
        // avoid this copy.
        err = av_new_packet(pkt, nvidia_encoder->frame_size);
        FATAL_ASSERT(err == 0);
        memcpy(pkt->data, nvidia_encoder->frame, pkt->size);

        err = av_bsf_send_packet(encoder->bsf, pkt);
        FATAL_ASSERT(err == 0);

        av_packet_free(&pkt);

        size_t size = 4;
        int i;
        for (i = 0; i < MAX_ENCODER_PACKETS; i++) {
            pkt = alloc_packet(encoder, i);

            err = av_bsf_receive_packet(encoder->bsf, pkt);
            if (err == AVERROR(EAGAIN)) {
                break;
            }
            FATAL_ASSERT(err == 0);

            size += 4 + pkt->size;
        }

        encoder->num_packets = i;
        encoder->encoded_frame_size = size;
    } else {
        pkt = alloc_packet(encoder, 0);

        pkt->data = nvidia_encoder->frame;
        pkt->size = nvidia_encoder->frame_size;

        encoder->num_packets = 1;
        encoder->encoded_frame_size = 8 + nvidia_encoder->frame_size;
    }
}

static WhistStatus create_output_filter(VideoEncoder *encoder) {
    if (encoder->codec_type != CODEC_TYPE_H264) {
        // This only applies to H.264.
        return WHIST_SUCCESS;
    }

    // This bitstream filter is used to override some internal
    // properties of the stream which may not be set correctly for our
    // use-case with long-term reference frames.

    const char *bsf_name = "h264_constraint";
    int err;

    const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsf_name);
    if (!bsf) {
        LOG_WARNING("Failed to find %s BSF.", bsf_name);
        goto fail;
    }
    err = av_bsf_alloc(bsf, &encoder->bsf);
    if (err < 0) {
        LOG_ERROR("Failed to open %s BSF: %d.", bsf_name, err);
        goto fail;
    }

    AVCodecParameters *par = encoder->bsf->par_in;
    par->codec_type = AVMEDIA_TYPE_VIDEO;
    par->codec_id = AV_CODEC_ID_H264;

    // Set gaps_in_frame_num_value_allowed_flag on.
    // This indicates to the decoder that frame_num values in the
    // bitstream may contain gaps, so it should not attempt to wait for
    // the next frame_num value.  This is required because gaps can be
    // introduced when recovering with a reference to a long-term frame,
    // where an unknown number of frames before the recovery point are
    // discarded.
    err = av_opt_set_int(encoder->bsf->priv_data, "gaps_in_frame_num_value_allowed_flag", 1, 0);
    if (err < 0) {
        LOG_ERROR("Failed to set gaps_in_frame_num_value_allowed_flag on %s BSF: %d.", bsf_name,
                  err);
        goto fail;
    }

    // Set max_num_reorder_frames to zero.
    // This indicates to the decoder that frames should be output
    // immediately after decoding with no delay.  This is needed because
    // gaps in the stream may cause the decoder to think that frames are
    // out of order when frame numbering wraps, which it can react to by
    // introducing unwanted delay.
    err = av_opt_set_int(encoder->bsf->priv_data, "max_num_reorder_frames", 0, 0);
    if (err < 0) {
        LOG_ERROR("Failed to set max_num_reorder_frames on %s BSF: %d.", bsf_name, err);
        goto fail;
    }

    err = av_bsf_init(encoder->bsf);
    if (err < 0) {
        LOG_ERROR("Failed to init %s BSF: %d.", bsf_name, err);
        goto fail;
    }

    LOG_INFO("Created output filter.");
    return WHIST_SUCCESS;

fail:
    av_bsf_free(&encoder->bsf);
    return WHIST_ERROR_EXTERNAL;
}

static int transfer_ffmpeg_data(VideoEncoder *encoder) {
    /*
        Set encoder metadata according to nvidia encoder members and receive all encoded packets
       from the ffmpeg encoder to the main encoder.

        Arguments:
            encoder (VideoEncoder*): encoder to use

        Returns:
            (int): 0 on success, -1 on failure
    */
    if (encoder->active_encoder != FFMPEG_ENCODER) {
        LOG_ERROR("Encoder is not using ffmpeg, but we are trying to call transfer_ffmpge_data!");
        return -1;
    }

    // The encoded frame size starts out as sizeof(int) because of the way we send packets. See
    // encode.h and decode.h.
    encoder->encoded_frame_size = sizeof(int);
    encoder->num_packets = 0;
    int res;

    // receive packets until we receive a nonzero code (indicating either an encoding error, or that
    // all packets have been received).
    while (1) {
        AVPacket *pkt = alloc_packet(encoder, encoder->num_packets);

        res = ffmpeg_encoder_receive_packet(encoder->ffmpeg_encoder, pkt);
        if (res != 0) {
            break;
        }
        encoder->encoded_frame_size += 4 + pkt->size;
        encoder->num_packets++;
        if (encoder->num_packets == MAX_ENCODER_PACKETS) {
            LOG_ERROR("TOO MANY PACKETS: REACHED %d", encoder->num_packets);
            return -1;
        }
    }
    if (res < 0) {
        return -1;
    }

    // set iframe metadata
    encoder->out_width = encoder->ffmpeg_encoder->out_width;
    encoder->out_height = encoder->ffmpeg_encoder->out_height;
    encoder->frame_type = encoder->ffmpeg_encoder->frame_type;
    return 0;
}

/*
============================
Public Function Implementations
============================
*/

VideoEncoder *create_video_encoder(int width, int height, int bitrate, int vbv_size,
                                   CodecType codec_type) {
    /*
       Create a video encoder with the specified parameters. Try Nvidia first if available, and fall
       back to FFmpeg if not.

        Arguments:
            width (int): Width of the frames that the encoder encodes
            height (int): height of the frames that the encoder encodes
            bitrate (int): bits per second the encoder will encode to
            codec_type (CodecType): Codec (currently H264 or H265) the encoder will use

        Returns:
            (VideoEncoder*): the newly created encoder
     */

    VideoEncoder *encoder = (VideoEncoder *)safe_malloc(sizeof(VideoEncoder));
    memset(encoder, 0, sizeof(VideoEncoder));
    encoder->in_width = width;
    encoder->in_height = height;
    encoder->codec_type = codec_type;

    if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
        // When long-term reference frames are enabled we need to make
        // sure that the output stream has the right constraints encoded
        // in it.  The Nvidia encoder does not, so this extra filter is
        // required to fix the output.
        create_output_filter(encoder);
        // TODO: when we have other encoders supporting long-term
        // reference frames, disable this if they can directly generate
        // the expected stream.
    }

#if OS_IS(OS_LINUX) && USING_NVIDIA_ENCODE
    LOG_INFO("Creating nvidia encoder...");

    // find next nonempty entry in nvidia_encoders
    encoder->nvidia_encoders[0] = create_nvidia_encoder(
        bitrate, codec_type, width, height, vbv_size, *get_video_thread_cuda_context_ptr());

    if (encoder->nvidia_encoders[0]) {
        LOG_INFO("Created nvidia encoder!");
        // nvidia creation succeeded!
        encoder->active_encoder = NVIDIA_ENCODER;
        encoder->active_encoder_idx = 0;
        return encoder;
    }

    LOG_ERROR("Failed to create nvidia encoder!");
#endif  // OS_IS(OS_LINUX) && USING_NVIDIA_ENCODE

    encoder->active_encoder = FFMPEG_ENCODER;

    LOG_INFO("Creating ffmpeg encoder...");
    encoder->ffmpeg_encoder =
        create_ffmpeg_encoder(width, height, width, height, bitrate, vbv_size, codec_type);
    if (!encoder->ffmpeg_encoder) {
        LOG_ERROR("FFmpeg encoder creation failed!");
        return NULL;
    }
    LOG_INFO("Created ffmpeg encoder!");

    return encoder;
}

int video_encoder_encode(VideoEncoder *encoder) {
    /*
        Encode a frame using the encoder and store the resulting packets into encoder->packets. If
       the frame has already been encoded, this function does nothing.

        Arguments:
            encoder (VideoEncoder*): encoder that will encode the frame

        Returns:
            (int): 0 on success, -1 on failure
     */
    if (!encoder) {
        LOG_ERROR("Tried to video_encoder_encode with a NULL encoder!");
        return -1;
    }
    switch (encoder->active_encoder) {
        case NVIDIA_ENCODER: {
#if OS_IS(OS_LINUX)
            NvidiaEncoder *nv_encoder = encoder->nvidia_encoders[encoder->active_encoder_idx];
            nv_encoder->ltr_action = encoder->next_ltr_action;
            nvidia_encoder_encode(nv_encoder);
            transfer_nvidia_data(encoder);
            return 0;
#else
            LOG_FATAL("NVIDIA_ENCODER should not be used on Windows!");
            UNUSED(transfer_nvidia_data);
#endif
        }
        case FFMPEG_ENCODER:
            encoder->ffmpeg_encoder->ltr_action = encoder->next_ltr_action;
            if (ffmpeg_encoder_send_frame(encoder->ffmpeg_encoder)) {
                LOG_ERROR("Unable to send frame to encoder!");
                return -1;
            }
            return transfer_ffmpeg_data(encoder);
        default:
            LOG_ERROR("Unknown encoder type: %d!", encoder->active_encoder);
            return -1;
    }
}

bool reconfigure_encoder(VideoEncoder *encoder, int width, int height, int bitrate, int vbv_size,
                         CodecType codec) {
    /*
        Attempt to reconfigure the encoder to use the specified width, height, bitrate, and codec.
       Unavailable on FFmpeg, but possible on Nvidia.

        Arguments:
            encoder (VideoEncoder*): encoder to reconfigure
            width (int): new width
            height (int): new height
            bitrate (int): new bitrate
            codec (CodecType): new codec

        Returns:
            (bool): true on success, false on failure
    */
    if (!encoder) {
        LOG_ERROR("Calling reconfigure_encoder on a NULL encoder!");
        return false;
    }
    encoder->in_width = width;
    encoder->in_height = height;
    encoder->codec_type = codec;
    if (encoder->nvidia_encoders[encoder->active_encoder_idx]) {
#if OS_IS(OS_LINUX)
        // NOTE: nvidia reconfiguration is currently disabled because it breaks CUDA resource
        // registration somehow.
        return nvidia_reconfigure_encoder(encoder->nvidia_encoders[encoder->active_encoder_idx],
                                          width, height, bitrate, vbv_size, codec);
#else
        LOG_FATAL("NVIDIA_ENCODER should not be used on Windows!");
#endif
    }
    return false;
}

void video_encoder_set_iframe(VideoEncoder *encoder) {
    /*
        Set the next frame to be an IDR-frame with SPS/PPS headers.

        Arguments:
            encoder (VideoEncoder*): Encoder containing the frame
    */
    if (!encoder) {
        LOG_ERROR("video_encoder_set_iframe received NULL encoder!");
        return;
    }
    LOG_INFO("Creating iframe");
    switch (encoder->active_encoder) {
        case NVIDIA_ENCODER:
#if OS_IS(OS_LINUX)
            nvidia_set_iframe(encoder->nvidia_encoders[encoder->active_encoder_idx]);
            return;
#else
            LOG_FATAL("NVIDIA_ENCODER should not be used on Windows!");
#endif
        case FFMPEG_ENCODER:
            ffmpeg_set_iframe(encoder->ffmpeg_encoder);
            return;
        default:
            LOG_ERROR("Unknown encoder type: %d!", encoder->active_encoder);
            return;
    }
}

void video_encoder_set_ltr_action(VideoEncoder *encoder, const LTRAction *action) {
    FATAL_ASSERT(encoder && action);
    encoder->next_ltr_action = *action;
}

void destroy_video_encoder(VideoEncoder *encoder) {
    /*
        Destroy all components of the encoder, then free the encoder itself.

        Arguments:
            encoder (VideoEncoder*): encoder to destroy
    */
    LOG_INFO("Destroying video encoder...");

    // Check if encoder exists
    if (encoder == NULL) {
        LOG_ERROR("NULL Encoder passed into destroy_video_encoder, nothing will be destroyed.");
        return;
    }

    // Destroy the nvidia encoder, if any
    for (int i = 0; i < NUM_ENCODERS; i++) {
        NvidiaEncoder *nvidia_encoder = encoder->nvidia_encoders[i];
        if (nvidia_encoder) {
#if OS_IS(OS_LINUX)
            LOG_INFO("Destroying nvidia encoder...");
            destroy_nvidia_encoder(nvidia_encoder);
            LOG_INFO("Done destroying nvidia encoder!");
#else
            LOG_FATAL("NVIDIA_ENCODER should not be used on Windows!");
#endif
        }
    }
    if (encoder->ffmpeg_encoder) {
        LOG_INFO("Destroying ffmpeg encoder...");
        destroy_ffmpeg_encoder(encoder->ffmpeg_encoder);
        LOG_INFO("Done destroying ffmpeg encoder!");
    }

    // Free output filter.
    av_bsf_free(&encoder->bsf);

    // free packets
    for (int i = 0; i < MAX_ENCODER_PACKETS; i++) {
        av_packet_free(&encoder->packets[i]);
    }
    free(encoder);

    LOG_INFO("Done destroying encoder!");
}
