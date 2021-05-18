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

/*
============================
Includes
============================
*/

#include "sdl_event_handler.h"

#include <fractal/utils/logging.h>
#include "sdlscreeninfo.h"
#include "sdl_utils.h"
#include "audio.h"
#include "client_utils.h"
#include "network.h"

// Keyboard state variables
extern bool alt_pressed;
extern bool ctrl_pressed;
extern bool lgui_pressed;
extern bool rgui_pressed;

// Main state variables
extern bool exiting;

extern SDL_mutex *window_resize_mutex;
extern const float window_resize_interval;
extern clock window_resize_timer;
extern volatile bool pending_resize_message;

extern volatile SDL_Window *window;

extern volatile int output_width;
extern volatile int output_height;
extern volatile float dpi;
extern volatile CodecType output_codec_type;

extern MouseMotionAccumulation mouse_state;

extern bool multigesture_active;

extern bool audio_refresh;

bool active_scroll = false;

/*
============================
Private Functions
============================
*/

int handle_window_size_changed(SDL_Event *event);
int handle_mouse_left_window(SDL_Event *event);
int handle_key_up_down(SDL_Event *event);
int handle_mouse_motion(SDL_Event *event);
int handle_mouse_wheel(SDL_Event *event);
int handle_mouse_button_up_down(SDL_Event *event);
int handle_multi_gesture(SDL_Event *event);
int handle_touch(SDL_Event *event);

/*
============================
Private Function Implementations
============================
*/

int handle_window_size_changed(SDL_Event *event) {
    /*
        Handle the SDL window size change event

        Arguments:
            event (SDL_Event*): SDL event for window change

        Return:
            (int): 0 on success
    */

    // Let video thread know about the resizing to
    // reinitialize display dimensions

    LOG_INFO("Received resize event for %dx%d, currently %dx%d", event->window.data1,
             event->window.data2, get_window_pixel_width((SDL_Window *)window),
             get_window_pixel_height((SDL_Window *)window));

#ifndef __linux__
    // Try to make pixel width and height conform to certain desirable dimensions
    int current_width = get_window_pixel_width((SDL_Window *)window);
    int current_height = get_window_pixel_height((SDL_Window *)window);
    int dpi = get_window_pixel_width((SDL_Window *)window) /
              get_window_virtual_width((SDL_Window *)window);

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

            SDL_SetWindowSize((SDL_Window *)window, desired_width / dpi, desired_height / dpi);
            LOG_INFO("Forcing a resize from %dx%d to %dx%d", current_width, current_height,
                     desired_width, desired_height);
            current_width = get_window_pixel_width((SDL_Window *)window);
            current_height = get_window_pixel_height((SDL_Window *)window);

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

    // This propagates the resize to the video thread, and marks it as no longer resizing.
    // output_width/output_height will now be updated
    set_video_active_resizing(false);

    safe_SDL_LockMutex(window_resize_mutex);
    if (get_timer(window_resize_timer) >= WINDOW_RESIZE_MESSAGE_INTERVAL / (float)MS_IN_SECOND) {
        pending_resize_message = false;
        send_message_dimensions();
        start_timer(&window_resize_timer);
    } else {
        pending_resize_message = true;
    }
    safe_SDL_UnlockMutex(window_resize_mutex);

    LOG_INFO("Window %d resized to %dx%d (Actual %dx%d)", event->window.windowID,
             event->window.data1, event->window.data2, output_width, output_height);

    return 0;
}

int handle_mouse_left_window(SDL_Event *event) {
    /*
        Handle the SDL event for mouse leaving window

        Arguments:
            event (SDL_Event*): SDL event for mouse leaving window

        Return:
            (int): 0 on success
    */

    UNUSED(event);
    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_INACTIVE;
    send_fmsg(&fmsg);
    return 0;
}

