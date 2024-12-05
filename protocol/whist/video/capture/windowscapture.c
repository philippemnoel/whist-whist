/*
 * GPU screen capture on Windows.
 *
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 **/
#include "windowscapture.h"
#include <whist/logging/logging.h>

typedef struct {
    CaptureDeviceImpl base;

} WindowsCaptureDevice;

static int windows_capture_device_init(WindowsCaptureDevice* dev, uint32_t width, uint32_t height,
                                       uint32_t dpi) {
    LOG_WARNING("Not implemented for Windows servers!");
    return 0;
}

static int windows_reconfigure_capture_device(WindowsCaptureDevice* dev, uint32_t width,
                                              uint32_t height, uint32_t dpi) {
    LOG_WARNING("Not implemented for Windows servers!");
    return 0;
}

static int windows_capture_screen(WindowsCaptureDevice* dev, uint32_t* width, uint32_t* height,
                                  uint32_t* pitch) {
    LOG_WARNING("Not implemented for Windows servers!");
    return 0;
}

static int windows_get_dimensions(WindowsCaptureDevice* dev, uint32_t* w, uint32_t* h,
                                  uint32_t* dpi) {
    LOG_WARNING("Not implemented for Windows servers!");
    return 0;
}

static int windows_capture_getdata(WindowsCaptureDevice* dev, void** buf, uint32_t* stride) {
    LOG_WARNING("Not implemented for Windows servers!");
    return 0;
}

static void windows_destroy_capture_device(WindowsCaptureDevice** pdev) {
    WindowsCaptureDevice* dev = *pdev;

    LOG_WARNING("Not implemented for Windows servers!");
    free(dev);
    *pdev = NULL;
}

CaptureDeviceImpl* create_windows_capture_device(CaptureDeviceInfos* infos, const char* d) {
    UNUSED(d);

    WindowsCaptureDevice* dev = (WindowsCaptureDevice*)safe_zalloc(sizeof(*dev));
    CaptureDeviceImpl* impl = &dev->base;

    impl->infos = infos;
    impl->init = (CaptureDeviceInitFn)windows_capture_device_init;
    impl->reconfigure = (CaptureDeviceReconfigureFn)windows_reconfigure_capture_device;
    impl->capture_screen = (CaptureDeviceCaptureScreenFn)windows_capture_screen;
    impl->capture_get_data = (CaptureDeviceGetDataFn)windows_capture_getdata;
    // impl->transfer_screen
    impl->get_dimensions = (CaptureDeviceGetDimensionsFn)windows_get_dimensions;
    impl->destroy = (CaptureDeviceDestroyFn)windows_destroy_capture_device;

    return &dev->base;
}
