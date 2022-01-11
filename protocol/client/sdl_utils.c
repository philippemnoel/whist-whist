/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file sdl_utils.c
 * @brief This file contains the code to create and destroy SDL windows on the
 *        client.
============================
Usage
============================

initSDL gets called first to create an SDL window, and destroySDL at the end to
close the window.
*/

/*
============================
Includes
============================
*/

#include "sdl_utils.h"
#include <whist/utils/png.h>
#include <whist/utils/lodepng.h>
#include "client_statistic.h"

#include <whist/utils/color.h>
#include "native_window_utils.h"
#include "client_utils.h"

extern volatile int output_width;
extern volatile int output_height;
extern volatile SDL_Window* window;
extern bool skip_taskbar;

#if defined(_WIN32)
HHOOK g_h_keyboard_hook;
LRESULT CALLBACK low_level_keyboard_proc(INT n_code, WPARAM w_param, LPARAM l_param);
#endif

// on macOS, we must initialize the renderer in `init_sdl()` instead of video.c
static SDL_Renderer* sdl_renderer = NULL;
static SDL_Texture* frame_buffer = NULL;
static WhistMutex renderer_mutex;

// pending render Update
static volatile bool pending_render = false;

// Loading screen framebuffer update
static volatile bool pending_loadingscreen = false;
static int pending_loadingscreen_idx;

// NV12 framebuffer update
static volatile bool pending_nv12data = false;
static Uint8* pending_nv12data_data[4];
static int pending_nv12data_linesize[4];
static int pending_nv12data_width;
static int pending_nv12data_height;

// Window Color Update
static volatile WhistRGBColor* native_window_color = NULL;
static volatile bool native_window_color_update = false;

// Cursor Image Update
static volatile WhistCursorState cursor_state = CURSOR_STATE_VISIBLE;
static volatile SDL_Cursor* sdl_cursor = NULL;
static volatile WhistCursorID last_cursor = (WhistCursorID)SDL_SYSTEM_CURSOR_ARROW;

// Window Title Update
static volatile char* window_title = NULL;
static volatile bool should_update_window_title = false;

// Full Screen Update
static volatile bool fullscreen_trigger = false;
static volatile bool fullscreen_value = false;

/*
============================
Private Function Declarations
============================
*/

/**
 * @brief                          Creates an SDL_Surface from a png file
 *
 * @param filename                 The filename of the png file that will be used
 *
 * @returns                        The SDL_Surface that got created from the png file
 */
SDL_Surface* sdl_surface_from_png_file(char* filename);

/**
 * @brief                          Destroys an SDL_Surface created by sdl_surface_from_png_file
 *
 * @param surface                  The SDL_Surface to free
 */
void sdl_free_png_file_rgb_surface(SDL_Surface* surface);

/**
 * @brief                          Updates the framebuffer from any pending framebuffer call,
 *                                 Then RenderPresent if sdl_render_framebuffer is pending.
 */
void sdl_present_pending_framebuffer();

/*
============================
Public Function Implementations
============================
*/

SDL_Window* init_sdl(int target_output_width, int target_output_height, char* name,
                     char* icon_filename) {
    /*
        Attaches the current thread to the specified current input client

        Arguments:
            target_output_width (int): The width of the SDL window to create, in pixels
            target_output_height (int): The height of the SDL window to create, in pixels
            name (char*): The title of the window
            icon_filename (char*): The filename of the window icon, pointing to a 64x64 png,
                or "" for the default icon

        Return:
            sdl_window (SDL_Window*): NULL if fails to create SDL window, else the created SDL
                window
    */

#if defined(_WIN32)
    // set Windows DPI
    SetProcessDpiAwareness(PROCESS_SYSTEM_DPI_AWARE);
#endif

#if defined(_WIN32) && CAPTURE_SPECIAL_WINDOWS_KEYS
    // Hook onto windows keyboard to intercept windows special key combinations
    g_hKeyboardHook =
        SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(NULL), 0);
