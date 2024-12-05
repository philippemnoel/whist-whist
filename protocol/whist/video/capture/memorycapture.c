/**
 * @copyright Copyright (c) 2022 Whist Technologies, Inc.
 * @file memorycapture.c
 * @brief Capture device which reads from a memory.
 */

#include "memorycapture.h"
#include "../cudacontext.h"

typedef struct {
    CaptureDeviceImpl base;

    const uint8_t *data_block;
} MemoryCaptureDevice;

static int memory_reconfigure_capture_device(MemoryCaptureDevice *dev, int width, int height,
                                             int dpi) {
    return 0;
}

static int memory_capture_device_init(MemoryCaptureDevice *dev, int width, int height, int dpi) {
    CaptureDeviceInfos *infos = dev->base.infos;
    infos->width = width;
    infos->height = height;
    infos->dpi = dpi;
    return 0;
}

static void memory_destroy_capture_device(MemoryCaptureDevice **pmemory_capture) {
    MemoryCaptureDevice *memory_capture = *pmemory_capture;
    free(memory_capture);

    *pmemory_capture = NULL;
}

static int memory_capture_screen(MemoryCaptureDevice *fc, int *width, int *height, int *pitch) {
    CaptureDeviceInfos *infos = fc->base.infos;
    *width = infos->width;
    *height = infos->height;
    *pitch = infos->pitch;
    return 0;
}

static int memory_transfer_screen(MemoryCaptureDevice *fc, CaptureEncoderHints *hints) {
    memset(hints, 0, sizeof(*hints));
    return 0;
}

static int memory_capture_getdata(MemoryCaptureDevice *device, void **buf, int *stride) {
    *buf = (void *)device->data_block;
    *stride = device->base.infos->pitch;
    return 0;
}

static int memory_get_dimensions(MemoryCaptureDevice *device, int *w, int *h, int *dpi) {
    *w = device->base.infos->width;
    *h = device->base.infos->height;
    *dpi = device->base.infos->dpi;
    return 0;
}

CaptureDeviceImpl *create_memory_capture_device(CaptureDeviceInfos *infos,
                                                MemoryCaptureParams *params) {
    MemoryCaptureDevice *memory_capture =
        (MemoryCaptureDevice *)safe_zalloc(sizeof(*memory_capture));

    CaptureDeviceImpl *impl = &memory_capture->base;
    impl->device_type = MEMORY_DEVICE;
    impl->infos = infos;
    impl->init = (CaptureDeviceInitFn)memory_capture_device_init;
    impl->reconfigure = (CaptureDeviceReconfigureFn)memory_reconfigure_capture_device;
    impl->capture_screen = (CaptureDeviceCaptureScreenFn)memory_capture_screen;
    impl->capture_get_data = (CaptureDeviceGetDataFn)memory_capture_getdata;
    impl->transfer_screen = (CaptureDeviceTransferScreenFn)memory_transfer_screen;
    impl->get_dimensions = (CaptureDeviceGetDimensionsFn)memory_get_dimensions;
    impl->destroy = (CaptureDeviceDestroyFn)memory_destroy_capture_device;

    infos->pitch = params->pitch;
    memory_capture->data_block = params->data;
    return impl;
}
