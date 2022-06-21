/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file handle_frontend_events.c
 * @brief This file exposes the functions to handle frontend events in the main loop.
============================
Usage
============================

handle_frontend_events() should be periodically called in the main loop to poll and handle
frontend events, including input, mouse motion, and resize events.
*/

/*
============================
Includes
============================
*/

#include "handle_frontend_events.h"

#include <whist/logging/logging.h>
#include <whist/core/whist_string.h>
#include "sdl_utils.h"
#include "audio.h"
#include "client_utils.h"
#include "network.h"
#include <whist/utils/atomic.h>
#include <whist/debug/debug_console.h>

// Main state variables
extern bool client_exiting;
extern MouseMotionAccumulation mouse_state;

// This variable represents whether there is an active pinch gesture
bool active_pinch = false;

// This variable represents whether or not we are actively sending momentum scrolls
//     On MOMENTUM_BEGIN, we set this to true, and start sending momentum scrolls
//     On MOMENTUM_END, or on other non-scroll input, we set this to false, and ignore future
//     momentum scrolls.
//     This is primarily to make cmd keypresses kill momentum scrolls, otherwise
//     scroll + a later cmd causes an unintentional cmd+scroll zoom event
bool active_momentum_scroll = false;

/*
============================
Private Globals
============================
*/

atomic_int g_pending_audio_device_update = ATOMIC_VAR_INIT(0);

/*
============================
Private Functions Declarations
============================
*/

static int handle_frontend_event(WhistFrontend* frontend, WhistFrontendEvent* event);

/*
============================
Public Function Implementations
============================
*/

bool handle_frontend_events(WhistFrontend* frontend, int timeout_ms) {
    WhistFrontendEvent event;

    // Wait for up to the timeout until we get an event.
    bool got_event = whist_frontend_wait_event(frontend, &event, timeout_ms);
    if (!got_event) {
        // No events within the timeout.
        return true;
    }
    // Handle that event then fetch and handle any more events which are
    // available without waiting further.
    do {
        if (handle_frontend_event(frontend, &event) != 0) {
            return false;
        }
        got_event = whist_frontend_poll_event(frontend, &event);
    } while (got_event);

    // After handle_sdl_event potentially captures a mouse motion,
    // We throttle it down to only update once every 0.5ms
    static WhistTimer mouse_motion_timer;
    static bool first_mouse_motion = true;
    if (first_mouse_motion || get_timer(&mouse_motion_timer) * MS_IN_SECOND > 0.5) {
        if (update_mouse_motion(frontend) != 0) {
            return false;
        }
        start_timer(&mouse_motion_timer);
        first_mouse_motion = false;
    }

    return true;
}

bool pending_audio_device_update(void) {
    // Mark as no longer pending "0",
    // and return whether or not it was pending since the last time we called this function
    int pending_audio_device_update = atomic_exchange(&g_pending_audio_device_update, 0);
    return pending_audio_device_update > 0;
}

/*
============================
Private Function Implementations
============================
*/

static void handle_keypress_event(FrontendKeypressEvent* event) {
    WhistClientMessage msg = {0};
    msg.type = MESSAGE_KEYBOARD;
    msg.keyboard.code = event->code;
    msg.keyboard.pressed = event->pressed;
    msg.keyboard.mod = event->mod;
    send_wcmsg(&msg);
}

static void handle_mouse_motion_event(FrontendMouseMotionEvent* event) {
    mouse_state.x_nonrel = event->absolute.x;
    mouse_state.y_nonrel = event->absolute.y;
    mouse_state.x_rel += event->relative.x;
    mouse_state.y_rel += event->relative.y;
    mouse_state.is_relative = event->relative_mode;
    mouse_state.update = true;
}

static void handle_mouse_button_event(FrontendMouseButtonEvent* event) {
    WhistClientMessage msg = {0};
    msg.type = MESSAGE_MOUSE_BUTTON;
    msg.mouseButton.button = event->button;
    msg.mouseButton.pressed = event->pressed;
    send_wcmsg(&msg);
}

