/*
 * Video decoding via FFmpeg library.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/
#if defined(_WIN32)
#pragma warning(disable : 4706)  // assignment within conditional warning
#endif

#include "videodecode.h"

#include <stdio.h>
#include <stdlib.h>

#define SHOW_DECODER_LOGS false

#if SHOW_DECODER_LOGS
void swap_decoder(void* t, int t2, const char* fmt, va_list vargs) {
  t;
  t2;
  mprintf("Error found\n");
  vprintf(fmt, vargs);
}
#endif

int hw_decoder_init(AVCodecContext* ctx, const enum AVHWDeviceType type) {
  int err = 0;

  if ((err = av_hwdevice_ctx_create(&ctx->hw_device_ctx, type, NULL, NULL, 0)) <
      0) {
    LOG_WARNING("Failed to create specified HW device. Error %d: %s\n", err,
                av_err2str(err));
    return err;
  }

  return err;
}

enum AVPixelFormat match_format(AVCodecContext* ctx,
                                const enum AVPixelFormat* pix_fmts,
                                enum AVPixelFormat match_pix_fmt) {
  ctx;

  for (const enum AVPixelFormat* p = pix_fmts; *p != -1; p++) {
    if (*p == match_pix_fmt) {
      LOG_WARNING("Hardware format found: %s\n", av_get_pix_fmt_name(*p));
      return *p;
    }
  }

  if (*pix_fmts != -1) {
    LOG_WARNING("Hardware format not found, using format %s\n",
                av_get_pix_fmt_name(*pix_fmts));
    return *pix_fmts;
  }

  LOG_WARNING("Failed to get HW surface format.");
  return AV_PIX_FMT_NONE;
}

enum AVPixelFormat match_qsv(AVCodecContext* ctx,
                             const enum AVPixelFormat* pix_fmts) {
  return match_format(ctx, pix_fmts, AV_PIX_FMT_QSV);
}
enum AVPixelFormat match_dxva2(AVCodecContext* ctx,
                               const enum AVPixelFormat* pix_fmts) {
  return match_format(ctx, pix_fmts, AV_PIX_FMT_DXVA2_VLD);
}
enum AVPixelFormat match_videotoolbox(AVCodecContext* ctx,
                                      const enum AVPixelFormat* pix_fmts) {
  return match_format(ctx, pix_fmts, AV_PIX_FMT_VIDEOTOOLBOX);
}
enum AVPixelFormat match_vaapi(AVCodecContext* ctx,
                               const enum AVPixelFormat* pix_fmts) {
  return match_format(ctx, pix_fmts, AV_PIX_FMT_VAAPI);
}

int try_setup_video_decoder(int width, int height, video_decoder_t* decoder) {
  // setup the AVCodec and AVFormatContext
  // avcodec_register_all is deprecated on FFmpeg 4+
  // only Linux uses FFmpeg 3.4.x because of canonical system packages
#if LIBAVCODEC_VERSION_MAJOR < 58
  avcodec_register_all();
#endif

  if (decoder->type == DECODE_TYPE_SOFTWARE) {
    LOG_INFO("Trying software decoder");
    decoder->codec = avcodec_find_decoder_by_name("h264");
    if (!decoder->codec) {
      LOG_WARNING("Could not find video codec");
      return -1;
    }
    decoder->context = avcodec_alloc_context3(decoder->codec);

    decoder->sw_frame = (AVFrame*)av_frame_alloc();
    decoder->sw_frame->format = AV_PIX_FMT_YUV420P;
    decoder->sw_frame->width = width;
    decoder->sw_frame->height = height;
    decoder->sw_frame->pts = 0;

    if (avcodec_open2(decoder->context, decoder->codec, NULL) < 0) {
      LOG_WARNING("Failed to open codec for stream");
      return -1;
    }
  } else if (decoder->type == DECODE_TYPE_QSV) {
    LOG_INFO("Trying QSV decoder");
    decoder->codec = avcodec_find_decoder_by_name("h264_qsv");
    decoder->context = avcodec_alloc_context3(decoder->codec);

    decoder->context->get_format = match_qsv;

    if (hw_decoder_init(decoder->context, AV_HWDEVICE_TYPE_QSV) < 0) {
      return -1;
    }

    av_buffer_unref(&decoder->context->hw_frames_ctx);
    decoder->context->hw_frames_ctx =
        av_hwframe_ctx_alloc(decoder->context->hw_device_ctx);

    AVHWFramesContext* frames_ctx;
    AVQSVFramesContext* frames_hwctx;

    frames_ctx = (AVHWFramesContext*)decoder->context->hw_frames_ctx->data;
    frames_hwctx = frames_ctx->hwctx;

    frames_ctx->format = AV_PIX_FMT_QSV;
    frames_ctx->sw_format = AV_PIX_FMT_NV12;
    frames_ctx->width = width;
    frames_ctx->height = height;
    frames_ctx->initial_pool_size = 32;
    frames_hwctx->frame_type = MFX_MEMTYPE_VIDEO_MEMORY_DECODER_TARGET;

    av_hwframe_ctx_init(decoder->context->hw_frames_ctx);
    av_opt_set(decoder->context->priv_data, "async_depth", "1", 0);

    if (avcodec_open2(decoder->context, NULL, NULL) < 0) {
      LOG_WARNING("Failed to open context for stream");
      return -1;
    }

    decoder->sw_frame = av_frame_alloc();
    decoder->hw_frame = av_frame_alloc();
    if (av_hwframe_get_buffer(decoder->context->hw_frames_ctx,
                              decoder->hw_frame, 0) < 0) {
      LOG_WARNING("Failed to init buffer for hw frames");
      return -1;
    }
  } else if (decoder->type == DECODE_TYPE_HARDWARE) {
    LOG_INFO("Trying hardware decoder");
    // set the appropriate video decoder format based on PS
#if defined(_WIN32)
#define match_hardware match_dxva2;
    char* device_type = "dxva2";
#elif __APPLE__
#define match_hardware match_videotoolbox;
    char* device_type = "videotoolbox";
#else  // linux
#define match_hardware match_vaapi;
    char* device_type = "vaapi";
#endif

    int ret = 0;

    // get the appropriate hardware device
    decoder->device_type = av_hwdevice_find_type_by_name(device_type);
    if (decoder->device_type == AV_HWDEVICE_TYPE_NONE) {
      LOG_WARNING("Device type %s is not supported.\n", device_type);
      LOG_WARNING("Available device types:");
      while ((decoder->device_type = av_hwdevice_iterate_types(
                  decoder->device_type)) != AV_HWDEVICE_TYPE_NONE) {
        LOG_WARNING(" %s", av_hwdevice_get_type_name(decoder->device_type));
      }
      LOG_WARNING(" ");
      return -1;
    }

    decoder->codec = avcodec_find_decoder_by_name("h264");

    if (!(decoder->context = avcodec_alloc_context3(decoder->codec))) {
      LOG_WARNING("alloccontext3 failed w/ error code: %d\n", AVERROR(ENOMEM));
      return -1;
    }

    decoder->context->get_format = match_hardware;
    av_opt_set(decoder->context->priv_data, "async_depth", "1", 0);

    if (hw_decoder_init(decoder->context, decoder->device_type) < 0) {
      LOG_WARNING("Failed to init hardware decoder");
      return -1;
    }

    if ((ret = avcodec_open2(decoder->context, decoder->codec, NULL)) < 0) {
      LOG_WARNING("Failed to open codec for stream");
      return -1;
    }

    if (!(decoder->hw_frame = av_frame_alloc()) ||
        !(decoder->sw_frame = av_frame_alloc())) {
      LOG_WARNING("Can not alloc frame");

      av_frame_free(&decoder->hw_frame);
      av_frame_free(&decoder->sw_frame);
      return -1;
    }
  } else {
    LOG_ERROR("Unsupported hardware type!");
    return -1;
  }
  return 0;
}

video_decoder_t* create_video_decoder(int width, int height,
                                      bool use_hardware) {
#if SHOW_DECODER_LOGS
  // av_log_set_level( AV_LOG_ERROR );
  av_log_set_callback(swap_decoder);
#endif

  video_decoder_t* decoder = (video_decoder_t*)malloc(sizeof(video_decoder_t));
  memset(decoder, 0, sizeof(video_decoder_t));

  decoder->width = width;
  decoder->height = height;

  if (use_hardware) {
#if defined(_WIN32)
    int decoder_precedence[] = {DECODE_TYPE_QSV, DECODE_TYPE_HARDWARE,
                                DECODE_TYPE_SOFTWARE};
#elif __APPLE__
    int decoder_precedence[] = {DECODE_TYPE_HARDWARE, DECODE_TYPE_SOFTWARE};
#else  // linux
    int decoder_precedence[] = {DECODE_TYPE_QSV, DECODE_TYPE_HARDWARE,
                                DECODE_TYPE_SOFTWARE};
#endif
    for (unsigned long i = 0;
         i < sizeof(decoder_precedence) / sizeof(decoder_precedence[0]); ++i) {
      decoder->type = decoder_precedence[i];
      if (try_setup_video_decoder(width, height, decoder) < 0) {
        LOG_INFO("Video decoder: Failed, trying next decoder");
      } else {
        LOG_INFO("Video decoder: Success!");
        return decoder;
      }
    }

    LOG_WARNING("Video decoder: All decoders failed!");
    return NULL;
  } else {
    LOG_WARNING("Video Decoder: NO HARDWARE");
    decoder->type = DECODE_TYPE_SOFTWARE;
    if (try_setup_video_decoder(width, height, decoder) < 0) {
      LOG_WARNING("Video decoder: Software decoder failed!");
      return NULL;
    } else {
      LOG_INFO("Video decoder: Success!");
      return decoder;
    }
  }
}

/// @brief destroy decoder decoder
/// @details frees FFmpeg decoder memory

void destroy_video_decoder(video_decoder_t* decoder) {
  // check if decoder decoder exists
  if (decoder == NULL) {
    LOG_WARNING("Cannot destroy decoder decoder.");
    return;
  }

  /* flush the decoder */
  decoder->packet.data = NULL;
  decoder->packet.size = 0;
  av_packet_unref(&decoder->packet);
  avcodec_free_context(&decoder->context);

  // free the ffmpeg contextes
  avcodec_close(decoder->context);

  // free the decoder context and frame
  av_free(decoder->context);
  av_free(decoder->sw_frame);
  av_free(decoder->hw_frame);

  // free the buffer and decoder
  free(decoder);
  return;
}

