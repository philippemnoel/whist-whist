#ifndef CAPTURE_NVX11CAPTURE_H
#define CAPTURE_NVX11CAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file nvx11capture.h
 * @brief This file contains the code to do screen capture via the Nvidia FBC SDK on Linux Ubuntu.
 */

#include "capture.h"
#include "x11capture.h"

CaptureDeviceImpl* create_nvx11_capture_device(CaptureDeviceInfos* infos, const char* display);

X11CaptureDevice* nvx11_capture_get_x11(CaptureDeviceImpl* impl);

#endif /* CAPTURE_NVX11CAPTURE_H */