static void handle_mouse_wheel_event(FrontendMouseWheelEvent* event) {
    if (active_pinch) {
        // Suppress scroll events during a pinch gesture
        return;
    }

    WhistMouseWheelMomentumType momentum_phase = event->momentum_phase;
    if (momentum_phase == MOUSEWHEEL_MOMENTUM_BEGIN) {
        active_momentum_scroll = true;
    } else if (momentum_phase == MOUSEWHEEL_MOMENTUM_END ||
               momentum_phase == MOUSEWHEEL_MOMENTUM_NONE) {
        active_momentum_scroll = false;
    }

    // We received another event while momentum was active, so cancel the
    // momentum scroll by ignoring the event
    if (!active_momentum_scroll && momentum_phase == MOUSEWHEEL_MOMENTUM_ACTIVE) {
        return;
    }

    // On Linux, Chrome scrolls much less than on other platforms when
    // the mouse wheel is turned.  So, if we want the client to appear
    // the same as Chrome on the host platform we need to modify the
    // scroll amount.

    // On Windows and Linux when a mouse wheel with discrete steps is
    // turned the OS returns the number of notches it moved by.  This
    // can then be mapped to an amount to scroll by.  For a wheel
    // without steps (or a different input, like a scroll gesture on a
    // touchpad), a turn gets mapped to a fraction in 120ths of that,
    // but the top-level units stay the same.  So, to get the Windows
    // client behaviour right with a Linux server we multiply the scroll
    // amount by a constant to make the size of the scroll event which
    // will be generated on the server.  The constant here is 1.9
    // (rounded to 2 when dealing with discrete scroll only), which
    // comes from other people fixing this up to get the Windows scroll
    // speed on Linux Chrome natively; see a Chrome extension doing
    // this at <https://github.com/Dedas/linux-scroll-speed-fix>.  If
    // Chrome ever changes this behaviour to make the platforms match
    // then we will need to adjust the value here.

    // On macOS, the OS returns values which indicate an exact number of
    // pixels to scroll rather than anything derived from mouse wheel
    // steps.  In our SDL code this is already mapped to a number in
    // wheel steps which matches what needs to be given to Linux Chrome,
    // so we don't modify the value further here.

    WhistClientMessage msg = {
        .type = MESSAGE_MOUSE_WHEEL,
        .mouseWheel =
            {
#ifdef _WIN32
                .x = 2 * event->delta.x,
                .y = 2 * event->delta.y,
                .precise_x = 1.9f * event->precise_delta.x,
                .precise_y = 1.9f * event->precise_delta.y,
#else
                .x = event->delta.x,
                .y = event->delta.y,
                .precise_x = event->precise_delta.x,
                .precise_y = event->precise_delta.y,
#endif
            },
    };

    send_wcmsg(&msg);
}

static void handle_gesture_event(FrontendGestureEvent* event) {
    WhistClientMessage msg = {0};
    msg.type = MESSAGE_MULTIGESTURE;
    msg.multigesture = (WhistMultigestureMessage){
        .d_theta = event->delta.theta,
        .d_dist = event->delta.dist,
        .x = event->center.x,
        .y = event->center.y,
        .num_fingers = event->num_fingers,
        .active_gesture = active_pinch,
        .gesture_type = event->type,
    };

    if (event->type == MULTIGESTURE_PINCH_OPEN || event->type == MULTIGESTURE_PINCH_CLOSE) {
        active_pinch = true;
    } else if (active_pinch) {
        active_pinch = false;
        msg.multigesture.gesture_type = MULTIGESTURE_CANCEL;
    }
    send_wcmsg(&msg);
}

static void handle_file_drop_event(WhistFrontend* frontend, FrontendFileDropEvent* event) {
    if (event->end_drop) {
        // If end_drop is set, this indicates the end of a series of file drop events
        //     corresponding to a single drop.
        file_synchronizer_end_type_group(FILE_TRANSFER_SERVER_DROP);
        return;
    }

    FileEventInfo drop_info;
    int dpi = whist_frontend_get_window_dpi(frontend);

    // Scale the drop coordinates for server-side compatibility
    drop_info.server_drop.x = event->position.x * dpi / 96;
    drop_info.server_drop.y = event->position.y * dpi / 96;
    // TODO: Clear any state from a stale file drag event
    file_synchronizer_set_file_reading_basic_metadata(event->filename, FILE_TRANSFER_SERVER_DROP,
                                                      &drop_info);
    free(event->filename);
}

static void handle_file_drag_event(WhistFrontend* frontend, FrontendFileDragEvent* event) {
    size_t data_len = 0;
    if (event->filename) {
        data_len = strlen((const char*)event->filename) + 1;
    }
    WhistClientMessage* msg = safe_malloc(sizeof(WhistClientMessage) + data_len);
    memset(msg, 0, sizeof(WhistClientMessage) + data_len);
    msg->type = CMESSAGE_FILE_DRAG;

    // The event->position.{x,y} values are in screen coordinates, so we
    // would need to multiply by output_{width,height} and divide by
    // window_{width,height} to know where to draw the file drag overlay
    if (event->end_drag) {
        // Handle the drag end case (this either means the drag has ended or the drag has
        // left the window)
        msg->file_drag_data.drag_state = END_DRAG;
    } else if (event->filename) {
        // When filename is set, the drag is starting.
        //     This means that filename is only sent with the message to the
        //     server on the drag starting message. All other messages don't
        //     have a file list and only send coordinates.
        safe_strncpy(msg->file_drag_data.filename, event->filename, data_len);
        msg->file_drag_data.drag_state = START_DRAG;
    } else {
        // In all other cases, the drag is in the middle of moving
        msg->file_drag_data.drag_state = IN_DRAG;
    }

    msg->file_drag_data.group_id = event->group_id;

    int dpi = whist_frontend_get_window_dpi(frontend);
    msg->file_drag_data.x = event->position.x * dpi / 96;
    msg->file_drag_data.y = event->position.y * dpi / 96;

    if (event->filename) {
        free(event->filename);
    }

    send_wcmsg(msg);
    free(msg);
}

