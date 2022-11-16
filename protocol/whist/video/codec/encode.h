#ifndef VIDEO_CODEC_ENCODE_H
#define VIDEO_CODEC_ENCODE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file encode.h
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

#include <libavcodec/bsf.h>

#include <whist/core/whist.h>
#include "whist/video/ltr.h"
#include "nvidia_encode.h"
#include "ffmpeg_encode.h"

/*
============================
Custom Types
============================
*/

#define MAX_ENCODER_PACKETS 20
#define NUM_ENCODERS 2

typedef enum VideoEncoderType { NVIDIA_ENCODER, FFMPEG_ENCODER } VideoEncoderType;

/**
 * @brief            Master video encoding struct. Contains pointers to Nvidia and FFmpeg encoders
 * as well as packet + frame metadata needed for sending encoded frames to the client. We default to
 * using the Nvidia encoder, and fall back to the FFmpeg encoder if that fails.
 */
typedef struct VideoEncoder {
    int active_encoder_idx;
    VideoEncoderType active_encoder;
    // packet metadata + data
    int num_packets;
    AVPacket* packets[MAX_ENCODER_PACKETS];

    // frame metadata + data
    uint32_t in_width, in_height;
    int out_width, out_height;
    VideoFrameType frame_type;
    size_t encoded_frame_size;  /// <size of encoded frame in bytes
    CodecType codec_type;
    NvidiaEncoder* nvidia_encoders[NUM_ENCODERS];
    FFmpegEncoder* ffmpeg_encoder;

    LTRAction next_ltr_action;

    // Output filter to fix up bitstream properties which do not match
    // out use-case with long-term reference frames.
    AVBSFContext* bsf;
} VideoEncoder;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Will create a new encoder
 *
 * @param width                    Width of the frames that the encoder must
 *                                 take in
 * @param height                   Height of the frames that the encoder must
 *                                 take in
 * @param bitrate                  The number of bits per second that this
 *                                 encoder will encode to
 * @param vbv_size                 VBV Buffer size in bits
 *
 * @param codec_type               Which codec type (h264 or h265) to use
 *
 * @returns                        The newly created encoder
 */
VideoEncoder* create_video_encoder(int width, int height, int bitrate, int vbv_size,
                                   CodecType codec_type);

/**
 * @brief                       Encode a frame. This will call the necessary encoding functions
 * depending on the encoder type, then record metadata and the encoded packets into
 * encoder->packets.
 *
 * @param encoder               Encoder to use
 *
 * @returns                     0 on success, otherwise -1
 */
int video_encoder_encode(VideoEncoder* encoder);

/**
 * @brief                          Reconfigure the encoder using new parameters
 *
 * @param encoder                  The encoder to be updated
 * @param width                    The new width
 * @param height                   The new height
 * @param bitrate                  The new bitrate
 * @param vbv_size                 The new VBV Buffer size in bits
 * @param codec                    The new codec
 *
 * @returns                        true if the encoder was successfully reconfigured,
 *                                 false if no reconfiguration was possible
 */
bool reconfigure_encoder(VideoEncoder* encoder, int width, int height, int bitrate, int vbv_size,
                         CodecType codec);

/**
 * @brief                          Set the next frame to be an IDR-frame,
 *                                 with SPS/PPS headers included as well.
 *
 * @param encoder                  Encoder to be updated
 */
void video_encoder_set_iframe(VideoEncoder* encoder);

/**
 * @brief                          Set LTR action for the next frame.
 *
 * @param encoder                  Encoder to set action on.
 * @param action                   LTR action to use with next frame.
 */
void video_encoder_set_ltr_action(VideoEncoder* encoder, const LTRAction* action);

/**
 * @brief                          Destroy encoder
 *
 * @param encoder                  Encoder to be destroyed
 */
void destroy_video_encoder(VideoEncoder* encoder);

#endif  // VIDEO_CODEC_ENCODE_H
