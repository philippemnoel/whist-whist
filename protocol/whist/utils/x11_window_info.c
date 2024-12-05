/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file x11_window_info.c
 * @brief This file contains all the code for getting and setting X11-specific window information.
============================
Usage
============================

Create a capture device for the display whose windows you want to work with. To get the active
window or a list of windows for the client to display, call get_active_window or get_valid_windows,
respectively; pass the resulting WhistWindows to the getter/setter functions here.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/core/whist_string.h>
#include "window_info.h"
#include <locale.h>
#include <whist/video/capture/x11capture.h>
#include <whist/video/capture/nvx11capture.h>

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <stdbool.h>

// See the EWMH specification; for ease of reading
#define _NET_WM_STATE_REMOVE 0
#define _NET_WM_STATE_ADD 1
#define _NET_WM_STATE_TOGGLE 2

static Display* display;

/*
============================
Private Functions
============================
*/

/**
 * @brief                          Helper function to call XGetWindowProperty.
 *
 * @param device                   Device whose display we are using
 *
 * @param w                        Window to get property for
 *
 * @param property                 Property to get
 *
 * @param req_type                 Required type (can pass in AnyPropertyType if N/A)
 *
 * @param nitems_return            Returns number of items stored in prop_return, will be nontrivial
 * if the property is an array
 *
 * @param prop_return              Returns data for specified property
 *
 * @returns                        Whether XGetWindowProperty was successful
 */
bool x11_get_window_property(X11CaptureDevice* device, Window w, Atom property, Atom req_type,
                             unsigned long* nitems_return, unsigned char** prop_return);

/**
 * @brief                          Helper function to call XSendEvent on a root message, as
 * described in the EWMH spec.
 *
 * @param device                   Device whose display we are using
 *
 * @param event_send               The XEvent to send
 */
bool send_message_to_root(X11CaptureDevice* device, XEvent* event_send);

/**
 * @brief                          Helper function to create an XEvent that requests a window state
 * change, e.g. minimize/maximize/fullscreen, as described in the EWMH spec
 *
 * @param device                   Device whose display we are using
 *
 * @param w                        The window whose state we want to change
 *
 * @param action                   The action to perform (set/unset/toggle)
 *
 * @param property1                First property to perform action on
 *
 * @param property2                Second property to perform action on. This must be 0 if there is
 * no second property to change
 *
 * @return                         An XEvent that can then be passed to send_message_to_root for the
 * state change
 */
XEvent create_change_state_message(X11CaptureDevice* device, Window w, long action, Atom property1,
                                   Atom property2);

/**
 * @brief                          Returns the window name of the specified window
 *
 * @param device                   Device whose display we are using
 *
 * @param w                        The window whose name we want
 *
 * @returns                        UTF-8 string for window name
 */
char* get_window_name(X11CaptureDevice* device, Window w);

/**
 * @brief                          Helper for recursive get_valid_windows calls, since
 * get_valid_windows recursively searches the window tree
 *
 * @param device                   Device whose display we are using
 *
 * @param list                     Linked list to append children of curr who are valid windows
 *
 * @param curr                     Current position in the X11 window tree
 */
void get_valid_windows_helper(X11CaptureDevice* device, LinkedList* list, Window curr);

static int handler(Display* disp, XErrorEvent* error);

bool is_window_fullscreen(X11CaptureDevice* device, WhistWindow whist_window);

static X11CaptureDevice* get_x11_capture_device(CaptureDeviceImpl* impl) {
    switch (impl->device_type) {
        case X11_DEVICE:
            return (X11CaptureDevice*)impl;
        case NVX11_DEVICE:
            return nvx11_capture_get_x11(impl);
        default:
            return NULL;
    }
}

/*
============================
Public Function Implementations
============================
*/

void init_window_info_getter(void) {
    if (display == NULL) {
        display = XOpenDisplay(NULL);
    }
    XSetErrorHandler(handler);
}

void get_valid_windows(CaptureDevice* capture_device, LinkedList* list) {
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);
    get_valid_windows_helper(device, list, device->root);
}

