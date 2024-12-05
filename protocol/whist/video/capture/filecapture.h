#ifndef CAPTURE_FILECAPTURE_H
#define CAPTURE_FILECAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file filecapture.h
 * @brief This file contains the code to do screen capture from a movie file
============================
Usage
============================

*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/utils/color.h>

#include "capture.h"

/*
============================
Custom Types
============================
*/

CaptureDeviceImpl* create_file_capture_device(CaptureDeviceInfos* infos, const char* movie);

#endif  // CAPTURE_FILECAPTURE_H
