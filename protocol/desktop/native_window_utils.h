#ifndef NATIVE_WINDOW_UTILS_H
#define NATIVE_WINDOW_UTILS_H

/**
 * Copyright Fractal Computers, Inc. 2021
 * @file native_window_utils.h
 * @brief This file exposes platform-independent APIs for
 *        native window modifications not handled by SDL.
============================
Usage
============================

Use these functions to modify the appearance or behavior of the native window
underlying the SDL window. These functions will almost always need to be called
from the same thread as SDL window creation and SDL event handling; usually this
is the main thread.
*/

/*
============================
Includes
============================
*/

#include "SDL.h"
#include "utils/color.h"

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Set the color of the titlebar of the provided window, and
 *                                 the corresponding titlebar text as well.
 *
 * @param window                   The SDL window handle whose titlebar to modify.
 *
 * @param color                    The RGB color to use when setting the titlebar.
 *
 * @returns                        Returns -1 on failure, 0 on success.
 */
int set_native_window_color(SDL_Window *window, FractalRGBColor color);

#endif  // NATIVE_WINDOW_UTILS_H