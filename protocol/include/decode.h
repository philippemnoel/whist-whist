/*
 * This file contains the headers and definitions of the FFmpeg decoder functions
 * that decode the video frames.

 Protocol version: 1.0
 Last modification: 12/14/2019

 By: Philippe Noël

 Copyright Fractal Computers, Inc. 2019
*/
#ifndef DECODE_H
#define DECODE_H

#include "fractal.h" // contains all the headers

// define decoder struct to pass as a type
typedef struct {
	AVCodec *codec;
	AVCodecContext *context;
	AVFrame *frame;
	void *frame_buffer;
	int in_width, in_height;
	int out_width, out_height;
	AVPacket packet;
	struct SwsContext *sws;
} decoder_t;

/// @brief creates encoder device
/// @details creates FFmpeg encoder
decoder_t *create_decoder(int in_width, int in_height, int out_width, int out_height, int bitrate);

/// @brief destroy decoder device
/// @details frees FFmpeg decoder memory
void destroy_decoder(decoder_t *decoder);

/// @brief decodes a frame using the decoder device
/// @details decode an encoded frame under YUV color format into RGB frame
void *decoder_decode(decoder_t *decoder, char *buffer, int buffer_size, void *decoded_data);

#endif // ENCODE_H
