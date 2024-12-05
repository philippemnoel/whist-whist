/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file x11capture.c
 * @brief This file contains the code to create a capture device and use it to capture the screen on
Linux.
============================
Usage
============================
We first try to create a capture device that uses Nvidia's FBC SDK for capturing frames. This
capture device must be paired with an Nvidia encoder. If those fail, we fall back to using X11's API
to create a capture device, which captures on the CPU, and encode using FFmpeg instead. See
codec/encode.h for more details on encoding. The type of capture device currently in use is
indicated in active_capture_device.
*/

/*
============================
Includes
============================
*/
#include "capture.h"
#if OS_IS(OS_LINUX)
#include "x11capture.h"
#include "nvx11capture.h"
#include "nvidiacapture.h"
#endif
#include "filecapture.h"
#include "memorycapture.h"

#include <whist/utils/window_info.h>

/*
============================
Private Functions
============================
*/

/*
============================
Private Function Implementations
============================
*/

/*
============================
Public Function Implementations
============================
*/
int create_capture_device(CaptureDevice* device, CaptureDeviceType capture_type, void* data,
                          int width, int height, int dpi) {
    /*
       Initialize the capture device at device with the given width, height and DPI. We use Nvidia
       whenever possible, and fall back to X11 when not. See nvidiacapture.c and x11capture.c for
       internal details of each capture device.

       Arguments:
            device (CaptureDevice*): pointer to the device to initialize
            width (uint32_t): desired device width
            height (uint32_t): desired device height
            dpi (uint32_t): desired device DPI

        Returns:
            (int): 0 on success, -1 on failure
    */
    if (device == NULL) {
        LOG_ERROR("NULL device was passed into create_capture_device");
        return -1;
    }
    memset(device, 0, sizeof(CaptureDevice));

    // resize the X11 display to the appropriate width and height
    if (width <= 0 || height <= 0) {
        LOG_ERROR("Invalid width/height of %d/%d", width, height);
        return -1;
    }
    if (width < MIN_SCREEN_WIDTH) {
        LOG_ERROR("Requested width too small: %d when the minimum is %d! Rounding up.", width,
                  MIN_SCREEN_WIDTH);
        width = MIN_SCREEN_WIDTH;
    }
    if (height < MIN_SCREEN_HEIGHT) {
        LOG_ERROR("Requested height too small: %d when the minimum is %d! Rounding up.", height,
                  MIN_SCREEN_HEIGHT);
        height = MIN_SCREEN_HEIGHT;
    }
    if (width > MAX_SCREEN_WIDTH || height > MAX_SCREEN_HEIGHT) {
        LOG_ERROR(
            "Requested dimensions are too large! "
            "%dx%d when the maximum is %dx%d! Rounding down.",
            width, height, MAX_SCREEN_WIDTH, MAX_SCREEN_HEIGHT);
        width = MAX_SCREEN_WIDTH;
        height = MAX_SCREEN_HEIGHT;
    }

#if OS_IS(OS_LINUX)
    // if we can create the nvidia capture device, do so
    bool res;
    if (USING_NVIDIA_ENCODE) {
        // cuda_init the encoder context
        res = cuda_init(get_video_thread_cuda_context_ptr(), true);
        if (!res) {
            LOG_ERROR("Failed to initialize cuda!");
        }
    }
#endif

    CaptureDeviceImpl* impl = device->impl;
    if (!impl) {
        switch (capture_type) {
            case FILE_DEVICE:
                device->impl = impl = create_file_capture_device(&device->infos, data);
                if (!impl) {
                    LOG_ERROR("Failed to create file capture device!");
                    return -1;
                }
                break;
            case MEMORY_DEVICE:
                device->impl = impl =
                    create_memory_capture_device(&device->infos, (MemoryCaptureParams*)data);
                if (!impl) {
                    LOG_ERROR("Failed to create file capture device!");
                    return -1;
                }
                break;

#if OS_IS(OS_LINUX)
            case X11_DEVICE:
                device->impl = impl = create_x11_capture_device(&device->infos, data);
                if (!impl) {
                    LOG_ERROR("Failed to create X11 capture device!");
                    return -1;
                }
                break;

            case NVX11_DEVICE:
                device->impl = impl = create_nvx11_capture_device(&device->infos, data);
                if (!impl) {
                    LOG_ERROR("Failed to create Nvidia-X11 capture device!");
                    return -1;
                }
                break;
#endif
            case NVIDIA_DEVICE:
            default:
                LOG_FATAL("Unable to create device type %d", capture_type);
                break;
        }

        if (impl->init(impl, width, height, dpi) < 0) {
            LOG_ERROR("Failed to initialize capture device!");
            return -1;
        }
    }

    return 0;
}

int capture_screen(CaptureDevice* device) {
    if (!device) {
        LOG_ERROR("Tried to call capture_screen with a NULL CaptureDevice! We shouldn't do this!");
        return -1;
    }

    /* TODO:
    LinkedList window_list;
    linked_list_init(&window_list);
    get_valid_windows(device, &window_list);
    // clear window data from previous pass
    for (int i = 0; i < MAX_WINDOWS; i++) {
        device->window_data[i].id = -1;
    }
    // just copy over the first MAX_WINDOW items
    int i = 0;
    linked_list_for_each(&window_list, WhistWindow, w) {
        if (i >= MAX_WINDOWS) {
            break;
        }
        device->window_data[i] = *w;
        i++;
    }
    */

    int w, h, pitch;
    CaptureDeviceImpl* impl = device->impl;
    if (!impl) return -1;
    int ret = impl->capture_screen(impl, &w, &h, &pitch);

    impl->capture_get_data(impl, &device->frame_data, &device->infos.pitch);
    return ret;
}

bool reconfigure_capture_device(CaptureDevice* device, int width, int height, int dpi) {
    FATAL_ASSERT(device && "NULL device was passed into reconfigure_capture_device!");

    if (device->impl) return device->impl->reconfigure(device->impl, width, height, dpi) >= 0;

    return true;
}

void destroy_capture_device(CaptureDevice* device) {
    if (device == NULL) {
        // nothing to do!
        return;
    }

    device->impl->destroy(&device->impl);
}

int capture_get_data(CaptureDevice* device, void** buf, int* stride) {
    FATAL_ASSERT(device && "NULL device was passed into capture_get_data!");
    if (!device->impl || !device->impl->capture_get_data) return -1;
    return device->impl->capture_get_data(device->impl, buf, stride);
}

int transfer_screen(CaptureDevice* device, CaptureEncoderHints* hints) {
    FATAL_ASSERT(device && "NULL device was passed into transfer_screen!");
    if (!device->impl || !device->impl->transfer_screen) return -1;

    return device->impl->transfer_screen(device->impl, hints);
}
