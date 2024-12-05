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

#include "capture.h"

/*
============================
Custom Types
============================
*/
typedef struct {
    CaptureDeviceImpl base;

    Display* display;
    XImage* image;
    XShmSegmentInfo segment;
    Window root;
    int counter;
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

CaptureDeviceImpl* create_x11_capture_device(CaptureDeviceInfos* infos, const char* display);

#endif  // CAPTURE_X11CAPTURE_H
