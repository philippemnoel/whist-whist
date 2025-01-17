/**
 * Copyright 2022 Whist Technologies, Inc.
 * @file decoder.c
 * @brief Decoder test utility.
 */

#include <libavcodec/avcodec.h>
#include <libavcodec/bsf.h>
#include <libavformat/avformat.h>
#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <openssl/evp.h>

#include <whist/core/platform.h>
#if OS_IS(OS_WIN32)
#include <libavutil/hwcontext_d3d11va.h>
#endif

#include "whist/core/whist.h"
#include "whist/video/codec/decode.h"
#include "whist/utils/aes.h"
#include "whist/utils/command_line.h"
#include "whist/logging/log_statistic.h"

typedef struct {
    const char *file_name;
    AVFormatContext *demux;
    enum AVMediaType media_type;
    int stream_index;
    AVStream *stream;
    AVBSFContext *bsf;
} TestInput;

static WhistStatus open_demuxer(TestInput *input) {
    int err;

    if (!input->file_name) {
        return WHIST_ERROR_INVALID_ARGUMENT;
    }

    input->demux = avformat_alloc_context();
    if (!input->demux) {
        return WHIST_ERROR_OUT_OF_MEMORY;
    }

    LOG_INFO("Opening demuxer for %s.", input->file_name);

    err = avformat_open_input(&input->demux, input->file_name, NULL, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to open input file \"%s\": %d.", input->file_name, err);
        return WHIST_ERROR_NOT_FOUND;
    }

    err = avformat_find_stream_info(input->demux, NULL);
    if (err < 0) {
        LOG_ERROR("Failed to find stream information in input file: %d.", err);
        return WHIST_ERROR_NOT_FOUND;
    }

    input->stream_index = av_find_best_stream(input->demux, input->media_type, -1, -1, NULL, 0);
    if (input->stream_index < 0) {
        LOG_ERROR("Failed to find any %s streams in input file.",
                  av_get_media_type_string(input->media_type));
        return WHIST_ERROR_NOT_FOUND;
    }

    input->stream = input->demux->streams[input->stream_index];

    const char *bsf_type = NULL;
    const AVCodecParameters *par = input->stream->codecpar;
    if (par->codec_id == AV_CODEC_ID_H264 && par->extradata && par->extradata[0]) {
        bsf_type = "h264_mp4toannexb";
    } else if (par->codec_id == AV_CODEC_ID_HEVC && par->extradata && par->extradata[0]) {
        bsf_type = "hevc_mp4toannexb";
    }
    if (bsf_type) {
        LOG_INFO("Opening %s BSF.", bsf_type);

        const AVBitStreamFilter *bsf = av_bsf_get_by_name(bsf_type);
        if (!bsf) {
            LOG_ERROR("Failed to find required %s BSF: %d.", bsf_type, err);
            return WHIST_ERROR_NOT_FOUND;
        }
        err = av_bsf_alloc(bsf, &input->bsf);
        if (err < 0) {
            LOG_ERROR("Failed to allocate BSF: %d.", err);
            return WHIST_ERROR_EXTERNAL;
        }

        err = avcodec_parameters_copy(input->bsf->par_in, par);
        if (err < 0) {
            LOG_ERROR("Failed to copy BSF parameters: %d.", err);
            return WHIST_ERROR_EXTERNAL;
        }

        err = av_bsf_init(input->bsf);
        if (err < 0) {
            LOG_ERROR("Failed to initialise BSF: %d.", err);
            return WHIST_ERROR_EXTERNAL;
        }
    }

    return WHIST_SUCCESS;
}

static WhistStatus get_next_packet(TestInput *input, AVPacket *pkt) {
    int err;

    while (1) {
        err = av_read_frame(input->demux, pkt);
        if (err == AVERROR_EOF) {
            return WHIST_ERROR_END_OF_FILE;
        }
        if (err < 0) {
            LOG_ERROR("Failed to demux packet: %d.", err);
            return WHIST_ERROR_IO;
        }
        if (pkt->size == 0 || pkt->stream_index != input->stream_index) {
            continue;
        }

        err = av_bsf_send_packet(input->bsf, pkt);
        if (err < 0) {
            LOG_ERROR("Failed to send packet to BSF: %d.", err);
            return WHIST_ERROR_IO;
        }
        av_packet_unref(pkt);

        err = av_bsf_receive_packet(input->bsf, pkt);
        if (err < 0) {
            if (err == EAGAIN) {
                continue;
            }
            LOG_ERROR("Failed to receive packet from BSF: %d.", err);
            return WHIST_ERROR_IO;
        }
        break;
    }

    return WHIST_SUCCESS;
}

