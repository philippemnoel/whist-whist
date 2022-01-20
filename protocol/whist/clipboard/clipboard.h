#ifndef CLIPBOARD_H
#define CLIPBOARD_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file clipboard.h
 * @brief This file contains the general clipboard functions for a shared
 *        client-server clipboard.
============================
Usage
============================

GET_OS_CLIPBOARD and SET_OS_CLIPBOARD will return strings representing directories
important for getting and setting file clipboards. When get_os_clipboard() is called
and it returns a CLIPBOARD_FILES type, then GET_OS_CLIPBOARD will be filled with
symlinks to the clipboard files. When set_os_clipboard(cb) is called and is given a
clipboard with a CLIPBOARD_FILES type, then the clipboard will be set to
whatever files are in the SET_OS_CLIPBOARD directory.

LGET_OS_CLIPBOARD and LSET_OS_CLIPBOARD are the wide-character versions of these
strings, for use on windows OS's
*/

/*
============================
Includes
============================
*/

#include <stdbool.h>

#ifdef _WIN32
#define _CRT_SECURE_NO_WARNINGS
#pragma warning(disable : 4200)
#include <windows.h>
#endif

/*
============================
Defines
============================
*/

#ifdef _WIN32
WCHAR* lget_os_clipboard_directory(void);
WCHAR* lset_os_clipboard_directory(void);
char* get_os_clipboard_directory(void);
char* set_os_clipboard_directory(void);
#define LGET_OS_CLIPBOARD (lget_os_clipboard_directory())
#define GET_OS_CLIPBOARD (get_cos_lipboard_directory())
#define LSET_OS_CLIPBOARD (lset_os_clipboard_directory())
#define SET_OS_CLIPBOARD (set_cos_lipboard_directory())
// If we are on Windows, MAX_PATH is defined; otherwise, we need to use PATH_MAX.
#define PATH_MAXLEN MAX_PATH - 1
#else
#define GET_OS_CLIPBOARD "./get_os_clipboard"
#define SET_OS_CLIPBOARD "./set_os_clipboard"
#define PATH_MAXLEN PATH_MAX - 1
#endif

/*
============================
Custom types
============================
*/

/**
 * @brief                          The type of data that a clipboard might be
 */
typedef enum ClipboardType {
    CLIPBOARD_NONE,
    CLIPBOARD_TEXT,
    CLIPBOARD_IMAGE,
    CLIPBOARD_FILES
} ClipboardType;

/**
 * @brief                          The type of the clipboard chunk being sent
 */
typedef enum ClipboardChunkType {
    CLIPBOARD_START,
    CLIPBOARD_MIDDLE,
    CLIPBOARD_FINAL
} ClipboardChunkType;

/**
 * @brief                          A packet of data referring to and containing
 *                                 the information of a clipboard
 */
typedef struct ClipboardData {
    int size;                       // Number of bytes for the clipboard data
    ClipboardType type;             // The type of data for the clipboard
    ClipboardChunkType chunk_type;  // Whether this is a first, middle or last chunk
    char data[0];                   // The data that stores the clipboard information
} ClipboardData;

/**
 * @brief                          Clipboard file data packet description
 */
typedef struct ClipboardFiles {
    int size;
    char* files[];
} ClipboardFiles;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Initialize the clipboard to put/get data
 *                                 to/from
 *
 * @param is_client                Whether the caller is the client or the server
 *
 */
void init_clipboard(bool is_client);

/**
 * @brief                          Returns whether the local clipboard should be preserved
 *                                 in the shared clipboard between server and client.
 *
 * @returns                        True if client, false if server
 */
bool should_preserve_local_clipboard(void);

/**
 * @brief                          Get the current OS clipboard data
 *
 * @returns                        Pointer to the current clipboard data as a
 *                                 ClipboardData struct
 */
ClipboardData* get_os_clipboard(void);

/**
 * @brief                         Set the OS clipboard to the given clipboard data
 *
 * @param cb                      Pointer to a clipboard data struct to set the
 *                                clipboard to
 */
void set_os_clipboard(ClipboardData* cb);

/**
 * @brief                          Frees a ClipboardData that was generated by
 *                                 get_os_clipboard
 *
 * @param clipboard                The clipboard buffer to free
 */
void free_clipboard_buffer(ClipboardData* clipboard);

/**
 * @brief                          Check whether the OS clipboard has new data
 *
 * @returns                        True if new clipboard data, false if else
 */
bool has_os_clipboard_updated(void);

/**
 * @brief                          Destroy current clipboard
 *
 */
void destroy_clipboard(void);

#endif  // CLIPBOARD_H
