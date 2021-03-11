#ifndef CAPTURE_X11CAPTURE_H
#define CAPTURE_X11CAPTURE_H
/**
 * Copyright Fractal Computers, Inc. 2021
 * @file x11capture.h
 * @brief This file contains the code to do screen capture in the GPU on Linux
 *        Ubuntu.
============================
Usage
============================

CaptureDevice contains all the information used to interface with the X11 screen
capture API and the data of a frame.

Call CreateCaptureDevice to initialize at the beginning of the program, and
DestroyCaptureDevice to clean up at the end of the program. Call CaptureScreen
to capture a frame.

You must release each frame you capture via ReleaseScreen before calling
CaptureScreen again.
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

#include <fractal/core/fractal.h>
#include "x11nvidiacapture.h"

/*
============================
Custom Types
============================
*/

typedef struct CaptureDevice {
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
    bool texture_on_gpu;
    bool released;
    bool using_nvidia;
    NvidiaCaptureDevice nvidia_capture_device;
    bool capture_is_on_nvidia;
} CaptureDevice;

typedef unsigned int UINT;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Create a XSHM screen capture device
 *
 * @param device                   Capture device struct to hold the capture
 *                                 device
 * @param width                    Width of the screen to capture, in pixels
 * @param height                   Height of the screen to capture, in pixels
 * @param dpi                      Dots per inch of the screen, in pixels
 * @param bitrate                  Bitrate (used for create_nvidia_capture_device)
 * @param codec                    Codec (used for create_nvidia_capture_device)
 *
 * @returns                        0 if succeeded, else -1
 */
int create_capture_device(CaptureDevice* device, UINT width, UINT height, UINT dpi, int bitrate,
                          CodecType codec);

/**
 * @brief                          Capture a bitmap snapshot of the screen, in
 *                                 the GPU, using XSHM
 *
 * @param device                   The device used to capture the screen
 *
 * @returns                        0 if succeeded, else -1
 */
int capture_screen(CaptureDevice* device);

/**
 * @brief                          Dummy function for API compatibility.
 *
 * @param device                   The device used to capture the screen
 *
 * @returns                        0 always
 */
int transfer_screen(CaptureDevice* device);

/**
 * @brief                          Release a captured screen bitmap snapshot
 *
 * @param device                   The Linux screencapture device holding the
 *                                 screen object captured
 */
void release_screen(CaptureDevice* device);

/**
 * @brief                          Destroys and frees the memory of a Linux
 *                                 Ubuntu screencapture device
 *
 * @param device                   The Linux Ubuntu screencapture device to
 *                                 destroy and free the memory of
 */
void destroy_capture_device(CaptureDevice* device);

/**
 * @brief                          Updates the capture device if the capture device is also the
 *                                 encoder
 *
 * @param device                   The Linux Ubuntu screencapture device
 * @param bitrate                  The new bitrate to use for encoding
 * @param codec                    The new codec to use for encoding
 *
 * @returns                        True if the capture device was indeed updated as an encoder
 */
void update_capture_encoder(CaptureDevice* device, int bitrate, CodecType codec);

#endif  // CAPTURE_X11CAPTURE_H
