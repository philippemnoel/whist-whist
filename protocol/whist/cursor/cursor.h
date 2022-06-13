#ifndef CURSOR_H
#define CURSOR_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file cursor.h
 * @brief This file defines the cursor types, functions, init and get.
============================
Usage
============================

Use InitCursor to load the appropriate cursor images for a specific OS, and then
GetCurrentCursor to retrieve what the cursor shold be on the OS (drag-window,
arrow, etc.).
*/

/*
============================
Includes
============================
*/

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/*
============================
Defines
============================
*/

#define MAX_CURSOR_WIDTH 64
#define MAX_CURSOR_HEIGHT 64

/*
============================
Custom Types
============================
*/

/**
 * @brief   Cursor state.
 * @details State of the cursor on the rendered screen.
 */
typedef enum WhistCursorState {
    CURSOR_STATE_HIDDEN = 0,
    CURSOR_STATE_VISIBLE = 1
} WhistCursorState;

/**
 * @brief   Cursor ID.
 * @details The type of the cursor showing up on the screen.
 */
typedef enum WhistCursorID {
    WHIST_CURSOR_ARROW,
    WHIST_CURSOR_IBEAM,
    WHIST_CURSOR_WAIT,
    WHIST_CURSOR_CROSSHAIR,
    WHIST_CURSOR_WAITARROW,
    WHIST_CURSOR_SIZENWSE,
    WHIST_CURSOR_SIZENESW,
    WHIST_CURSOR_SIZEWE,
    WHIST_CURSOR_SIZENS,
    WHIST_CURSOR_SIZEALL,
    WHIST_CURSOR_NO,
    WHIST_CURSOR_HAND,
    INVALID
} WhistCursorID;

/**
 * @brief   Cursor image.
 * @details The image used for the rendered cursor.
 */
typedef struct WhistCursorInfo {
    WhistCursorID cursor_id;
    WhistCursorState cursor_state;
    uint32_t hash;
    bool cached;
    bool using_png;
    size_t png_size;
    unsigned short png_width;
    unsigned short png_height;
    unsigned short png_hot_x;
    unsigned short png_hot_y;
    unsigned char png[];
} WhistCursorInfo;

typedef struct WhistCursorCache WhistCursorCache;

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Initialize cursor capturing
 */
void whist_cursor_capture_init(void);

/**
 * @brief                          Shut down cursor capturing
 */
void whist_cursor_capture_destroy(void);

/**
 * @brief                          Returns the size of the WhistCursorInfo struct
 *
 * @param image                    The WhistCursorInfo struct
 *
 * @returns                        The size of the WhistCursorInfo struct
 */
size_t whist_cursor_info_get_size(const WhistCursorInfo* image);

/**
 * @brief                          Return RGBA pixel data from a WhistCursorInfo struct
 *
 * @param info                     The WhistCursorInfo struct from which to generate the RGBA pixel
 * data
 *
 * @returns                        The RGBA pixel data as a pointer to a uint8_t array, which must
 * be freed by the caller
 */
uint8_t* whist_cursor_info_to_rgba(const WhistCursorInfo* info);

/**
 * @brief                          Returns the current cursor image
 *
 * @returns                        The current cursor image as a pointer to a
 *                                 WhistCursorInfo struct, which must be freed
 *                                 by the caller.
 */
WhistCursorInfo* whist_cursor_capture(void);

/**
 * Create a new cursor cache instance.
 *
 * @param max_entries  The number of entries in the cache.
 * @param store_data   Whether to store full cursor data (metadata is
 *                     always stored).
 * @return  New cursor cache instance.
 */
WhistCursorCache* whist_cursor_cache_create(int max_entries, bool store_data);

/**
 * Clear the cursor cache of all entries.
 *
 * @param cache  Cache instance to clear.
 */
void whist_cursor_cache_clear(WhistCursorCache* cache);

/**
 * Destroy a cursor cache instance.
 *
 * @param cache  Cache instance to destroy.
 */
void whist_cursor_cache_destroy(WhistCursorCache* cache);

/**
 * Add a new entry to the cursor cache.
 *
 * If the cache is already full this will evict the least recently used
 * entry currently in the cache.
 *
 * @param cache  Cache to add new entry to.
 * @param info   Cursor to add to the cache.  All data is copied.
 */
void whist_cursor_cache_add(WhistCursorCache* cache, WhistCursorInfo* info);

/**
 * Check whether a given hash is in the cache.
 *
 * @param cache  Cache to do the lookup in.
 * @param hash   Hash to look for.
 * @return  Cursor with the matching hash, or NULL if not found.
 */
const WhistCursorInfo* whist_cursor_cache_check(WhistCursorCache* cache, uint32_t hash);

#endif  // CURSOR_H
