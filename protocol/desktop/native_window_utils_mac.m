/**
 * Copyright Fractal Computers, Inc. 2021
 * @file native_window_utils_mac.m
 * @brief This file implements macOS APIs for native window
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
#include <Cocoa/Cocoa.h>
#include "SDL_syswm.h"

/*
============================
Public Function Implementations
============================
*/

int set_native_window_color(SDL_Window *window, RGBColor color) {
    /*
        Set the color of the titlebar of the native macOS window, and the corresponding
        titlebar text color.

        Arguments:
            window (SDL_Window*): SDL window wrapper for the NSWindow whose titlebar to modify
            color (RGBColor):     The RGB color to use when setting the titlebar color

        Returns:
            (int): 0 on success, -1 on failure.
    */
    
    SDL_SysWMinfo sys_info = {0};
    SDL_GetWindowWMInfo(window, &sys_info);
    NSWindow *native_window = sys_info.info.cocoa.window;

    if (color_requires_dark_text(color)) {
        // dark font color for titlebar
        [native_window setAppearance: [NSAppearance appearanceNamed:NSAppearanceNameVibrantLight]];
    } else {
        // light font color for titlebar
        [native_window setAppearance: [NSAppearance appearanceNamed:NSAppearanceNameVibrantDark]];
    }

    // RGBA; in this case, alpha is just 1.0.
    const CGFloat components[4] = {
        (float) color.red / 255., 
        (float) color.green / 255., 
        (float) color.blue / 255.,
        1.0
    };

    // We technically only need to set this the first time
    [native_window setTitlebarAppearsTransparent: true];

    // Use color space for current monitor for perfect color matching
    [native_window setBackgroundColor: [NSColor colorWithColorSpace: [[native_window screen] colorSpace]  components: &components[0] count: 4]];
    return 0;
}
