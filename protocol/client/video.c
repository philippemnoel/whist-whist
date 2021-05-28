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

#include "peercursor.h"
#include "sdlscreeninfo.h"
#include <SDL2/SDL.h>
#include <fractal/utils/color.h>
#include <fractal/utils/png.h>
#include "network.h"

#define USE_HARDWARE true
#define NO_NACKS_DURING_IFRAME false

#define MAX_SCREEN_WIDTH 8192
#define MAX_SCREEN_HEIGHT 4096

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
extern volatile double latency;

#if CAN_UPDATE_WINDOW_TITLEBAR_COLOR
extern volatile FractalRGBColor* native_window_color;
extern volatile bool native_window_color_update;
#endif  // CAN_UPDATE_WINDOW_TITLEBAR_COLOR

// START VIDEO VARIABLES
volatile FractalCursorState cursor_state = CURSOR_STATE_VISIBLE;
volatile SDL_Cursor* sdl_cursor = NULL;
volatile FractalCursorID last_cursor = (FractalCursorID)SDL_SYSTEM_CURSOR_ARROW;
volatile bool pending_sws_update = false;
volatile bool pending_texture_update = false;
volatile bool pending_resize_render = false;
volatile bool initialized_video_renderer = false;

static enum AVPixelFormat sws_input_fmt;

#ifdef __APPLE__
// on macOS, we must initialize the renderer in `init_sdl()` instead of video.c
extern volatile SDL_Renderer* init_sdl_renderer;
#endif

// number of frames ahead we can receive packets for before asking for iframe
#define MAX_UNSYNCED_FRAMES 10
#define MAX_UNSYNCED_FRAMES_RENDER 12  // not sure if i need this
// number of packets we are allowed to miss before asking for iframe
#define MAX_MISSING_PACKETS 20

#define LOG_VIDEO false

#define BITRATE_BUCKET_SIZE 500000
#define NUMBER_LOADING_FRAMES 50

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

    double target_mbps;
    int num_nacked;
    clock missing_frame_nack_timer;
    int bucket;  // = STARTING_BITRATE / BITRATE_BUCKET_SIZE;
    int nack_by_bitrate[MAXIMUM_BITRATE / BITRATE_BUCKET_SIZE + 5];
    double seconds_by_bitrate[MAXIMUM_BITRATE / BITRATE_BUCKET_SIZE + 5];

    // Loading animation data
    int loading_index;
    clock last_loading_frame_timer;
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
static volatile FrameData render_context;

// True if RenderScreen is currently rendering a frame
static volatile bool rendering = false;
volatile bool skip_render = false;
volatile bool can_render;

SDL_mutex* render_mutex;

// Hold information about frames as the packets come in
#define RECV_FRAMES_BUFFER_SIZE 275
FrameData receiving_frames[RECV_FRAMES_BUFFER_SIZE];
BlockAllocator* frame_buffer_allocator;

bool has_video_rendered_yet = false;

// END VIDEO VARIABLES

// START VIDEO FUNCTIONS

/*
============================
Private Functions
============================
*/

int32_t mulithreaded_destroy_decoder(void* opaque);
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

/*
============================
Private Function Implementations
============================
*/

int32_t multithreaded_destroy_decoder(void* opaque) {
    VideoDecoder* decoder = (VideoDecoder*)opaque;
    destroy_video_decoder(decoder);
    return 0;
}

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
        SDL_CreateThread(multithreaded_destroy_decoder, "multithreaded_destroy_decoder",
                         video_context.decoder);
    }

    VideoDecoder* decoder = create_video_decoder(width, height, USE_HARDWARE, codec_type);
    if (!decoder) {
        LOG_FATAL("ERROR: Decoder could not be created!");
    }
    video_context.decoder = decoder;

    sws_input_fmt = AV_PIX_FMT_NONE;

    server_width = width;
    server_height = height;
    server_codec_type = codec_type;
    output_codec_type = codec_type;
}

