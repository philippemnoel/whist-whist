/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
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

#include <whist/core/whist.h>

extern "C" {
#include "handle_server_message.h"

#include <stddef.h>

#include <whist/file/file_synchronizer.h>
#include <whist/utils/clock.h>
#include <whist/logging/logging.h>
#include <whist/logging/log_statistic.h>
#include <whist/utils/window_info.h>
#include <whist/utils/os_utils.h>
#include "client_utils.h"
#include "audio.h"
#include "network.h"
#include "sdl_utils.h"

#include <stddef.h>
}

std::atomic<bool> client_exiting = false;
std::atomic<bool> upload_initiated = false;

/*
============================
Private Functions
============================
*/

static int handle_quit_message(WhistServerMessage *wsmsg, size_t wsmsg_size);
static int handle_clipboard_message(WhistServerMessage *wsmsg, size_t wsmsg_size);
static int handle_fullscreen_message(WhistServerMessage *wsmsg, size_t wsmsg_size);
static int handle_file_metadata_message(WhistServerMessage *wsmsg, size_t wsmsg_size,
                                        WhistFrontend *frontend);
static int handle_file_chunk_message(WhistServerMessage *wsmsg, size_t wsmsg_size,
                                     WhistFrontend *frontend);
static int handle_file_group_end_message(WhistServerMessage *wsmsg, size_t wsmsg_size);
static int handle_notification_message(WhistServerMessage *wsmsg, size_t wsmsg_size);
static int handle_upload_message(WhistServerMessage *wsmsg, size_t wsmsg_size);

/*
============================
Private Function Implementations
============================
*/

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int handle_server_message(WhistServerMessage *wsmsg, size_t wsmsg_size, WhistFrontend *frontend) {
    /*
        Handle message packets from the server

        Arguments:
            wsmsg (WhistServerMessage*): server message packet
            wsmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    switch (wsmsg->type) {
        case SMESSAGE_QUIT:
            return handle_quit_message(wsmsg, wsmsg_size);
        case SMESSAGE_CLIPBOARD:
            return handle_clipboard_message(wsmsg, wsmsg_size);
        case SMESSAGE_FULLSCREEN:
            return handle_fullscreen_message(wsmsg, wsmsg_size);
        case SMESSAGE_FILE_DATA:
            return handle_file_chunk_message(wsmsg, wsmsg_size, frontend);
        case SMESSAGE_FILE_METADATA:
            return handle_file_metadata_message(wsmsg, wsmsg_size, frontend);
        case SMESSAGE_FILE_GROUP_END:
            return handle_file_group_end_message(wsmsg, wsmsg_size);
        case SMESSAGE_NOTIFICATION:
            return handle_notification_message(wsmsg, wsmsg_size);
        case SMESSAGE_INITIATE_UPLOAD:
            return handle_upload_message(wsmsg, wsmsg_size);
        default:
            LOG_WARNING("Unknown WhistServerMessage Received (type: %d)", wsmsg->type);
            return -1;
    }
}

static int handle_quit_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle server quit message

        Arguments:
            wsmsg (WhistServerMessage*): server quit message
            wsmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    UNUSED(wsmsg);
    if (wsmsg_size != sizeof(WhistServerMessage)) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: quit message)!");
        return -1;
    }
    LOG_INFO("Server signaled a quit!");
    client_exiting = true;
    return 0;
}

static int handle_clipboard_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle server clipboard message

        Arguments:
            wsmsg (WhistServerMessage*): server clipboard message
            wsmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    if (wsmsg_size != sizeof(WhistServerMessage) + wsmsg->clipboard.size) {
        LOG_ERROR(
            "Incorrect message size for a server message"
            " (type: clipboard message)! Expected %zu, but received %zu",
            sizeof(WhistServerMessage) + wsmsg->clipboard.size, wsmsg_size);
        return -1;
    }
    LOG_INFO("Received %zu byte clipboard message from server!", wsmsg_size);
    // Known to run in less than ~100 assembly instructions
    push_clipboard_chunk(&wsmsg->clipboard);
    return 0;
}

static int handle_fullscreen_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle server fullscreen message (enter or exit events)

        Arguments:
            wsmsg (WhistServerMessage*): server fullscreen message
            wsmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    LOG_INFO("Received fullscreen message from the server! Value: %d", wsmsg->fullscreen);

    sdl_set_fullscreen(0, wsmsg->fullscreen);

    return 0;
}

static int handle_file_metadata_message(WhistServerMessage *wsmsg, size_t wsmsg_size,
                                        WhistFrontend *frontend) {
    /*
        Handle a file metadata message.

        Arguments:
            wsmsg (WhistServerMessage*): message packet from client
        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    TransferringFile *active_file = file_synchronizer_open_file_for_writing(&wsmsg->file_metadata);
    active_file->opaque = whist_frontend_file_download_start(frontend, active_file->file_path,
                                                             wsmsg->file_metadata.file_size);

    return 0;
}

static int handle_file_chunk_message(WhistServerMessage *wsmsg, size_t wsmsg_size,
                                     WhistFrontend *frontend) {
    /*
        Handle a file chunk message.

        Arguments:
            wsmsg (WhistServerMessage*): message packet from client
        Returns:
            (int): Returns -1 on failure, 0 on success
    */
    TransferringFile *active_file = file_synchronizer_write_file_chunk(
        &wsmsg->file, whist_frontend_file_download_complete, frontend);
    if (active_file) {
        whist_frontend_file_download_update(frontend, active_file->opaque,
                                            active_file->bytes_written, active_file->bytes_per_sec);
    }

    return 0;
}

static int handle_file_group_end_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle a file group end message.

        Arguments:
            wsmsg (WhistServerMessage*): message packet from client
        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    // This is a no-op for now, but should be filled if it becomes relevant for handling
    // multiple files transferred in a group.

    return 0;
}

static int handle_notification_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle a file chunk message.

        Arguments:
            wsmsg (WhistServerMessage*): message packet from client
        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    sdl_client_display_notification(&wsmsg->notif);
    log_double_statistic(NOTIFICATIONS_RECEIVED, 1.);

    return 0;
}

static int handle_upload_message(WhistServerMessage *wsmsg, size_t wsmsg_size) {
    /*
        Handle initiate upload trigger message from server.
        The macOS filepicker must be called from the main thread and this function does
        not run on the main thread. This is why we use the global variable upload_initiated
        which is monitored in the main thread in whist_client.c (instead of just handling
        the upload here). When upload_initiated is true the main thread initiates a file
        dialog and the corresponding transfer.

        Arguments:
            wsmsg (WhistServerMessage*): server clipboard message
            wsmsg_size (size_t): size of the packet message contents

        Return:
            (int): 0 on success, -1 on failure
    */

    upload_initiated = true;
    LOG_INFO("Received upload trigger from server");
    return 0;
}
