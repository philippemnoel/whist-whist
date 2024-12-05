#ifndef CAPTURE_X11NVIDIACAPTURE_H
#define CAPTURE_X11NVIDIACAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file nvidiacapture.h
 * @brief This file contains the code to do screen capture via the Nvidia FBC SDK on Linux Ubuntu.
============================
Usage
============================

NvidiaCaptureDevice contains all the information used to interface with the Nvidia FBC SDK and the
data of a frame. Call create_nvidia_capture_device to initialize a device, nvidia_capture_screen to
capture the screen with said device, and destroy_nvidia_capture_device when done capturing frames.
*/

/*
============================
Includes
============================
*/
#include "../nvidia-linux/NvFBCUtils.h"
#include "../nvidia-linux/nvEncodeAPI.h"
#include <whist/core/whist.h>

#include "../cudacontext.h"
#include "capture.h"

/*
============================
Custom Types
============================
*/

/**
 * @brief       Struct to handle using Nvidia for capturing the screen. p_fbc_fn contains pointers
 * to the API functions, and dw_texture_index keeps track of the GL texture that the capture is
 * stored on.
 */
typedef struct {
    CaptureDeviceImpl base;

    // The gpu texture of the most recently captured frame
    CUdeviceptr p_gpu_texture;
    // Colour of the top-left-corner of the texture.
    WhistRGBColor corner_color;

    // Internal Nvidia API structs
    NVFBC_SESSION_HANDLE fbc_handle;
    NVFBC_API_FUNCTION_LIST p_fbc_fn;
} NvidiaCaptureDevice;

/*
============================
Public Functions
============================
*/
/**
 * @brief                          Creates an nvidia capture device
 *
 * @param infos					   The capture device infos
 * @param d						   Private user data
 * @returns                        The new capture device.
 */
CaptureDeviceImpl* create_nvidia_capture_device(CaptureDeviceInfos* infos, void* d);

int nvidia_bind_context(NvidiaCaptureDevice* device);
int nvidia_release_context(NvidiaCaptureDevice* device);

#endif  // CAPTURE_X11NVIDIACAPTURE_H