void get_window_attributes(CaptureDevice* capture_device, WhistWindow* whist_window) {
    Window w = (Window)whist_window->id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);
    XWindowAttributes attr;
    if (!XGetWindowAttributes(device->display, w, &attr)) {
        LOG_ERROR("Failed to get window %lu attributes!", w);
        whist_window->width = -1;
        whist_window->height = -1;
        whist_window->x = -1;
        whist_window->y = -1;
        return;
    }
    whist_window->width = attr.width;
    whist_window->height = attr.height;
    whist_window->x = attr.x;
    whist_window->y = attr.y;
}

WhistWindow get_active_window(CaptureDevice* capture_device) {
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);
    WhistWindow w;
    static unsigned long nitems;
    static unsigned char* result;  // window stored here

    if (x11_get_window_property(device, device->root, device->_NET_ACTIVE_WINDOW, AnyPropertyType,
                                &nitems, &result) &&
        *(unsigned long*)result != 0) {
        Window active_window = (Window) * (unsigned long*)result;
        // XFree(result);
        w.id = (unsigned long)active_window;
    } else {
        // revert to XGetInputFocus
        Window focus;
        int revert;
        XGetInputFocus(device->display, &focus, &revert);
        if (focus != PointerRoot) {
            w.id = (unsigned long)focus;
        } else {
            LOG_INFO("No active window found, setting root as active");
            w.id = (unsigned long)device->root;
        }
    }
    return w;
}

void minimize_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent =
        create_change_state_message(device, w, _NET_WM_STATE_ADD, device->_NET_WM_STATE_HIDDEN, 0);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to minimize window %lu", w);
    }
}

void unminimize_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    // I'm not sure what will happen if you call this on a window that's already not minimized --
    // hopefully a noop
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent = create_change_state_message(device, w, _NET_WM_STATE_REMOVE,
                                                device->_NET_WM_STATE_HIDDEN, 0);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to unminimize window %lu", w);
    }
}

void maximize_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent = create_change_state_message(device, w, _NET_WM_STATE_ADD,
                                                device->_NET_WM_STATE_MAXIMIZED_VERT,
                                                device->_NET_WM_STATE_MAXIMIZED_HORZ);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to maximize window %lu", w);
    }
}

void fullscreen_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent = create_change_state_message(device, w, _NET_WM_STATE_ADD,
                                                device->_NET_WM_STATE_FULLSCREEN, 0);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to fullscreen window %lu", w);
    }
}

void unfullscreen_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent = create_change_state_message(device, w, _NET_WM_STATE_REMOVE,
                                                device->_NET_WM_STATE_FULLSCREEN, 0);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to unfullscreen window %lu", w);
    }
}

void bring_window_to_top(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent =
        create_change_state_message(device, w, _NET_WM_STATE_ADD, device->_NET_WM_STATE_ABOVE, 0);
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to bring window %lu to top", w);
    }
}

bool is_window_fullscreen(X11CaptureDevice* device, WhistWindow whist_window) {
    /*
     * Query whether the given window is fullscreen or not.
     *
     *  Return:
     *      (bool): 0 if not fullscreen, 1 if fullscreen.
     *
     *  Note:
     *      By "fullscreen", we mean that the window is rendering directly
     *      to the screen, not through the window manager. Examples include
     *      fullscreen video playback in a browser, or fullscreen games.
     */

    Window w = (Window)whist_window.id;
    static unsigned long nitems;
    static unsigned char* states;  // name stored here
    if (x11_get_window_property(device, w, device->_NET_WM_STATE, device->ATOM_ARRAY, &nitems,
                                &states)) {
        Atom* state_hints = (Atom*)states;
        for (int i = 0; i < (int)nitems; i++) {
            if (state_hints[i] == device->_NET_WM_STATE_FULLSCREEN) {
                return true;
            }
        }
        return false;
    }
    LOG_ERROR("Couldn't get states, assuming window is not fullscreen!");
    return false;
}

