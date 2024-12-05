/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file x11capture.c
 * @brief This file contains the code to do screen capture via the X11 API
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
#include "x11capture.h"
#include "../cudacontext.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <X11/Xlib.h>
#include <X11/extensions/Xdamage.h>
#include <X11/Xutil.h>

/*
============================
Private Functions Declarations
============================
*/

/*
 * @brief           Our custom error handler, that passes to LOG_ERROR
 *
 * @param display   The X11 Display
 *
 * @param error     The X11 Error Event
 */
int handler(Display* display, XErrorEvent* error);

/*
 * @brief           Initialize the X11 atoms
 *
 * @param device    The X11 Device
 */
void init_atoms(X11CaptureDevice* device);

/*
============================
Private Function Implementations
============================
*/
int handler(Display* display, XErrorEvent* error) {
    LOG_ERROR("X11 Error: %d", error->error_code);
    return 0;
}

#define INIT_ATOM(DEVICE, ATOM_VAR, NAME)                        \
    DEVICE->ATOM_VAR = XInternAtom(DEVICE->display, NAME, True); \
    if (DEVICE->ATOM_VAR == None) {                              \
        LOG_FATAL("XInternAtom failed to return atom %s", NAME); \
    }

void init_atoms(X11CaptureDevice* device) {
    // Initialize all atoms for the device->display we need
    // If an XInternAtom call fails, we'll LOG_ERROR and any calls using that atom will do nothing
    INIT_ATOM(device, _NET_ACTIVE_WINDOW, "_NET_ACTIVE_WINDOW");
    INIT_ATOM(device, _NET_WM_STATE_HIDDEN, "_NET_WM_STATE_HIDDEN");
    INIT_ATOM(device, _NET_WM_STATE_MAXIMIZED_VERT, "_NET_WM_STATE_MAXIMIZED_VERT");
    INIT_ATOM(device, _NET_WM_STATE_MAXIMIZED_HORZ, "_NET_WM_STATE_MAXIMIZED_HORZ");
    INIT_ATOM(device, _NET_WM_STATE_FULLSCREEN, "_NET_WM_STATE_FULLSCREEN");
    INIT_ATOM(device, _NET_WM_STATE_ABOVE, "_NET_WM_STATE_ABOVE");
    INIT_ATOM(device, _NET_CLOSE_WINDOW, "_NET_CLOSE_WINDOW");
    INIT_ATOM(device, _NET_WM_NAME, "_NET_WM_NAME");
    INIT_ATOM(device, UTF8_STRING, "UTF8_STRING");
    INIT_ATOM(device, _NET_WM_STATE, "_NET_WM_STATE");
}

/*
============================
Public Function Implementations
============================
*/

static int x11_update_dimensions(X11CaptureDevice* device, int width, int height, int dpi) {
    /*
       Using XRandR, try updating the device's display to the given width, height, and DPI. Even if
       this fails to set the dimensions, device->width and device->height will always equal the
       actual dimensions of the screen.

        Arguments:
            device (CaptureDevice*): pointer to the device whose window we're resizing
            width (uint32_t): desired width
            height (uint32_t): desired height
            dpi (uint32_t): desired DPI
    */

    CaptureDeviceInfos* infos = device->base.infos;
    char cmd[1024];

    // If the device's width/height must be updated
    if (infos->width != width || infos->height != height) {
        char modename[128];

        snprintf(modename, sizeof(modename), "Whist-%dx%d", width, height);

        char* display_name;
        runcmd("xrandr --current | grep \" connected\"", &display_name);
        *strchr(display_name, ' ') = '\0';

        snprintf(cmd, sizeof(cmd), "xrandr --delmode %s %s", display_name, modename);
        runcmd(cmd, NULL);
        snprintf(cmd, sizeof(cmd), "xrandr --rmmode %s", modename);
        runcmd(cmd, NULL);
        double pixel_clock = 60.0 * (width + 24) * (height + 24);
        snprintf(cmd, sizeof(cmd), "xrandr --newmode %s %.2f %d %d %d %d %d %d %d %d +hsync +vsync",
                 modename, pixel_clock / 1000000.0, width, width + 8, width + 16, width + 24,
                 height, height + 8, height + 16, height + 24);
        runcmd(cmd, NULL);
        snprintf(cmd, sizeof(cmd), "xrandr --addmode %s %s", display_name, modename);
        runcmd(cmd, NULL);
        snprintf(cmd, sizeof(cmd), "xrandr --output %s --mode %s", display_name, modename);
        runcmd(cmd, NULL);

        free(display_name);

        infos->width = width;
        infos->height = height;
    }

    // This script must be built in to the Mandelbox. It writes new DPI for X11 and
    // AwesomeWM, and uses SIGHUP to XSettingsd to trigger application and window
    // manager refreshes to use the new DPI.
    if (dpi && dpi != infos->dpi) {
        snprintf(cmd, sizeof(cmd), "/usr/share/whist/update-whist-dpi.sh %d", dpi);
        runcmd(cmd, NULL);
        infos->dpi = dpi;
    }

    return 0;
}

