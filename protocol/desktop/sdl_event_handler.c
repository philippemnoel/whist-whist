/**
 * Copyright Fractal Computers, Inc. 2020
 * @file sdl_event_handler.c
 * @brief This file contains client-specific wrappers to low-level network
 *        functions.
============================
Usage
============================

handleSDLEvent() must be called on any SDL event that occurs. Any action
trigged an SDL event must be triggered in sdl_event_handler.c
*/

#include "sdl_event_handler.h"

#include "../fractal/utils/logging.h"
#include "../fractal/utils/sdlscreeninfo.h"
#include "sdl_utils.h"
#include "audio.h"
#include "desktop_utils.h"
#include "network.h"

#define UNUSED(x) (void)(x)
#define WINDOWS_DEFAULT_DPI 96.0

// Keyboard state variables
extern bool alt_pressed;
extern bool ctrl_pressed;
extern bool lgui_pressed;
extern bool rgui_pressed;

// Main state variables
extern bool exiting;

extern clock window_resize_timer;

extern volatile SDL_Window *window;

extern volatile int output_width;
extern volatile int output_height;
extern volatile CodecType output_codec_type;

extern mouse_motion_accumulation mouse_state;

int handleWindowSizeChanged(SDL_Event *event);
int handleMouseLeftWindow(SDL_Event *event);
int handleKeyUpDown(SDL_Event *event);
int handleMouseMotion(SDL_Event *event);
int handleMouseWheel(SDL_Event *event);
int handleMouseButtonUpDown(SDL_Event *event);

int tryHandleSDLEvent(void) {
    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        if (handleSDLEvent(&event) != 0) {
            return -1;
        }
    }
    return 0;
}

int handleSDLEvent(SDL_Event *event) {
    switch (event->type) {
        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                if (handleWindowSizeChanged(event) != 0) {
                    return -1;
                }
            } else if (event->window.event == SDL_WINDOWEVENT_LEAVE) {
                if (handleMouseLeftWindow(event) != 0) {
                    return -1;
                }
            }
            break;
        case SDL_AUDIODEVICEADDED:
            LOG_INFO("AUDIO DEVICE ADDED!\n");
        case SDL_AUDIODEVICEREMOVED:
            destroyAudio();
            initAudio();
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
#ifdef __APPLE__
            // On Mac, map cmd to ctrl
            if (event->key.keysym.scancode == FK_LGUI) {
                event->key.keysym.scancode = (SDL_Scancode) FK_LCTRL;
            }
#endif
            if (handleKeyUpDown(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEMOTION:
            if (handleMouseMotion(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (handleMouseButtonUpDown(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (handleMouseWheel(event) != 0) {
                return -1;
            }
            break;
        case SDL_QUIT:
            LOG_ERROR("Forcefully Quitting...");
            exiting = true;
            break;
    }
    return 0;
}

int handleWindowSizeChanged(SDL_Event *event) {
    // Let video thread know about the resizing to
    // reinitialize display dimensions
    set_video_active_resizing(false);

    if (GetTimer(window_resize_timer) > 0.2) {
        // Let the server know the new dimensions so that it
        // can change native dimensions for monitor
        FractalClientMessage fmsg = {0};
        fmsg.type = MESSAGE_DIMENSIONS;
        fmsg.dimensions.width = output_width;
        fmsg.dimensions.height = output_height;
        fmsg.dimensions.codec_type = (CodecType)output_codec_type;
        fmsg.dimensions.dpi =
            (int)(WINDOWS_DEFAULT_DPI * output_width / get_virtual_screen_width());
        SendFmsg(&fmsg);

        StartTimer(&window_resize_timer);
    }

    LOG_INFO("Window %d resized to %dx%d (Physical %dx%d)", event->window.windowID,
             event->window.data1, event->window.data2, output_width, output_height);

    return 0;
}

int handleMouseLeftWindow(SDL_Event *event) {
    UNUSED(event);
    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_INACTIVE;
    SendFmsg(&fmsg);
    return 0;
}

int handleKeyUpDown(SDL_Event *event) {
    FractalKeycode keycode = (FractalKeycode)event->key.keysym.scancode;
    bool is_pressed = event->key.type == SDL_KEYDOWN;

    // Keep memory of alt/ctrl/lgui/rgui status
    if (keycode == FK_LALT) {
        alt_pressed = is_pressed;
    }
    if (keycode == FK_LCTRL) {
        ctrl_pressed = is_pressed;
    }
    if (keycode == FK_LGUI) {
        lgui_pressed = is_pressed;
    }
    if (keycode == FK_RGUI) {
        rgui_pressed = is_pressed;
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_F4) {
        LOG_INFO("Quitting...");
        exiting = true;
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_B && is_pressed) {
        FractalClientMessage fmsg = {0};
        fmsg.type = CMESSAGE_INTERACTION_MODE;
        fmsg.interaction_mode = SPECTATE;
        SendFmsg(&fmsg);
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_G && is_pressed) {
        FractalClientMessage fmsg = {0};
        fmsg.type = CMESSAGE_INTERACTION_MODE;
        fmsg.interaction_mode = CONTROL;
        SendFmsg(&fmsg);
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_M && is_pressed) {
        FractalClientMessage fmsg = {0};
        fmsg.type = CMESSAGE_INTERACTION_MODE;
        fmsg.interaction_mode = EXCLUSIVE_CONTROL;
        SendFmsg(&fmsg);
    }

    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_KEYBOARD;
    fmsg.keyboard.code = keycode;
    fmsg.keyboard.pressed = is_pressed;
    fmsg.keyboard.mod = event->key.keysym.mod;
    SendFmsg(&fmsg);

    return 0;
}

// Relative motion is the delta x and delta y from last
// mouse position Absolute mouse position is where it is
// on the screen We multiply by scaling factor so that
// integer division doesn't destroy accuracy
int handleMouseMotion(SDL_Event *event) {
    bool is_relative = SDL_GetRelativeMouseMode() == SDL_TRUE;

    if (is_relative && !mouse_state.is_relative) {
        // old datum was absolute, new is relative
        // hence, clear any pending absolute motion
        if (updateMouseMotion() != 0) {
            return -1;
        }
    }

    mouse_state.x_nonrel = event->motion.x;
    mouse_state.y_nonrel = event->motion.y;
    mouse_state.is_relative = is_relative;

    if (is_relative) {
        mouse_state.x_rel += event->motion.xrel;
        mouse_state.y_rel += event->motion.yrel;
    }

    mouse_state.update = true;

    return 0;
}

int handleMouseButtonUpDown(SDL_Event *event) {
    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_BUTTON;
    // Record if left / right / middle button
    fmsg.mouseButton.button = event->button.button;
    fmsg.mouseButton.pressed = event->button.type == SDL_MOUSEBUTTONDOWN;
    SendFmsg(&fmsg);

    return 0;
}

int handleMouseWheel(SDL_Event *event) {
    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_WHEEL;
    fmsg.mouseWheel.x = event->wheel.x;
    fmsg.mouseWheel.y = event->wheel.y;
    SendFmsg(&fmsg);

    return 0;
}
