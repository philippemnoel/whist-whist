/**
 * Copyright Fractal Computers, Inc. 2020
 * @file handle_server_message.c
 * @brief This file contains all the code for client-side processing of messages
 *        received from the server
============================
Usage
============================

handleServerMessage() must be called on any received message from the server.
Any action trigged a server message must be initiated in network.c.
*/

/*
============================
Includes
============================
*/

#include "handle_server_message.h"

#include <stddef.h>

#include <fractal/core/fractal.h>
#include <fractal/utils/clock.h>
#include <fractal/logging/logging.h>
#include <fractal/utils/window_name.h>
#include "client_utils.h"
#include "audio.h"
#include "network.h"

#include <stddef.h>

extern bool exiting;
extern int audio_frequency;
extern volatile double latency;
extern volatile int try_amount;
extern volatile char *window_title;
extern volatile bool should_update_window_title;

/*
============================
Private Functions
============================
*/

static int handle_pong_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_quit_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_audio_frequency_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_clipboard_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_window_title_message(FractalServerMessage *fmsg, size_t fmsg_size);
static int handle_open_uri_message(FractalServerMessage *fmsg, size_t fmsg_size);

/*
============================
Private Function Implementations
============================
*/

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int handle_server_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle message packets from the server

        Arguments:
            fmsg (FractalServerMessage*): server message packet
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

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
        case SMESSAGE_OPEN_URI:
            return handle_open_uri_message(fmsg, fmsg_size);
        default:
            LOG_WARNING("Unknown FractalServerMessage Received");
            return -1;
    }
}

static int handle_pong_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle server pong message

        Arguments:
            fmsg (FractalServerMessage*): server pong message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    if (fmsg_size != sizeof(FractalServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: pong message)!");
        return -1;
    }
    receive_pong(fmsg->ping_id);
    return 0;
}

static int handle_quit_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle server quit message

        Arguments:
            fmsg (FractalServerMessage*): server quit message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

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
    /*
        Handle server audio frequency message

        Arguments:
            fmsg (FractalServerMessage*): server audio frequency message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    if (fmsg_size != sizeof(FractalServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: audio frequency message)!");
        return -1;
    }
    LOG_INFO("Changing audio frequency to %d", fmsg->frequency);
    set_audio_frequency(fmsg->frequency);
    return 0;
}

static int handle_clipboard_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle server clipboard message

        Arguments:
            fmsg (FractalServerMessage*): server clipboard message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    if (fmsg_size != sizeof(FractalServerMessage) + fmsg->clipboard.size) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: clipboard message)! Expected %d, but received %d",
            sizeof(FractalServerMessage) + fmsg->clipboard.size, fmsg_size);
        return -1;
    }
    LOG_INFO("Received %d byte clipboard message from server!", fmsg_size);
    // Known to run in less than ~100 assembly instructions
    if (!clipboard_synchronizer_set_clipboard_chunk(&fmsg->clipboard)) {
        LOG_ERROR("Failed to set local clipboard from server message.");
        return -1;
    }
    return 0;
}

static int handle_window_title_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle server window title message
        Since only the main thread is allowed to perform UI functionality on MacOS, instead of
        calling SDL_SetWindowTitle directly, this function updates a global variable window_title.
        The main thread periodically polls this variable to determine if it needs to update the
        window title.

        Arguments:
            fmsg (FractalServerMessage*): server window title message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    LOG_INFO("Received window title message from server!");
    if (should_update_window_title) {
        LOG_WARNING(
            "Failed to update window title, as the previous window title update is still pending");
        return -1;
    }

    char *title = (char *)&fmsg->window_title;
    LOG_WARNING("Got title: '%s' of len %lu", title, strlen(title));
    size_t len = strlen(title) + 1;
    char *new_window_title = safe_malloc(len);
    safe_strncpy(new_window_title, title, strlen(title) + 1);
    window_title = new_window_title;

    should_update_window_title = true;
    return 0;
}

static int handle_open_uri_message(FractalServerMessage *fmsg, size_t fmsg_size) {
    /*
        Handle server open URI message by launching the relevant URI locally

        Arguments:
            fmsg (FractalerverMessage*): server open uri message
            fmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */
    LOG_INFO("Received Open URI message from the server!");

#if defined(_WIN32)
#define OPEN_URI_CMD "cmd /c start \"\""
#elif __APPLE__
#define OPEN_URI_CMD "open"
#else
#define OPEN_URI_CMD "xdg-open"
#endif  // _WIN32
// just to be safe from off-by-1 errors
#define OPEN_URI_CMD_MAXLEN 30

    const char *uri = (const char *)&fmsg->requested_uri;
    const int cmd_len = (int)strlen(uri) + OPEN_URI_CMD_MAXLEN + 1;
    char *cmd = safe_malloc(cmd_len);
    memset(cmd, 0, cmd_len);
    snprintf(cmd, cmd_len, OPEN_URI_CMD " \"%s\"", uri);
    runcmd(cmd, NULL);
    free(cmd);
    return 0;
}