static void close_demuxer(TestInput *input) {
    av_bsf_free(&input->bsf);
    avformat_close_input(&input->demux);
}

typedef struct TestOutput {
    const char *file_name;
    bool download;
    void (*func)(struct TestOutput *output, AVFrame *frame);

    int frame_number;
    FILE *file;
    const EVP_MD *hash_type;

    SDL_Window *sdl_window;
    SDL_Renderer *sdl_renderer;
    SDL_Texture *sdl_texture;
    SDL_PixelFormatEnum sdl_texture_format;
    enum AVPixelFormat hardware_format;
    AVBufferRef *hardware_device;
} TestOutput;

static void output_to_hash(TestOutput *output, AVFrame *frame) {
    EVP_MD_CTX *ctx = EVP_MD_CTX_new();
    EVP_DigestInit_ex(ctx, output->hash_type, NULL);

    if (frame->format == AV_PIX_FMT_NV12) {
        for (int line = 0; line < frame->height; line++) {
            EVP_DigestUpdate(ctx, frame->data[0] + line * frame->linesize[0], frame->width);
        }
        for (int line = 0; line < frame->height / 2; line++) {
            EVP_DigestUpdate(ctx, frame->data[1] + line * frame->linesize[1], frame->width);
        }
    } else if (frame->format == AV_PIX_FMT_YUV420P) {
        for (int line = 0; line < frame->height; line++) {
            EVP_DigestUpdate(ctx, frame->data[0] + line * frame->linesize[0], frame->width);
        }
        for (int line = 0; line < frame->height / 2; line++) {
            EVP_DigestUpdate(ctx, frame->data[1] + line * frame->linesize[1], frame->width / 2);
            EVP_DigestUpdate(ctx, frame->data[2] + line * frame->linesize[2], frame->width / 2);
        }
    } else {
        LOG_INFO("Unknown format %s for hash.", av_get_pix_fmt_name(frame->format));
        return;
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len;
    EVP_DigestFinal_ex(ctx, hash, &hash_len);
    EVP_MD_CTX_free(ctx);

    char str[2 * EVP_MAX_MD_SIZE + 1];
    for (unsigned int i = 0; i < hash_len; i++) {
        snprintf(str + 2 * i, 3, "%02x", hash[i]);
    }
    LOG_INFO("Frame %d hash %s.", output->frame_number, str);
}

static void output_to_file(TestOutput *output, AVFrame *frame) {
    if (frame->format == AV_PIX_FMT_NV12) {
        for (int line = 0; line < frame->height; line++) {
            fwrite(frame->data[0] + line * frame->linesize[0], frame->width, 1, output->file);
        }
        for (int line = 0; line < frame->height / 2; line++) {
            fwrite(frame->data[1] + line * frame->linesize[1], frame->width, 1, output->file);
        }
    } else if (frame->format == AV_PIX_FMT_YUV420P) {
        for (int line = 0; line < frame->height; line++) {
            fwrite(frame->data[0] + line * frame->linesize[0], frame->width, 1, output->file);
        }
        for (int line = 0; line < frame->height / 2; line++) {
            fwrite(frame->data[1] + line * frame->linesize[1], frame->width / 2, 1, output->file);
            fwrite(frame->data[2] + line * frame->linesize[2], frame->width / 2, 1, output->file);
        }
    } else {
        LOG_INFO("Unknown format %s for file output.", av_get_pix_fmt_name(frame->format));
    }
}

static void output_to_sdl(TestOutput *output, AVFrame *frame) {
    if (frame->format == AV_PIX_FMT_D3D11) {
        // Render texture directly without an extra copy.

        struct {
            void *texture;
            int index;
        } handle;
        handle.texture = frame->data[0];
        handle.index = (intptr_t)frame->data[1];

        SDL_Texture *tex;

        tex = SDL_CreateTextureFromHandle(output->sdl_renderer, SDL_PIXELFORMAT_NV12,
                                          SDL_TEXTUREACCESS_STATIC, frame->width, frame->height,
                                          &handle);
        if (tex == NULL) {
            LOG_ERROR("Failed to create texture: %s.", SDL_GetError());
            return;
        }

        SDL_RenderCopy(output->sdl_renderer, tex, NULL, NULL);

        SDL_DestroyTexture(tex);

    } else {
        // Copy to an intermediate texture, then render with that.

        SDL_PixelFormatEnum format;
        if (frame->format == AV_PIX_FMT_YUV420P) {
            format = SDL_PIXELFORMAT_IYUV;
        } else {
            format = SDL_PIXELFORMAT_NV12;
        }

        if (format == output->sdl_texture_format) {
            // Existing texture can be reused.
        } else {
            if (output->sdl_texture) {
                // Destroy an existing texture.
                SDL_DestroyTexture(output->sdl_texture);
            }

            output->sdl_texture =
                SDL_CreateTexture(output->sdl_renderer, format, SDL_TEXTUREACCESS_STREAMING,
                                  frame->width, frame->height);
            if (!output->sdl_texture) {
                LOG_ERROR("Failed to create SDL texture: %s.", SDL_GetError());
                return;
            }

            output->sdl_texture_format = format;
        }

        int res;
        if (frame->format == AV_PIX_FMT_VIDEOTOOLBOX) {
            res = SDL_UpdateNVTexture(output->sdl_texture, NULL, frame->data[3], frame->width,
                                      frame->data[3], frame->width);
        } else if (frame->format == AV_PIX_FMT_NV12) {
            res = SDL_UpdateNVTexture(output->sdl_texture, NULL, frame->data[0], frame->linesize[0],
                                      frame->data[1], frame->linesize[1]);
        } else if (frame->format == AV_PIX_FMT_YUV420P) {
            res = SDL_UpdateYUVTexture(output->sdl_texture, NULL, frame->data[0],
                                       frame->linesize[0], frame->data[1], frame->linesize[1],
                                       frame->data[2], frame->linesize[2]);
        } else {
            LOG_ERROR("Pixel format %s not supported with SDL output.",
                      av_get_pix_fmt_name(frame->format));
            return;
        }
        if (res < 0) {
            LOG_ERROR("Failed to update texture: %s.", SDL_GetError());
            return;
        }

        SDL_RenderCopy(output->sdl_renderer, output->sdl_texture, NULL, NULL);
    }

    SDL_RenderPresent(output->sdl_renderer);

    // Also empty the SDL event queue.
    SDL_Event event;
    while (SDL_PollEvent(&event)) {
        // Do nothing, we don't actually care about these events.
    }
}

static int sdl_window_width = 1280;
static int sdl_window_height = 720;

COMMAND_LINE_INT_OPTION(sdl_window_width, 0, "sdl-window-width", 256, 8192,
                        "Width of the window when using SDL output.")
COMMAND_LINE_INT_OPTION(sdl_window_height, 0, "sdl-window-height", 256, 8192,
                        "Height of the window when using SDL output.")

static bool hardware;

COMMAND_LINE_BOOL_OPTION(hardware, 0, "hw", "Use hardware decoder.")

static WhistStatus sdl_get_hardware_device(TestOutput *output) {
    SDL_RendererInfo info;
    int err;

    err = SDL_GetRendererInfo(output->sdl_renderer, &info);
    if (err < 0) {
        LOG_ERROR("Failed to get renderer info: %s.", SDL_GetError());
        return WHIST_ERROR_EXTERNAL;
    }
    LOG_INFO("SDL renderer is %s.", info.name);

#if OS_IS(OS_WIN32)
    if (!strcmp(info.name, "direct3d11")) {
        ID3D11Device *d3d11_device = SDL_RenderGetD3D11Device(output->sdl_renderer);
        if (!d3d11_device) {
            LOG_ERROR("Failed to fetch D3D11 device: %s.", SDL_GetError());
            return WHIST_ERROR_EXTERNAL;
        }

        if (d3d11_device) {
            LOG_INFO("Using D3D11 device from SDL renderer.");

            AVBufferRef *dev_ref = av_hwdevice_ctx_alloc(AV_HWDEVICE_TYPE_D3D11VA);
            FATAL_ASSERT(dev_ref);

            AVHWDeviceContext *dev = (AVHWDeviceContext *)dev_ref->data;
            AVD3D11VADeviceContext *hwctx = dev->hwctx;
            hwctx->device = d3d11_device;

            err = av_hwdevice_ctx_init(dev_ref);
            if (err < 0) {
                LOG_WARNING("Failed to create hardware device.");
                av_buffer_unref(&dev_ref);
                return WHIST_ERROR_EXTERNAL;
            }

            output->hardware_device = dev_ref;
            output->hardware_format = AV_PIX_FMT_D3D11;
        }
    }
#endif

#if OS_IS(OS_MACOS)
    if (!strcmp(info.name, "metal")) {
        // No device required; output can use VideoToolbox.
        output->hardware_format = AV_PIX_FMT_VIDEOTOOLBOX;
    }
#endif

    return WHIST_SUCCESS;
}

static WhistStatus create_sdl(TestOutput *output) {
    int res;

    res = SDL_Init(SDL_INIT_VIDEO);
    if (res < 0) {
        LOG_ERROR("Failed to initialise SDL: %s.", SDL_GetError());
        return WHIST_ERROR_EXTERNAL;
    }

#if OS_IS(OS_WIN32)
    // Ensure that we use D3D11 and enable debug layers.
    SDL_SetHint(SDL_HINT_RENDER_DRIVER, "direct3d11");
    SDL_SetHint(SDL_HINT_RENDER_DIRECT3D11_DEBUG, "1");
#endif

    // Enable Vsync to match the framerate to the display - normally
    // the decoder is run as fast as possible, but we don't want that
    // when using SDL output.
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, "1");

    uint32_t window_flags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE;

    output->sdl_window =
        SDL_CreateWindow("Whist Decoder Test", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
                         sdl_window_width, sdl_window_height, window_flags);
    if (!output->sdl_window) {
        LOG_ERROR("Failed to create SDL window: %s.", SDL_GetError());
        return WHIST_ERROR_EXTERNAL;
    }

    // No flags - tests want to allow a software renderer here.
    output->sdl_renderer = SDL_CreateRenderer(output->sdl_window, -1, 0);
    if (!output->sdl_renderer) {
        LOG_ERROR("Failed to create SDL renderer: %s.", SDL_GetError());
        return WHIST_ERROR_EXTERNAL;
    }

    // We only make a texture when the first frame arrives.
    output->sdl_texture_format = SDL_PIXELFORMAT_UNKNOWN;

    sdl_get_hardware_device(output);

    return WHIST_SUCCESS;
}

