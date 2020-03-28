#ifndef VIDEO_ENCODE_H
#define VIDEO_ENCODE_H

#include "fractal.h"  // contains all the headers

// define encoder struct to pass as a type
typedef struct {
    AVCodec *codec;
    AVCodecContext *context;
    AVFrame *sw_frame;
    AVFrame *hw_frame;
    void *frame_buffer;
    int width, height;
    AVPacket packet;
    struct SwsContext *sws;
    EncodeType type;
} encoder_t;

/// @brief creates encoder device
/// @details creates FFmpeg encoder
encoder_t *create_video_encoder(int width, int height, int bitrate, int gop_size);

/// @brief destroy encoder device
/// @details frees FFmpeg encoder memory
void destroy_video_encoder(encoder_t *encoder);

/// @brief encodes a frame using the encoder device
/// @details encodes a RGB frame into encoded format as YUV color
void video_encoder_encode(encoder_t *encoder, void *rgb_pixels);

void video_encoder_set_iframe( encoder_t* encoder );
void video_encoder_unset_iframe( encoder_t* encoder );

#endif  // ENCODE_H
