#ifndef SCREENCAPTURE_H
#define SCREENCAPTURE_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file screencapture.h
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

// Defines the CaptureDevice type
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
 * @brief                          Creates a screen capture device
 *
 * @param device                   Capture device struct to hold the capture
 *                                 device
 * @param width                    Width of the screen to capture, in pixels
 * @param height                   Height of the screen to capture, in pixels
 * @param dpi                      Dots per inch of the screen, in pixels
 * @param bitrate                  Bitrate (used for nvidia_capture_device)
 * @param codec                    Codec (used for nvidia_capture_device)
 *
 * @returns                        0 if succeeded, else -1
 */
int create_capture_device(CaptureDevice* device, UINT width, UINT height, UINT dpi, int bitrate,
                          CodecType codec);

/**
 * @brief                          Capture a bitmap snapshot of the screen
 *
 * @param device                   The device used to capture the screen
 *
 * @returns                        0 if succeeded, else -1
 */
int capture_screen(CaptureDevice* device);

/**
 * @brief                          Transfers screen capture to CPU buffer
 *                                 This is used if all attempts of a
 *                                 hardware screen transfer have failed
 *
 * @param device                   The capture device used to capture the screen
 *
 * @returns                        0 always
 */
int transfer_screen(CaptureDevice* device);

/**
 * @brief                          Release a captured screen bitmap snapshot
 *
 * @param device                   The capture device holding the
 *                                 screen object captured
 */
void release_screen(CaptureDevice* device);

/**
 * @brief                          Destroys and frees the memory of a capture device
 *
 * @param device                   The capture device to free
 */
void destroy_capture_device(CaptureDevice* device);

/**
 * @brief                          Updates the capture device parameters,
 *                                 if the capture device is also the encoder
 *
 * @param device                   The Capture device
 * @param bitrate                  The new bitrate to use for encoding
 * @param codec                    The new codec to use for encoding
 */
void update_capture_encoder(CaptureDevice* device, int bitrate, CodecType codec);

#endif  // SCREENCAPTURE_H
