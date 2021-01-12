/**
 * Copyright Fractal Computers, Inc. 2020
 * @file video.c
 * @brief This file contains all code that interacts directly with receiving and
 *        processing video packets on the client.
============================
Usage
============================

initVideo() gets called before any video packet can be received. The video
packets are received as standard FractalPackets by ReceiveVideo(FractalPacket*
packet), before being saved in a proper video frame format.
*/

/*
============================
Includes
============================
*/

#include "video.h"
#include "sdl_utils.h"

#include <stdio.h>

#include "../fractal/cursor/peercursor.h"
#include "../fractal/utils/png.h"
#include "../fractal/utils/sdlscreeninfo.h"
#include "SDL2/SDL.h"
#include "network.h"

#define UNUSED(x) (void)(x)

#define USE_HARDWARE true

// Global Variables
extern volatile SDL_Window* window;

extern volatile int server_width;
extern volatile int server_height;
extern volatile CodecType server_codec_type;

extern int client_id;

// Keeping track of max mbps
extern volatile int max_bitrate;
extern volatile bool update_mbps;

extern volatile int output_width;
extern volatile int output_height;
extern volatile CodecType output_codec_type;

extern volatile int running_ci;

// START VIDEO VARIABLES
volatile FractalCursorState cursor_state = CURSOR_STATE_VISIBLE;
volatile SDL_Cursor* cursor = NULL;
volatile FractalCursorID last_cursor = (FractalCursorID)SDL_SYSTEM_CURSOR_ARROW;
volatile bool pending_sws_update = false;
volatile bool pending_texture_update = false;
volatile bool pending_resize_render = false;

static enum AVPixelFormat sws_input_fmt;

#define LOG_VIDEO false

#define BITRATE_BUCKET_SIZE 500000

#define CURSORIMAGE_A 0xff000000
#define CURSORIMAGE_R 0x00ff0000
#define CURSORIMAGE_G 0x0000ff00
#define CURSORIMAGE_B 0x000000ff

/*
============================
Custom Types
============================
*/

typedef struct FrameData {
    char* frame_buffer;
    int frame_size;
    int id;
    int packets_received;
    int num_packets;
    bool received_indicies[LARGEST_FRAME_SIZE / MAX_PAYLOAD_SIZE + 5];
    bool nacked_indicies[LARGEST_FRAME_SIZE / MAX_PAYLOAD_SIZE + 5];
    bool rendered;

    int num_times_nacked;

    int last_nacked_index;

    clock last_nacked_timer;

    clock last_packet_timer;

    clock frame_creation_timer;
} FrameData;

struct VideoData {
    FrameData* pending_ctx;
    int frames_received;
    int bytes_transferred;
    clock frame_timer;
    int last_statistics_id;
    int last_rendered_id;
    int max_id;
    int most_recent_iframe;

    clock last_iframe_request_timer;
    bool is_waiting_for_iframe;

    SDL_Thread* render_screen_thread;
    bool run_render_screen_thread;

    SDL_sem* renderscreen_semaphore;

    double target_mbps;
    int num_nacked;
    int bucket;  // = STARTING_BITRATE / BITRATE_BUCKET_SIZE;
    int nack_by_bitrate[MAXIMUM_BITRATE / BITRATE_BUCKET_SIZE + 5];
    double seconds_by_bitrate[MAXIMUM_BITRATE / BITRATE_BUCKET_SIZE + 5];
} video_data;

typedef struct SDLVideoContext {
    SDL_Renderer* renderer;
    SDL_Texture* texture;

    Uint8* data[4];
    int linesize[4];

    VideoDecoder* decoder;
    struct SwsContext* sws;
} SDLVideoContext;
SDLVideoContext video_context;

// mbps that currently works
volatile double working_mbps;

// Context of the frame that is currently being rendered
volatile FrameData render_context;

// True if RenderScreen is currently rendering a frame
volatile bool rendering = false;
volatile bool skip_render = false;
volatile bool can_render;

SDL_mutex* render_mutex;

// Hold information about frames as the packets come in
#define RECV_FRAMES_BUFFER_SIZE 275
FrameData receiving_frames[RECV_FRAMES_BUFFER_SIZE];
char frame_bufs[RECV_FRAMES_BUFFER_SIZE][LARGEST_FRAME_SIZE];

bool has_rendered_yet = false;

// END VIDEO VARIABLES

// START VIDEO FUNCTIONS

/*
============================
Private Functions
============================
*/

void update_decoder_parameters(int width, int height, CodecType codec_type);
int32_t render_screen(SDL_Renderer* renderer);
void loading_sdl(SDL_Renderer* renderer, int loading_index);
void nack(int id, int index);
bool request_iframe();
void update_sws_context();
void update_pixel_format();
void update_texture();
static int render_peers(SDL_Renderer* renderer, PeerUpdateMessage* msgs, size_t num_msgs);
void clear_sdl(SDL_Renderer* renderer);
int init_multithreaded_video(void* opaque);

