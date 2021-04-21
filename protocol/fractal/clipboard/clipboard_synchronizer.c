/**
 * Copyright Fractal Computers, Inc. 2020
 * @file clipboard_synchronizer.c
 * @brief This file contains code meant, to be used by both the server and client
 *        to synchronize clipboard contents, both setting and gettings. Client
 *        and server clipboard activities are parallel, and therefore the
 *        synchronizer abstracts out and allows threading of clipboard actions.
============================
Usage
============================

initClipboardSynchronizer();

ClipboardData server_clipboard;

// Will set the client clipboard to that data
ClipboardSynchronizerSetClipboard(&server_clipboard);

// Will likely return true because it's waiting on server_clipboard to be set
LOG_INFO("Is Synchronizing? %d", isClipboardSynchronizing());

// Wait for clipboard to be done synchronizing
while(isClipboardSynchronizing());

ClipboardData* client_clipboard = ClipboardSynchronizerGetNewClipboard();

if (client_clipboard) {
  // We have a new clipboard, this should be sent to the server
  Send(client_clipboard);
} else {
  // There is no new clipboard
}

destroyClipboardSynchronizer();
*/

#include <stdio.h>

#include <fractal/core/fractal.h>
#include "clipboard.h"

#define MS_IN_SECOND 1000

int update_clipboard(void* opaque);

extern char filename[300];
extern char username[50];
volatile bool updating_set_clipboard;  // set to true when SetClipboard() needs to be called
volatile bool updating_get_clipboard;  // set to true when GetClipboard() needs to be called
volatile bool updating_clipboard;  // acts as a mutex to prevent clipboard activity from overlapping
volatile bool pending_update_clipboard;  // set to true when GetClipboard() has finished running
clock last_clipboard_update;
FractalSemaphore clipboard_semaphore;  // used to signal clipboard_synchronizer_thread to continue
ClipboardData* clipboard;
FractalThread clipboard_synchronizer_thread;
static bool connected = false;

bool pending_clipboard_push;

bool is_clipboard_synchronizing() {
    if (!connected) {
        LOG_ERROR("Tried to is_clipboard_synchronizing, but the clipboard is not initialized");
        return true;
    }
    return updating_clipboard;
}

void init_clipboard_synchronizer(bool is_client) {
    /*
        Initialize the clipboard and the synchronizer thread

        Arguments:
            is_client (bool): whether the caller is the client (true) or the server (false)
    */

    LOG_INFO("Initializing clipboard");

    if (connected) {
        LOG_ERROR("Tried to init_clipboard, but the clipboard is already initialized");
        return;
    }

    init_clipboard(is_client);

    connected = true;

    pending_clipboard_push = false;
    updating_clipboard = false;
    pending_update_clipboard = false;
    start_timer((clock*)&last_clipboard_update);
    clipboard_semaphore = fractal_create_semaphore(0);

    clipboard_synchronizer_thread = fractal_create_thread(update_clipboard, "update_clipboard", NULL);

    pending_update_clipboard = true;
}

void destroy_clipboard_synchronizer() {
    /*
        Destroy the clipboard synchronizer
    */

    LOG_INFO("Destroying clipboard");

    if (!connected) {
        LOG_ERROR("Tried to destroy_clipboard, but the clipboard is already destroyed");
        return;
    }

    connected = false;

    if (updating_clipboard) {
        LOG_FATAL("Trying to destroy clipboard while the clipboard is being updated");
    }

    destroy_clipboard();

    fractal_post_semaphore(clipboard_semaphore);
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
bool clipboard_synchronizer_set_clipboard(ClipboardData* cb) {
    /*
        When called, signal that the clipboard can be set to the given contents

        Arguments:
            cb (ClipboardData*): pointer to the clipboard to load

        Returns:
            updatable (bool): whether the clipboard can be set right now
    */

    if (!connected) {
        LOG_ERROR("Tried to set_clipboard, but the clipboard is not initialized");
        return false;
    }

    if (updating_clipboard) {
        LOG_INFO("Tried to SetClipboard, but clipboard is updating");
        return false;
    }

    updating_clipboard = true;
    updating_set_clipboard = true;
    updating_get_clipboard = false;
    clipboard = cb;

    fractal_post_semaphore(clipboard_semaphore);

    return true;
}

ClipboardData* clipboard_synchronizer_get_new_clipboard() {
    /*
        When called, return the current clipboard if a new clipboard activity
        has registered.

        Returns:
            cb (ClipboardData*): pointer to the current clipboard
    */

    if (!connected) {
        LOG_ERROR("Tried to get_new_clipboard, but the clipboard is not initialized");
        return NULL;
    }

    if (pending_clipboard_push) {
        pending_clipboard_push = false;
        return clipboard;
    }

    if (updating_clipboard) {
        return NULL;
    }

    // If the clipboard has updated since we last checked, or a previous
    // clipboard update is still pending, then we try to update the clipboard
    if (has_clipboard_updated() || pending_update_clipboard) {
        if (updating_clipboard) {
            // Clipboard is busy, to set pending update clipboard to true to
            // make sure we keep checking the clipboard state
            pending_update_clipboard = true;
        } else {
            LOG_INFO("Pushing update to clipboard");
            // Clipboard is no longer pending
            pending_update_clipboard = false;
            updating_clipboard = true;
            updating_set_clipboard = false;
            updating_get_clipboard = true;
            fractal_post_semaphore(clipboard_semaphore);
        }
    }

    return NULL;
}

int update_clipboard(void* opaque) {
    /*
        Thread to get and set clipboard as signals are received.
    */

    UNUSED(opaque);

    while (connected) {
        fractal_wait_semaphore(clipboard_semaphore);

        if (!connected) {
            break;
        }

        if (updating_set_clipboard) {
            LOG_INFO("Trying to set clipboard!");

            set_clipboard(clipboard);
            updating_set_clipboard = false;
        } else if (updating_get_clipboard) {
            LOG_INFO("Trying to get clipboard!");

            clipboard = get_clipboard();
            pending_clipboard_push = true;
            updating_get_clipboard = false;
        } else {
            clock clipboard_time;
            start_timer(&clipboard_time);

            // If it hasn't been 500ms yet, then wait 500ms to prevent too much
            // spam
            const int spam_time_ms = 500;
            if (get_timer(clipboard_time) < spam_time_ms / (double)MS_IN_SECOND) {
                fractal_sleep(max((int)(spam_time_ms - MS_IN_SECOND * get_timer(clipboard_time)), 1));
            }
        }

        LOG_INFO("Updated clipboard!");
        updating_clipboard = false;
    }

    return 0;
}
