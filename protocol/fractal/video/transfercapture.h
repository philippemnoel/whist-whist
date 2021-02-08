#ifndef CPU_CAPTURE_TRANSFER_H
#define CPU_CAPTURE_TRANSFER_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file cpucapturetransfer.h
 * @brief This code handles the data transfer from a video capture device to a
 *        video encoder via the CPU.
============================
Usage
============================
*/

/*
============================
Includes
============================
*/

#include "videoencode.h"
#if defined(_WIN32)
#include "dxgicapture.h"
#else
#include "x11capture.h"
#endif

/*
============================
Public Functions
============================
*/

/**
 * @brief                         Initialize or reinitialize the dxgi cuda transfer context,
 *                                if it is needed for the given (device, encoder) pair.
 *                                This should be called after deciding to use a new encoder,
 *                                as it may be able to hardware optimize the transfer from device to
 * encoder
 *
 * @param device                  The capture device being used
 *
 * @param encoder                 The encoder being used
 */
void reinitialize_transfer_context(CaptureDevice* device, VideoEncoder* encoder);

/**
 * @brief                         Transfer the texture stored in the capture
 *                                device to the encoder, either via GPU or CPU
 *
 * @param device                  The capture device from which to get the
 *                                texture
 * @param encoder                 The encoder into which to load the frame data
 *
 * @returns                       0 on success, else -1
 */
int transfer_capture(CaptureDevice* device, VideoEncoder* encoder);

#endif  // CPU_CAPTURE_TRANSFER_H
