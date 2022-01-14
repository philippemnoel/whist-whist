/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
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
#include <SDL2/SDL_syswm.h>
#include <Cocoa/Cocoa.h>
#include <IOKit/pwr_mgt/IOPMLib.h>
#include <VideoToolbox/VideoToolbox.h>
#include <whist/core/whist.h>
#include "sdl_utils.h"

/*
============================
Public Function Implementations
============================
*/

void hide_native_window_taskbar() {
    /*
        Hide the taskbar icon for the app. This only works on macOS (for Window and Linux,
        SDL already implements this in the `SDL_WINDOW_SKIP_TASKBAR` flag).
    */
    [NSApp setActivationPolicy:NSApplicationActivationPolicyAccessory];

    // most likely not necessary
    [NSApp setPresentationOptions:NSApplicationPresentationDefault];

    // hack to make menubar show up
    [NSMenu setMenuBarVisible:NO];
    [NSMenu setMenuBarVisible:YES];
}

NSWindow *get_native_window(SDL_Window *window) {
    SDL_SysWMinfo sys_info = {0};
    SDL_GetWindowWMInfo(window, &sys_info);
    return sys_info.info.cocoa.window;
}

void init_native_window_options(SDL_Window *window) {
    /*
        Initialize the customized native window. This is called from the main thread
        right after the window finishes loading.

        Arguments:
            window (SDL_Window*): The SDL window wrapper for the NSWindow to customize.
    */
    NSWindow *native_window = get_native_window(window);

    // Hide the titlebar
    [native_window setTitlebarAppearsTransparent:true];

    // Hide the title itself
    [native_window setTitleVisibility:NSWindowTitleHidden];
}

int set_native_window_color(SDL_Window *window, WhistRGBColor color) {
    /*
        Set the color of the titlebar of the native macOS window, and the corresponding
        titlebar text color.

        Arguments:
            window (SDL_Window*):       SDL window wrapper for the NSWindow whose titlebar to modify
            color (WhistRGBColor):    The RGB color to use when setting the titlebar color

        Returns:
            (int): 0 on success, -1 on failure.
    */

    NSWindow *native_window = get_native_window(window);

    if (color_requires_dark_text(color)) {
        // dark font color for titlebar
        [native_window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameVibrantLight]];
    } else {
        // light font color for titlebar
        [native_window setAppearance:[NSAppearance appearanceNamed:NSAppearanceNameVibrantDark]];
    }

    // RGBA; in this case, alpha is just 1.0.
    const CGFloat components[4] = {(float)color.red / 255., (float)color.green / 255.,
                                   (float)color.blue / 255., 1.0};

    // Use color space for current monitor for perfect color matching
    [native_window
        setBackgroundColor:[NSColor colorWithColorSpace:[[native_window screen] colorSpace]
                                             components:&components[0]
                                                  count:4]];
    return 0;
}

int get_native_window_dpi(SDL_Window *window) {
    /*
        Get the DPI for the display of the provided window.

        Arguments:
            window (SDL_Window*):       SDL window whose DPI to retrieve.

        Returns:
            (int): The DPI as an int, with 96 as a base.
    */

    NSWindow *native_window = get_native_window(window);

    const CGFloat scale_factor = [[native_window screen] backingScaleFactor];
    return (int)(96 * scale_factor);
}

static IOPMAssertionID power_assertion_id = kIOPMNullAssertionID;
static WhistMutex last_user_activity_timer_mutex;
static WhistTimer last_user_activity_timer;
static bool assertion_set = false;

// Timeout the screensaver after 1min of inactivity,
// Their own screensaver timer will pick up the rest of the time
#define SCREENSAVER_TIMEOUT_SECONDS (1 * 60)

int user_activity_deactivator(void *unique) {
    UNUSED(unique);

    while (true) {
        // Only wake up once every 30 seconds, resolution doesn't matter that much here
        whist_sleep(30 * MS_IN_SECOND);
        whist_lock_mutex(last_user_activity_timer_mutex);
        if (get_timer(&last_user_activity_timer) > SCREENSAVER_TIMEOUT_SECONDS && assertion_set) {
            IOReturn result = IOPMAssertionRelease(power_assertion_id);
            if (result != kIOReturnSuccess) {
                LOG_ERROR("IOPMAssertionRelease Failed!");
            }
            assertion_set = false;
        }
        whist_unlock_mutex(last_user_activity_timer_mutex);
    }

    return 0;
}

void declare_user_activity() {
    if (!last_user_activity_timer_mutex) {
        last_user_activity_timer_mutex = whist_create_mutex();
        start_timer(&last_user_activity_timer);
        whist_create_thread(user_activity_deactivator, "user_activity_deactivator", NULL);
    }

    // Static bool because we'll only accept one failure,
    // in order to not spam LOG_ERROR's
    static bool failed = false;
    if (!failed) {
        // Reset the last activity timer
        whist_lock_mutex(last_user_activity_timer_mutex);
        start_timer(&last_user_activity_timer);
        whist_unlock_mutex(last_user_activity_timer_mutex);

        // Declare user activity to MacOS
        if (!assertion_set) {
            IOReturn result =
                IOPMAssertionCreateWithName(kIOPMAssertionTypeNoDisplaySleep, kIOPMAssertionLevelOn,
                                            CFSTR("WhistNewFrameActivity"), &power_assertion_id);
            // Immediatelly calling IOPMAssertionRelease here,
            // Doesn't seem to fully reset the screensaver timer
            // Instead, we need to call IOPMAssertionRelease a minute later
            if (result == kIOReturnSuccess) {
                // If the call succeeded, mark as set
                assertion_set = true;
            } else {
                LOG_ERROR("Failed to IOPMAssertionCreateWithName");
                // If the call failed, we'll just disable the screensaver then
                SDL_DisableScreenSaver();
                failed = true;
            }
        }
    }
}
