/**
 * Copyright Fractal Computers, Inc. 2020
 * @file server_message_handler.c
 * @brief This file contains all the code for client-side processing of messages
 *        received from the server
============================
Usage
============================

handleServerMessage() must be called on any received message from the server.
Any action trigged a server message must be initiated in network.c.
*/

#include "server_message_handler.h"

#include <stddef.h>

#include "../fractal/core/fractal.h"
#include "../fractal/utils/clock.h"
#include "../fractal/utils/logging.h"
#include "../fractal/utils/window_name.h"
#include "desktop_utils.h"
#include "server_message_handler.h"

#include <stddef.h>

#define UNUSED(x) (void)(x)

extern char filename[300];
extern char username[50];
extern bool exiting;
extern int audio_frequency;
extern volatile bool is_timing_latency;
extern volatile clock latency_timer;
extern volatile int ping_id;
extern volatile int ping_failures;
extern volatile int try_amount;
extern volatile char *window_title;
extern volatile bool should_update_window_title;
extern int client_id;

static int handle_pong_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_quit_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_audio_frequency_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_clipboard_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_window_title_message(FractalServerMessage *fmsg, size_t fmsg_size);

int handle_server_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    switch (fmsg->type) {
        case MESSAGE_PONG:
            return handle_pong_message(fmsg, fmsg_size);
        case SMESSAGE_QUIT:
            return handle_quit_message(fmsg, fmsg_size);
        case MESSAGE_AUDIO_FREQUENCY:
            return handle_audio_frequency_message(fmsg, fmsg_size);
        case SMESSAGE_CLIPBOARD:
            return handle_clipboard_message(fmsg, fmsg_size);
        case SMESSAGE_WINDOW_TITLE:
            return handle_window_title_message(fmsg, fmsg_size);
        default:
            LOG_WARNING("Unknown FractalServerMessage Received");
            return -1;
    }
}

static int handle_pong_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    if (fmsg_size != sizeof(FractalServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: pong message)!");
        return -1;
    }
    if (ping_id == fmsg->ping_id) {
        LOG_INFO("Latency: %f", get_timer(latency_timer));
        is_timing_latency = false;
        ping_failures = 0;
        try_amount = 0;
    } else {
        LOG_INFO("Old Ping ID found: Got %d but expected %d", fmsg->ping_id, ping_id);
    }
    return 0;
}

static int handle_quit_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    UNUSED(fmsg);
    if (fmsg_size != sizeof(FractalServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: quit message)!");
        return -1;
    }
    LOG_INFO("Server signaled a quit!");
    exiting = true;
    return 0;
}

static int handle_audio_frequency_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    if (fmsg_size != sizeof(FractalServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: audio frequency message)!");
        return -1;
    }
    LOG_INFO("Changing audio frequency to %d", fmsg->frequency);
    audio_frequency = fmsg->frequency;
    return 0;
}

static int handle_clipboard_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    if (fmsg_size != sizeof(FractalServerMessage) + fmsg->clipboard.size) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: clipboard message)!");
        return -1;
    }
    LOG_INFO("Received %d byte clipboard message from server!", fmsg_size);
    if (!clipboard_synchronizer_set_clipboard(&fmsg->clipboard)) {
        LOG_ERROR("Failed to set local clipboard from server message.");
        return -1;
    }
    return 0;
}

static int handle_window_title_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    // Since only the main thread is allowed to perform UI functionality on MacOS, instead of
    // calling SDL_SetWindowTitle directly, this function updates a global variable window_title.
    // The main thread periodically polls this variable to determine if it needs to update the
    // window title.
    LOG_INFO("Received window title message from server!");
    while (should_update_window_title) {
        // wait for the main thread to process the previous request
    }

    // format title so it ends with (Fractal)
    char* title = &fmsg->window_title;
    const char suffix[] = " (Fractal)";
    size_t len = strlen(title) + strlen(suffix) + 1;
    window_title = malloc(len);
    strncpy(window_title, title, strlen(title));
    strncpy(window_title + strlen(title), suffix, strlen(suffix));
    window_title[len - 1] = '\0';

    should_update_window_title = true;
    return 0;
}