static void destroy_sdl(TestOutput *output) {
    SDL_DestroyTexture(output->sdl_texture);
    SDL_DestroyRenderer(output->sdl_renderer);
    SDL_DestroyWindow(output->sdl_window);
    SDL_Quit();
}

static const char *input_file;
static const char *input_type;
static const char *output_type;
static const char *output_file;

COMMAND_LINE_STRING_OPTION(input_file, 0, "input-file", 256, "File to take input from.")
COMMAND_LINE_STRING_OPTION(input_type, 0, "input-type", 256, "Type of input (audio, video).")
COMMAND_LINE_STRING_OPTION(output_type, 0, "output-type", 256, "Type of output (null, file, sdl).")
COMMAND_LINE_STRING_OPTION(output_file, 0, "output-file", 256, "File to write output to.");

static const char *hash_name;

COMMAND_LINE_STRING_OPTION(hash_name, 0, "hash", 16, "Hash function to use (defaults to MD5).")

static TestInput *create_input(void) {
    TestInput *input = safe_malloc(sizeof(*input));
    memset(input, 0, sizeof(*input));

    input->file_name = input_file;

    input->media_type = AVMEDIA_TYPE_VIDEO;
    if (input_type) {
        if (!strcmp(input_type, "video")) {
            input->media_type = AVMEDIA_TYPE_VIDEO;
        } else if (!strcmp(input_type, "audio")) {
            input->media_type = AVMEDIA_TYPE_AUDIO;
        } else {
            LOG_ERROR("Invalid input type %s.", input_type);
            return NULL;
        }
    }

    return input;
}

