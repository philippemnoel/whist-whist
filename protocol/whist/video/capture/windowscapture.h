#ifndef PROTOCOL_WHIST_VIDEO_CAPTURE_WINDOWSCAPTURE_H_
#define PROTOCOL_WHIST_VIDEO_CAPTURE_WINDOWSCAPTURE_H_
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file windowscapture.h
 * @brief This file contains the code to do screen capture via the X11 API on Linux Ubuntu.
============================
Usage
============================
*/

#include "capture.h"

CaptureDeviceImpl* create_windows_capture_device(CaptureDeviceInfos* infos, const char* d);

#endif /* PROTOCOL_WHIST_VIDEO_CAPTURE_WINDOWSCAPTURE_H_ */