// superseded by x11_get_active_window
static Window get_focused_window(void) {
    Window w;
    // TODO: Is this needed, or can we pass in NULL?
    int revert;
    XGetInputFocus(display, &w, &revert);
    return w;
}

bool is_focused_window_fullscreen(void) {
    /*
     * Query whether the focused window is fullscreen or not.
     *
     *  Return:
     *      (bool): 0 if not fullscreen, 1 if fullscreen.
     *
     *  Note:
     *      By "fullscreen", we mean that the window is rendering directly
     *      to the screen, not through the window manager. Examples include
     *      fullscreen video playback in a browser, or fullscreen games.
     */
    Window w = get_focused_window();

    if (!w) {
        // No window is active.
        return false;
    }

    Atom state = XInternAtom(display, "_NET_WM_STATE", True);
    Atom fullscreen = XInternAtom(display, "_NET_WM_STATE_FULLSCREEN", True);
    Atom actual_type;
    int format;
    unsigned long num_items, bytes_after;
    uint8_t* data = NULL;
    int res = XGetWindowProperty(display, w, state, 0, ~0L, False, AnyPropertyType, &actual_type,
                                 &format, &num_items, &bytes_after, &data);
    if (res == Success && data) {
        Atom windowprop = ((Atom*)data)[0];
        if (windowprop == fullscreen) {
            return true;
        } else {
            return false;
        }
    }
    return false;
}

void destroy_window_info_getter(void) {
    if (display != NULL) {
        XCloseDisplay(display);
        display = NULL;
    }
}

void move_resize_window(CaptureDevice* capture_device, WhistWindow whist_window, int x, int y,
                        int width, int height) {
    // note: _NET_MOVERESIZE_WINDOW is not supported
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);
    XMoveResizeWindow(device->display, w, x, y, width, height);
}

void close_window(CaptureDevice* capture_device, WhistWindow whist_window) {
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = get_x11_capture_device(capture_device->impl);

    XEvent xevent = {0};
    XClientMessageEvent xclient = {0};
    xclient.type = ClientMessage;
    xclient.window = w;
    xclient.display = device->display;
    xclient.message_type = device->_NET_CLOSE_WINDOW;
    xclient.format = 32;
    xclient.data.l[0] = 0;  // timestamp: not sure if it should be 0
    xclient.data.l[1] = 0;  // source: 0 for now
    xclient.data.l[2] = 0;
    xclient.data.l[3] = 0;
    xclient.data.l[4] = 0;
    xevent.xclient = xclient;
    if (!send_message_to_root(device, &xevent)) {
        LOG_ERROR("Failed to send message to close window %lu", w);
    }
}

bool is_window_resizable(CaptureDevice* capture_device, WhistWindow whist_window) {
    // for now, because awesome doesn't support _NET_WM_ALLOWED_ACTIONS
    return true;
    /*
    Window w = (Window)whist_window.id;
    X11CaptureDevice* device = capture_device->x11_capture_device;
    static unsigned long nitems;
    static unsigned char* actions;  // name stored here

    if (x11_get_window_property(device, w, device->_NET_WM_ALLOWED_ACTIONS, device->ATOM_ARRAY,
                                &nitems, &actions)) {
        Atom* allowed_actions = (Atom*)actions;
        // check if one of the actions is net_wm_action_resize
        for (int i = 0; i < (int)nitems; i++) {
            if (allowed_actions[i] == device->_NET_WM_ACTION_RESIZE) {
                return true;
            }
        }
        return false;
    }
    LOG_ERROR("Couldn't get allowed actions, assuming window is resizable!");
    return true;
    */
}

/*
============================
Private Function Implementations
============================
*/

static int handler(Display* disp, XErrorEvent* error) {
    // ignore BadWindow because windows can be deleted while calling XQueryTree
    if (error->error_code == BadWindow) {
        return 0;
    }
    static int buflen = 128;
    char buffer[buflen];
    XGetErrorText(disp, error->error_code, buffer, buflen);
    LOG_ERROR("X11 Error: %d (%s), major opcode %d, minor opcode %d", error->error_code, buffer,
              error->request_code, error->minor_code);
    return 0;
}