#endif

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
        LOG_ERROR("Could not initialize SDL - %s", SDL_GetError());
        return NULL;
    }

    renderer_mutex = whist_create_mutex();

    // Allow the screensaver to activate
    SDL_EnableScreenSaver();

    int full_width = get_virtual_screen_width();
    int full_height = get_virtual_screen_height();

    bool maximized = target_output_width == 0 && target_output_height == 0;

    // Default output dimensions will be a quarter of the full screen if the window
    // starts maximized. Even if this isn't a multiple of 8, it's fine because
    // clicking the minimize button will trigger an SDL resize event
    if (target_output_width == 0) {
        target_output_width = full_width / 2;
    }

    if (target_output_height == 0) {
        target_output_height = full_height / 2;
    }

    SDL_Window* sdl_window;

    if (skip_taskbar) {
        hide_native_window_taskbar();
    }

    const uint32_t window_flags = SDL_WINDOW_ALLOW_HIGHDPI | SDL_WINDOW_OPENGL |
                                  SDL_WINDOW_RESIZABLE | (maximized ? SDL_WINDOW_MAXIMIZED : 0) |
                                  (skip_taskbar ? SDL_WINDOW_SKIP_TASKBAR : 0);

    // Simulate fullscreen with borderless always on top, so that it can still
    // be used with multiple monitors
    sdl_window = SDL_CreateWindow((name == NULL ? "Whist" : name), SDL_WINDOWPOS_CENTERED,
                                  SDL_WINDOWPOS_CENTERED, target_output_width, target_output_height,
                                  window_flags);
    if (!sdl_window) {
        LOG_ERROR("SDL: could not create window - exiting: %s", SDL_GetError());
        return NULL;
    }

    // Initialize the renderer
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "best");
    SDL_SetHint(SDL_HINT_RENDER_VSYNC, VSYNC_ON ? "1" : "0");

    Uint32 flags = SDL_RENDERER_ACCELERATED;
#if VSYNC_ON
    flags |= SDL_RENDERER_PRESENTVSYNC;
#endif
    sdl_renderer = SDL_CreateRenderer(sdl_window, -1, flags);
    if (sdl_renderer == NULL) {
        LOG_FATAL("SDL: could not create renderer - exiting: %s", SDL_GetError());
    }
    SDL_SetRenderDrawBlendMode(sdl_renderer, SDL_BLENDMODE_BLEND);

    // Render a black screen before anything else,
    // To prevent being exposed to random colors
    pending_nv12data = false;
    pending_loadingscreen = false;
    pending_render = true;
    sdl_present_pending_framebuffer();

    // Initialize the framebuffer texture
    frame_buffer =
        SDL_CreateTexture(sdl_renderer, SDL_PIXELFORMAT_NV12, SDL_TEXTUREACCESS_STREAMING,
                          MAX_SCREEN_WIDTH, MAX_SCREEN_HEIGHT);
    if (frame_buffer == NULL) {
        LOG_FATAL("SDL: could not create texture - exiting: %s", SDL_GetError());
    }

    // Set icon, if an icon_filename was given
    if (strcmp(icon_filename, "") != 0) {
        SDL_Surface* icon_surface = sdl_surface_from_png_file(icon_filename);
        SDL_SetWindowIcon(sdl_window, icon_surface);
        sdl_free_png_file_rgb_surface(icon_surface);
    }

    // Initialize the window color to white
    const WhistRGBColor white = {255, 255, 255};
    set_native_window_color(sdl_window, white);

    SDL_Event cur_event;
    while (SDL_PollEvent(&cur_event)) {
        // spin to clear SDL event queue
        // this effectively waits for window load on Mac
    }

    // Only after the window finishes loading is it safe to initialize native options.
    init_native_window_options(sdl_window);

    // After creating the window, we will grab DPI-adjusted dimensions in real
    // pixels
    output_width = get_window_pixel_width((SDL_Window*)sdl_window);
    output_height = get_window_pixel_height((SDL_Window*)sdl_window);

    return sdl_window;
}

void destroy_sdl(SDL_Window* window_param) {
    /*
        Destroy the SDL resources

        Arguments:
            window_param (SDL_Window*): SDL window to be destroyed
    */

    // Destroy the framebuffer
    if (frame_buffer) {
        SDL_DestroyTexture(frame_buffer);
        frame_buffer = NULL;
    }

    // Destroy the renderer
    if (sdl_renderer) {
        SDL_DestroyRenderer(sdl_renderer);
        sdl_renderer = NULL;
    }

    if (native_window_color) {
        free((WhistRGBColor*)native_window_color);
        native_window_color = NULL;
    }

    LOG_INFO("Destroying SDL");
#if defined(_WIN32)
    UnhookWindowsHookEx(g_h_keyboard_hook);
#endif
    if (window_param) {
        SDL_DestroyWindow((SDL_Window*)window_param);
        window_param = NULL;
    }

    whist_destroy_mutex(renderer_mutex);

    SDL_Quit();
}