static int x11_reconfigure_capture_device(X11CaptureDevice* dev, int width, int height, int dpi) {
    CaptureDeviceInfos* infos = dev->base.infos;
    int ret;

    ret = x11_update_dimensions(dev, width, height, dpi);
    if (ret != 0) return ret;

    if (dev->image) {
        XFree(dev->image);
        dev->image = NULL;
    }

    XWindowAttributes window_attributes;
    if (!XGetWindowAttributes(dev->display, dev->root, &window_attributes)) {
        LOG_ERROR("Error while getting window attributes");
        return -1;
    }

    Screen* screen = window_attributes.screen;

    dev->image =
        XShmCreateImage(dev->display,
                        DefaultVisualOfScreen(screen),  // DefaultVisual(device->display, 0), // Use
                                                        // a correct visual. Omitted for brevity
                        DefaultDepthOfScreen(screen),   // 24,   // Determine correct depth from
                                                        // the visual. Omitted for brevity
                        ZPixmap, NULL, &dev->segment, width, height);

    if (dev->image == NULL) {
        LOG_ERROR("Could not XShmCreateImage!");
        return -2;
    }

    dev->segment.shmid =
        shmget(IPC_PRIVATE, dev->image->bytes_per_line * dev->image->height, IPC_CREAT | 0777);

    dev->segment.shmaddr = dev->image->data = shmat(dev->segment.shmid, 0, 0);
    dev->segment.readOnly = False;

    if (!XShmAttach(dev->display, &dev->segment)) {
        LOG_ERROR("Error while attaching display");
        return -3;
    }

    dev->frame_data = dev->image->data;
    infos->pitch = dev->image->bytes_per_line;

    return 0;
}

static int x11_capture_device_init(X11CaptureDevice* dev, int width, int height, int dpi) {
    return x11_reconfigure_capture_device(dev, width, height, dpi);
}

static int x11_capture_screen(X11CaptureDevice* dev, int* width, int* height, int* pitch) {
    /*
        Capture the screen using our X11 device. TODO: needs more documentation, I (Serina) am not
       really sure what's happening here.

        Arguments:
            device (X11CaptureDevice*): device used to capture the screen

        Returns:
            (int): 0 on success, -1 on failure
    */
    FATAL_ASSERT(dev);

    int accumulated_frames = 0;
    while (XPending(dev->display)) {
        // XDamageNotifyEvent* dev; unused, remove or is this needed and should
        // be used?
        XEvent ev;
        XNextEvent(dev->display, &ev);
        if (ev.type == dev->event + XDamageNotify) {
            // accumulated_frames will eventually be the number of damage events (accumulated
            // frames)
            accumulated_frames++;
        }
    }
    // Don't Lock and UnLock Display unneccesarily, if there are no frames to capture
    if (accumulated_frames == 0) return 0;

    CaptureDeviceInfos* infos = dev->base.infos;
    XLockDisplay(dev->display);
    if (accumulated_frames || dev->first) {
        dev->first = false;

        XDamageSubtract(dev->display, dev->damage, None, None);

        XWindowAttributes window_attributes;
        if (XGetWindowAttributes(dev->display, dev->root, &window_attributes) == 0) {
            LOG_ERROR("Couldn't get window width and height!");
            accumulated_frames = -1;
        } else if (infos->width != window_attributes.width ||
                   infos->height != window_attributes.height) {
            LOG_ERROR("Wrong width/height (infos=%dx%d window=%dx%d) !", infos->width,
                      infos->height, window_attributes.width, window_attributes.height);
            accumulated_frames = -1;
        } else {
            XErrorHandler prev_handler = XSetErrorHandler(handler);
            if (!XShmGetImage(dev->display, dev->root, dev->image, 0, 0, AllPlanes)) {
                LOG_ERROR("Error while capturing the screen");
                accumulated_frames = -1;
            } else {
                *pitch = dev->image->bytes_per_line;
            }
            if (accumulated_frames != -1) {
                // get the color
                XColor c;

                c.pixel = XGetPixel(dev->image, 0, 0);
                XQueryColor(dev->display,
                            DefaultColormap(dev->display, XDefaultScreen(dev->display)), &c);
                // Color format is r/g/b 0x0000-0xffff
                // We just need the leading byte
                dev->corner_color.red = c.red >> 8;
                dev->corner_color.green = c.green >> 8;
                dev->corner_color.blue = c.blue >> 8;
            }
            XSetErrorHandler(prev_handler);
        }
    }
    XUnlockDisplay(dev->display);

    *width = infos->width;
    *height = infos->height;
    return accumulated_frames;
}

