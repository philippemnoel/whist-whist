/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file ffmpeg_encode.h
 * @brief This file contains the code to create and destroy FFmpeg encoders and use
 *        them to encode captured screens.
============================
Usage
============================
Video is encoded to H264 via either a hardware encoder (currently, we use NVidia GPUs, so we use
NVENC) or a software encoder if hardware encoding fails. H265 is also supported but not currently
used. For encoders, create an H264 encoder via create_ffmpeg_encoder, and use it to encode frames
via ffmpeg_encoder_send_frame. Retrieve encoded packets using ffmpeg_encoder_receive_packet. When
finished, destroy the encoder using destroy_ffmpeg_encoder.
*/

/*
============================
Includes
============================
*/
#include "ffmpeg_encode.h"

#define GOP_SIZE 999999
#define MIN_NVENC_WIDTH 33
#define MIN_NVENC_HEIGHT 17

/*
============================
Private Functions
============================
*/
bool set_opt(FFmpegEncoder *encoder, char *option, char *value);
FFmpegEncoder *create_nvenc_encoder(int in_width, int in_height, int out_width, int out_height,
                                    int bitrate, CodecType codec_type);
FFmpegEncoder *create_qsv_encoder(int in_width, int in_height, int out_width, int out_height,
                                  int bitrate, CodecType codec_type);
FFmpegEncoder *create_sw_encoder(int in_width, int in_height, int out_width, int out_height,
                                 int bitrate, CodecType codec_type);

/*
============================
Private Function Implementations
============================
*/
bool set_opt(FFmpegEncoder *encoder, char *option, char *value) {
    /*
        Wrapper function to set encoder options, like presets, latency, and bitrate.

        Arguments:
            encoder (FFmpegEncoder*): video encoder to set options for
            option (char*): name of option as string
            value (char*): value of option as string
    */
    int ret = av_opt_set(encoder->context->priv_data, option, value, 0);
    if (ret < 0) {
        LOG_WARNING("Could not av_opt_set %s to %s!", option, value);
        return false;
    } else {
        return true;
    }
}

typedef FFmpegEncoder *(*FFmpegEncoderCreator)(int, int, int, int, int, CodecType);