WhistMutex window_resize_mutex;
clock window_resize_timer;
// pending_resize_message should be set to true if sdl event handler was not able to process resize
// event due to throttling, so the main loop should process it
volatile bool pending_resize_message = false;
void sdl_renderer_resize_window(int width, int height) {
    // Try to make pixel width and height conform to certain desirable dimensions
    int current_width = get_window_pixel_width((SDL_Window*)window);
    int current_height = get_window_pixel_height((SDL_Window*)window);

    LOG_INFO("Received resize event for %dx%d, currently %dx%d", width, height, current_width,
             current_height);

#ifndef __linux__
    int dpi = current_width / get_window_virtual_width((SDL_Window*)window);

    // The server will round the dimensions up in order to satisfy the YUV pixel format
    // requirements. Specifically, it will round the width up to a multiple of 8 and the height up
    // to a multiple of 2. Here, we try to force the window size to be valid values so the
    // dimensions of the client and server match. We round down rather than up to avoid extending
    // past the size of the display.
    int desired_width = current_width - (current_width % 8);
    int desired_height = current_height - (current_height % 2);
    static int prev_desired_width = 0;
    static int prev_desired_height = 0;
    static int tries = 0;  // number of attemps to force window size to be prev_desired_width/height
    if (current_width != desired_width || current_height != desired_height) {
        // Avoid trying to force the window size forever, stop after 4 attempts
        if (!(prev_desired_width == desired_width && prev_desired_height == desired_height &&
              tries > 4)) {
            if (prev_desired_width == desired_width && prev_desired_height == desired_height) {
                tries++;
            } else {
                prev_desired_width = desired_width;
                prev_desired_height = desired_height;
                tries = 0;
            }

            SDL_SetWindowSize((SDL_Window*)window, desired_width / dpi, desired_height / dpi);
            LOG_INFO("Forcing a resize from %dx%d to %dx%d", current_width, current_height,
                     desired_width, desired_height);
            current_width = get_window_pixel_width((SDL_Window*)window);
            current_height = get_window_pixel_height((SDL_Window*)window);

            if (current_width != desired_width || current_height != desired_height) {
                LOG_WARNING(
                    "Unable to change window size to match desired dimensions using "
                    "SDL_SetWindowSize: "
                    "actual output=%dx%d, desired output=%dx%d",
                    current_width, current_height, desired_width, desired_height);
            }
        }
    }
#endif

    // Update output width / output height
    if (current_width != output_width || current_height != output_height) {
        output_width = current_width;
        output_height = current_height;
    }

    whist_lock_mutex(window_resize_mutex);
    pending_resize_message = true;
    whist_unlock_mutex(window_resize_mutex);

    LOG_INFO("Window resized to %dx%d (Actual %dx%d)", width, height, output_width, output_height);
}

void sdl_update_framebuffer_loading_screen(int idx) {
    /*
        Make the screen black and show the loading screen
        Arguments:
            idx (int): the index of the loading frame
    */

    whist_lock_mutex(renderer_mutex);

    // Clear any other pending framebuffer
    pending_nv12data = false;
    // Mark the pending framebuffer as the loading screen
    pending_loadingscreen_idx = idx;
    pending_loadingscreen = true;

    whist_unlock_mutex(renderer_mutex);
}