/// @brief decode a frame using the decoder decoder
/// @details decode an encoded frame under YUV color format into RGB frame
bool video_decoder_decode(video_decoder_t* decoder, void* buffer,
                          int buffer_size) {
  static double total_time = 0.0;
  static int num_times = 0;

  clock t;
  StartTimer(&t);

  // init packet to prepare decoding
  av_init_packet(&decoder->packet);

  // copy the received packet back into the decoder AVPacket
  // memcpy(&decoder->packet.data, &buffer, buffer_size);
  decoder->packet.data = buffer;
  decoder->packet.size = buffer_size;

  // decode the frame
  if (avcodec_send_packet(decoder->context, &decoder->packet) < 0) {
    LOG_WARNING("Failed to avcodec_send_packet!");
    return false;
  }

  // If frame was computed on the CPU
  if (decoder->context->hwaccel) {
      // If frame was computed on the GPU
      if( avcodec_receive_frame( decoder->context, decoder->hw_frame ) < 0 )
      {
          LOG_WARNING( "Failed to avcodec_receive_frame!" );
          return false;
      }

      av_hwframe_transfer_data( decoder->sw_frame, decoder->hw_frame, 0 );
  } else {
      if( decoder->type != DECODE_TYPE_SOFTWARE )
      {
          LOG_INFO( "Decoder cascaded from hardware to software" );
          decoder->type = DECODE_TYPE_SOFTWARE;
      }

      if( avcodec_receive_frame( decoder->context, decoder->sw_frame ) < 0 )
      {
          LOG_WARNING( "Failed to avcodec_receive_frame!" );
          return false;
      }
  }

  av_packet_unref(&decoder->packet);

  double time = GetTimer(t);
  // mprintf( "Decode Time: %f\n", time );
  if (time < 0.020) {
    total_time += time;
    num_times++;
    // mprintf( "Avg Decode Time: %f\n", total_time / num_times );
  }

  return true;
}

#if defined(_WIN32)
#pragma warning(default : 4706)
#pragma warning(default : 4100)
#endif
