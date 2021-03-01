#ifndef INPUT_DRIVER_H
#define INPUT_DRIVER_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file input_driver.h
 * @brief This file defines the methods required to implement an input driver
============================
Usage
============================

Create an input device by calling `CreateInputDevice()`. Now, you may use this device to query
keyboard state, or to emit mouse button, mouse motion, scroll, and keyboard events. These methods
are used, for example, in input.c, to build `FractalClientMessage` handlers and to generate keyboard
input from an arbitrary string.
*/

/*
============================
Includes
============================
*/

#include "../core/fractal.h"

#if INPUT_DRIVER == WINAPI_INPUT_DRIVER
#include "winapi_input_driver.h"
#elif INPUT_DRIVER == XTEST_INPUT_DRIVER
#include "xtest_input_driver.h"
#elif INPUT_DRIVER == SOCKET_INPUT_DRIVER
#include "socket_input_driver.h"
#else
#error "Unrecognized INPUT_DRIVER option"
#endif

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Create an input device struct to receive user
 *                                 actions to be replayed on a server
 *
 * @returns                        Initialized input device struct defining
 *                                 mouse and keyboard states
 */
InputDevice* create_input_device();

/**
 * @brief                          Destroy and free the memory of an input
 *                                 device struct
 *
 * @param input_device             The initialized input device struct to
 *                                 destroy and free the memory of
 */
void destroy_input_device(InputDevice* input_device);

/**
 * @brief                          Get the active/inactive state of a key
 *                                 modifier (caps lock, num lock, etc.)
 *
 * @param input_device             The initialized input device to query
 *
 * @param sdl_keycode              The SDL keycode of the modifier to
 *                                 query (`FK_CAPSLOCK` or `FK_NUMLOCK`)
 *
 * @returns                        1 if the queried modifier is active,
 *                                 0 if inactive, and -1 on error
 */
int get_keyboard_modifier_state(InputDevice* input_device, FractalKeycode sdl_keycode);

/**
 * @brief                          Get the pressed/unpressed state of a
 *                                 keyboard key
 *
 * @param input_device             The initialized input device to query
 *
 * @param sdl_keycode              The SDL keycode to query
 *
 * @returns                        1 if the queried key is pressed,
 *                                 0 if unpressed, and -1 on error
 */
int get_keyboard_key_state(InputDevice* input_device, FractalKeycode sdl_keycode);

/**
 * @brief                          Emit a keyboard press/unpress event
 *
 * @param input_device             The initialized input device to write
 *
 * @param sdl_keycode              The SDL keycode to modify
 *
 * @param pressed                  1 for a key press, 0 for a key unpress
 *
 * @returns                        0 on success, -1 on failure
 */
int emit_key_event(InputDevice* input_device, FractalKeycode sdl_keycode, int pressed);

/**
 * @brief                          Emit a relative/absolute mouse motion event
 *
 * @param input_device             The initialized input device to write
 *
 * @param x                        The normalized x coordinate of the mouse event,
 *                                 on a scale from 0 to `MOUSE_SCALING_FACTOR` in the
 *                                 case of absolute mouse motion, or signed in pixels
 *                                 in the case of relative mouse motion
 *
 * @param y                        The normalized y coordinate of the mouse event,
 *                                 on a scale from 0 to `MOUSE_SCALING_FACTOR` in the
 *                                 case of absolute mouse motion, or signed in pixels
 *                                 in the case of relative mouse motion
 *
 * @param relative                 1 for relative, 0 for absolute
 *
 * @returns                        0 on success, -1 on failure
 */
int emit_mouse_motion_event(InputDevice* input_device, int32_t x, int32_t y, int relative);

/**
 * @brief                          Emit a mouse button press/unpress event
 *
 * @param input_device             The initialized input device to write
 *
 * @param button                   The mouse button (left, right, or center) to press
 *
 * @param pressed                  1 for a button press, 0 for a button unpress
 *
 * @returns                        0 on success, -1 on failure
 */
int emit_mouse_button_event(InputDevice* input_device, FractalMouseButton button, int pressed);

/**
 * @brief                          Emit a mouse vertical or horizontal scroll event
 *
 * @param input_device             The initialized input device to write
 *
 * @param x                        Horizontal scroll direction/amount (-1,0,+1 always work)
 *
 * @param y                        Vertical scroll direction/amount (-1,0,+1 always work)
 *
 * @returns                        0 on success, -1 on failure
 */
int emit_mouse_wheel_event(InputDevice* input_device, int32_t x, int32_t y);

#endif  // INPUT_DRIVER_H
