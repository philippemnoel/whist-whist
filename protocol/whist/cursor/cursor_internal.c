/**
 * Copyright (c) 2022 Whist Technologies, Inc.
 * @file cursor_internal.c
 * @brief This file implements platform-independent internal utilities for cursor capture
============================
Usage
============================
Our various cursor capture implementations use these functions to generate `WhistCursorInfo`
structs.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/utils/lodepng.h>
#include <whist/utils/aes.h>

#include "cursor_internal.h"

// Value to be hashed for hidden cursor.
#define HIDDEN_CURSOR_HASH_OFFSET 0x1000
// Value to be added to the ID enum value when hashing an ID cursor. Otherwise,
// accidental collisions are going to be very easy.
#define ID_CURSOR_HASH_OFFSET 0x2000

WhistCursorInfo* whist_cursor_info_from_type(WhistCursorType type, WhistMouseMode mode) {
    FATAL_ASSERT(type != WHIST_CURSOR_PNG);
    WhistCursorInfo* info = safe_malloc(sizeof(WhistCursorInfo));
    memset(info, 0, sizeof(WhistCursorInfo));
    info->type = type;
    info->mode = mode;
    if (mode == MOUSE_MODE_RELATIVE) {
        const int hidden_id = HIDDEN_CURSOR_HASH_OFFSET;
        info->hash = hash(&hidden_id, sizeof(int));
    } else {
        const int offset_id = type + ID_CURSOR_HASH_OFFSET;
        info->hash = hash(&offset_id, sizeof(int));
    }
    return info;
}

WhistCursorInfo* whist_cursor_info_from_rgba(const uint32_t* rgba, unsigned short width,
                                             unsigned short height, unsigned short hot_x,
                                             unsigned short hot_y, WhistMouseMode mode) {
    unsigned char* png;
    size_t png_size;

    unsigned int ret = lodepng_encode32(&png, &png_size, (unsigned char*)rgba, width, height);
    if (ret) {
        LOG_WARNING("Failed to encode PNG cursor: %s", lodepng_error_text(ret));
        return NULL;
    }

    WhistCursorInfo* info = safe_malloc(sizeof(WhistCursorInfo) + png_size);
    memset(info, 0, sizeof(WhistCursorInfo));
    info->type = WHIST_CURSOR_PNG;
    info->png_width = width;
    info->png_height = height;
    info->png_size = png_size;
    info->png_hot_x = hot_x;
    info->png_hot_y = hot_y;
    info->mode = mode;
    memcpy(info->png, png, png_size);
    if (mode == MOUSE_MODE_RELATIVE) {
        const int hidden_id = HIDDEN_CURSOR_HASH_OFFSET;
        info->hash = hash(&hidden_id, sizeof(int));
    } else {
        info->hash = hash(png, png_size);
    }
    free(png);
    return info;
}