static void destroy_input(TestInput *input) {
    close_demuxer(input);
    free(input);
}

static TestOutput *create_output(void) {
    TestOutput *output = safe_malloc(sizeof(*output));
    memset(output, 0, sizeof(*output));

    output->file_name = output_file;
    if (output_type) {
        if (!strcmp(output_type, "null")) {
            output->func = NULL;
        } else if (!strcmp(output_type, "hash")) {
            output->download = true;
            output->func = &output_to_hash;
            output->hash_type = EVP_get_digestbyname(hash_name ? hash_name : "MD5");
            if (output->hash_type == NULL) {
                LOG_ERROR("Unknown hash type %s.", hash_name);
                return NULL;
            }
        } else if (!strcmp(output_type, "file")) {
            output->download = true;
            output->func = &output_to_file;
            output->file = fopen(output->file_name, "wb");
            if (output->file == NULL) {
                LOG_ERROR("Failed to open output file %s.", output->file_name);
                return NULL;
            }
        } else if (!strcmp(output_type, "sdl")) {
            WhistStatus err = create_sdl(output);
            if (err != WHIST_SUCCESS) {
                return NULL;
            }
            output->func = &output_to_sdl;
        } else {
            LOG_ERROR("Invalid output type %s.", output_type);
        }
    }

    return output;
}