/*
============================
Private Function Implementations
============================
*/

void update_decoder_parameters(int width, int height, CodecType codec_type) {
    /*
        Update video decoder parameters

        Arguments:
            width (int): video width
            height (int): video height
            codec_type (CodecType): decoder codec type
    */

    LOG_INFO("Updating Width & Height to %dx%d and Codec to %d", width, height, codec_type);

    if (video_context.decoder) {
        destroy_video_decoder(video_context.decoder);
    }

    VideoDecoder* decoder = create_video_decoder(width, height, USE_HARDWARE, codec_type);

    video_context.decoder = decoder;
    if (!decoder) {
        LOG_WARNING("ERROR: Decoder could not be created!");
        destroy_logger();
        exit(-1);
    }

    sws_input_fmt = AV_PIX_FMT_NONE;

    server_width = width;
    server_height = height;
    server_codec_type = codec_type;
    output_codec_type = codec_type;
}

int32_t render_screen(SDL_Renderer* renderer) {
    /*
        Render the video screen that the user sees

        Arguments:
            renderer (SDL_Renderer*): SDL renderer used to generate video

        Return:
            (int32_t): 0 on success, -1 on failure
    */

    LOG_INFO("RenderScreen running on Thread %d", SDL_GetThreadID(NULL));

//    Windows GHA VM cannot render, it just segfaults on creating the renderer
// TODO test rendering in windows CI.
#if _WIN32
    if (running_ci) {
        return 0;
    }
#endif
    int loading_index = 0;

    // present the loading screen
    loading_sdl(renderer, loading_index);

    while (video_data.run_render_screen_thread) {
        int ret = SDL_SemTryWait(video_data.renderscreen_semaphore);
        SDL_LockMutex(render_mutex);
        if (pending_resize_render) {
            SDL_RenderCopy((SDL_Renderer*)video_context.renderer, video_context.texture, NULL,
                           NULL);
            SDL_RenderPresent((SDL_Renderer*)video_context.renderer);
        }
        SDL_UnlockMutex(render_mutex);

        if (ret == SDL_MUTEX_TIMEDOUT) {
            if (loading_index >= 0) {
                loading_index++;
                loading_sdl(renderer, loading_index);
            }

            SDL_Delay(1);
            continue;
        }

        loading_index = -1;

        if (ret < 0) {
            LOG_ERROR("Semaphore Error");
            return -1;
        }

        if (!rendering) {
            LOG_WARNING("Sem opened but rendering is not true!");
            continue;
        }

        // Cast to Frame* because this variable is not volatile in this section
        Frame* frame = (Frame*)render_context.frame_buffer;
        PeerUpdateMessage* peer_update_msgs =
            (PeerUpdateMessage*)(((char*)frame->compressed_frame) + frame->size);
        size_t num_peer_update_msgs = frame->num_peer_update_msgs;

#if LOG_VIDEO
        mprintf("Rendering ID %d (Age %f) (Packets %d) %s\n", renderContext.id,
                get_timer(renderContext.frame_creation_timer), renderContext.num_packets,
                frame->is_iframe ? "(I-Frame)" : "");
#endif

        if (get_timer(render_context.frame_creation_timer) > 25.0 / 1000.0) {
            LOG_INFO("Late! Rendering ID %d (Age %f) (Packets %d) %s", render_context.id,
                     get_timer(render_context.frame_creation_timer), render_context.num_packets,
                     frame->is_iframe ? "(I-Frame)" : "");
        }

        if ((int)(sizeof(Frame) + frame->size +
                  sizeof(PeerUpdateMessage) * frame->num_peer_update_msgs) !=
            render_context.frame_size) {
            mprintf("Incorrect Frame Size! %d instead of %d\n",
                    sizeof(Frame) + frame->size +
                        sizeof(PeerUpdateMessage) * frame->num_peer_update_msgs,
                    render_context.frame_size);
        }

        if (frame->width != server_width || frame->height != server_height ||
            frame->codec_type != server_codec_type) {
            if (frame->is_iframe) {
                LOG_INFO(
                    "Updating client rendering to match server's width and "
                    "height and codec! "
                    "From %dx%d, codec %d to %dx%d, codec %d",
                    server_width, server_height, server_codec_type, frame->width, frame->height,
                    frame->codec_type);
                update_decoder_parameters(frame->width, frame->height, frame->codec_type);
            } else {
                LOG_INFO("Wants to change resolution, but not an i-frame!");
            }
        }

        clock decode_timer;
        start_timer(&decode_timer);

        if (!video_decoder_decode(video_context.decoder, frame->compressed_frame, frame->size)) {
            LOG_WARNING("Failed to video_decoder_decode!");
            rendering = false;
            continue;
        }

        // LOG_INFO( "Decode Time: %f", get_timer( decode_timer ) );

        SDL_LockMutex(render_mutex);
        update_pixel_format();

        if (!skip_render && can_render) {
            clock sws_timer;
            start_timer(&sws_timer);

            update_texture();

            if (video_context.sws) {
                sws_scale(
                    video_context.sws, (uint8_t const* const*)video_context.decoder->sw_frame->data,
                    video_context.decoder->sw_frame->linesize, 0, video_context.decoder->height,
                    video_context.data, video_context.linesize);
            } else {
                memcpy(video_context.data, video_context.decoder->sw_frame->data,
                       sizeof(video_context.data));
                memcpy(video_context.linesize, video_context.decoder->sw_frame->linesize,
                       sizeof(video_context.linesize));
            }

            // LOG_INFO( "SWS Time: %f", get_timer( sws_timer ) );

            SDL_UpdateYUVTexture(video_context.texture, NULL, video_context.data[0],
                                 video_context.linesize[0], video_context.data[1],
                                 video_context.linesize[1], video_context.data[2],
                                 video_context.linesize[2]);

            if (!video_context.sws) {
                // Clear out bits that aren't used from av_alloc_frame
                memset(video_context.data, 0, sizeof(video_context.data));
            }
        }

        // Set cursor to frame's desired cursor type
        if ((FractalCursorID)frame->cursor.cursor_id != last_cursor ||
            frame->cursor.cursor_use_bmp) {
            if (cursor) {
                SDL_FreeCursor((SDL_Cursor*)cursor);
            }
            if (frame->cursor.cursor_use_bmp) {
                // use bitmap data to set cursor

                SDL_Surface* cursor_surface = SDL_CreateRGBSurfaceFrom(
                    frame->cursor.cursor_bmp, frame->cursor.cursor_bmp_width,
                    frame->cursor.cursor_bmp_height, sizeof(uint32_t) * 8,
                    sizeof(uint32_t) * frame->cursor.cursor_bmp_width, CURSORIMAGE_R, CURSORIMAGE_G,
                    CURSORIMAGE_B, CURSORIMAGE_A);
                // potentially SDL_SetSurfaceBlendMode since X11 cursor BMPs are
                // pre-alpha multplied
                cursor = SDL_CreateColorCursor(cursor_surface, frame->cursor.cursor_bmp_hot_x,
                                               frame->cursor.cursor_bmp_hot_y);
                SDL_FreeSurface(cursor_surface);
            } else {
                // use cursor id to set cursor
                cursor = SDL_CreateSystemCursor(frame->cursor.cursor_id);
            }
            SDL_SetCursor((SDL_Cursor*)cursor);

            last_cursor = (FractalCursorID)frame->cursor.cursor_id;
        }

        if (frame->cursor.cursor_state != cursor_state) {
            if (frame->cursor.cursor_state == CURSOR_STATE_HIDDEN) {
                SDL_SetRelativeMouseMode(SDL_TRUE);
            } else {
                SDL_SetRelativeMouseMode(SDL_FALSE);
            }

            cursor_state = frame->cursor.cursor_state;
        }

        // mprintf("Client Frame Time for ID %d: %f\n", renderContext.id,
        // get_timer(renderContext.client_frame_timer));

        if (!skip_render && can_render) {
            // SDL_SetRenderDrawColor((SDL_Renderer*)renderer, 100, 20, 160,
            // SDL_ALPHA_OPAQUE); SDL_RenderClear((SDL_Renderer*)renderer);

            SDL_RenderCopy((SDL_Renderer*)renderer, video_context.texture, NULL, NULL);
            if (render_peers((SDL_Renderer*)renderer, peer_update_msgs, num_peer_update_msgs) !=
                0) {
                LOG_ERROR("Failed to render peers.");
            }
            SDL_RenderPresent((SDL_Renderer*)renderer);
        }

        SDL_UnlockMutex(render_mutex);

#if LOG_VIDEO
        LOG_DEBUG("Rendered %d (Size: %d) (Age %f)", renderContext.id, renderContext.frame_size,
                  get_timer(renderContext.frame_creation_timer));
#endif

        if (frame->is_iframe) {
            video_data.is_waiting_for_iframe = false;
        }

        video_data.last_rendered_id = render_context.id;
        has_rendered_yet = true;
        rendering = false;
    }

    SDL_Delay(5);
    return 0;
}

