#ifndef AUDIO_DECODE_H
#define AUDIO_DECODE_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file audiodecode.h
 * @brief This file contains the code to decode AAC-encoded audio using FFmpeg.
============================
Usage
============================

Audio is decoded from AAC via FFmpeg. In order for FFmpeg to be able to decoder
an audio frame, it needs to be have a certain duration of data. This is
frequently more than a single packet, which is why we have a FIFO encoding
queue. This is abstracted away in the decoder, each packet will already have
enough data from the way the encoder encodes. You can initialize the AAC decoder
via create_audio_decoder. You then decode packets via
audio_decoder_decode_packet and convert them into readable format via
audio_decoder_packet_readout.
*/

#include <libavcodec/avcodec.h>
#include <libavdevice/avdevice.h>
#include <libavfilter/avfilter.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavformat/avformat.h>
#include <libavutil/avutil.h>
#include <libswresample/swresample.h>
#include <libswscale/swscale.h>

#include <fractal/core/fractal.h>

/*
============================
Defines
============================
*/

#define MAX_AUDIO_FRAME_SIZE 192000

/*
============================
Custom Types
============================
*/

/**
 * @brief                          Audio decoder features
 */
typedef struct AudioDecoder {
    AVCodec* pCodec;
    AVCodecContext* pCodecCtx;
    AVFrame* pFrame;
    SwrContext* pSwrContext;
    uint8_t* out_buffer;
} AudioDecoder;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Initialize a FFmpeg AAC audio decoder for a
 *                                 specific sample rate
 *
 * @param sample_rate              The sample rate, in Hertz, of the audio to
 *                                 decode
 *
 * @returns                        The initialized FFmpeg AAC audio decoder
 */
AudioDecoder* create_audio_decoder(int sample_rate);

/**
 * @brief                          Initialize an AVFrame to receive a decoded
 *                                 audio frame
 *
 * @param decoder                  The audio decoder to associate the AVFrame
 *                                 with
 *
 * @returns                        0 if success, else -1
 */
int init_av_frame(AudioDecoder* decoder);

/**
 * @brief                          Retrieve the size of an audio frame
 *
 * @param decoder                  The audio decoder associated with the audio
 *                                 frame
 *
 * @returns                        The size of the audio frame, in bytes
 */
int audio_decoder_get_frame_data_size(AudioDecoder* decoder);

/**
 * @brief                          Read a decoded audio packet from the decoder
 *                                 into a data buffer
 *
 * @param decoder                  The audio decoder that decoded the audio
 *                                 packet
 *
 * @param data                     Data buffer to receive the decoded audio data
 */
void audio_decoder_packet_readout(AudioDecoder* decoder, uint8_t* data);

/**
 * @brief                          Decode an AAC encoded audio packet
 *
 * @param decoder                  The audio decoder used to decode the AAC
 *                                 packet
 *
 * @param encoded_packet           The AAC encoded audio packet to decode
 *
 * @returns                        0 if success, else -1
 */

int audio_decoder_decode_packet(AudioDecoder* decoder, AVPacket* encoded_packet);

/**
 * @brief                          Destroy a FFmpeg AAC audio decoder, and free
 *                                 its memory
 *
 * @param decoder                  The audio decoder to destroy
 */
void destroy_audio_decoder(AudioDecoder* decoder);

#endif  // DECODE_H