static void destroy_output(TestOutput *output) {
    if (output->file) {
        fclose(output->file);
    }
    if (output->sdl_window) {
        destroy_sdl(output);
    }
    free(output);
}

static int max_frames;

COMMAND_LINE_INT_OPTION(max_frames, 0, "frames", 1, INT_MAX,
                        "Stop after processing this many frames.")

int main(int argc, const char **argv) {
    WhistStatus err = whist_parse_command_line(argc, argv, NULL);
    if (err != WHIST_SUCCESS) {
        LOG_ERROR("Failed to parse command line: %s.", whist_error_string(err));
        return 1;
    }

    TestInput *input;
    TestOutput *output;

    input = create_input();

    whist_init_subsystems();
    whist_init_statistic_logger(1);

    err = open_demuxer(input);
    if (err != WHIST_SUCCESS) {
        LOG_ERROR("Failed to open demuxer: %s.", whist_error_string(err));
        return 1;
    }

    output = create_output();

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    uint8_t *input_buffer = safe_malloc(MAX_VIDEOFRAME_DATA_SIZE);
    size_t input_buffer_size;

    CodecType video_codec_type;
    VideoDecoder *video_decoder;
    const AVCodecParameters *par = input->stream->codecpar;
    if (input->media_type == AVMEDIA_TYPE_VIDEO) {
        if (par->codec_id == AV_CODEC_ID_H264) {
            video_codec_type = CODEC_TYPE_H264;
        } else if (par->codec_id == AV_CODEC_ID_HEVC) {
            video_codec_type = CODEC_TYPE_H265;
        } else {
            LOG_ERROR("Codec %s is not supported.", avcodec_get_name(par->codec_id));
            return 1;
        }
        VideoDecoderParams video_decoder_params = {
            .codec_type = video_codec_type,
            .width = par->width,
            .height = par->height,
            .hardware_decode = hardware,
            .renderer_output_format = output->hardware_format,
            .hardware_device = output->hardware_device,
        };
        video_decoder = video_decoder_create(&video_decoder_params);
        if (!video_decoder) {
            LOG_ERROR("Failed to create video decoder.");
            return 1;
        }
    } else {
        LOG_ERROR("Audio is not currently supported.");
        return 1;
    }

    while (1) {
        err = get_next_packet(input, pkt);
        if (err != WHIST_SUCCESS) {
            if (err == WHIST_ERROR_END_OF_FILE) {
                LOG_INFO("End of file!");
            } else {
                LOG_ERROR("Failed to get next packet: %s.", whist_error_string(err));
            }
            break;
        }

        bool key_frame = !!(pkt->flags & AV_PKT_FLAG_KEY);
        write_avpackets_to_buffer(1, &pkt, input_buffer);
        input_buffer_size = 8 + pkt->size;
        av_packet_unref(pkt);

        if (input->media_type == AVMEDIA_TYPE_VIDEO) {
            int dec_err;
            dec_err = video_decoder_send_packets(video_decoder, input_buffer, input_buffer_size,
                                                 key_frame);
            if (dec_err < 0) {
                LOG_ERROR("Failed to send packets to decoder: %d.", dec_err);
                break;
            }

            dec_err = video_decoder_decode_frame(video_decoder);
            if (dec_err < 0) {
                LOG_ERROR("Failed to decode frame: %d.", dec_err);
                break;
            }
            if (dec_err > 0) {
                // Decoder didn't produce a frame.
                continue;
            }

            if (output->download) {
                dec_err = av_hwframe_transfer_data(frame, video_decoder->decoded_frame, 0);
                if (dec_err < 0) {
                    LOG_ERROR("Failed to download frame: %d.", dec_err);
                    break;
                }
            } else {
                av_frame_ref(frame, video_decoder->decoded_frame);
            }

            LOG_INFO("Decoded frame: format %s size %dx%d pic_type %d.",
                     av_get_pix_fmt_name(frame->format), frame->width, frame->height,
                     frame->pict_type);

            if (output->func) {
                output->func(output, frame);
            }
            ++output->frame_number;

            av_frame_unref(frame);
        }

        if (max_frames && output->frame_number >= max_frames) {
            break;
        }
    }

    if (input->media_type == AVMEDIA_TYPE_VIDEO) {
        destroy_video_decoder(video_decoder);
    }

    free(input_buffer);
    av_packet_free(&pkt);
    av_frame_free(&frame);

    destroy_output(output);
    destroy_input(input);

    destroy_statistic_logger();
    destroy_logger();

    return 0;
}