void loading_sdl(SDL_Renderer* renderer, int loading_index) {
    /*
        Make the screen black and show the loading screen

        Arguments:
            renderer (SDL_Renderer*): video renderer
            loading_index (int): the index of the loading frame
    */

    int gif_frame_index = loading_index % 83;

    clock c;
    start_timer(&c);

    char frame_name[24];
    if (gif_frame_index < 10) {
        snprintf(frame_name, sizeof(frame_name), "loading/frame_0%d.png", gif_frame_index);
        //            LOG_INFO("Frame loading/frame_0%d.png", gif_frame_index);
    } else {
        snprintf(frame_name, sizeof(frame_name), "loading/frame_%d.png", gif_frame_index);
        //            LOG_INFO("Frame loading/frame_%d.png", gif_frame_index);
    }

    AVPacket pkt;
    av_init_packet(&pkt);
    png_file_to_bmp(frame_name, &pkt);
    // LOG_INFO( "Test: %f", get_timer(c) );

    SDL_RWops* rw = SDL_RWFromMem(pkt.data, pkt.size);

    // second parameter nonzero means free the rw after reading it, no need to free rw ourselves
    SDL_Surface* loading_screen = SDL_LoadBMP_RW(rw, 1);
    if (loading_screen == NULL) {
        LOG_INFO("IMG_Load");
        return;
    }

    // free pkt.data which is initialized by calloc in png_file_to_bmp
    free(pkt.data);

    SDL_Texture* loading_screen_texture = SDL_CreateTextureFromSurface(renderer, loading_screen);

    // surface can now be freed
    SDL_FreeSurface(loading_screen);

    int w = 200;
    int h = 200;
    SDL_Rect dstrect;

    // SDL_QueryTexture( loading_screen_texture, NULL, NULL, &w, &h );
    dstrect.x = output_width / 2 - w / 2;
    dstrect.y = output_height / 2 - h / 2;
    dstrect.w = w;
    dstrect.h = h;

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, loading_screen_texture, NULL, &dstrect);
    SDL_RenderPresent(renderer);

    // texture may now be destroyed
    SDL_DestroyTexture(loading_screen_texture);

    int remaining_ms = 30 - (int)get_timer(c);
    if (remaining_ms > 0) {
        SDL_Delay(remaining_ms);
    }
    gif_frame_index += 1;
    gif_frame_index %= 83;  // number of loading frames
}