int handle_key_up_down(SDL_Event *event) {
    /*
        Handle the SDL key press or release event

        Arguments:
            event (SDL_event*): SDL event for key press/release

        Return:
            (int): 0 on success
    */

    FractalKeycode keycode =
        (FractalKeycode)SDL_GetScancodeFromName(SDL_GetKeyName(event->key.keysym.sym));
    bool is_pressed = event->key.type == SDL_KEYDOWN;

    // LOG_INFO("Scancode: %d", event->key.keysym.scancode);
    // LOG_INFO("Keycode: %d %d", keycode, is_pressed);
    // LOG_INFO("%s %s", (is_pressed ? "Pressed" : "Released"),
    // SDL_GetKeyName(event->key.keysym.sym));

    // Keep memory of alt/ctrl/lgui/rgui status
    if (keycode == FK_LALT) {
        alt_pressed = is_pressed;
    }
    if (keycode == FK_LCTRL) {
        ctrl_pressed = is_pressed;
    }
    if (keycode == FK_RCTRL) {
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
        send_fmsg(&fmsg);
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_G && is_pressed) {
        FractalClientMessage fmsg = {0};
        fmsg.type = CMESSAGE_INTERACTION_MODE;
        fmsg.interaction_mode = CONTROL;
        send_fmsg(&fmsg);
    }

    if (ctrl_pressed && alt_pressed && keycode == FK_M && is_pressed) {
        FractalClientMessage fmsg = {0};
        fmsg.type = CMESSAGE_INTERACTION_MODE;
        fmsg.interaction_mode = EXCLUSIVE_CONTROL;
        send_fmsg(&fmsg);
    }

    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_KEYBOARD;
    fmsg.keyboard.code = keycode;
    fmsg.keyboard.pressed = is_pressed;
    fmsg.keyboard.mod = event->key.keysym.mod;
    send_fmsg(&fmsg);

    return 0;
}