static int x11_capture_transfer_screen(X11CaptureDevice* device, CaptureEncoderHints* hints) {
    memset(hints, 0, sizeof(*hints));
    return 0;
}

static int x11_get_dimensions(X11CaptureDevice* device, int* w, int* h, int* dpi) {
    /*
        Get the width and height of the display associated with device, and store them in w and h,
        respectively.

        Arguments:
            device (CaptureDevice*): device containing the display whose dimensions we are getting
            w (int*): pointer to store width
            h (int*): pointer to store hight
    */
    if (!device) return -1;

    XWindowAttributes window_attributes;
    if (!XGetWindowAttributes(device->display, device->root, &window_attributes)) {
        *w = 0;
        *h = 0;
        LOG_ERROR("Error while getting window attributes");
        return -1;
    }

    *w = (uint32_t)window_attributes.width;
    *h = (uint32_t)window_attributes.height;
    *dpi = 0;
    return 0;
}

static int x11_capture_getdata(X11CaptureDevice* device, void** buf, int* stride) {
    *buf = device->frame_data;
    *stride = device->base.infos->pitch;
    return 0;
}

static void x11_destroy_capture_device(X11CaptureDevice** pdev) {
    /*
        Destroy the X11 device and free it.

        Arguments:
            device (X11CaptureDevice*): device to destroy
    */
    X11CaptureDevice* device = *pdev;
    if (device) {
        if (device->image) {
            XFree(device->image);
            device->image = NULL;
        }
        XCloseDisplay(device->display);
    }
    free(device);
    *pdev = NULL;
}

CaptureDeviceImpl* create_x11_capture_device(CaptureDeviceInfos* infos, const char* display) {
    X11CaptureDevice* dev = (X11CaptureDevice*)safe_zalloc(sizeof(X11CaptureDevice));
    CaptureDeviceImpl* impl = &dev->base;
    int ret;

    LOG_INFO("creating x11 capture driver(%s)", display);
    dev->display = XOpenDisplay(display);
    if (!dev->display) {
        LOG_ERROR("ERROR: create_x11_capture_device display did not open");
        goto out_display;
    }

    dev->root = DefaultRootWindow(dev->display);
    if (!dev->root) {
        LOG_ERROR("ERROR: create_x11_capture_device unable to retrieve root window");
        goto out_root;
    }

    ret = x11_get_dimensions(dev, &infos->width, &infos->height, &infos->dpi);
    if (ret < 0) {
        goto out_dimensions;
    }

    int damage_event, damage_error;
    if (!XDamageQueryExtension(dev->display, &damage_event, &damage_error)) goto out_damage_ext;

    dev->damage = XDamageCreate(dev->display, dev->root, XDamageReportRawRectangles);
    if (!dev->damage) goto out_damage_create;

    dev->event = damage_event;
    dev->segment.shmid = -1;
    dev->first = true;

    init_atoms(dev);

    impl->infos = infos;
    impl->init = (CaptureDeviceInitFn)x11_capture_device_init;
    impl->reconfigure = (CaptureDeviceReconfigureFn)x11_reconfigure_capture_device;
    impl->capture_screen = (CaptureDeviceCaptureScreenFn)x11_capture_screen;
    impl->capture_get_data = (CaptureDeviceGetDataFn)x11_capture_getdata;
    impl->transfer_screen = (CaptureDeviceTransferScreenFn)x11_capture_transfer_screen;
    impl->get_dimensions = (CaptureDeviceGetDimensionsFn)x11_get_dimensions;
    impl->destroy = (CaptureDeviceDestroyFn)x11_destroy_capture_device;
    infos->last_capture_device = X11_DEVICE;
    return impl;

out_damage_create:
out_damage_ext:
out_dimensions:
out_root:
    XCloseDisplay(dev->display);
out_display:
    free(dev);
    return NULL;
}
