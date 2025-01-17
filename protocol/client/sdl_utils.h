#ifndef WHIST_SDL_UTILS_H
#define WHIST_SDL_UTILS_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file sdl_utils.h
 * @brief This file contains the code to create and destroy SDL windows on the
 *        client.
============================
Usage
============================

initSDL gets called first to create an SDL window, and destroySDL at the end to
close the window.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include "video.h"
#include "frontend/frontend.h"

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Creates the SDL window frontend,
 *                                 with the currently running thread at the owner
 *
 * @returns                        A handle for the created frontend
 *
 */
WhistFrontend* create_frontend(void);

/**
 * @brief                          Destroys an SDL window and associated
 *                                 parameters
 *
 * @param frontend                 The frontend to be destroyed
 */
void destroy_frontend(WhistFrontend* frontend);

/**
 * @brief                          When the window gets resized, call this function
 *                                 to update the internal rendering dimensions.
 *                                 This function will also sync the server to those dimensions.
 */
void sdl_renderer_resize_window(WhistFrontend* frontend, int width, int height);

/**
 * @brief                          Updates the framebuffer to a loading screen image.
 *                                 Remember to call sdl_render_framebuffer later.
 *
 * @param idx                      The index of the animation to render.
 *                                 This should normally be strictly increasing or reset to 0.
 */
void sdl_update_framebuffer_loading_screen(int idx);

// The pixel format required for the data/linesize passed into sdl_update_framebuffer
#define WHIST_CLIENT_FRAMEBUFFER_PIXEL_FORMAT AV_PIX_FMT_NV12

/**
 * @brief                          Update the renderer's framebuffer,
 *                                 using the provided frame.
 *
 * @param frame                    Decoded frame to update the framebuffer texture with.
 *                                 The format of this frame must match
 *                                 WHIST_CLIENT_FRAMEBUFFER_PIXEL_FORMAT.
 *
 * @param window_data              The array of whist windows to render
 *
 * @param num_windows              The number of whist windows to render
 *
 * @note                           The renderer takes ownership of the frame after this
 *                                 call.  If that is not desirable, a copy must be made
 *                                 beforehand.
 */
void sdl_update_framebuffer(AVFrame* frame, WhistWindow* window_data, int num_windows);

/**
 * @brief                          This will mark the framebuffer as ready-to-render,
 *                                 and `update_pending_sdl_tasks` will eventually render it.
 *
 * @note                           Will make `sdl_render_pending` return true, up until
 *                                 `update_pending_sdl_tasks` is called on the main thread.
 */
void sdl_render_framebuffer(void);

/**
 * @brief                          Returns whether or not
 *                                 a framebuffer is pending to render
 *
 * @returns                        True if `sdl_render_framebuffer` has been called,
 *                                 but `update_pending_sdl_tasks` has not been called.
 */
bool sdl_render_pending(void);

/**
 * @brief                          Set the cursor info as pending, so that it will be draw in the
 *                                 main thread.
 *
 * @param cursor_info              The WhistCursorInfo to use for the new cursor
 *
 * @note                           ALL rendering related APIs are only safe inside main thread.
 */
void sdl_set_cursor_info_as_pending(const WhistCursorInfo* cursor_info);

/**
 * @brief                          Do the rendering job of the pending cursor info, if any.
 *
 * @note                           This function is virtually instantaneous. Should be only called
 *                                 in main thread, since it's the only safe way to do any render.
 */
void sdl_present_pending_cursor(WhistFrontend* frontend);

/**
 * @brief                          Update the color of the window's titlebar
 *
 * @param id                       The window ID to set the color for
 *
 * @param color                    The color to set the titlebar too
 *
 * @note                           This function is virtually instantaneous
 */
void sdl_render_window_titlebar_color(int id, WhistRGBColor color);

/**
 * @brief                          Update the window's fullscreen state
 *
 * @param id                       The window ID to change fullscreen status of
 *
 * @param is_fullscreen            If True, the window will become fullscreen
 *                                 If False, the window will return to windowed mode
 *
 * @note                           This function is virtually instantaneous
 */
void sdl_set_fullscreen(int id, bool is_fullscreen);

/**
 * Display a notification with the frontend.
 *
 * @param notif  Notification to display.
 */
void sdl_client_display_notification(const WhistNotification* notif);

/**
 * @brief                          The above functions may be expensive, and thus may
 *                                 have to be queued up. This function executes all of the
 *                                 currently internally queued actions.
 *
 * @param frontend                 The WhistFrontend to use for the actions
 *
 * @note                           This function must be called by the
 *                                 same thread that originally called create_frontend.
 *                                 This will also render out any pending framebuffer,
 *                                 thereby setting `sdl_render_pending` to false.
 */
void sdl_update_pending_tasks(WhistFrontend* frontend);

/**
 * @brief                          Copies private variable values to the variables pointed by the
 * non-NULL pointers passed as parameters. Used for testing.
 *
 * @param pending_resize_message_ptr   If non-NULL, the function will save the value of
 * pending_resize_message in the variable pointed to by this pointer.
 */
void sdl_utils_check_private_vars(bool* pending_resize_message_ptr);

#endif  // WHIST_SDL_UTILS_H