void sdl_update_framebuffer(Uint8* data[4], int linesize[4], int width, int height) {
    whist_lock_mutex(renderer_mutex);

    // Check dimensions as a fail-safe
    if (width < 0 || width > MAX_SCREEN_WIDTH || height < 0 || height > MAX_SCREEN_HEIGHT) {
        LOG_ERROR("Invalid Dimensions! %dx%d. nv12 update dropped", width, height);
    } else {
        // Clear any other pending framebuffer
        pending_loadingscreen = false;
        // Overwrite the pending framebuffer metadata,
        // And mark the nv12 framebuffer as pending
        memcpy(pending_nv12data_data, data, sizeof(pending_nv12data_data));
        memcpy(pending_nv12data_linesize, linesize, sizeof(pending_nv12data_linesize));
        pending_nv12data_width = width;
        pending_nv12data_height = height;
        pending_nv12data = true;
        // NOTE: The Uint8*'s that data[] points to, CANNOT be invalidated
        // until AFTER pending_nv12data is set back to false.
    }

    whist_unlock_mutex(renderer_mutex);
}

void sdl_render_framebuffer() {
    whist_lock_mutex(renderer_mutex);
    // Mark a render as pending
    pending_render = true;
    whist_unlock_mutex(renderer_mutex);
}

bool sdl_render_pending() {
    whist_lock_mutex(renderer_mutex);
    bool pending_render_val = pending_render;
    whist_unlock_mutex(renderer_mutex);
    return pending_render_val;
}

