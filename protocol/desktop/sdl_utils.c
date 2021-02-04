/**
 * Copyright Fractal Computers, Inc. 2020
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
#include "../fractal/utils/png.h"

#if CAN_UPDATE_WINDOW_TITLEBAR_COLOR
#include "native_window_utils.h"
#endif  // CAN_UPDATE_WINDOW_TITLEBAR_COLOR

extern volatile int output_width;
extern volatile int output_height;
extern volatile SDL_Window* window;

#if defined(_WIN32)
HHOOK g_h_keyboard_hook;
LRESULT CALLBACK low_level_keyboard_proc(INT n_code, WPARAM w_param, LPARAM l_param);
#endif

#ifdef __APPLE__
// on macOS, we must initialize the renderer in `init_sdl()` instead of video.c
volatile SDL_Renderer* renderer;
#endif

/*
============================
Private Function Implementations
============================
*/

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
    LOG_INFO("KEY: %d", key);
    e.key.timestamp = time;
    SDL_PushEvent(&e);
}

int resizing_event_watcher(void* data, SDL_Event* event) {
    /*
        Event watcher to be used in SDL_AddEventWatch to capture
        and handle window resize events

        Arguments:
            data (void*): SDL Window data
            event (SDL_Event*): SDL event to be analyzed

        Return:
            (int): 0 on success
    */

    if (event->type == SDL_WINDOWEVENT && event->window.event == SDL_WINDOWEVENT_RESIZED) {
        // If the resize event if for the current window
        SDL_Window* win = SDL_GetWindowFromID(event->window.windowID);
        if (win == (SDL_Window*)data) {
            // Notify video.c about the active resizing
            set_video_active_resizing(true);
        }
    }
    return 0;
}

void set_window_icon_from_png(SDL_Window* sdl_window, char* filename) {
    /*
        Set the icon for a SDL window from a PNG file

        Arguments:
            sdl_window (SDL_Window*): The window whose icon will be set
            filename (char*): A path to a `.png` file containing the 64x64 pixel icon.

        Return:
            None
    */

    AVPacket pkt;
    av_init_packet(&pkt);
    png_file_to_bmp(filename, &pkt);

    SDL_RWops* rw = SDL_RWFromMem(pkt.data, pkt.size);

    // second parameter nonzero means free the rw after reading it, no need to free rw ourselves
    SDL_Surface* icon_surface = SDL_LoadBMP_RW(rw, 1);
    if (icon_surface == NULL) {
        LOG_ERROR("Failed to load icon from file '%s': %s", filename, SDL_GetError());
        return;
    }

    // free pkt.data which is initialized by calloc in png_file_to_bmp
    free(pkt.data);

    SDL_SetWindowIcon(sdl_window, icon_surface);

    // surface can now be freed
    SDL_FreeSurface(icon_surface);
}

SDL_Window* init_sdl(int target_output_width, int target_output_height, char* name,
                     char* icon_filename) {
    /*
        Attaches the current thread to the specified current input desktop

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

    // TODO: make this a commandline argument based on client app settings!
    int full_width = get_virtual_screen_width();
    int full_height = get_virtual_screen_height();

    bool is_fullscreen = target_output_width == 0 && target_output_height == 0;

    // Default output dimensions will be full screen
    if (target_output_width == 0) {
        target_output_width = full_width;
    }

    if (target_output_height == 0) {
        target_output_height = full_height;
    }

    SDL_Window* sdl_window;

#if defined(_WIN32)
    static const uint32_t fullscreen_flags = SDL_WINDOW_BORDERLESS | SDL_WINDOW_ALWAYS_ON_TOP;
#else
    static const uint32_t fullscreen_flags =
        SDL_WINDOW_FULLSCREEN_DESKTOP | SDL_WINDOW_ALWAYS_ON_TOP;
#endif
    static const uint32_t windowed_flags = SDL_WINDOW_OPENGL;

    // Simulate fullscreen with borderless always on top, so that it can still
    // be used with multiple monitors
    sdl_window = SDL_CreateWindow(
        (name == NULL ? "Fractal" : name), SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
        target_output_width, target_output_height,
        SDL_WINDOW_ALLOW_HIGHDPI | (is_fullscreen ? fullscreen_flags : windowed_flags));

/*
    On macOS, we must initialize the renderer in the main thread -- seems not needed
    and not possible on other OSes. If the renderer is created later in the main thread
    on macOS, the user will see a window open in this function, then close/reopen during
    renderer creation
*/
#ifdef __APPLE__
    renderer =
        SDL_CreateRenderer(sdl_window, -1, SDL_RENDERER_PRESENTVSYNC | SDL_RENDERER_ACCELERATED);
#endif

    // Set icon
    if (strcmp(icon_filename, "") != 0) {
        set_window_icon_from_png(sdl_window, icon_filename);
    }

#if CAN_UPDATE_WINDOW_TITLEBAR_COLOR
    set_native_window_color(sdl_window, 0, 0, 0);
#endif  // CAN_UPDATE_WINDOW_TITLEBAR_COLOR

    if (!is_fullscreen) {
        // Resize event handling
        SDL_AddEventWatch(resizing_event_watcher, (SDL_Window*)sdl_window);
        if (!sdl_window) {
            LOG_ERROR("SDL: could not create window - exiting: %s", SDL_GetError());
            return NULL;
        }
        SDL_SetWindowResizable((SDL_Window*)sdl_window, true);
    }

    SDL_Event cur_event;
    while (SDL_PollEvent(&cur_event)) {
        // spin to clear SDL event queue
        // this effectively waits for window load on Mac
    }

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

    LOG_INFO("Destroying SDL");
#if defined(_WIN32)
    UnhookWindowsHookEx(g_h_keyboard_hook);
#endif
    if (window_param) {
        SDL_DestroyWindow((SDL_Window*)window_param);
        window_param = NULL;
    }
    SDL_Quit();
}

#if defined(_WIN32)
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