void get_valid_windows_helper(X11CaptureDevice* device, LinkedList* list, Window curr) {
    Window parent;
    Window* children;
    unsigned int nchildren;
    if (XQueryTree(device->display, curr, &device->root, &parent, &children, &nchildren) != 0) {
        char* window_name = get_window_name(device, curr);
        // check the dimensions of each window
        XWindowAttributes attr;
        XGetWindowAttributes(device->display, curr, &attr);
        if (attr.x >= 0 && attr.y >= 0 && attr.width >= MIN_SCREEN_WIDTH &&
            attr.height >= MIN_SCREEN_HEIGHT && window_name != NULL && *window_name != '\0') {
            WhistWindow* valid_window = safe_malloc(sizeof(WhistWindow));
            valid_window->id = (unsigned long)curr;
            valid_window->width = attr.width;
            valid_window->height = attr.height;
            // x/y are relative to parent, so we need to add x/y to parent's x/y
            XWindowAttributes parent_attr;
            XGetWindowAttributes(device->display, parent, &parent_attr);
            valid_window->x = attr.x + parent_attr.x;
            valid_window->y = attr.y + parent_attr.y;
            valid_window->is_fullscreen = is_window_fullscreen(device, *valid_window);
            valid_window->has_titlebar = false;
            linked_list_add_tail(list, valid_window);
        }

        if (nchildren != 0) {
            for (unsigned int i = 0; i < nchildren; i++) {
                get_valid_windows_helper(device, list, children[i]);
            }
        }
    }
}

// wrapper around XGetWindowProperty, returns whether successful or not
bool x11_get_window_property(X11CaptureDevice* device, Window w, Atom property, Atom req_type,
                             unsigned long* nitems_return, unsigned char** prop_return) {
    static Atom actual_type;
    static int actual_format;
    static unsigned long bytes_after;
    return (XGetWindowProperty(
                device->display, w, property, 0,
                65536,  // the internet doesn't seem to agree on what long_length should be
                False, req_type, &actual_type, &actual_format, nitems_return, &bytes_after,
                prop_return) == Success);
}

// utf-8 string; helper (processed by something like get_focused_window_name with the
// last_valid_title and everything)
char* get_window_name(X11CaptureDevice* device, Window w) {
    // first try using EWMH
    static unsigned long nitems;
    static unsigned char* name;  // name stored here

    if (x11_get_window_property(device, w, device->_NET_WM_NAME, device->UTF8_STRING, &nitems,
                                &name)) {
        return (char*)name;
    }
    // fall back to XGetWMName
    XTextProperty prop;
    // unclear if needed
    XLockDisplay(device->display);
    Status s = XGetWMName(device->display, w, &prop);
    if (s) {
        int count = 0;
        char** list = NULL;
        int result = XmbTextPropertyToTextList(device->display, &prop, &list, &count);
        XUnlockDisplay(device->display);
        if (count && result == Success) {
            return list[0];
        } else {
            return "";
        }
    } else {
        XUnlockDisplay(device->display);
        return "";
    }
}

// HELPER
bool send_message_to_root(X11CaptureDevice* device, XEvent* event_send) {
    return (XSendEvent(device->display, device->root, False,
                       SubstructureNotifyMask | SubstructureRedirectMask, event_send) == Success);
}

// HELPER
XEvent create_change_state_message(X11CaptureDevice* device, Window w, long action, Atom property1,
                                   Atom property2) {
    XEvent xevent = {0};
    XClientMessageEvent xclient = {0};
    xclient.type = ClientMessage;
    xclient.window = w;
    xclient.display = device->display;
    xclient.message_type = device->_NET_WM_STATE;
    xclient.format = 32;
    xclient.data.l[0] = action;           // timestamp: not sure if it should be 0
    xclient.data.l[1] = (long)property1;  // source: 0 for now
    xclient.data.l[2] = (long)property2;
    xclient.data.l[3] = 0;
    xclient.data.l[4] = 0;
    xevent.xclient = xclient;
    return xevent;
}
