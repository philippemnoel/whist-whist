#ifndef CAPTURE_MEMORYCAPTURE_H
#define CAPTURE_MEMORYCAPTURE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file memorycapture.h
 * @brief This file contains the code to do screen capture from a memory block.
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

/** @brief parameters for the memory capture device */
typedef struct {
    const uint8_t* data;
    uint32_t pitch;
} MemoryCaptureParams;

CaptureDeviceImpl* create_memory_capture_device(CaptureDeviceInfos* infos,
                                                MemoryCaptureParams* params);

#endif  // CAPTURE_MEMORYCAPTURE_H