int handle_mouse_motion(SDL_Event *event) {
    /*
        Handle the SDL mouse motion event

        Arguments:
            event (SDL_Event*): SDL event for mouse motion

        Result:
            (int): 0 on success, -1 on failure
    */

    // Relative motion is the delta x and delta y from last
    // mouse position Absolute mouse position is where it is
    // on the screen We multiply by scaling factor so that
    // integer division doesn't destroy accuracy
    bool is_relative = SDL_GetRelativeMouseMode() == SDL_TRUE;

    if (is_relative && !mouse_state.is_relative) {
        // old datum was absolute, new is relative
        // Hence, we flush out the old datum
        if (update_mouse_motion() != 0) {
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

int handle_mouse_button_up_down(SDL_Event *event) {
    /*
        Handle the SDL mouse button press/release event

        Arguments:
            event (SDL_Event*): SDL event for mouse button press/release event

        Result:
            (int): 0 on success
    */

    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_BUTTON;
    // Record if left / right / middle button
    fmsg.mouseButton.button = event->button.button;
    fmsg.mouseButton.pressed = event->button.type == SDL_MOUSEBUTTONDOWN;
    if (fmsg.mouseButton.button == MOUSE_L) {
        SDL_CaptureMouse(fmsg.mouseButton.pressed);
    }
    send_fmsg(&fmsg);

    return 0;
}

int handle_mouse_wheel(SDL_Event *event) {
    /*
        Handle the SDL mouse wheel event

        Arguments:
            event (SDL_Event*): SDL event for mouse wheel event

        Result:
            (int): 0 on success
    */

    active_scroll = true;

    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MOUSE_WHEEL;
    fmsg.mouseWheel.x = event->wheel.x;
    fmsg.mouseWheel.y = event->wheel.y;
    fmsg.mouseWheel.precise_x = event->wheel.preciseX;
    fmsg.mouseWheel.precise_y = event->wheel.preciseY;
    send_fmsg(&fmsg);

    return 0;
}

int handle_multi_gesture(SDL_Event *event) {
    static FractalMultigestureType current_gesture_type = NONE; // static, so only set to NONE on first call

    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_MULTIGESTURE;
    fmsg.multigesture = (FractalMultigestureMessage){.d_theta = event->mgesture.dTheta,
                                                     .d_dist = event->mgesture.dDist * (int)dpi,
                                                     .x = event->mgesture.x,
                                                     .y = event->mgesture.y,
                                                     .num_fingers = event->mgesture.numFingers,
                                                     .active_gesture = multigesture_active};
    // LOG_INFO("multigesture detected!! d_theta: %f d_dist: %f, x: %d, y: %d, num_fingers: %u",
    //     event->mgesture.dTheta, event->mgesture.dDist, event->mgesture.x, event->mgesture.y, event->mgesture.numFingers);

    // If not scrolling and detected pinch, then populate fmsg to send pinch event to server
    if (!active_scroll && fabs(event->mgesture.dDist) > 10.0 / ((float) output_width)) {
        multigesture_active = true;
        if (event->mgesture.dDist > 0) {
            current_gesture_type = PINCH_OPEN;
            LOG_INFO("START PINCH OPEN - %f > %f, %d, %d", event->mgesture.dDist, 10.0 / ((float) output_width), output_width, output_height);
        } else {
            current_gesture_type = PINCH_CLOSE;
            LOG_INFO("START PINCH CLOSE - %f > %f, %d, %d", event->mgesture.dDist, 10.0 / ((float) output_width), output_width, output_height);
        }
    // } else if (!multigesture_active) {
    } else {
        // if there is no active scroll and this is not a valid multigesture, then return
        current_gesture_type = NONE;
        return 0;
    }

    // if (current_gesture_type == ROTATE) {
    //     LOG_INFO("ROTATING");
    // } else if (current_gesture_type == PINCH_OPEN) {
    //     LOG_INFO("PINCHING OPEN d_dist: %f", event->mgesture.dDist);
    // } else if (current_gesture_type == PINCH_CLOSE) {
    //     LOG_INFO("PINCHING CLOSED d_dist: %f", event->mgesture.dDist);
    // }

    fmsg.multigesture.gesture_type = current_gesture_type;

    send_fmsg(&fmsg);

    return 0;
}

int handle_touch_up(SDL_Event *event) {
    FractalClientMessage fmsg = {0};
    fmsg.type = MESSAGE_TOUCH;
    fmsg.touch = (FractalTouchMessage){.x = event->tfinger.x,
                                       .y = event->tfinger.y,
                                       .dx = event->tfinger.dx,
                                       .dy = event->tfinger.dy,
                                       .active_gesture = multigesture_active};
    fmsg.touch.touch_type = FINGER_UP;

    send_fmsg(&fmsg);

    // the multigesture or scroll has ended
    if (multigesture_active || active_scroll) {
        LOG_INFO("RELEASED GESTURE");
        multigesture_active = false;
        active_scroll = false;
    }

    return 0;
}

/*
============================
Public Function Implementations
============================
*/

int try_handle_sdl_event(void) {
    /*
        Handle an SDL event if polled

        Return:
            (int): 0 on success, -1 on failure
    */

    SDL_Event event;
    if (SDL_PollEvent(&event)) {
        if (handle_sdl_event(&event) != 0) {
            return -1;
        }
    }
    return 0;
}

int handle_sdl_event(SDL_Event *event) {
    /*
        Handle SDL event based on type

        Return:
            (int): 0 on success, -1 on failure
    */

    // LOG_INFO("sdl event->type: 0x%x; mgesture: 0x%x; motion: %d; tfinger: %d",
    //     event->type, event->mgesture.type, event->motion.which, event->tfinger.type);

    switch (event->type) {
        case SDL_WINDOWEVENT:
            if (event->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
                if (handle_window_size_changed(event) != 0) {
                    return -1;
                }
            } else if (event->window.event == SDL_WINDOWEVENT_LEAVE) {
                if (handle_mouse_left_window(event) != 0) {
                    return -1;
                }
            }
            break;
        case SDL_AUDIODEVICEADDED:
        case SDL_AUDIODEVICEREMOVED:
            // Refresh the audio device
            set_audio_refresh();
            break;
        case SDL_KEYDOWN:
        case SDL_KEYUP:
#ifdef __APPLE__
            // On Mac, map cmd to ctrl
            if (event->key.keysym.scancode == FK_LGUI) {
                event->key.keysym.scancode = (SDL_Scancode)FK_LCTRL;
                event->key.keysym.sym = SDLK_LCTRL;
            }
            if (event->key.keysym.scancode == FK_RGUI) {
                event->key.keysym.scancode = (SDL_Scancode)FK_RCTRL;
                event->key.keysym.sym = SDLK_RCTRL;
            }
#endif

            if (handle_key_up_down(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEMOTION:
            if (handle_mouse_motion(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEBUTTONDOWN:
        case SDL_MOUSEBUTTONUP:
            if (handle_mouse_button_up_down(event) != 0) {
                return -1;
            }
            break;
        case SDL_MOUSEWHEEL:
            if (handle_mouse_wheel(event) != 0) {
                return -1;
            }
            break;
        case SDL_MULTIGESTURE:
            if (handle_multi_gesture(event) != 0) {
                return -1;
            }
            break;
        case SDL_FINGERUP:
            if (handle_touch_up(event) != 0) {
                return -1;
            }
            break;
        case SDL_QUIT:
            LOG_INFO("Forcefully Quitting...");
            exiting = true;
            break;
    }
    return 0;
}
