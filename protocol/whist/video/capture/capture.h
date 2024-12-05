#ifndef VIDEO_CAPTURE_H
#define VIDEO_CAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file capture.h
 * @brief This file defines the proper header for capturing the screen depending
 *        on the local OS.
============================
Usage
============================

Toggles automatically between the screen capture files based on OS.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/utils/color.h>
#include <whist/utils/linked_list.h>
#include <whist/video/nvidia-linux/cuda_drvapi_dynlink_cuda.h>
#if OS_IS(OS_LINUX)
#include <X11/Xlib.h>
#endif

/*
============================
Custom Types
============================
*/
typedef struct CaptureDevice CaptureDevice;
typedef struct CaptureDeviceImpl CaptureDeviceImpl;

/** @brief hints returned by the capture device for the encoder */
typedef struct {
    CUcontext cuda_context;
} CaptureEncoderHints;

typedef int (*CaptureDeviceInitFn)(CaptureDeviceImpl* device, int width, int height, int dpi);
typedef int (*CaptureDeviceReconfigureFn)(CaptureDeviceImpl* device, int width, int height,
                                          int dpi);
typedef int (*CaptureDeviceCaptureScreenFn)(CaptureDeviceImpl* device, int* width, int* height,
                                            int* pitch);
typedef int (*CaptureDeviceTransferScreenFn)(CaptureDeviceImpl* device, CaptureEncoderHints* hints);
typedef int (*CaptureDeviceGetDataFn)(CaptureDeviceImpl* device, void** buf, int* stride);
typedef int (*CaptureDeviceGetDimensionsFn)(CaptureDeviceImpl* device, int* width, int* height,
                                            int* dpi);
typedef int (*CaptureDeviceUpdateDimensionsFn)(CaptureDeviceImpl* device, int width, int height,
                                               int dpi);
typedef void (*CaptureDeviceDestroyFn)(CaptureDeviceImpl** device);

/** @brief main info about a capture device */
typedef struct {
    int width;
    int height;
    int dpi;
    int pitch;
    CaptureDeviceType last_capture_device;
} CaptureDeviceInfos;

/** @brief a capture implementation */
struct CaptureDeviceImpl {
    CaptureDeviceType device_type;
    CaptureDeviceInfos* infos;

    CaptureDeviceInitFn init;
    CaptureDeviceReconfigureFn reconfigure;
    CaptureDeviceCaptureScreenFn capture_screen;
    CaptureDeviceTransferScreenFn transfer_screen;
    CaptureDeviceGetDataFn capture_get_data;
    CaptureDeviceGetDimensionsFn get_dimensions;
    CaptureDeviceUpdateDimensionsFn update_dimensions;
    CaptureDeviceDestroyFn destroy;
};

typedef struct CaptureDevice {
    CaptureDeviceInfos infos;
    void* frame_data;
    WhistWindow window_data[MAX_WINDOWS];
    WhistRGBColor corner_color;
    void* internal;
    CaptureDeviceImpl* impl;
} CaptureDevice;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Creates a screen capture device
 *
 * @param device                   Capture device struct to hold the capture
 *                                 device
 * @param capture_type             Kind of capture device
 * @param data                     An opaque data pointer given to the device
 * @param width                    Width of the screen to capture, in pixels
 * @param height                   Height of the screen to capture, in pixels
 * @param dpi                      Dots per sq inch of the screen, (Where 96 is neutral)
 *
 * @returns                        0 if succeeded, else -1
 */
int create_capture_device(CaptureDevice* device, CaptureDeviceType capture_type, void* data,
                          int width, int height, int dpi);

/**
 * @brief                          Tries to reconfigure the capture device
 *                                 using the newly provided parameters
 *
 * @param device                   The Capture Device to reconfigure
 * @param width                    New width of the screen to capture, in pixels
 * @param height                   New height of the screen to capture, in pixels
 * @param dpi                      New DPI
 *
 * @returns                        0 if succeeded, else -1
 */
bool reconfigure_capture_device(CaptureDevice* device, int width, int height, int dpi);

/**
 * @brief                          Capture a bitmap snapshot of the screen
 *                                 The width/height of the image is guaranteed to be
 *                                 the width/height passed into create_/reconfigure_ capture device,
 *                                 If the screen dimensions changed, then -1 will be returned
 *
 * @param device                   The device used to capture the screen
 *
 * @returns                        Number of frames since last capture if succeeded, else -1
 */
int capture_screen(CaptureDevice* device);

/**
 * @brief                          Transfers screen capture to CPU buffer
 *                                 This is used if all attempts of a
 *                                 hardware screen transfer have failed
 *
 * @param device                   The capture device used to capture the screen
 *
 * @param hints			   		   Encoder hints reported by the capture device
 *
 * @returns                        0 always
 */
int transfer_screen(CaptureDevice* device, CaptureEncoderHints* hints);

/**
 * @brief 							Retrieve data pointer and stride from the capture
 * 									device
 * @param device					The capture device to get data from
 * @param buf						the target buffer pointer
 * @param stride					the target stride
 * @return							0 on success, -1 on error
 */
int capture_get_data(CaptureDevice* device, void** buf, int* stride);

/**
 * @brief                          Destroys and frees the memory of a capture device
 *
 * @param device                   The capture device to free
 */
void destroy_capture_device(CaptureDevice* device);

#endif  // VIDEO_CAPTURE_H
