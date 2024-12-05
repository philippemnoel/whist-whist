/**
 * @copyright Copyright (c) 2022 Whist Technologies, Inc.
 * @file filecapture.c
 * @brief Capture device which reads from a file.
 */

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#include "filecapture.h"

typedef struct {
    CaptureDeviceImpl base;

    char *filename;

    // Demuxing.
    AVFormatContext *demux;
    AVPacket *demux_packet;
    int stream_index;
    AVStream *stream;
    int input_width;
    int input_height;
    enum AVPixelFormat input_format;

    // Decoding.
    AVCodecContext *decode;
    AVFrame *decode_frame;

    // Scaling.
    struct SwsContext *scale;
    AVFrame *scale_frame;
    int output_width;
    int output_height;
    enum AVPixelFormat output_format;

    // Output.
    AVFrame *output_frame;
} FileCaptureDevice;

static int file_capture_open_input(FileCaptureDevice *fc) {
    int err;

    avformat_close_input(&fc->demux);

    fc->demux = avformat_alloc_context();
    if (!fc->demux) {
        LOG_ERROR("Failed to allocate demuxer.");
        return -1;
    }

    err = avformat_open_input(&fc->demux, fc->filename, NULL, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to open input file \"%s\": %d.", fc->filename, err);
        return -1;
    }

    err = avformat_find_stream_info(fc->demux, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to find stream information in input file: %d.", err);
        return -1;
    }

    fc->stream_index = av_find_best_stream(fc->demux, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (fc->stream_index < 0) {
        LOG_ERROR("No video streams in input file.");
        return -1;
    }
    fc->stream = fc->demux->streams[fc->stream_index];

    return 0;
}

static int file_capture_configure_scaler(FileCaptureDevice *fc) {
    int err;

    av_frame_free(&fc->scale_frame);
    sws_freeContext(fc->scale);
    fc->scale = NULL;

    if (fc->output_width == fc->input_width && fc->output_height == fc->input_height) {
        // No scaling required.
        return 0;
    }

    fc->scale = sws_alloc_context();
    if (!fc->scale) {
        LOG_ERROR("Failed to create scaler.");
        return -1;
    }

    av_opt_set_int(fc->scale, "srcw", fc->input_width, 0);
    av_opt_set_int(fc->scale, "srch", fc->input_height, 0);
    av_opt_set_int(fc->scale, "src_format", fc->input_format, 0);

    av_opt_set_int(fc->scale, "dstw", fc->output_width, 0);
    av_opt_set_int(fc->scale, "dsth", fc->output_height, 0);
    av_opt_set_int(fc->scale, "dst_format", fc->output_format, 0);

    av_opt_set_int(fc->scale, "sws_flags", SWS_BILINEAR, 0);

    err = sws_init_context(fc->scale, NULL, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to initialise scaler: %d.", err);
        return -1;
    }

    fc->scale_frame = av_frame_alloc();
    if (!fc->scale_frame) {
        LOG_ERROR("Failed to allocate scale frame.");
        return -1;
    }

    LOG_INFO("Configured scaler for %dx%d -> %dx%d.", fc->input_width, fc->input_height,
             fc->output_width, fc->output_height);

    return 0;
}

static int file_capture_open(FileCaptureDevice *fc) {
    int err;

    err = file_capture_open_input(fc);
    if (err < 0) {
        LOG_ERROR("Failed to open input.");
        return -1;
    }

    fc->demux_packet = av_packet_alloc();
    if (!fc->demux_packet) {
        LOG_ERROR("Failed to allocate demux packet.");
        return -1;
    }

    const AVCodecParameters *par = fc->stream->codecpar;
    const AVCodec *codec = avcodec_find_decoder(par->codec_id);
    if (!codec) {
        LOG_ERROR("No decoder available for video stream.");
        return -1;
    }

    LOG_INFO("Using video stream %d: %s %dx%d.", fc->stream_index, codec->name, par->width,
             par->height);
    fc->input_width = par->width;
    fc->input_height = par->height;
    fc->input_format = par->format;

    fc->decode = avcodec_alloc_context3(codec);
    if (!fc->decode) {
        LOG_ERROR("Failed to allocate decoder.");
        return -1;
    }

    err = avcodec_parameters_to_context(fc->decode, par);
    if (err < 0) {
        LOG_ERROR("Failed to copy codec parameters to decoder: %d.", err);
        return -1;
    }

    err = avcodec_open2(fc->decode, codec, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to open decoder: %d.", err);
        return -1;
    }

    fc->decode_frame = av_frame_alloc();
    if (!fc->decode_frame) {
        LOG_ERROR("Failed to allocate decode frame.");
        return -1;
    }

    err = file_capture_configure_scaler(fc);
    if (err < 0) {
        return err;
    }

    fc->output_frame = av_frame_alloc();
    if (!fc->output_frame) {
        LOG_ERROR("Failed to allocate output frame.");
        return -1;
    }

    return 0;
}

