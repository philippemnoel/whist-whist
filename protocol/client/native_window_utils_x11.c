/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file native_window_utils_x11.c
 * @brief This file implements X11 APIs for native window
 *        modifications not handled by SDL.
============================
Usage
============================

Use these functions to modify the appearance or behavior of the native window
underlying the SDL window. These functions will almost always need to be called
from the same thread as SDL window creation and SDL event handling; usually this
is the main thread.
*/

/*
============================
Includes
============================
*/

#include "native_window_utils.h"
#include <whist/logging/logging.h>
#include <SDL2/SDL_video.h>
#include <SDL2/SDL_syswm.h>

void hide_native_window_taskbar() { LOG_INFO("Not implemented on X11."); }

int set_native_window_color(SDL_Window* window, WhistRGBColor color) {
    LOG_INFO("Not implemented on X11.");
    return 0;
}

void init_native_window_options(SDL_Window* window) { return; }

int get_native_window_dpi(SDL_Window* window) {
    /*
        Get the DPI for the display of the provided window.

        Arguments:
            window (SDL_Window*):       SDL window whose DPI to retrieve.

        Returns:
            (int): The DPI as an int, with 96 as a base.
    */

    // We just fall back to the SDL implementation here.
    int display_index = SDL_GetWindowDisplayIndex(window);
    float dpi;
    SDL_GetDisplayDPI(display_index, NULL, &dpi, NULL);
    return (int)dpi;
}

void declare_user_activity() {
    // For now, we'll just disable the screensaver
    LOG_INFO("Not implemented on Linux.");
    SDL_DisableScreenSaver();
}