void nack(int id, int index) {
    /*
        Send a negative acknowledgement to the server if a video
        packet is missing

        Arguments:
            id (int): missing packet ID
            index (int): missing packet index
    */

    if (video_data.is_waiting_for_iframe) {
        return;
    }
    video_data.num_nacked++;
    LOG_INFO("Missing Video Packet ID %d Index %d, NACKing...", id, index);
    FractalClientMessage fmsg;
    fmsg.type = MESSAGE_VIDEO_NACK;
    fmsg.nack_data.id = id;
    fmsg.nack_data.index = index;
    send_fmsg(&fmsg);
}

bool request_iframe() {
    /*
        Request an IFrame from the server if too long since last frame

        Return:
            (bool): true if IFrame requested, false if not
    */

    if (get_timer(video_data.last_iframe_request_timer) > 1500.0 / 1000.0) {
        FractalClientMessage fmsg;
        fmsg.type = MESSAGE_IFRAME_REQUEST;
        if (video_data.last_rendered_id == 0) {
            fmsg.reinitialize_encoder = true;
        } else {
            fmsg.reinitialize_encoder = false;
        }
        send_fmsg(&fmsg);
        start_timer(&video_data.last_iframe_request_timer);
        video_data.is_waiting_for_iframe = true;
        return true;
    } else {
        return false;
    }
}

void update_sws_context() {
    /*
        Update the SWS context for the decoded video
    */

    LOG_INFO("Updating SWS Context");
    VideoDecoder* decoder = video_context.decoder;

    sws_input_fmt = decoder->sw_frame->format;

    LOG_INFO("Decoder Format: %s", av_get_pix_fmt_name(sws_input_fmt));

    if (video_context.sws) {
        av_freep(&video_context.data[0]);
        sws_freeContext(video_context.sws);
    }

    video_context.sws = NULL;

    memset(video_context.data, 0, sizeof(video_context.data));

    if (sws_input_fmt != AV_PIX_FMT_YUV420P || decoder->width != output_width ||
        decoder->height != output_height) {
        av_image_alloc(video_context.data, video_context.linesize, output_width, output_height,
                       AV_PIX_FMT_YUV420P, 32);

        LOG_INFO("Will be resizing from %dx%d to %dx%d", decoder->width, decoder->height,
                 output_width, output_height);
        video_context.sws =
            sws_getContext(decoder->width, decoder->height, sws_input_fmt, output_width,
                           output_height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
    }
}

void update_pixel_format() {
    /*
        Update the pixel format for the SWS context
    */

    if (sws_input_fmt != video_context.decoder->sw_frame->format || pending_sws_update) {
        sws_input_fmt = video_context.decoder->sw_frame->format;
        pending_sws_update = false;
        update_sws_context();
    }
}

void update_texture() {
    /*
        Update the SDL video texture
    */

    if (pending_texture_update) {
        LOG_INFO("Beginning to use %d x %d", output_width, output_height);
        SDL_Texture* texture =
            SDL_CreateTexture((SDL_Renderer*)video_context.renderer, SDL_PIXELFORMAT_YV12,
                              SDL_TEXTUREACCESS_STREAMING, output_width, output_height);
        if (!texture) {
            LOG_ERROR("SDL: could not create texture - exiting");
            exit(1);
        }

        SDL_DestroyTexture(video_context.texture);
        pending_resize_render = false;
        video_context.texture = texture;
        pending_texture_update = false;
    }
}

static int render_peers(SDL_Renderer* renderer, PeerUpdateMessage* msgs, size_t num_msgs) {
    /*
        Render peer cursors for multiclient

        Arguments:
            renderer (SDL_Renderer*): the video renderer
            msgs (PeerUpdateMessage*): array of peer update message packets
            num_msgs (size_t): how many peer update messages there are in `msgs`

        Return:
            (int): 0 on success, -1 on failure
    */

    int ret = 0;

    int window_width, window_height;
    SDL_GetWindowSize((SDL_Window*)window, &window_width, &window_height);
    int x = msgs->x * window_width / (int32_t)MOUSE_SCALING_FACTOR;
    int y = msgs->y * window_height / (int32_t)MOUSE_SCALING_FACTOR;

    for (; num_msgs > 0; msgs++, num_msgs--) {
        if (client_id == msgs->peer_id) {
            continue;
        }
        if (draw_peer_cursor(renderer, x, y, msgs->color.r, msgs->color.g, msgs->color.b) != 0) {
            LOG_ERROR("Failed to draw spectator cursor.");
            ret = -1;
        }
    }
    return ret;
}

void clear_sdl(SDL_Renderer* renderer) {
    /*
        Clear the SDL renderer

        Arguments:
            renderer (SDL_Renderer*): the video renderer
    */

    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);
}