static void file_capture_close(FileCaptureDevice *fc) {
    avformat_close_input(&fc->demux);
    av_packet_free(&fc->demux_packet);

    av_frame_free(&fc->decode_frame);
    avcodec_free_context(&fc->decode);

    av_frame_free(&fc->scale_frame);
    sws_freeContext(fc->scale);
    fc->scale = NULL;

    av_frame_free(&fc->output_frame);
}

static int file_reconfigure_capture_device(FileCaptureDevice *fc, int width, int height, int dpi) {
    int err;

    fc->output_width = width;
    fc->output_height = height;

    err = file_capture_configure_scaler(fc);
    if (err < 0) {
        return -1;
    }

    return 0;
}

static void file_destroy_capture_device(FileCaptureDevice **pfc) {
    FileCaptureDevice *fc = *pfc;
    file_capture_close(fc);
    free(fc->filename);
    free(fc);

    *pfc = NULL;
}

static int file_capture_screen(FileCaptureDevice *fc, int *width, int *height, int *pitch) {
    int err;

    av_frame_unref(fc->output_frame);

    while (1) {
        err = avcodec_receive_frame(fc->decode, fc->decode_frame);
        if (err >= 0) break;
        if (err < 0 && err != AVERROR(EAGAIN)) {
            LOG_ERROR("Failed to receive frame from decoder: %d.", err);
            return -1;
        }

        do {
            err = av_read_frame(fc->demux, fc->demux_packet);
            if (err == AVERROR_EOF) {
                LOG_INFO("Input file reached EOF; reopening.");
                err = file_capture_open_input(fc);
                if (err < 0) {
                    LOG_ERROR("Failed to reopen input.");
                    return -1;
                }
                continue;
            }
            if (err < 0) {
                LOG_ERROR("Failed to read packet from demuxer: %d.", err);
                return -1;
            }
        } while (fc->demux_packet->size == 0 || fc->demux_packet->stream_index != fc->stream_index);

        err = avcodec_send_packet(fc->decode, fc->demux_packet);
        if (err < 0) {
            LOG_ERROR("Failed to send packet to decoder: %d.", err);
            return -1;
        }

        av_packet_unref(fc->demux_packet);
    }

    if (fc->scale) {
        fc->scale_frame->format = fc->decode_frame->format;
        fc->scale_frame->width = fc->output_width;
        fc->scale_frame->height = fc->output_height;

        err = av_frame_get_buffer(fc->scale_frame, 0);
        if (err < 0) {
            LOG_ERROR("Failed to allocate scale frame: %d.", err);
            return -1;
        }

        err = sws_scale_frame(fc->scale, fc->scale_frame, fc->decode_frame);
        if (err < 0) {
            LOG_ERROR("Failed to scale frame: %d.", err);
            return -1;
        }

        av_frame_unref(fc->decode_frame);
        av_frame_move_ref(fc->output_frame, fc->scale_frame);
    } else {
        av_frame_move_ref(fc->output_frame, fc->decode_frame);
    }

    *width = fc->output_width;
    *height = fc->output_height;
    *pitch = fc->base.infos->pitch;
    return 0;
}

static int file_transfer_screen(FileCaptureDevice *fc, CaptureEncoderHints *hints) {
    if (!fc->output_frame || !fc->output_frame->data[0]) {
        LOG_ERROR("No output frame available when trying to transfer data.");
        return -1;
    }

    memset(hints, 0, sizeof(*hints));
    return 0;
}

static int file_capture_getdata(FileCaptureDevice *device, void **buf, int *stride) {
    *buf = device->output_frame->data[0];
    *stride = device->output_frame->linesize[0];
    return 0;
}

static int file_get_dimensions(FileCaptureDevice *device, int *w, int *h, int *dpi) {
    *w = device->output_width;
    *h = device->output_height;
    *dpi = 0;
    return 0;
}

static int file_capture_device_init(FileCaptureDevice *dev, int width, int height, int dpi) {
    dev->output_width = width;
    dev->output_height = height;

    int err = file_capture_open(dev);
    if (err < 0) {
        return -1;
    }

    return 0;
}

CaptureDeviceImpl *create_file_capture_device(CaptureDeviceInfos *infos, const char *movie) {
    FileCaptureDevice *fc = (FileCaptureDevice *)safe_zalloc(sizeof(FileCaptureDevice));

    CaptureDeviceImpl *impl = &fc->base;
    impl->device_type = FILE_DEVICE;
    impl->infos = infos;
    impl->init = (CaptureDeviceInitFn)file_capture_device_init;
    impl->reconfigure = (CaptureDeviceReconfigureFn)file_reconfigure_capture_device;
    impl->capture_screen = (CaptureDeviceCaptureScreenFn)file_capture_screen;
    impl->capture_get_data = (CaptureDeviceGetDataFn)file_capture_getdata;
    impl->transfer_screen = (CaptureDeviceTransferScreenFn)file_transfer_screen;
    impl->get_dimensions = (CaptureDeviceGetDimensionsFn)file_get_dimensions;
    impl->destroy = (CaptureDeviceDestroyFn)file_destroy_capture_device;

    fc->filename = strdup(movie);
    if (!fc->filename) goto out_error;
    return impl;

out_error:
    free(fc);
    return NULL;
}