static void handle_open_url_event(WhistFrontend* frontend, FrontendOpenURLEvent* event) {
    // Send any new URL to the server
    const size_t data_len = strlen(event->url) + 1;
    WhistClientMessage* msg = safe_malloc(sizeof(WhistClientMessage) + data_len);
    memset(msg, 0, sizeof(WhistClientMessage) + data_len);
    msg->type = MESSAGE_OPEN_URL;
    memcpy(&msg->urls_to_open, event->url, data_len);
    send_wcmsg(msg);
    free(msg);

    free(event->url);

    // Unmimimize the window if needed
    if (!whist_frontend_is_any_window_visible(frontend)) {
        whist_frontend_restore_window(frontend, 0);
    }
}

static void handle_quit_event(FrontendQuitEvent* event) {
    if (event->quit_application) {
        const char* quit_client_app_notification = "QUIT_APPLICATION";
        LOG_INFO("%s", quit_client_app_notification);
    }

    LOG_INFO("The user triggered a Quit event! WhistClient is now Quitting...");
    client_exiting = true;
}

static int handle_frontend_event(WhistFrontend* frontend, WhistFrontendEvent* event) {
    if (event->type != FRONTEND_EVENT_MOUSE_WHEEL && event->type != FRONTEND_EVENT_INTERRUPT) {
        // Cancel momentum scrolls on external non-wheel events.
        active_momentum_scroll = false;
    }

    switch (event->type) {
        case FRONTEND_EVENT_RESIZE: {
            sdl_renderer_resize_window(frontend, event->resize.width, event->resize.height);
            break;
        }
        case FRONTEND_EVENT_VISIBILITY: {
            if (event->visibility.visible) {
                LOG_INFO("Window now visible -- start streaming");
                WhistClientMessage wcmsg = {0};
                wcmsg.type = MESSAGE_START_STREAMING;
                send_wcmsg(&wcmsg);
            } else {
                LOG_INFO("Window now hidden -- stop streaming");
                WhistClientMessage wcmsg = {0};
                wcmsg.type = MESSAGE_STOP_STREAMING;
                send_wcmsg(&wcmsg);
            }
            break;
        }
        case FRONTEND_EVENT_AUDIO_UPDATE: {
            // Mark the audio device as pending an update
            atomic_fetch_or(&g_pending_audio_device_update, 1);
            break;
        }
        case FRONTEND_EVENT_KEYPRESS: {
            handle_keypress_event(&event->keypress);
            break;
        }
        case FRONTEND_EVENT_MOUSE_MOTION: {
            handle_mouse_motion_event(&event->mouse_motion);
            break;
        }
        case FRONTEND_EVENT_MOUSE_BUTTON: {
            handle_mouse_button_event(&event->mouse_button);
            break;
        }
        case FRONTEND_EVENT_MOUSE_WHEEL: {
            handle_mouse_wheel_event(&event->mouse_wheel);
            break;
        }
        case FRONTEND_EVENT_MOUSE_LEAVE: {
            break;
        }
        case FRONTEND_EVENT_GESTURE: {
            handle_gesture_event(&event->gesture);
            break;
        }
        case FRONTEND_EVENT_FILE_DROP: {
            handle_file_drop_event(frontend, &event->file_drop);
            break;
        }
        case FRONTEND_EVENT_FILE_DRAG: {
            handle_file_drag_event(frontend, &event->file_drag);
            break;
        }
        case FRONTEND_EVENT_OPEN_URL: {
            handle_open_url_event(frontend, &event->open_url);
            break;
        }
        case FRONTEND_EVENT_STARTUP_PARAMETER: {
            // We manually handle these during startup, but
            // must free caller-managed data from the event.
            free(event->startup_parameter.key);
            free(event->startup_parameter.value);
            break;
        }
        case FRONTEND_EVENT_QUIT: {
            handle_quit_event(&event->quit);
            break;
        }
        case FRONTEND_EVENT_INTERRUPT:
        case FRONTEND_EVENT_UNHANDLED: {
            break;
        }
    }
    return 0;
}