int init_multithreaded_video(void* opaque) {
    /*
        Initialized the video rendering thread. Used as a thread function.

        Arguments:
            opaque (void*): thread argument

        Return:
            (int): 0 on success, -1 on failure
    */

    UNUSED(opaque);

    if (init_peer_cursors() != 0) {
        LOG_ERROR("Failed to init peer cursors.");
    }

    can_render = true;
    memset(video_context.data, 0, sizeof(video_context.data));

    render_mutex = SDL_CreateMutex();

    LOG_INFO("Creating renderer for %dx%d display", output_width, output_height);

    SDL_SetThreadPriority(SDL_THREAD_PRIORITY_HIGH);

    // configure renderer
    if (SDL_GetWindowFlags((SDL_Window*)window) & SDL_WINDOW_OPENGL) {
        // only opengl if windowed mode
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

    SDL_Renderer* renderer = SDL_CreateRenderer(
        (SDL_Window*)window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);

    // Show a black screen initially before anything else
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderPresent(renderer);

    video_context.renderer = renderer;
    if (!renderer) {
        LOG_WARNING("SDL: could not create renderer - exiting: %s", SDL_GetError());
        return -1;
    }

    // mbps that currently works
    working_mbps = STARTING_BITRATE;
    video_data.is_waiting_for_iframe = false;

    // True if RenderScreen is currently rendering a frame
    rendering = false;
    has_rendered_yet = false;

    SDL_Texture* texture;

    SDL_SetRenderDrawBlendMode((SDL_Renderer*)renderer, SDL_BLENDMODE_BLEND);
    // Allocate a place to put our YUV image on that screen
    texture = SDL_CreateTexture((SDL_Renderer*)renderer, SDL_PIXELFORMAT_YV12,
                                SDL_TEXTUREACCESS_STREAMING, output_width, output_height);
    if (!texture) {
        LOG_ERROR("SDL: could not create texture - exiting");
        destroy_logger();
        exit(1);
    }

    pending_sws_update = false;
    sws_input_fmt = AV_PIX_FMT_NONE;
    video_context.texture = texture;
    video_context.sws = NULL;

    max_bitrate = STARTING_BITRATE;
    video_data.target_mbps = STARTING_BITRATE;
    video_data.pending_ctx = NULL;
    video_data.frames_received = 0;
    video_data.bytes_transferred = 0;
    start_timer(&video_data.frame_timer);
    video_data.last_statistics_id = 1;
    video_data.last_rendered_id = 0;
    video_data.max_id = 0;
    video_data.most_recent_iframe = -1;
    video_data.num_nacked = 0;
    video_data.bucket = STARTING_BITRATE / BITRATE_BUCKET_SIZE;
    start_timer(&video_data.last_iframe_request_timer);

    for (int i = 0; i < RECV_FRAMES_BUFFER_SIZE; i++) {
        receiving_frames[i].id = -1;
    }

    video_data.renderscreen_semaphore = SDL_CreateSemaphore(0);
    video_data.run_render_screen_thread = true;

    render_screen(renderer);
    SDL_DestroyRenderer(renderer);
    return 0;
}
// END VIDEO FUNCTIONS

/*
============================
Public Function Implementations
============================
*/

void init_video() {
    /*
        Create the SDL video thread
    */

    video_data.render_screen_thread =
        SDL_CreateThread(init_multithreaded_video, "VideoThread", NULL);
}

int last_rendered_index = 0;

void update_video() {
    /*
        Calculate statistics about bitrate, I-Frame, etc. and request video
        update from the server
    */

    // Get statistics from the last 3 seconds of data
    if (get_timer(video_data.frame_timer) > 3) {
        double time = get_timer(video_data.frame_timer);

        // Calculate statistics
        /*
        int expected_frames = VideoData.max_id - VideoData.last_statistics_id;
        // double fps = 1.0 * expected_frames / time; // TODO: finish birate
        // throttling alg double mbps = VideoData.bytes_transferred * 8.0 /
        // 1024.0 / 1024.0 / time; // TODO bitrate throttle
        double receive_rate =
            expected_frames == 0
                ? 1.0
                : 1.0 * VideoData.frames_received / expected_frames;
        double dropped_rate = 1.0 - receive_rate;
        */

        double nack_per_second = video_data.num_nacked / time;
        video_data.nack_by_bitrate[video_data.bucket] += video_data.num_nacked;
        video_data.seconds_by_bitrate[video_data.bucket] += time;

        LOG_INFO("====\nBucket: %d\nSeconds: %f\nNacks/Second: %f\n====",
                 video_data.bucket * BITRATE_BUCKET_SIZE, time, nack_per_second);

        // Print statistics

        // mprintf("FPS: %f\nmbps: %f\ndropped: %f%%\n\n", fps, mbps, 100.0 *
        // dropped_rate);

        LOG_INFO("MBPS: %f %f", video_data.target_mbps, nack_per_second);

        // Adjust mbps based on dropped packets
        if (nack_per_second > 50) {
            video_data.target_mbps = video_data.target_mbps * 0.75;
            working_mbps = video_data.target_mbps;
            update_mbps = true;
        } else if (nack_per_second > 25) {
            video_data.target_mbps = video_data.target_mbps * 0.83;
            working_mbps = video_data.target_mbps;
            update_mbps = true;
        } else if (nack_per_second > 15) {
            video_data.target_mbps = video_data.target_mbps * 0.9;
            working_mbps = video_data.target_mbps;
            update_mbps = true;
        } else if (nack_per_second > 10) {
            video_data.target_mbps = video_data.target_mbps * 0.95;
            working_mbps = video_data.target_mbps;
            update_mbps = true;
        } else if (nack_per_second > 6) {
            video_data.target_mbps = video_data.target_mbps * 0.98;
            working_mbps = video_data.target_mbps;
            update_mbps = true;
        } else {
            working_mbps = max(video_data.target_mbps * 1.05, working_mbps);
            video_data.target_mbps = (video_data.target_mbps + working_mbps) / 2.0;
            video_data.target_mbps = min(video_data.target_mbps, MAXIMUM_BITRATE);
            update_mbps = true;
        }

        LOG_INFO("MBPS2: %f", video_data.target_mbps);

        video_data.bucket = (int)video_data.target_mbps / BITRATE_BUCKET_SIZE;
        max_bitrate = (int)video_data.bucket * BITRATE_BUCKET_SIZE + BITRATE_BUCKET_SIZE / 2;

        LOG_INFO("MBPS3: %d", max_bitrate);
        video_data.num_nacked = 0;

        video_data.bytes_transferred = 0;
        video_data.frames_received = 0;
        video_data.last_statistics_id = video_data.max_id;
        start_timer(&video_data.frame_timer);
    }

    if (video_data.last_rendered_id == -1 && video_data.most_recent_iframe > 0) {
        video_data.last_rendered_id = video_data.most_recent_iframe - 1;
    }

    if (!rendering && video_data.last_rendered_id >= 0) {
        if (video_data.most_recent_iframe - 1 > video_data.last_rendered_id) {
            LOG_INFO("Skipping from %d to i-frame %d!", video_data.last_rendered_id,
                     video_data.most_recent_iframe);
            // If `last_rendered_id` is further back than the first frame received, start from the
            // first frame received
            for (int i = max(video_data.last_rendered_id + 1,
                             video_data.most_recent_iframe - video_data.frames_received + 1);
                 i < video_data.most_recent_iframe; i++) {
                int index = i % RECV_FRAMES_BUFFER_SIZE;
                if (receiving_frames[index].id == i) {
                    LOG_WARNING("Frame dropped with ID %d: %d/%d", i,
                                receiving_frames[index].packets_received,
                                receiving_frames[index].num_packets);

                    for (int j = 0; j < receiving_frames[index].num_packets; j++) {
                        if (!receiving_frames[index].received_indicies[j]) {
                            LOG_WARNING("Did not receive ID %d, Index %d", i, j);
                        }
                    }
                } else {
                    LOG_WARNING("Bad ID? %d instead of %d", receiving_frames[index].id, i);
                }
            }
            video_data.last_rendered_id = video_data.most_recent_iframe - 1;
        }

        int next_render_id = video_data.last_rendered_id + 1;

        int index = next_render_id % RECV_FRAMES_BUFFER_SIZE;

        FrameData* ctx = &receiving_frames[index];

        if (ctx->id == next_render_id) {
            if (ctx->packets_received == ctx->num_packets) {
                // mprintf( "Packets: %d %s\n", ctx->num_packets,
                // ((Frame*)ctx->frame_buffer)->is_iframe ? "(I-frame)" : "" );
                // mprintf("Rendering %d (Age %f)\n", ctx->id,
                // get_timer(ctx->frame_creation_timer));

                render_context = *ctx;
                rendering = true;

                skip_render = false;

                int after_render_id = next_render_id + 1;
                int after_index = after_render_id % RECV_FRAMES_BUFFER_SIZE;
                FrameData* after_ctx = &receiving_frames[after_index];

                if (after_ctx->id == after_render_id &&
                    after_ctx->packets_received == after_ctx->num_packets) {
                    skip_render = true;
                    LOG_INFO("Skip this render");
                }
                SDL_SemPost(video_data.renderscreen_semaphore);
            } else {
                if ((get_timer(ctx->last_packet_timer) > 6.0 / 1000.0) &&
                    get_timer(ctx->last_nacked_timer) >
                        (8.0 + 8.0 * ctx->num_times_nacked) / 1000.0) {
                    if (ctx->num_times_nacked == -1) {
                        ctx->num_times_nacked = 0;
                        ctx->last_nacked_index = -1;
                    }
                    int num_nacked = 0;
                    // mprintf("************NACKING PACKET %d, alive for %f
                    // MS\n", ctx->id, get_timer(ctx->frame_creation_timer));
                    for (int i = ctx->last_nacked_index + 1; i < ctx->num_packets && num_nacked < 1;
                         i++) {
                        if (!ctx->received_indicies[i]) {
                            num_nacked++;
                            LOG_INFO(
                                "************NACKING VIDEO PACKET %d %d (/%d), "
                                "alive for %f MS",
                                ctx->id, i, ctx->num_packets, get_timer(ctx->frame_creation_timer));
                            ctx->nacked_indicies[i] = true;
                            nack(ctx->id, i);
                        }
                        ctx->last_nacked_index = i;
                    }
                    if (ctx->last_nacked_index == ctx->num_packets - 1) {
                        ctx->last_nacked_index = -1;
                        ctx->num_times_nacked++;
                    }
                    start_timer(&ctx->last_nacked_timer);
                }
            }
        }

        if (!rendering) {
            // FrameData* cur_ctx =
            // &receiving_frames[VideoData.last_rendered_id %
            // RECV_FRAMES_BUFFER_SIZE];

            if (video_data.max_id >
                video_data.last_rendered_id + 3)  // || (cur_ctx->id == VideoData.last_rendered_id
                                                  // && get_timer( cur_ctx->last_packet_timer )
                                                  // > 96.0 / 1000.0) )
            {
                if (request_iframe()) {
                    LOG_INFO("TOO FAR BEHIND! REQUEST FOR IFRAME!");
                }
            }
        }

        if (video_data.max_id > video_data.last_rendered_id + 5) {
            if (request_iframe()) {
                LOG_INFO("WAYY TOO FAR BEHIND! REQUEST FOR IFRAME!");
            }
        }
    }
}

int32_t receive_video(FractalPacket* packet) {
    /*
        Receive video packet

        Arguments:
            packet (FractalPacket*): Packet received from the server, which gets
                sorted as video packet with proper parameters

        Returns:
            (int32_t): -1 if failed to receive packet into video frame, else 0
    */

    // mprintf("Video Packet ID %d, Index %d (Packets: %d) (Size: %d)\n",
    // packet->id, packet->index, packet->num_indices, packet->payload_size);

    // Find frame in linked list that matches the id
    video_data.bytes_transferred += packet->payload_size;

    int index = packet->id % RECV_FRAMES_BUFFER_SIZE;

    FrameData* ctx = &receiving_frames[index];

    // Check if we have to initialize the frame buffer
    if (packet->id < ctx->id) {
        LOG_INFO("Old packet received! %d is less than the previous %d", packet->id, ctx->id);
        return -1;
    } else if (packet->id > ctx->id) {
        if (rendering && render_context.id == ctx->id) {
            LOG_INFO(
                "Error! Currently rendering an ID that will be overwritten! "
                "Skipping packet.");
            return 0;
        }
        ctx->id = packet->id;
        ctx->frame_buffer = (char*)&frame_bufs[index];
        ctx->packets_received = 0;
        ctx->num_packets = packet->num_indices;
        ctx->last_nacked_index = -1;
        ctx->num_times_nacked = -1;
        ctx->rendered = false;
        ctx->frame_size = 0;
        memset(ctx->received_indicies, 0, sizeof(ctx->received_indicies));
        memset(ctx->nacked_indicies, 0, sizeof(ctx->nacked_indicies));
        start_timer(&ctx->last_nacked_timer);
        start_timer(&ctx->frame_creation_timer);
        // mprintf("Initialized packet %d!\n", ctx->id);
    } else {
        // mprintf("Already Started: %d/%d - %f\n", ctx->packets_received + 1,
        // ctx->num_packets, get_timer(ctx->client_frame_timer));
    }

    start_timer(&ctx->last_packet_timer);

    // If we already received this packet, we can skip
    if (packet->is_a_nack) {
        if (!ctx->received_indicies[packet->index]) {
            LOG_INFO("NACK for Video ID %d, Index %d Received!", packet->id, packet->index);
        } else {
            LOG_INFO(
                "NACK for Video ID %d, Index %d Received! But didn't need "
                "it.",
                packet->id, packet->index);
        }
    } else if (ctx->nacked_indicies[packet->index]) {
        LOG_INFO(
            "Received the original Video ID %d Index %d, but we had NACK'ed "
            "for it.",
            packet->id, packet->index);
    }

    // Already received
    if (ctx->received_indicies[packet->index]) {
#if LOG_VIDEO
        mprintf(
            "Skipping duplicate Video ID %d Index %d at time since creation %f "
            "%s\n",
            packet->id, packet->index, get_timer(ctx->frame_creation_timer),
            packet->is_a_nack ? "(nack)" : "");
#endif
        return 0;
    }

#if LOG_VIDEO
    // mprintf("Received Video ID %d Index %d at time since creation %f %s\n",
    // packet->id, packet->index, get_timer(ctx->frame_creation_timer),
    // packet->is_a_nack ? "(nack)" : "");
#endif

    video_data.max_id = max(video_data.max_id, ctx->id);

    ctx->received_indicies[packet->index] = true;
    if (packet->index > 0 && get_timer(ctx->last_nacked_timer) > 6.0 / 1000) {
        int to_index = packet->index - 5;
        for (int i = max(0, ctx->last_nacked_index + 1); i <= to_index; i++) {
            // Nacking index i
            ctx->last_nacked_index = max(ctx->last_nacked_index, i);
            if (!ctx->received_indicies[i]) {
                ctx->nacked_indicies[i] = true;
                nack(packet->id, i);
                start_timer(&ctx->last_nacked_timer);
                break;
            }
        }
    }
    ctx->packets_received++;

    // Copy packet data
    int place = packet->index * MAX_PAYLOAD_SIZE;
    if (place + packet->payload_size >= LARGEST_FRAME_SIZE) {
        LOG_WARNING("Packet total payload is too large for buffer!");
        return -1;
    }
    memcpy(ctx->frame_buffer + place, packet->data, packet->payload_size);
    ctx->frame_size += packet->payload_size;

    // If we received all of the packets
    if (ctx->packets_received == ctx->num_packets) {
        bool is_iframe = ((Frame*)ctx->frame_buffer)->is_iframe;

        video_data.frames_received++;

#if LOG_VIDEO
        mprintf("Received Video Frame ID %d (Packets: %d) (Size: %d) %s\n", ctx->id,
                ctx->num_packets, ctx->frame_size, is_iframe ? "(i-frame)" : "");
#endif

        // If it's an I-frame, then just skip right to it, if the id is ahead of
        // the next to render id
        if (is_iframe) {
            video_data.most_recent_iframe = max(video_data.most_recent_iframe, ctx->id);
        }
    }

    return 0;
}

void destroy_video() {
    /*
        Free the video thread and VideoContext data to exit
    */

    video_data.run_render_screen_thread = false;
    SDL_WaitThread(video_data.render_screen_thread, NULL);
    SDL_DestroySemaphore(video_data.renderscreen_semaphore);
    SDL_DestroyMutex(render_mutex);

    //    SDL_DestroyTexture(videoContext.texture); not needed, the renderer
    //    destroys it
    av_freep(&video_context.data[0]);

    has_rendered_yet = false;

    if (destroy_peer_cursors() != 0) {
        LOG_ERROR("Failed to destroy peer cursors.");
    }
}

void set_video_active_resizing(bool is_resizing) {
    /*
        Set the global variable 'resizing' to true if the SDL window is
        being resized, else false

        Arguments:
            is_resizing (bool): Boolean indicating whether or not the SDL
                window is being resized
    */

    if (!is_resizing) {
        SDL_LockMutex(render_mutex);

        int new_width = get_window_pixel_width((SDL_Window*)window);
        int new_height = get_window_pixel_height((SDL_Window*)window);
        if (new_width != output_width || new_height != output_height) {
            pending_texture_update = true;
            pending_sws_update = true;
            output_width = new_width;
            output_height = new_height;
        }
        can_render = true;
        SDL_UnlockMutex(render_mutex);
    } else {
        SDL_LockMutex(render_mutex);
        can_render = false;
        pending_resize_render = true;
        SDL_UnlockMutex(render_mutex);

        for (int i = 0; pending_resize_render && (i < 10); ++i) {
            SDL_Delay(1);
        }

        if (pending_resize_render) {
            SDL_LockMutex(render_mutex);
            pending_resize_render = false;
            SDL_UnlockMutex(render_mutex);
        }
    }
}
