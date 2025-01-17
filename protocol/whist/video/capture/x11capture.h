#ifndef CAPTURE_X11CAPTURE_H
#define CAPTURE_X11CAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file x11capture.h
 * @brief This file contains the code to do screen capture via the X11 API on Linux Ubuntu.
============================
Usage
============================

X11CaptureDevice contains all the information used to interface with the X11 screen
capture API and the data of a frame. Call create_x11_capture_device to initialize a device,
x11_capture_screen to capture the screen with said device, and destroy_x11_capture_device when done
capturing frames.
*/

/*
============================
Includes
============================
*/

#include <X11/Xlib.h>
#include <X11/extensions/XShm.h>
#include <X11/extensions/Xdamage.h>
#include <stdbool.h>

#include <whist/core/whist.h>
#include <whist/utils/color.h>

/*
============================
Custom Types
============================
*/

/**
 * @brief Struct to handle using X11 for capturing the screen. The screen capture data is saved in
 * frame_data.
 */
typedef struct X11CaptureDevice {
    Display* display;
    XImage* image;
    XShmSegmentInfo segment;
    Window root;
    int counter;
    int width;
    int height;
    int pitch;
    char* frame_data;
    Damage damage;
    int event;
    bool first;
    WhistRGBColor corner_color;
    Atom _NET_ACTIVE_WINDOW;
    Atom _NET_WM_STATE_HIDDEN;
    Atom _NET_WM_STATE_MAXIMIZED_VERT;
    Atom _NET_WM_STATE_MAXIMIZED_HORZ;
    Atom _NET_WM_STATE_FULLSCREEN;
    Atom _NET_WM_STATE_ABOVE;
    Atom _NET_MOVERESIZE_WINDOW;
    Atom _NET_CLOSE_WINDOW;
    Atom _NET_WM_ALLOWED_ACTIONS;
    Atom ATOM_ARRAY;
    Atom _NET_WM_ACTION_RESIZE;
    Atom _NET_WM_NAME;
    Atom UTF8_STRING;
    Atom _NET_WM_STATE;
} X11CaptureDevice;

/*
============================
Public Functions
============================
*/
/**
 * @brief           Create a device for capturing via X11 with the specified width, height, and DPI.
 *
 * @param width     The desired width of the device
 * @param height    The desired height of the device
 * @param dpi       The desired DPI of the device
 *
 * @returns         A pointer to the newly created X11CaptureDevice
 */
X11CaptureDevice* create_x11_capture_device(uint32_t width, uint32_t height, uint32_t dpi);

bool reconfigure_x11_capture_device(X11CaptureDevice* device, uint32_t width, uint32_t height,
                                    uint32_t dpi);

/**
 * @brief           Capture the screen with given device. Afterwards, the frame capture is stored in
 * frame_data.
 *
 * @param device    Device to use for screen captures
 *
 * @returns         0 on success, -1 on failure
 */
int x11_capture_screen(X11CaptureDevice* device);

/**
 * @brief           Destroy the given X11CaptureDevice and free it.
 *
 * @param device    Device to destroy
 */
void destroy_x11_capture_device(X11CaptureDevice* device);

#endif  // CAPTURE_X11CAPTURE_H
