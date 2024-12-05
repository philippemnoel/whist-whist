#ifndef WHIST_SERVER_STATE_H
#define WHIST_SERVER_STATE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file state.h
 * @brief This file defines a struct to hold server global state
 */

/*
============================
Includes
============================
*/
#include <whist/core/platform.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#if OS_IS(OS_WIN32)
#include <process.h>
#include <shlwapi.h>
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#endif
#include <time.h>

#include <whist/core/whist.h>
#include <whist/input/input.h>
#include <whist/logging/error_monitor.h>
#include <whist/video/transfercapture.h>
#include <whist/logging/log_statistic.h>
#include "whist/video/ltr.h"
#include "client.h"
#include "network.h"
#include "video.h"
#include "audio.h"

#include <whist/utils/window_info.h>
#include <whist/utils/sysinfo.h>

#if OS_IS(OS_WIN32)
#pragma comment(lib, "ws2_32.lib")
#endif
// Linux shouldn't have this

/** @brief whist server configuration parameters */
struct _whist_server_config {
    char binary_aes_private_key[16];
    char hex_aes_private_key[33];
    char identifier[WHIST_IDENTIFIER_MAXLEN + 1];
    int wait_time_to_exit;
    int disconnect_time_to_exit;
};

typedef struct _whist_server_config whist_server_config;

// NOTE: PLEASE DO NOT ADD MORE TO THIS STRUCT
// See code standards, this counts as "extern global state".
// Instead, make classes with init/destroy and getters/setters.

/** @brief internal state of the whist server */
typedef struct WhistServerState {
    // TODO:  Currently it is possible for asynchronous updates to this
    // structure to cause the video thread to see an inconsistent state
    // (for example, an updated width without an updated height).  This
    // is undefined behaviour and should be fixed by adding mutexes
    // protecting parts which can be updated by other threads.

    whist_server_config* config;

    // TODO: Move to client.h struct, and get out of global state
    volatile int connection_id;
    volatile WhistOSType client_os;

    volatile bool exiting;
    volatile bool stop_streaming;
    Client* client;

    // This `update_encoder` should be set to true,
    // when the above "requested" variables have changed
    volatile bool update_encoder;

    InputDevice* input_device;

    // Whether the stream needs to be restarted with new parameter sets
    // and an intra frame.
    bool stream_needs_restart;
    // Whether the stream needs to be recovered (but does not require
    // new parameter sets and an intra frame).
    bool stream_needs_recovery;

    // Long-term reference state.
    LTRState* ltr_context;

    // Frame acks for LTR input.
    uint32_t frame_ack_id;
    bool update_frame_ack;

    /* video */
    volatile int client_width;
    volatile int client_height;
    volatile int client_dpi;
    volatile bool update_device;
    bool saturate_bandwidth;

    bool pending_encoder;
    bool encoder_finished;
    VideoEncoder* encoder_factory_result;

    int encoder_factory_server_w;
    int encoder_factory_server_h;
    int encoder_factory_client_w;
    int encoder_factory_client_h;
    int encoder_factory_bitrate;
    int encoder_factory_vbv_size;
    CodecType encoder_factory_codec_type;

    /**
     * Server-side cursor cache.
     */
    WhistCursorCache* cursor_cache;
    /**
     * Hash of the cursor used with the last frame.
     *
     * If the hash of the new cursor does not match this then the cursor
     * has changed and a new cursor will need to be sent.
     */
    uint32_t last_cursor_hash;
} WhistServerState;

#endif  // WHIST_SERVER_STATE_H
