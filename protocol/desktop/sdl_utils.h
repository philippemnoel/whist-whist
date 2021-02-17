#ifndef SDL_UTILS_H
#define SDL_UTILS_H
/**
 * Copyright Fractal Computers, Inc. 2020
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

#include "../fractal/core/fractal.h"
#include "../fractal/utils/sdlscreeninfo.h"
#include "video.h"

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Attaches the current thread to the specified
 *                                 current input desktop
 *
 * @param output_width             The width of the SDL window to create, in
 *                                 pixels
 * @param output_height            The height of the SDL window to create, in
 *                                 pixels
 *
 * @param name                     The title of the window
 *
 * @param icon_filename            The filename of the window icon, pointing to a 64x64 png
 *
 * @returns                        NULL if fails to create SDL window, else it
 *                                 returns the SDL window variable
 */
SDL_Window* init_sdl(int output_width, int output_height, char* name, char* icon_filename);

int resizing_event_watcher(void* data, SDL_Event* event);
/*


    Arguments:
        data (void*): SDL Window data
        event (SDL_Event*): SDL event to be analyzed

    Return:
        (int): 0 on success
*/

/**
 * @brief                          Destroys an SDL window and associated
 *                                 parameters
 *
 * @param window                   The SDL window to destroy
 */
void destroy_sdl(SDL_Window* window);

#endif  // SDL_UTILS_H
