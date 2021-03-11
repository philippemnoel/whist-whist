#ifndef WINDOW_NAME_H
#define WINDOW_NAME_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file window_name.h
 * @brief This file contains all the code for getting the name of a window.
============================
Usage
============================

init_window_name_getter();

char name[WINDOW_NAME_MAXLEN];
get_focused_window_name(name);

destroy_window_name_getter();
*/

/*
============================
Includes
============================
*/

#include <fractal/core/fractal.h>

/*
============================
Defines
============================
*/

#define WINDOW_NAME_MAXLEN 128

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Initialize variables required to get window names.
 *
 */
void init_window_name_getter();

/**
 * @brief                          Get the name of the focused window.
 *
 * @param name_return              Address to write name. Should have at least WINDOW_NAME_MAXLEN
 *                                 bytes available.
 *
 * @returns                        0 on success, any other int on failure.
 */
int get_focused_window_name(char* name_return);

/**
 * @brief                          Destroy variables that were initialized.
 *
 */
void destroy_window_name_getter();

#endif  // WINDOW_NAME_H