FFmpegEncoder *create_nvenc_encoder(int in_width, int in_height, int out_width, int out_height,
                                    int bitrate, CodecType codec_type) {
    /*
        Create an encoder using Nvidia's video encoding alorithms.

        Arguments:
            in_width (int): Width of the frames that the encoder intakes
            in_height (int): height of the frames that the encoder intakes
            out_width (int): width of the frames that the encoder outputs
            out_height (int): Height of the frames that the encoder outputs
            bitrate (int): bits per second the encoder will encode to
            codec_type (CodecType): Codec (currently H264 or H265) the encoder will use

        Returns:
            (FFmpegEncoder*): the newly created encoder
     */
    LOG_INFO("Trying NVENC encoder...");
    FFmpegEncoder *encoder = (FFmpegEncoder *)safe_malloc(sizeof(FFmpegEncoder));
    memset(encoder, 0, sizeof(FFmpegEncoder));

    encoder->type = NVENC_ENCODE;
    encoder->in_width = in_width;
    encoder->in_height = in_height;
    if (out_width <= 32) out_width = MIN_NVENC_WIDTH;
    if (out_height <= 16) out_height = MIN_NVENC_HEIGHT;
    encoder->out_width = out_width;
    encoder->out_height = out_height;
    encoder->codec_type = codec_type;
    encoder->gop_size = GOP_SIZE;
    encoder->frames_since_last_iframe = 0;

    enum AVPixelFormat in_format = AV_PIX_FMT_RGB32;
    enum AVPixelFormat hw_format = AV_PIX_FMT_CUDA;
    enum AVPixelFormat sw_format = AV_PIX_FMT_0RGB32;

    // init intake format in sw_frame

    encoder->sw_frame = av_frame_alloc();
    encoder->sw_frame->format = in_format;
    encoder->sw_frame->width = encoder->in_width;
    encoder->sw_frame->height = encoder->in_height;
    encoder->sw_frame->pts = 0;

    // set frame size and allocate memory for it
    int frame_size =
        av_image_get_buffer_size(in_format, encoder->out_width, encoder->out_height, 1);
    encoder->sw_frame_buffer = safe_malloc(frame_size);

    // fill picture with empty frame buffer
    av_image_fill_arrays(encoder->sw_frame->data, encoder->sw_frame->linesize,
                         (uint8_t *)encoder->sw_frame_buffer, in_format, encoder->out_width,
                         encoder->out_height, 1);

    // init hw_device_ctx
    if (av_hwdevice_ctx_create(&encoder->hw_device_ctx, AV_HWDEVICE_TYPE_CUDA, "CUDA", NULL, 0) <
        0) {
        LOG_WARNING("Failed to create hardware device context");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init encoder format in context

    if (encoder->codec_type == CODEC_TYPE_H264) {
        encoder->codec = avcodec_find_encoder_by_name("h264_nvenc");
    } else if (encoder->codec_type == CODEC_TYPE_H265) {
        encoder->codec = avcodec_find_encoder_by_name("hevc_nvenc");
    }

    encoder->context = avcodec_alloc_context3(encoder->codec);
    encoder->context->width = encoder->out_width;
    encoder->context->height = encoder->out_height;
    encoder->context->bit_rate = bitrate;  // averageBitRate
    // encoder->context->rc_max_rate = 4 * bitrate;  // maxBitRate
    encoder->context->rc_buffer_size =
        (VBV_BUF_SIZE_IN_MS * bitrate) / MS_IN_SECOND;  // vbvBufferSize
    encoder->context->qmax = MAX_QP;
    encoder->context->time_base.num = 1;
    encoder->context->time_base.den = MAX_FPS;
    encoder->context->gop_size = encoder->gop_size;
    // encoder->context->keyint_min = 5;
    encoder->context->pix_fmt = hw_format;

    // enable automatic insertion of non-reference P-frames
    set_opt(encoder, "nonref_p", "1");
    // llhq is deprecated - we are now supposed to use p1-p7 and tune
    // p1: fastest, but lowest quality -- p7: slowest, best quality
    // only constqp/cbr/vbr are supported now with these presets
    // tune: high quality, low latency, ultra low latency, or lossless; we use ultra low latency
    set_opt(encoder, "preset", "p4");
    set_opt(encoder, "tune", "ull");
    set_opt(encoder, "rc", "cbr");
    // zerolatency: no reordering delay
    set_opt(encoder, "zerolatency", "1");
    // delay frame output by 0 frames
    set_opt(encoder, "delay", "0");
    // Make all I-Frames IDR Frames
    if (!set_opt(encoder, "forced-idr", "1")) {
        LOG_ERROR("Cannot create encoder if IDR's cannot be forced");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // assign hw_device_ctx
    av_buffer_unref(&encoder->context->hw_frames_ctx);
    encoder->context->hw_frames_ctx = av_hwframe_ctx_alloc(encoder->hw_device_ctx);

    // init HWFramesContext
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)encoder->context->hw_frames_ctx->data;
    frames_ctx->format = hw_format;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width = encoder->in_width;
    frames_ctx->height = encoder->in_height;
    if (av_hwframe_ctx_init(encoder->context->hw_frames_ctx) < 0) {
        LOG_WARNING("Failed to initialize hardware frames context");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    if (avcodec_open2(encoder->context, encoder->codec, NULL) < 0) {
        LOG_WARNING("Failed to open context for stream");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init hardware frame
    encoder->hw_frame = av_frame_alloc();
    int res = av_hwframe_get_buffer(encoder->context->hw_frames_ctx, encoder->hw_frame, 0);
    if (res < 0) {
        LOG_WARNING("Failed to init buffer for video encoder hw frames: %s", av_err2str(res));
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init resizing in filter_graph

    encoder->filter_graph = avfilter_graph_alloc();
    if (!encoder->filter_graph) {
        LOG_WARNING("Unable to create filter graph");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

#define N_FILTERS_NVENC 2
    // source -> sink
    const AVFilter *filters[N_FILTERS_NVENC] = {0};
    filters[0] = avfilter_get_by_name("buffer");
    filters[1] = avfilter_get_by_name("buffersink");

    for (int i = 0; i < N_FILTERS_NVENC; ++i) {
        if (!filters[i]) {
            LOG_WARNING("Could not find filter %d in the list!", i);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    AVFilterContext *filter_contexts[N_FILTERS_NVENC] = {0};

    // source buffer
    filter_contexts[0] = avfilter_graph_alloc_filter(encoder->filter_graph, filters[0], "src");
    AVBufferSrcParameters *avbsp = av_buffersrc_parameters_alloc();
    avbsp->width = encoder->in_width;
    avbsp->height = encoder->in_height;
    avbsp->format = hw_format;
    avbsp->frame_rate = (AVRational){MAX_FPS, 1};
    avbsp->time_base = (AVRational){1, MAX_FPS};
    avbsp->hw_frames_ctx = encoder->context->hw_frames_ctx;
    av_buffersrc_parameters_set(filter_contexts[0], avbsp);
    if (avfilter_init_str(filter_contexts[0], NULL) < 0) {
        LOG_WARNING("Unable to initialize buffer source");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    av_free(avbsp);
    encoder->filter_graph_source = filter_contexts[0];
    // sink buffer
    if (avfilter_graph_create_filter(&filter_contexts[1], filters[1], "sink", NULL, NULL,
                                     encoder->filter_graph) < 0) {
        LOG_WARNING("Unable to initialize buffer sink");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    encoder->filter_graph_sink = filter_contexts[1];

    // connect the filters in a simple line
    for (int i = 0; i < N_FILTERS_NVENC - 1; ++i) {
        if (avfilter_link(filter_contexts[i], 0, filter_contexts[i + 1], 0) < 0) {
            LOG_WARNING("Unable to link filters %d to %d", i, i + 1);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    int err = avfilter_graph_config(encoder->filter_graph, NULL);
    if (err < 0) {
        LOG_WARNING("Unable to configure the filter graph: %s", av_err2str(err));
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init transfer frame
    encoder->filtered_frame = av_frame_alloc();

    return encoder;
}

FFmpegEncoder *create_qsv_encoder(int in_width, int in_height, int out_width, int out_height,
                                  int bitrate, CodecType codec_type) {
    /*
        Create a QSV (Intel Quick Sync Video) encoder for Intel hardware encoding.

        Arguments:
            in_width (int): Width of the frames that the encoder intakes
            in_height (int): height of the frames that the encoder intakes
            out_width (int): width of the frames that the encoder outputs
            out_height (int): Height of the frames that the encoder outputs
            bitrate (int): bits per second the encoder will encode to
            codec_type (CodecType): Codec (currently H264 or H265) the encoder will use

        Returns:
            (FFmpegEncoder*): the newly created encoder
     */
    LOG_INFO("Trying QSV encoder...");
    FFmpegEncoder *encoder = (FFmpegEncoder *)safe_malloc(sizeof(FFmpegEncoder));
    memset(encoder, 0, sizeof(FFmpegEncoder));

    encoder->type = QSV_ENCODE;
    encoder->in_width = in_width;
    encoder->in_height = in_height;
    encoder->out_width = out_width;
    encoder->out_height = out_height;
    encoder->codec_type = codec_type;
    encoder->gop_size = GOP_SIZE;
    encoder->frames_since_last_iframe = 0;
    enum AVPixelFormat in_format = AV_PIX_FMT_RGB32;
    enum AVPixelFormat hw_format = AV_PIX_FMT_QSV;
    enum AVPixelFormat sw_format = AV_PIX_FMT_RGB32;

    // init intake format in sw_frame

    encoder->sw_frame = av_frame_alloc();
    encoder->sw_frame->format = in_format;
    encoder->sw_frame->width = encoder->in_width;
    encoder->sw_frame->height = encoder->in_height;
    encoder->sw_frame->pts = 0;

    // set frame size and allocate memory for it
    int frame_size =
        av_image_get_buffer_size(in_format, encoder->out_width, encoder->out_height, 1);
    encoder->sw_frame_buffer = safe_malloc(frame_size);

    // fill picture with empty frame buffer
    av_image_fill_arrays(encoder->sw_frame->data, encoder->sw_frame->linesize,
                         (uint8_t *)encoder->sw_frame_buffer, in_format, encoder->out_width,
                         encoder->out_height, 1);

    // init hw_device_ctx
    if (av_hwdevice_ctx_create(&encoder->hw_device_ctx, AV_HWDEVICE_TYPE_QSV, NULL, NULL, 0) < 0) {
        LOG_WARNING("Failed to create hardware device context");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init encoder format in context

    if (encoder->codec_type == CODEC_TYPE_H264) {
        encoder->codec = avcodec_find_encoder_by_name("h264_qsv");
    } else if (encoder->codec_type == CODEC_TYPE_H265) {
        encoder->codec = avcodec_find_encoder_by_name("hevc_qsv");
    }

    encoder->context = avcodec_alloc_context3(encoder->codec);
    encoder->context->width = encoder->out_width;
    encoder->context->height = encoder->out_height;
    encoder->context->bit_rate = bitrate;
    // encoder->context->rc_max_rate = 4 * bitrate;
    encoder->context->rc_buffer_size =
        (VBV_BUF_SIZE_IN_MS * bitrate) / MS_IN_SECOND;  // vbvBufferSize
    encoder->context->qmax = MAX_QP;
    encoder->context->time_base.num = 1;
    encoder->context->time_base.den = MAX_FPS;
    encoder->context->gop_size = encoder->gop_size;
    encoder->context->keyint_min = 5;
    encoder->context->pix_fmt = hw_format;

    // Make all I-Frames IDR Frames
    if (!set_opt(encoder, "forced-idr", "1")) {
        LOG_ERROR("Cannot create encoder if IDR's cannot be forced");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // assign hw_device_ctx
    av_buffer_unref(&encoder->context->hw_frames_ctx);
    encoder->context->hw_frames_ctx = av_hwframe_ctx_alloc(encoder->hw_device_ctx);

    // init HWFramesContext
    AVHWFramesContext *frames_ctx = (AVHWFramesContext *)encoder->context->hw_frames_ctx->data;
    frames_ctx->format = hw_format;
    frames_ctx->sw_format = sw_format;
    frames_ctx->width = encoder->in_width;
    frames_ctx->height = encoder->in_height;
    frames_ctx->initial_pool_size = 2;

    if (av_hwframe_ctx_init(encoder->context->hw_frames_ctx) < 0) {
        LOG_WARNING("Failed to initialize hardware frames context");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    if (avcodec_open2(encoder->context, encoder->codec, NULL) < 0) {
        LOG_WARNING("Failed to open context for stream");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init hardware frame
    encoder->hw_frame = av_frame_alloc();
    int res = av_hwframe_get_buffer(encoder->context->hw_frames_ctx, encoder->hw_frame, 0);
    if (res < 0) {
        LOG_WARNING("Failed to init buffer for video encoder hw frames: %s", av_err2str(res));
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init resizing in filter_graph

    encoder->filter_graph = avfilter_graph_alloc();
    if (!encoder->filter_graph) {
        LOG_WARNING("Unable to create filter graph");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

#define N_FILTERS_QSV 3
    // source -> scale_qsv -> sink
    const AVFilter *filters[N_FILTERS_QSV] = {0};
    filters[0] = avfilter_get_by_name("buffer");
    filters[1] = avfilter_get_by_name("scale_qsv");
    filters[2] = avfilter_get_by_name("buffersink");

    for (int i = 0; i < N_FILTERS_NVENC; ++i) {
        if (!filters[i]) {
            LOG_WARNING("Could not find filter %d in the list!", i);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    AVFilterContext *filter_contexts[N_FILTERS_QSV] = {0};

    // source buffer
    filter_contexts[0] = avfilter_graph_alloc_filter(encoder->filter_graph, filters[0], "src");
    AVBufferSrcParameters *avbsp = av_buffersrc_parameters_alloc();
    avbsp->width = encoder->in_width;
    avbsp->height = encoder->in_height;
    avbsp->format = hw_format;
    avbsp->frame_rate = (AVRational){MAX_FPS, 1};
    avbsp->time_base = (AVRational){1, MAX_FPS};
    avbsp->hw_frames_ctx = encoder->context->hw_frames_ctx;
    av_buffersrc_parameters_set(filter_contexts[0], avbsp);
    if (avfilter_init_str(filter_contexts[0], NULL) < 0) {
        LOG_WARNING("Unable to initialize buffer source");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    av_free(avbsp);
    encoder->filter_graph_source = filter_contexts[0];

    // scale_qsv (this is not tested yet, but should either just work or be easy
    // to fix on QSV-supporting machines)
    filter_contexts[1] =
        avfilter_graph_alloc_filter(encoder->filter_graph, filters[1], "scale_qsv");
    char options_string[60] = "";
    snprintf(options_string, 60, "w=%d:h=%d", encoder->out_width, encoder->out_height);
    if (avfilter_init_str(filter_contexts[1], options_string) < 0) {
        LOG_WARNING("Unable to initialize scale filter");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // sink buffer
    if (avfilter_graph_create_filter(&filter_contexts[2], filters[2], "sink", NULL, NULL,
                                     encoder->filter_graph) < 0) {
        LOG_WARNING("Unable to initialize buffer sink");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    encoder->filter_graph_sink = filter_contexts[2];

    // connect the filters in a simple line
    for (int i = 0; i < N_FILTERS_QSV - 1; ++i) {
        if (avfilter_link(filter_contexts[i], 0, filter_contexts[i + 1], 0) < 0) {
            LOG_WARNING("Unable to link filters %d to %d", i, i + 1);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    int err = avfilter_graph_config(encoder->filter_graph, NULL);
    if (err < 0) {
        LOG_WARNING("Unable to configure the filter graph: %s", av_err2str(err));
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init transfer frame
    encoder->filtered_frame = av_frame_alloc();

    return encoder;
}

FFmpegEncoder *create_sw_encoder(int in_width, int in_height, int out_width, int out_height,
                                 int bitrate, CodecType codec_type) {
    /*
        Create an FFmpeg software encoder.

        Arguments:
            in_width (int): Width of the frames that the encoder intakes
            in_height (int): height of the frames that the encoder intakes
            out_width (int): width of the frames that the encoder outputs
            out_height (int): Height of the frames that the encoder outputs
            bitrate (int): bits per second the encoder will encode to
            codec_type (CodecType): Codec (currently H264 or H265) the encoder will use

        Returns:
            (FFmpegEncoder*): the newly created encoder
     */
    LOG_INFO("Trying software encoder...");
    FFmpegEncoder *encoder = (FFmpegEncoder *)safe_malloc(sizeof(FFmpegEncoder));
    memset(encoder, 0, sizeof(FFmpegEncoder));

    encoder->type = SOFTWARE_ENCODE;
    encoder->in_width = in_width;
    encoder->in_height = in_height;
    if (out_width % 2) out_width = out_width + 1;
    if (out_height % 2) out_height = out_height + 1;
    encoder->out_width = out_width;
    encoder->out_height = out_height;
    encoder->codec_type = codec_type;
    encoder->gop_size = GOP_SIZE;
    encoder->frames_since_last_iframe = 0;

    enum AVPixelFormat in_format = AV_PIX_FMT_RGB32;
    enum AVPixelFormat out_format = AV_PIX_FMT_YUV420P;

    // init intake format in sw_frame

    encoder->sw_frame = av_frame_alloc();
    encoder->sw_frame->format = in_format;
    encoder->sw_frame->width = encoder->in_width;
    encoder->sw_frame->height = encoder->in_height;
    encoder->sw_frame->pts = 0;

    // set frame size and allocate memory for it
    int frame_size =
        av_image_get_buffer_size(out_format, encoder->out_width, encoder->out_height, 1);
    encoder->sw_frame_buffer = safe_malloc(frame_size);

    // fill picture with empty frame buffer
    av_image_fill_arrays(encoder->sw_frame->data, encoder->sw_frame->linesize,
                         (uint8_t *)encoder->sw_frame_buffer, out_format, encoder->out_width,
                         encoder->out_height, 1);

    // init resizing and resampling in filter_graph

    encoder->filter_graph = avfilter_graph_alloc();
    if (!encoder->filter_graph) {
        LOG_WARNING("Unable to create filter graph");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

#define N_FILTERS_SW 4
    // source -> format -> scale -> sink
    const AVFilter *filters[N_FILTERS_SW] = {0};
    filters[0] = avfilter_get_by_name("buffer");
    filters[1] = avfilter_get_by_name("format");
    filters[2] = avfilter_get_by_name("scale");
    filters[3] = avfilter_get_by_name("buffersink");

    for (int i = 0; i < N_FILTERS_SW; ++i) {
        if (!filters[i]) {
            LOG_WARNING("Could not find filter %d in the list!", i);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    AVFilterContext *filter_contexts[N_FILTERS_SW] = {0};

    // source buffer
    filter_contexts[0] = avfilter_graph_alloc_filter(encoder->filter_graph, filters[0], "src");
    av_opt_set_int(filter_contexts[0], "width", encoder->in_width, AV_OPT_SEARCH_CHILDREN);
    av_opt_set_int(filter_contexts[0], "height", encoder->in_height, AV_OPT_SEARCH_CHILDREN);
    av_opt_set(filter_contexts[0], "pix_fmt", av_get_pix_fmt_name(in_format),
               AV_OPT_SEARCH_CHILDREN);
    av_opt_set_q(filter_contexts[0], "time_base", (AVRational){1, MAX_FPS}, AV_OPT_SEARCH_CHILDREN);
    if (avfilter_init_str(filter_contexts[0], NULL) < 0) {
        LOG_WARNING("Unable to initialize buffer source");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    encoder->filter_graph_source = filter_contexts[0];

    // format
    filter_contexts[1] = avfilter_graph_alloc_filter(encoder->filter_graph, filters[1], "format");
    av_opt_set(filter_contexts[1], "pix_fmts", av_get_pix_fmt_name(out_format),
               AV_OPT_SEARCH_CHILDREN);
    if (avfilter_init_str(filter_contexts[1], NULL) < 0) {
        LOG_WARNING("Unable to initialize format filter");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // scale
    filter_contexts[2] = avfilter_graph_alloc_filter(encoder->filter_graph, filters[2], "scale");
    char options_string[60] = "";
    snprintf(options_string, 60, "w=%d:h=%d", encoder->out_width, encoder->out_height);
    if (avfilter_init_str(filter_contexts[2], options_string) < 0) {
        LOG_WARNING("Unable to initialize scale filter");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // sink buffer
    if (avfilter_graph_create_filter(&filter_contexts[3], filters[3], "sink", NULL, NULL,
                                     encoder->filter_graph) < 0) {
        LOG_WARNING("Unable to initialize buffer sink");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }
    encoder->filter_graph_sink = filter_contexts[3];

    // connect the filters in a simple line
    for (int i = 0; i < N_FILTERS_SW - 1; ++i) {
        if (avfilter_link(filter_contexts[i], 0, filter_contexts[i + 1], 0) < 0) {
            LOG_WARNING("Unable to link filters %d to %d", i, i + 1);
            destroy_ffmpeg_encoder(encoder);
            return NULL;
        }
    }

    // configure the graph
    int err = avfilter_graph_config(encoder->filter_graph, NULL);
    if (err < 0) {
        LOG_WARNING("Unable to configure the filter graph: %s", av_err2str(err));
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    // init transfer frame
    encoder->filtered_frame = av_frame_alloc();

    // init encoder format in context

    if (encoder->codec_type == CODEC_TYPE_H264) {
        encoder->codec = avcodec_find_encoder_by_name("libx264");
    } else if (encoder->codec_type == CODEC_TYPE_H265) {
        encoder->codec = avcodec_find_encoder_by_name("libx265");
    }

    encoder->context = avcodec_alloc_context3(encoder->codec);
    encoder->context->width = encoder->out_width;
    encoder->context->height = encoder->out_height;
    encoder->context->bit_rate = bitrate;
    // encoder->context->rc_max_rate = 4 * bitrate;
    encoder->context->rc_buffer_size =
        (VBV_BUF_SIZE_IN_MS * bitrate) / MS_IN_SECOND;  // vbvBufferSize
    encoder->context->qmax = MAX_QP;
    encoder->context->time_base.num = 1;
    encoder->context->time_base.den = MAX_FPS;
    encoder->context->gop_size = encoder->gop_size;
    encoder->context->keyint_min = 5;
    encoder->context->pix_fmt = out_format;
    encoder->context->max_b_frames = 0;

    set_opt(encoder, "preset", "fast");
    set_opt(encoder, "tune", "zerolatency");
    // Make all I-Frames IDR Frames
    if (!set_opt(encoder, "forced-idr", "1")) {
        LOG_ERROR("Cannot create encoder if IDR's cannot be forced");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    if (avcodec_open2(encoder->context, encoder->codec, NULL) < 0) {
        LOG_WARNING("Failed to open context for stream");
        destroy_ffmpeg_encoder(encoder);
        return NULL;
    }

    return encoder;
}

/*
============================
Public Function Implementations
============================
*/
FFmpegEncoder *create_ffmpeg_encoder(int in_width, int in_height, int out_width, int out_height,
                                     int bitrate, CodecType codec_type) {
    /*
        Create an FFmpeg encoder with the specified parameters. First try NVENC hardware encoding,
       then software encoding if that fails.

        Arguments:
            in_width (int): Width of the frames that the encoder intakes
            in_height (int): height of the frames that the encoder intakes
            out_width (int): width of the frames that the encoder outputs
            out_height (int): Height of the frames that the encoder outputs
            bitrate (int): bits per second the encoder will encode to
            codec_type (CodecType): Codec (currently H264 or H265) the encoder will use

        Returns:
            (FFmpegEncoder*): the newly created encoder
     */
    FFmpegEncoder *ffmpeg_encoder = NULL;
    // TODO: Get QSV Encoder Working
    FFmpegEncoderCreator encoder_precedence[] = {create_nvenc_encoder, create_sw_encoder};
    for (unsigned int i = 0; i < sizeof(encoder_precedence) / sizeof(FFmpegEncoderCreator); ++i) {
        ffmpeg_encoder =
            encoder_precedence[i](in_width, in_height, out_width, out_height, bitrate, codec_type);

        if (!ffmpeg_encoder) {
            LOG_WARNING("FFmpeg encoder: Failed, trying next encoder");
            ffmpeg_encoder = NULL;
        } else {
            LOG_INFO("CODEC TYPE: %d", ffmpeg_encoder->codec_type);
            LOG_INFO("Video encoder: Success!");
            break;
        }
    }
    if (ffmpeg_encoder == NULL) {
        LOG_ERROR("All ffmpeg encoders failed!");
    } else {
        ffmpeg_encoder->bitrate = bitrate;
    }
    return ffmpeg_encoder;
}

// Reconfigure the encoder, with the same parameters as in create_ffmpeg_encoder
bool ffmpeg_reconfigure_encoder(FFmpegEncoder *encoder, int in_width, int in_height, int out_width,
                                int out_height, int bitrate, CodecType codec_type) {
    if (in_width != encoder->in_width || in_height != encoder->in_height ||
        out_width != encoder->out_width || out_height != encoder->out_height ||
        bitrate != encoder->bitrate || codec_type != encoder->codec_type) {
        return false;
    } else {
        return true;
    }
}

int ffmpeg_encoder_frame_intake(FFmpegEncoder *encoder, void *rgb_pixels, int pitch) {
    /*
        Copy frame data in rgb_pixels and pitch to the software frame, and to the hardware frame if
       possible.

        Arguments:
            encoder (FFmpegEncoder*): video encoder containing encoded frames
            rgb_pixels (void*): pixel data for the frame
            pitch (int): Pitch data for the frame

        Returns:
            (int): 0 on success, -1 on failure
     */
    if (!encoder) {
        LOG_ERROR("ffmpeg_encoder_frame_intake received NULL encoder!");
        return -1;
    }
    memset(encoder->sw_frame->data, 0, sizeof(encoder->sw_frame->data));
    memset(encoder->sw_frame->linesize, 0, sizeof(encoder->sw_frame->linesize));
    encoder->sw_frame->data[0] = (uint8_t *)rgb_pixels;
    encoder->sw_frame->linesize[0] = pitch;
    encoder->sw_frame->pts++;

    if (encoder->hw_frame) {
        int res = av_hwframe_transfer_data(encoder->hw_frame, encoder->sw_frame, 0);
        if (res < 0) {
            LOG_ERROR("Unable to transfer frame to hardware frame: %s", av_err2str(res));
            return -1;
        }
    }
    return 0;
}

void ffmpeg_set_iframe(FFmpegEncoder *encoder) {
    /*
        Set the next frame to be an IDR frame. Unreliable for FFmpeg.

        Arguments:
            encoder (FFmpegEncoder*): encoder to use
    */
    if (!encoder) {
        LOG_ERROR("ffmpeg_set_iframe received NULL encoder!");
        return;
    }
    encoder->wants_iframe = true;
}

void destroy_ffmpeg_encoder(FFmpegEncoder *encoder) {
    /*
        Destroy the ffmpeg encoder and its members.

        Arguments:
            encoder (FFmpegEncoder*): encoder to destroy
    */
    if (!encoder) {
        return;
    }

    if (encoder->context) {
        avcodec_free_context(&encoder->context);
    }

    if (encoder->filter_graph) {
        avfilter_graph_free(&encoder->filter_graph);
    }

    if (encoder->hw_device_ctx) {
        av_buffer_unref(&encoder->hw_device_ctx);
    }

    av_frame_free(&encoder->hw_frame);
    av_frame_free(&encoder->sw_frame);
    av_frame_free(&encoder->filtered_frame);

    // free the buffer and encoder
    free(encoder->sw_frame_buffer);
    free(encoder);
}

// Goes through NVENC/QSV/SOFTWARE and sees which one works, cascading to the
// next one when the previous one doesn't work
int ffmpeg_encoder_send_frame(FFmpegEncoder *encoder) {
    /*
        Send a frame through the filter graph, then encode it.

        Arguments:
            encoder (FFmpegEncoder*): encoder used to encode the frame

        Returns:
            (int): 0 on success, -1 on failure
    */
    AVFrame *active_frame = NULL;
    if (encoder->hw_frame) {
        active_frame = encoder->hw_frame;
    } else {
        active_frame = encoder->sw_frame;
    }

    if (encoder->wants_iframe) {
        if (encoder->type != SOFTWARE_ENCODE && encoder->type != NVENC_ENCODE) {
            LOG_FATAL("ffmpeg_set_iframe not implemented on QSV yet!");
        }
        active_frame->pict_type = AV_PICTURE_TYPE_I;
        active_frame->key_frame = 1;
    } else {
        active_frame->pict_type = AV_PICTURE_TYPE_NONE;
        active_frame->key_frame = 0;
    }

    int res = av_buffersrc_add_frame(encoder->filter_graph_source, active_frame);
    if (res < 0) {
        LOG_WARNING("Error submitting frame to the filter graph: %s", av_err2str(res));
    }

    if (encoder->hw_frame) {
        // have to re-create buffers after sending to filter graph
        av_hwframe_get_buffer(encoder->context->hw_frames_ctx, encoder->hw_frame, 0);
    }

    int res_buffer;

    // submit all available frames to the encoder
    while ((res_buffer = av_buffersink_get_frame(encoder->filter_graph_sink,
                                                 encoder->filtered_frame)) >= 0) {
        int res_encoder = avcodec_send_frame(encoder->context, encoder->filtered_frame);

        // unref the frame so it may be reused
        av_frame_unref(encoder->filtered_frame);

        if (res_encoder < 0) {
            LOG_WARNING("Error sending frame for encoding: %s", av_err2str(res_encoder));
            return -1;
        }
    }
    if (res_buffer < 0 && res_buffer != AVERROR(EAGAIN) && res_buffer != AVERROR_EOF) {
        LOG_WARNING("Error getting frame from the filter graph: %d -- %s", res_buffer,
                    av_err2str(res_buffer));
        return -1;
    }

    // Wrap around GOP size
    if (encoder->frames_since_last_iframe % encoder->gop_size == 0) {
        encoder->frames_since_last_iframe = 0;
    }
    // If we're at the start of a GOP
    if (encoder->frames_since_last_iframe == 0) {
        encoder->is_iframe = true;
    } else {
        encoder->is_iframe = false;
    }
    // Increment GOP counter
    encoder->frames_since_last_iframe++;

    // Note: We can't check active_frame->pict_type bc ffmpeg cobbles it for some reason
    if (encoder->wants_iframe) {
        encoder->is_iframe = true;
    }
    encoder->wants_iframe = false;

    return 0;
}

int ffmpeg_encoder_receive_packet(FFmpegEncoder *encoder, AVPacket *packet) {
    /*
        Wrapper around FFmpeg's avcodec_receive_packet. Get an encoded packet from the encoder
       and store it in packet.

        Arguments:
            encoder (FFmpegEncoder*): encoder used to encode the frame
            packet (AVPacket*): packet in which to store encoded data

        Returns:
            (int): 1 on EAGAIN (no more packets), 0 on success (call this function again), and
       -1 on failure.
    */
    int res_encoder;

    // receive_packet already calls av_packet_unref, no need to reinitialize packet
    res_encoder = avcodec_receive_packet(encoder->context, packet);
    if (res_encoder == AVERROR(EAGAIN) || res_encoder == AVERROR(EOF)) {
        return 1;
    } else if (res_encoder < 0) {
        LOG_ERROR("Error getting frame from the encoder: %s", av_err2str(res_encoder));
        return -1;
    }

    return 0;
}