void sdl_update_cursor(WhistCursorImage* cursor) {
    /*
      Update the cursor image on the screen. If the cursor hasn't changed since the last frame we
      received, we don't do anything. Otherwise, we either use the provided bitmap or  update the
      cursor ID to tell SDL which cursor to render.
     */
    // Set cursor to frame's desired cursor type
    // Only update the cursor, if a cursor image is even embedded in the frame at all.
#define CURSORIMAGE_A 0xff000000
#define CURSORIMAGE_R 0x00ff0000
#define CURSORIMAGE_G 0x0000ff00
#define CURSORIMAGE_B 0x000000ff

    if (cursor) {
        if ((WhistCursorID)cursor->cursor_id != last_cursor || cursor->using_bmp) {
            if (sdl_cursor) {
                SDL_FreeCursor((SDL_Cursor*)sdl_cursor);
            }
            if (cursor->using_bmp) {
                // use bitmap data to set cursor
                SDL_Surface* cursor_surface = SDL_CreateRGBSurfaceFrom(
                    cursor->bmp, cursor->bmp_width, cursor->bmp_height, sizeof(uint32_t) * 8,
                    sizeof(uint32_t) * cursor->bmp_width, CURSORIMAGE_R, CURSORIMAGE_G,
                    CURSORIMAGE_B, CURSORIMAGE_A);

                // Use BLENDMODE_NONE to allow for proper cursor blit-resize
                SDL_SetSurfaceBlendMode(cursor_surface, SDL_BLENDMODE_NONE);

#ifdef _WIN32
                // on Windows, the cursor DPI is unchanged
                const int cursor_dpi = 96;
#else
                // on other platforms, use the window DPI
                const int cursor_dpi = get_native_window_dpi((SDL_Window*)window);
#endif  // _WIN32

                // Create the scaled cursor surface which takes DPI into account
                SDL_Surface* scaled_cursor_surface = SDL_CreateRGBSurface(
                    0, cursor->bmp_width * 96 / cursor_dpi, cursor->bmp_height * 96 / cursor_dpi,
                    cursor_surface->format->BitsPerPixel, cursor_surface->format->Rmask,
                    cursor_surface->format->Gmask, cursor_surface->format->Bmask,
                    cursor_surface->format->Amask);

                // Copy the original cursor into the scaled surface
                SDL_BlitScaled(cursor_surface, NULL, scaled_cursor_surface, NULL);

                // Potentially SDL_SetSurfaceBlendMode here since X11 cursor BMPs are
                // pre-alpha multplied. Remember to adjust hot_x/y by the DPI scaling.
                sdl_cursor = SDL_CreateColorCursor(scaled_cursor_surface,
                                                   cursor->bmp_hot_x * 96 / cursor_dpi,
                                                   cursor->bmp_hot_y * 96 / cursor_dpi);
                SDL_FreeSurface(cursor_surface);
                SDL_FreeSurface(scaled_cursor_surface);
            } else {
                // use cursor id to set cursor
                sdl_cursor = SDL_CreateSystemCursor((SDL_SystemCursor)cursor->cursor_id);
            }
            SDL_SetCursor((SDL_Cursor*)sdl_cursor);

            last_cursor = (WhistCursorID)cursor->cursor_id;
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
}

void sdl_render_window_titlebar_color(WhistRGBColor color) {
    /*
      Update window titlebar color using the colors of the new frame
     */
    WhistRGBColor* current_color = (WhistRGBColor*)native_window_color;
    if (current_color != NULL) {
        if (current_color->red != color.red || current_color->green != color.green ||
            current_color->blue != color.blue) {
            // delete the old color we were using
            free(current_color);

            // make the new color and signal that we're ready to update
            WhistRGBColor* new_native_window_color = safe_malloc(sizeof(WhistRGBColor));
            *new_native_window_color = color;
            native_window_color = new_native_window_color;
            native_window_color_update = true;
        }
    } else {
        // make the new color and signal that we're ready to update
        WhistRGBColor* new_native_window_color = safe_malloc(sizeof(WhistRGBColor));
        *new_native_window_color = color;
        native_window_color = new_native_window_color;
        native_window_color_update = true;
    }
}

void sdl_set_window_title(const char* requested_window_title) {
    if (should_update_window_title) {
        LOG_WARNING(
            "Failed to update window title, as the previous window title update is still pending");
        return;
    }

    size_t len = strlen(requested_window_title) + 1;
    char* new_window_title = safe_malloc(len);
    safe_strncpy(new_window_title, requested_window_title, len);
    window_title = new_window_title;

    should_update_window_title = true;
}

void sdl_set_fullscreen(bool is_fullscreen) {
    fullscreen_trigger = true;
    fullscreen_value = is_fullscreen;
}

bool sdl_is_window_visible() {
    return !(SDL_GetWindowFlags((SDL_Window*)window) &
             (SDL_WINDOW_OCCLUDED | SDL_WINDOW_MINIMIZED));
}

void sdl_update_pending_tasks() {
    // Handle any pending window title updates
    if (should_update_window_title) {
        if (window_title) {
            SDL_SetWindowTitle((SDL_Window*)window, (char*)window_title);
            free((char*)window_title);
            window_title = NULL;
        } else {
            LOG_ERROR("Window Title should not be null!");
        }
        should_update_window_title = false;
    }

    // Handle any pending fullscreen events
    if (fullscreen_trigger) {
        if (fullscreen_value) {
            SDL_SetWindowFullscreen((SDL_Window*)window, SDL_WINDOW_FULLSCREEN_DESKTOP);
        } else {
            SDL_SetWindowFullscreen((SDL_Window*)window, 0);
        }
        fullscreen_trigger = false;
    }

    // Handle any pending window titlebar color events
    if (native_window_color_update && native_window_color) {
        set_native_window_color((SDL_Window*)window, *(WhistRGBColor*)native_window_color);
        native_window_color_update = false;
    }

    // Check if a pending window resize message should be sent to server
    whist_lock_mutex(window_resize_mutex);
    if (pending_resize_message &&
        get_timer(window_resize_timer) >= WINDOW_RESIZE_MESSAGE_INTERVAL / (float)MS_IN_SECOND) {
        pending_resize_message = false;
        send_message_dimensions();
        start_timer(&window_resize_timer);
    }
    whist_unlock_mutex(window_resize_mutex);

    sdl_present_pending_framebuffer();
}

/*
============================
Private Function Implementations
============================
*/

void sdl_present_pending_framebuffer() {
    // Render out the current framebuffer, if there's a pending render
    whist_lock_mutex(renderer_mutex);

    // If there's no pending render, just do nothing,
    // Don't consume and discard any pending nv12 or loading screen.
    if (!pending_render) {
        whist_unlock_mutex(renderer_mutex);
        return;
    }

    // Wipes the renderer to all-black before we present
    SDL_SetRenderDrawColor(sdl_renderer, 0, 0, 0, SDL_ALPHA_OPAQUE);
    SDL_RenderClear(sdl_renderer);

    clock statistics_timer;
    start_timer(&statistics_timer);

    // Copy nv12 data, if any pending nv12 data exists
    if (pending_nv12data) {
        // The texture object we allocate is larger than the frame,
        // so we only copy the valid section of the frame into the texture.
        SDL_Rect texture_rect = {
            .x = 0,
            .y = 0,
            .w = pending_nv12data_width,
            .h = pending_nv12data_height,
        };

        // Update the SDLTexture with the given nvdata
        int res = SDL_UpdateNVTexture(frame_buffer, &texture_rect, pending_nv12data_data[0],
                                      pending_nv12data_linesize[0], pending_nv12data_data[1],
                                      pending_nv12data_linesize[1]);
        if (res < 0) {
            LOG_ERROR("SDL_UpdateNVTexture failed: %s", SDL_GetError());
        } else {
            // Take the subsection of texture that should be rendered to screen,
            // And draw it on the renderer
            SDL_Rect output_rect = {
                .x = 0,
                .y = 0,
                .w = output_width,
                .h = output_height,
            };
            SDL_RenderCopy(sdl_renderer, frame_buffer, &output_rect, NULL);
        }

        // No longer pending nv12 data
        pending_nv12data = false;
    }

    // Copy loading screen texture, if any pending one exists
    if (pending_loadingscreen) {
#define NUMBER_LOADING_FRAMES 50
        int gif_frame_index = pending_loadingscreen_idx % NUMBER_LOADING_FRAMES;

        char frame_filename[256];
        snprintf(frame_filename, sizeof(frame_filename), "loading/frame_%02d.png", gif_frame_index);

        SDL_Surface* loading_screen = sdl_surface_from_png_file(frame_filename);
        if (loading_screen == NULL) {
            LOG_ERROR("Loading screen image failed to load: %s", frame_filename);
        } else {
            SDL_Texture* loading_screen_texture =
                SDL_CreateTextureFromSurface(sdl_renderer, loading_screen);

            // The surface can now be freed
            sdl_free_png_file_rgb_surface(loading_screen);

            // Position the rectangle such that the texture will be centered
            int w, h;
            SDL_QueryTexture(loading_screen_texture, NULL, NULL, &w, &h);
            SDL_Rect centered_rect = {
                .x = output_width / 2 - w / 2,
                .y = output_height / 2 - h / 2,
                .w = w,
                .h = h,
            };

            // The texture is semi-transparent, so we clear to white first
            SDL_SetRenderDrawColor(sdl_renderer, 255, 255, 255, SDL_ALPHA_OPAQUE);
            SDL_RenderClear(sdl_renderer);
            // Now, we write the texture out to the renderer
            SDL_RenderCopy(sdl_renderer, loading_screen_texture, NULL, &centered_rect);

            // The loading screen texture may now be destroyed
            SDL_DestroyTexture(loading_screen_texture);
        }

        // The loading screen render is no longer pending
        pending_loadingscreen = false;
    }

    log_double_statistic(VIDEO_RENDER_TIME, get_timer(statistics_timer) * MS_IN_SECOND);

    whist_unlock_mutex(renderer_mutex);

    // RenderPresent outside of the mutex, since RenderCopy made a copy anyway
    // and this will take ~8ms if VSYNC is on.
    // (If this causes a bug, feel free to pull back to inside of the mutex)
    TIME_RUN(SDL_RenderPresent(sdl_renderer), VIDEO_RENDER_TIME, statistics_timer);

    whist_lock_mutex(renderer_mutex);
    pending_render = false;
    whist_unlock_mutex(renderer_mutex);
}

SDL_Surface* sdl_surface_from_png_file(char* filename) {
    /*
        Load a PNG file to an SDL surface using lodepng.

        Arguments:
            filename (char*): PNG image file path

        Returns:
            surface (SDL_Surface*): the loaded surface on success, and NULL on failure

        NOTE:
            After a successful call to sdl_surface_from_png_file, remember to call
            `sdl_free_png_file_rgb_surface(surface)` to free the memory later!
    */

    unsigned int w, h, error;
    unsigned char* image;

    // decode to 32-bit RGBA
    // TODO: We need to free image after destroying the SDL_Surface!
    // Perhaps we should simply return a PNG buffer and have the caller
    // manage memory allocation.
    error = lodepng_decode32_file(&image, &w, &h, filename);
    if (error) {
        LOG_ERROR("decoder error %u: %s\n", error, lodepng_error_text(error));
        return NULL;
    }

// masks for little-endian RGBA
#define RGBA_MASK_A 0xff000000
#define RGBA_MASK_B 0x00ff0000
#define RGBA_MASK_G 0x0000ff00
#define RGBA_MASK_R 0x000000ff

    // buffer pointer, width, height, bits per pixel, bytes per row, R/G/B/A masks
    SDL_Surface* surface =
        SDL_CreateRGBSurfaceFrom(image, w, h, sizeof(uint32_t) * 8, sizeof(uint32_t) * w,
                                 RGBA_MASK_R, RGBA_MASK_G, RGBA_MASK_B, RGBA_MASK_A);

    if (surface == NULL) {
        LOG_ERROR("Failed to load SDL surface from file '%s': %s", filename, SDL_GetError());
        free(image);
        return NULL;
    }

    return surface;
}

void sdl_free_png_file_rgb_surface(SDL_Surface* surface) {
    // Specified to call `SDL_FreeSurface` on the surface, and `free` on the pixels array
    char* pixels = surface->pixels;
    SDL_FreeSurface(surface);
    free(pixels);
}

#if defined(_WIN32)
void send_captured_key(SDL_Keycode key, int type, int time) {
    /*
        Send a key to SDL event queue, presumably one that is captured and wouldn't
        naturally make it to the event queue by itself

        Arguments:
            key (SDL_Keycode): key that was captured
            type (int): event type (press or release)
            time (int): time that the key event was registered
    */

    SDL_Event e = {0};
    e.type = type;
    e.key.keysym.sym = key;
    e.key.keysym.scancode = SDL_GetScancodeFromName(SDL_GetKeyName(key));
    e.key.timestamp = time;
    SDL_PushEvent(&e);
}

HHOOK mule;
LRESULT CALLBACK low_level_keyboard_proc(INT n_code, WPARAM w_param, LPARAM l_param) {
    /*
        Function to capture keyboard strokes and block them if they encode special
        key combinations, with intent to redirect them to send_captured_key so that the
        keys can still be streamed over to the host

        Arguments:
            n_code (INT): keyboard code
            w_param (WPARAM): w_param to be passed to CallNextHookEx
            l_param (LPARAM): l_param to be passed to CallNextHookEx

        Return:
            (LRESULT CALLBACK): CallNextHookEx return callback value
    */

    // By returning a non-zero value from the hook procedure, the
    // message does not get passed to the target window
    KBDLLHOOKSTRUCT* pkbhs = (KBDLLHOOKSTRUCT*)l_param;
    int flags = SDL_GetWindowFlags((SDL_Window*)window);
    if ((flags & SDL_WINDOW_INPUT_FOCUS)) {
        switch (n_code) {
            case HC_ACTION: {
                // Check to see if the CTRL key is pressed
                BOOL b_control_key_down = GetAsyncKeyState(VK_CONTROL) >> ((sizeof(SHORT) * 8) - 1);
                BOOL b_alt_key_down = pkbhs->flags & LLKHF_ALTDOWN;

                int type = (pkbhs->flags & LLKHF_UP) ? SDL_KEYUP : SDL_KEYDOWN;
                int time = pkbhs->time;

                // Disable LWIN
                if (pkbhs->vkCode == VK_LWIN) {
                    send_captured_key(SDLK_LGUI, type, time);
                    return 1;
                }

                // Disable RWIN
                if (pkbhs->vkCode == VK_RWIN) {
                    send_captured_key(SDLK_RGUI, type, time);
                    return 1;
                }

                // Disable CTRL+ESC
                if (pkbhs->vkCode == VK_ESCAPE && b_control_key_down) {
                    send_captured_key(SDLK_ESCAPE, type, time);
                    return 1;
                }

                // Disable ALT+ESC
                if (pkbhs->vkCode == VK_ESCAPE && b_alt_key_down) {
                    send_captured_key(SDLK_ESCAPE, type, time);
                    return 1;
                }

                // Disable ALT+TAB
                if (pkbhs->vkCode == VK_TAB && b_alt_key_down) {
                    send_captured_key(SDLK_TAB, type, time);
                    return 1;
                }

                // Disable ALT+F4
                if (pkbhs->vkCode == VK_F4 && b_alt_key_down) {
                    send_captured_key(SDLK_F4, type, time);
                    return 1;
                }

                break;
            }
            default:
                break;
        }
    }
    return CallNextHookEx(mule, n_code, w_param, l_param);
}
#endif