int render_video() {
    /*
        Render the video screen that the user sees

        Arguments:
            renderer (SDL_Renderer*): SDL renderer used to generate video

        Return:
            (int): 0 on success, -1 on failure
    */

    if (!initialized_video_renderer) {
        LOG_ERROR(
            "Called render_video, but init_video_renderer not called yet! (Or has since been "
            "destroyed");
        return -1;
    }

    SDL_Renderer* renderer = video_context.renderer;

    if (rendering) {
        // Stop loading animation once rendering occurs
        video_data.loading_index = -1;

        safe_SDL_LockMutex(render_mutex);
        if (pending_resize_render) {
            // User is in the middle of resizing the window
            SDL_Rect output_rect;
            output_rect.x = 0;
            output_rect.y = 0;
            output_rect.w = server_width;
            output_rect.h = server_height;
            SDL_RenderCopy(video_context.renderer, video_context.texture, &output_rect, NULL);
            SDL_RenderPresent(video_context.renderer);
        }
        safe_SDL_UnlockMutex(render_mutex);

        // Cast to Frame* because this variable is not volatile in this section
        Frame* frame = (Frame*)render_context.frame_buffer;
        PeerUpdateMessage* peer_update_msgs = get_frame_peer_messages(frame);
        size_t num_peer_update_msgs = frame->num_peer_update_msgs;

        if (get_timer(render_context.frame_creation_timer) > 25.0 / 1000.0) {
            LOG_INFO("Late! Rendering ID %d (Age %f) (Packets %d) %s", render_context.id,
                     get_timer(render_context.frame_creation_timer), render_context.num_packets,
                     frame->is_iframe ? "(I-Frame)" : "");
        }
#if LOG_VIDEO
        else {
            LOG_INFO("Rendering ID %d (Age %f) (Packets %d) %s", render_context.id,
                     get_timer(render_context.frame_creation_timer), render_context.num_packets,
                     frame->is_iframe ? "(I-Frame)" : "");
        }
#endif

        if ((int)(get_total_frame_size(frame)) != render_context.frame_size) {
            LOG_INFO("Incorrect Frame Size! %d instead of %d", get_total_frame_size(frame),
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

        if (!video_decoder_decode(video_context.decoder, get_frame_videodata(frame),
                                  frame->videodata_length)) {
            LOG_WARNING("Failed to video_decoder_decode!");
            // Since we're done, we free the frame buffer
            free_block(frame_buffer_allocator, render_context.frame_buffer);
            // rendering = false is set to false last,
            // since that can trigger the next frame render
            rendering = false;
            return -1;
        }

        // LOG_INFO( "Decode Time: %f", get_timer( decode_timer ) );

        safe_SDL_LockMutex(render_mutex);
        update_pixel_format();

        bool render_this_frame = can_render && !skip_render;

        if (render_this_frame) {
            clock sws_timer;
            start_timer(&sws_timer);

            update_texture();
            pending_resize_render = false;

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

#if CAN_UPDATE_WINDOW_TITLEBAR_COLOR
            FractalYUVColor new_yuv_color = {video_context.data[0][0], video_context.data[1][0],
                                             video_context.data[2][0]};

            FractalRGBColor new_rgb_color = yuv_to_rgb(new_yuv_color);

            if ((FractalRGBColor*)native_window_color == NULL) {
                // no window color has been set; create it!
                FractalRGBColor* new_native_window_color = safe_malloc(sizeof(FractalRGBColor));
                *new_native_window_color = new_rgb_color;
                native_window_color = new_native_window_color;
                native_window_color_update = true;
            } else if (rgb_compare(new_rgb_color, *(FractalRGBColor*)native_window_color)) {
                // window color has changed; update it!
                FractalRGBColor* old_native_window_color = (FractalRGBColor*)native_window_color;
                FractalRGBColor* new_native_window_color = safe_malloc(sizeof(FractalRGBColor));
                *new_native_window_color = new_rgb_color;
                native_window_color = new_native_window_color;
                free(old_native_window_color);
                native_window_color_update = true;
            }
#endif  // CAN_UPDATE_WINDOW_TITLEBAR_COLOR

            // The texture object we allocate is larger than the frame (unless
            // MAX_SCREEN_WIDTH/HEIGHT) are violated, so we only copy the valid section of the frame
            // into the texture.
            SDL_Rect texture_rect;
            texture_rect.x = 0;
            texture_rect.y = 0;
            texture_rect.w = video_context.decoder->width;
            texture_rect.h = video_context.decoder->height;
            int ret = SDL_UpdateYUVTexture(video_context.texture, &texture_rect,
                                           video_context.data[0], video_context.linesize[0],
                                           video_context.data[1], video_context.linesize[1],
                                           video_context.data[2], video_context.linesize[2]);
            if (ret == -1) {
                LOG_ERROR("SDL_UpdateYUVTexture failed: %s", SDL_GetError());
            }

            if (!video_context.sws) {
                // Clear out bits that aren't used from av_alloc_frame
                memset(video_context.data, 0, sizeof(video_context.data));
            }
        }

        // Set cursor to frame's desired cursor type
        FractalCursorImage* cursor = get_frame_cursor_image(frame);
        // Only update the cursor, if a cursor image is even embedded in the frame at all.
        if (cursor) {
            if ((FractalCursorID)cursor->cursor_id != last_cursor || cursor->using_bmp) {
                if (sdl_cursor) {
                    SDL_FreeCursor((SDL_Cursor*)sdl_cursor);
                }
                if (cursor->using_bmp) {
                    // use bitmap data to set cursor

                    SDL_Surface* cursor_surface = SDL_CreateRGBSurfaceFrom(
                        cursor->bmp, cursor->bmp_width, cursor->bmp_height, sizeof(uint32_t) * 8,
                        sizeof(uint32_t) * cursor->bmp_width, CURSORIMAGE_R, CURSORIMAGE_G,
                        CURSORIMAGE_B, CURSORIMAGE_A);
                    // potentially SDL_SetSurfaceBlendMode since X11 cursor BMPs are
                    // pre-alpha multplied
                    sdl_cursor =
                        SDL_CreateColorCursor(cursor_surface, cursor->bmp_hot_x, cursor->bmp_hot_y);
                    SDL_FreeSurface(cursor_surface);
                } else {
                    // use cursor id to set cursor
                    sdl_cursor = SDL_CreateSystemCursor((SDL_SystemCursor)cursor->cursor_id);
                }
                SDL_SetCursor((SDL_Cursor*)sdl_cursor);

                last_cursor = (FractalCursorID)cursor->cursor_id;
            }

            if (cursor->cursor_state != cursor_state) {
                if (cursor->cursor_state == CURSOR_STATE_HIDDEN) {
                    SDL_SetRelativeMouseMode(SDL_TRUE);
                } else {
                    SDL_SetRelativeMouseMode(SDL_FALSE);
                }

                cursor_state = cursor->cursor_state;
            }
        }

        // LOG_INFO("Client Frame Time for ID %d: %f", renderContext.id,
        // get_timer(renderContext.client_frame_timer));

        if (render_this_frame) {
            // Subsection of texture that should be rendered to screen.
            SDL_Rect output_rect;
            output_rect.x = 0;
            output_rect.y = 0;
            if (output_width <= server_width && server_width <= output_width + 8 &&
                output_height <= server_height && server_height <= output_height + 2) {
                // Since RenderCopy scales the texture to the size of the window by default, we use
                // this to truncate the frame to the size of the window to avoid scaling
                // artifacts (blurriness). The frame may be larger than the window because the
                // video encoder rounds the width up to a multiple of 8, and the height to a
                // multiple of 2.
                output_rect.w = output_width;
                output_rect.h = output_height;
                if (server_width > output_width || server_height > output_height) {
                    // We failed to force the window dimensions to be multiples of 8, 2 in
                    // `handle_window_size_changed`
                    static bool already_sent_message = false;
                    static long long last_server_dims = -1;
                    static long long last_output_dims = -1;
                    if (server_width * 100000LL + server_height != last_server_dims ||
                        output_width * 100000LL + output_height != last_output_dims) {
                        // If truncation to/from dimensions have changed, then we should resend
                        // Truncating message
                        already_sent_message = false;
                    }
                    last_server_dims = server_width * 100000LL + server_height;
                    last_output_dims = output_width * 100000LL + output_height;
                    if (!already_sent_message) {
                        LOG_WARNING("Truncating window from %dx%d to %dx%d", server_width,
                                    server_height, output_width, output_height);
                        already_sent_message = true;
                    }
                }
            } else {
                // If the condition is false, most likely that means the server has not yet updated
                // to use the new dimensions, so we render the entire frame. This makes resizing
                // look more consistent.
                output_rect.w = server_width;
                output_rect.h = server_height;
            }
            SDL_RenderCopy(renderer, video_context.texture, &output_rect, NULL);

            if (render_peers(renderer, peer_update_msgs, num_peer_update_msgs) != 0) {
                LOG_ERROR("Failed to render peers.");
            }
            // this call takes up to 16 ms: takes 8 ms on average.
            SDL_RenderPresent(renderer);
        }

        safe_SDL_UnlockMutex(render_mutex);

#if LOG_VIDEO
        LOG_DEBUG("Rendered %d (Size: %d) (Age %f)", render_context.id, render_context.frame_size,
                  get_timer(render_context.frame_creation_timer));
#endif

        if (frame->is_iframe) {
            video_data.is_waiting_for_iframe = false;
        }

        video_data.last_rendered_id = render_context.id;
        // Since we're done, we free the frame buffer
        free_block(frame_buffer_allocator, render_context.frame_buffer);
        has_video_rendered_yet = true;
        // rendering = false is set to false last,
        // since that can trigger the next frame render
        rendering = false;
    } else {
        // If rendering == false,
        // Then we potentially render the loading screen as long as loading_index is valid
        if (video_data.loading_index >= 0) {
            const float loading_animation_fps = 20.0;
            if (get_timer(video_data.last_loading_frame_timer) > 1 / loading_animation_fps) {
                // Present the loading screen
                loading_sdl(renderer, video_data.loading_index);
                // Progress animation
                video_data.loading_index++;
                // Reset timer
                start_timer(&video_data.last_loading_frame_timer);
            }
        }
    }

    return 0;
}

void loading_sdl(SDL_Renderer* renderer, int loading_index) {
    /*
        Make the screen black and show the loading screen
        Arguments:
            renderer (SDL_Renderer*): video renderer
            loading_index (int): the index of the loading frame
    */

    int gif_frame_index = loading_index % NUMBER_LOADING_FRAMES;

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

    SDL_Surface* loading_screen = sdl_surface_from_png_file(frame_name);
    if (loading_screen == NULL) {
        LOG_WARNING("Loading screen not loaded from %s", frame_name);
        return;
    }

    // free pkt.data which is initialized by calloc in png_file_to_bmp

    SDL_Texture* loading_screen_texture = SDL_CreateTextureFromSurface(renderer, loading_screen);

    // surface can now be freed
    free_sdl_rgb_surface(loading_screen);

    int w = 200;
    int h = 200;
    SDL_Rect dstrect;

    // SDL_QueryTexture( loading_screen_texture, NULL, NULL, &w, &h );
    dstrect.x = output_width / 2 - w / 2;
    dstrect.y = output_height / 2 - h / 2;
    dstrect.w = w;
    dstrect.h = h;

    SDL_SetRenderDrawColor(renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, loading_screen_texture, NULL, &dstrect);
    SDL_RenderPresent(renderer);

    // texture may now be destroyed
    SDL_DestroyTexture(loading_screen_texture);

    gif_frame_index += 1;
    gif_frame_index %= NUMBER_LOADING_FRAMES;
}

void nack(int id, int index) {
    /*
        Send a negative acknowledgement to the server if a video
        packet is missing

        Arguments:
            id (int): missing packet ID
            index (int): missing packet index
    */
    // If we can get the server to generate iframes quickly (i.e. about 60 ms), flip
    // NO_NACKS_DURING_IFRAME to true
#if NO_NACKS_DURING_IFRAME
    if (video_data.is_waiting_for_iframe) {
        return;
    }
#endif
    video_data.num_nacked++;
    LOG_INFO("Missing Video Packet ID %d Index %d, NACKing...", id, index);
    FractalClientMessage fmsg = {0};
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
        FractalClientMessage fmsg = {0};
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

    // Rather than scaling the video frame data to the size of the window, we keep its original
    // dimensions so we can truncate it later.
    av_image_alloc(video_context.data, video_context.linesize, decoder->width, decoder->height,
                   AV_PIX_FMT_YUV420P, 32);
    LOG_INFO("Will be converting pixel format from %s to %s", av_get_pix_fmt_name(sws_input_fmt),
             av_get_pix_fmt_name(AV_PIX_FMT_YUV420P));
    video_context.sws =
        sws_getContext(decoder->width, decoder->height, sws_input_fmt, decoder->width,
                       decoder->height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, NULL, NULL, NULL);
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
        // Destroy the old texture
        if (video_context.texture) {
            SDL_DestroyTexture(video_context.texture);
        }
        // Create a new texture
        SDL_Texture* texture =
            SDL_CreateTexture(video_context.renderer, SDL_PIXELFORMAT_YV12,
                              SDL_TEXTUREACCESS_STREAMING, MAX_SCREEN_WIDTH, MAX_SCREEN_HEIGHT);
        if (!texture) {
            LOG_FATAL("SDL: could not create texture - exiting");
        }
        // Save the new texture over the old one
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
        if (draw_peer_cursor(renderer, x, y, msgs->color.red, msgs->color.green,
                             msgs->color.blue) != 0) {
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

int init_video_renderer() {
    /*
        Initialize the video renderer. Used as a thread function.

        Return:
            (int): 0 on success, -1 on failure
    */

    if (init_peer_cursors() != 0) {
        LOG_WARNING("Failed to init peer cursors.");
    }

    // Here we create the frame buffer allocator,
    // And make it allocate blocks of size LARGEST_FRAME_SIZE
    frame_buffer_allocator = create_block_allocator(LARGEST_FRAME_SIZE);

    can_render = true;

    LOG_INFO("Creating renderer for %dx%d display", output_width, output_height);

    // configure renderer
    if (SDL_GetWindowFlags((SDL_Window*)window) & SDL_WINDOW_OPENGL) {
        // only opengl if windowed mode
        SDL_SetHint(SDL_HINT_RENDER_DRIVER, "opengl");
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");

// SDL guidelines say that renderer functions should be done on the main thread,
//      but our implementation requires that the renderer is made in this thread
//      for non-MacOS
#ifdef __APPLE__
    SDL_Renderer* renderer = (SDL_Renderer*)init_sdl_renderer;
#else
    SDL_Renderer* renderer = SDL_CreateRenderer(
        (SDL_Window*)window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
#endif

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
    has_video_rendered_yet = false;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    // Allocate a place to put our YUV image on that screen.
    // Rather than allocating a new texture every time the dimensions change, we instead allocate
    // the texture once and render sub-rectangles of it.
    pending_texture_update = true;
    update_texture();

    pending_sws_update = false;
    sws_input_fmt = AV_PIX_FMT_NONE;
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
    start_timer(&video_data.missing_frame_nack_timer);
    video_data.num_nacked = 0;
    video_data.bucket = STARTING_BITRATE / BITRATE_BUCKET_SIZE;
    start_timer(&video_data.last_iframe_request_timer);

    for (int i = 0; i < RECV_FRAMES_BUFFER_SIZE; i++) {
        receiving_frames[i].id = -1;
    }

    // Resize event handling
    SDL_AddEventWatch(resizing_event_watcher, (SDL_Window*)window);

    // Init loading animation variables
    video_data.loading_index = 0;
    start_timer(&video_data.last_loading_frame_timer);
    // Present first frame of loading animation
    loading_sdl(renderer, video_data.loading_index);
    video_data.loading_index++;

    // Mark as initialized and return
    initialized_video_renderer = true;
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
        Initializes the video system
    */
    initialized_video_renderer = false;
    memset(&video_context, 0, sizeof(video_context));
    render_mutex = safe_SDL_CreateMutex();
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

        // LOG_INFO("FPS: %f\nmbps: %f\ndropped: %f%%\n", fps, mbps, 100.0 *
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

        // When we receive a packet that is a part of the next_render_id, and we have received every
        // packet for that frame, we set rendering=true
        if (ctx->id == next_render_id) {
            if (ctx->packets_received == ctx->num_packets) {
                // LOG_INFO( "Packets: %d %s", ctx->num_packets,
                // ((Frame*)ctx->frame_buffer)->is_iframe ? "(I-frame)" : "" );
                // LOG_INFO("Rendering %d (Age %f)", ctx->id,
                // get_timer(ctx->frame_creation_timer));

                // Now render_context will own the frame_buffer memory block
                render_context = *ctx;
                // So we make sure that ctx->frame_buffer no longer owns the memory block
                ctx->frame_buffer = NULL;
                // Get the FrameData for the next frame
                int next_frame_render_id = next_render_id + 1;
                int next_frame_index = next_frame_render_id % RECV_FRAMES_BUFFER_SIZE;
                FrameData* next_frame_ctx = &receiving_frames[next_frame_index];

                // If the next frame has been received,
                // lets skip the rendering so we can render the next frame faster
                // we do this because rendering is synced with screen refresh
                // so rendering the backlogged frames requires the client to wait
                if (next_frame_ctx->id == next_frame_render_id &&
                    next_frame_ctx->packets_received == next_frame_ctx->num_packets) {
                    skip_render = true;
                    LOG_INFO("Skip this render");
                } else {
                    skip_render = false;
                }
                rendering = true;
            } else {
                if ((get_timer(ctx->last_packet_timer) > latency) &&
                    get_timer(ctx->last_nacked_timer) > latency + latency * ctx->num_times_nacked) {
                    if (ctx->num_times_nacked == -1) {
                        ctx->num_times_nacked = 0;
                        ctx->last_nacked_index = -1;
                    }
                    int num_nacked = 0;
                    // LOG_INFO("************NACKING PACKET %d, alive for %f
                    // MS", ctx->id, get_timer(ctx->frame_creation_timer));
                    for (int i = ctx->last_nacked_index + 1; i < ctx->num_packets && num_nacked < 1;
                         i++) {
                        if (!ctx->received_indicies[i]) {
                            num_nacked++;
                            LOG_INFO(
                                "************NACKING VIDEO PACKET %d %d (/%d), "
                                "alive for %f MS",
                                ctx->id, i, ctx->num_packets,
                                get_timer(ctx->frame_creation_timer) * MS_IN_SECOND);
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

            // if we are more than MAX_UNSYNCED_FRAMES behind, we should request an iframe.
            if (video_data.max_id >
                video_data.last_rendered_id +
                    MAX_UNSYNCED_FRAMES)  // || (cur_ctx->id == VideoData.last_rendered_id
                                          // && get_timer( cur_ctx->last_packet_timer )
                                          // > 96.0 / 1000.0) )
            {
                if (request_iframe()) {
                    LOG_INFO(
                        "The most recent ID is %d frames ahead of the most recent rendered frame, "
                        "and there is no available frame to render. I-Frame is now being requested "
                        "to catch-up.",
                        MAX_UNSYNCED_FRAMES);
                }
            } else {
                // we should also request an iframe if we are missing a lot of packets
                // in case frames are large.
                // Serina: I am not sure what's a good number here, I put 20 to start
                int missing_packets = 0;
                for (int i = video_data.last_rendered_id + 1;
                     i < video_data.last_rendered_id + MAX_UNSYNCED_FRAMES; i++) {
                    int buffer_index = i % RECV_FRAMES_BUFFER_SIZE;
                    if (receiving_frames[buffer_index].id == i) {
                        for (int j = 0; j < receiving_frames[buffer_index].num_packets; j++) {
                            if (!receiving_frames[buffer_index].received_indicies[j]) {
                                missing_packets++;
                            }
                        }
                    }
                }
                if (missing_packets > MAX_MISSING_PACKETS) {
                    if (request_iframe()) {
                        LOG_INFO(
                            "Missing %d packets in the %d frames ahead of the most recently "
                            "rendered frame,"
                            "and there is no available frame to render. I-Frame is now being "
                            "requested "
                            "to catch-up.",
                            MAX_MISSING_PACKETS, MAX_UNSYNCED_FRAMES);
                    }
                }
            }
        } else {
            // If we're rendering, we might catch up in a bit, so we can be more lenient
            // and will only i-frame if we're MAX_UNSYNCED_FRAMES_RENDER frames behind.
            if (video_data.max_id > video_data.last_rendered_id + MAX_UNSYNCED_FRAMES_RENDER) {
                if (request_iframe()) {
                    LOG_INFO(
                        "The most recent ID is %d frames ahead of the most recent rendered frame. "
                        "I-Frame is now being requested to catch-up.",
                        MAX_UNSYNCED_FRAMES_RENDER);
                }
            }
        }
        // if we haven't requested an iframe, we should be asking for our missing out-of-order
        // frames the below if check is here to make sure that at the beginning, we don't request a
        // bunch of frames. there must be a better way to resolve that though. I still want to be
        // able to nack for missing frames when I'm waiting for an iframe.
        if (!video_data.is_waiting_for_iframe) {
            // check for any out-of-order frames
            // currently, checks if the ring buffer has an old frame
            if (get_timer(video_data.missing_frame_nack_timer) > latency) {
                for (int i = next_render_id; i < video_data.max_id; i++) {
                    int buffer_index = i % RECV_FRAMES_BUFFER_SIZE;
                    if (receiving_frames[buffer_index].id != i) {
                        // we just didn't receive any packets for a frame
                        // though we did receive packets for future frames
                        // we should nack the index 0 packet for said frame
                        LOG_INFO("Missing all packets for frame %d, nacking now for index 0", i);
                        start_timer(&video_data.missing_frame_nack_timer);
                        nack(i, 0);
                    }
                }
            }
        }
    }
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int32_t receive_video(FractalPacket* packet) {
    /*
        Receive video packet

        Arguments:
            packet (FractalPacket*): Packet received from the server, which gets
                sorted as video packet with proper parameters

        Returns:
            (int32_t): -1 if failed to receive packet into video frame, else 0
    */

    // LOG_INFO("Video Packet ID %d, Index %d (Packets: %d) (Size: %d)",
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
        // If the frame buffer doesn't exist yet, we allocate one
        // in order to hold the frame data
        if (ctx->frame_buffer == NULL) {
            char* p = allocate_block(frame_buffer_allocator);
            ctx->frame_buffer = p;
        }
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
        // LOG_INFO("Initialized packet %d!", ctx->id);
    } else {
        // LOG_INFO("Already Started: %d/%d - %f", ctx->packets_received + 1,
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
        LOG_DEBUG(
            "Skipping duplicate Video ID %d Index %d at time since creation %f "
            "%s",
            packet->id, packet->index, get_timer(ctx->frame_creation_timer),
            packet->is_a_nack ? "(nack)" : "");
#endif
        return 0;
    }

#if LOG_VIDEO
    // LOG_INFO("Received Video ID %d Index %d at time since creation %f %s",
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
        LOG_INFO("Received Video Frame ID %d (Packets: %d) (Size: %d) %s", ctx->id,
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

    if (!initialized_video_renderer) {
        LOG_ERROR("Destroying video, but never called init_video_renderer");
    } else {
        SDL_DestroyRenderer((SDL_Renderer*)video_context.renderer);

#if CAN_UPDATE_WINDOW_TITLEBAR_COLOR
        if (native_window_color) {
            free((FractalRGBColor*)native_window_color);
        }
#endif  // CAN_UPDATE_WINDOW_TITLEBAR_COLOR

        // SDL_DestroyTexture(videoContext.texture); is not needed,
        // the renderer destroys it
        av_freep(&video_context.data[0]);
        if (destroy_peer_cursors() != 0) {
            LOG_ERROR("Failed to destroy peer cursors.");
        }
    }

    SDL_DestroyMutex(render_mutex);
    render_mutex = NULL;

    has_video_rendered_yet = false;
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
        safe_SDL_LockMutex(render_mutex);

        int new_width = get_window_pixel_width((SDL_Window*)window);
        int new_height = get_window_pixel_height((SDL_Window*)window);
        if (new_width != output_width || new_height != output_height) {
            pending_texture_update = true;
            pending_sws_update = true;
            output_width = new_width;
            output_height = new_height;
        }
        can_render = true;
        safe_SDL_UnlockMutex(render_mutex);
    } else {
        safe_SDL_LockMutex(render_mutex);
        can_render = false;
        pending_resize_render = true;
        safe_SDL_UnlockMutex(render_mutex);

        for (int i = 0; pending_resize_render && (i < 10); ++i) {
            SDL_Delay(1);
        }

        if (pending_resize_render) {
            safe_SDL_LockMutex(render_mutex);
            pending_resize_render = false;
            safe_SDL_UnlockMutex(render_mutex);
        }
    }
}
