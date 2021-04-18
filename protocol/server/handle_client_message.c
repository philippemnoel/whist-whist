/**
 * Copyright Fractal Computers, Inc. 2020
 * @file client_message_handler.c
 * @brief This file contains all the code for server-side processing of messages
 *        received from a client
============================
Usage
============================

handle_client_message() must be called on any received message from a client.
*/

/*
============================
Includes
============================
*/

#include <fractal/core/fractal.h>

#include <fractal/input/input.h>
#include <fractal/network/network.h>
#include <fractal/utils/clock.h>
#include <fractal/utils/logging.h>
#include "client.h"
#include "handle_client_message.h"
#include "network.h"
#include "webserver.h"

#ifdef _WIN32
#include <fractal/utils/windows_utils.h>
#endif

extern Client clients[MAX_NUM_CLIENTS];

#define VIDEO_BUFFER_SIZE 25
#define MAX_VIDEO_INDEX 500
extern FractalPacket video_buffer[VIDEO_BUFFER_SIZE][MAX_VIDEO_INDEX];
extern int video_buffer_packet_len[VIDEO_BUFFER_SIZE][MAX_VIDEO_INDEX];

#define AUDIO_BUFFER_SIZE 100
#define MAX_NUM_AUDIO_INDICES 3
extern FractalPacket audio_buffer[AUDIO_BUFFER_SIZE][MAX_NUM_AUDIO_INDICES];
extern int audio_buffer_packet_len[AUDIO_BUFFER_SIZE][MAX_NUM_AUDIO_INDICES];

extern volatile double max_mbps;
extern volatile int client_width;
extern volatile int client_height;
extern volatile int client_dpi;
extern volatile bool update_device;
extern volatile CodecType client_codec_type;

extern volatile bool wants_iframe;
extern volatile bool update_encoder;
extern InputDevice *input_device;

extern int host_id;

extern bool using_sentry;

/*
============================
Private Functions
============================
*/

static int handle_user_input_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling);
static int handle_keyboard_state_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling);
static int handle_bitrate_message(FractalClientMessage *fmsg, int client_id, bool is_controlling);
static int handle_ping_message(FractalClientMessage *fmsg, int client_id, bool is_controlling);
static int handle_dimensions_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling);
static int handle_clipboard_message(FractalClientMessage *fmsg, int client_id, bool is_controlling);
static int handle_audio_nack_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling);
static int handle_video_nack_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling);
static int handle_iframe_request_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling);
static int handle_interaction_mode_message(FractalClientMessage *fmsg, int client_id,
                                           bool is_controlling);
static int handle_quit_message(FractalClientMessage *fmsg, int client_id, bool is_controlling);
static int handle_init_message(FractalClientMessage *fmsg, int client_id, bool is_controlling);
static int handle_mouse_inactive_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling);

/*
============================
Public Function Implementations
============================
*/

int handle_client_message(FractalClientMessage *fmsg, int client_id, bool is_controlling) {
    /*
        Handle message from the client.

        NOTE: Needs read is_active_rwlock

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    switch (fmsg->type) {
        case MESSAGE_KEYBOARD:
        case MESSAGE_MOUSE_BUTTON:
        case MESSAGE_MOUSE_WHEEL:
        case MESSAGE_MOUSE_MOTION:
        case MESSAGE_MULTIGESTURE:
            return handle_user_input_message(fmsg, client_id, is_controlling);
        case MESSAGE_KEYBOARD_STATE:
            return handle_keyboard_state_message(fmsg, client_id, is_controlling);
        case MESSAGE_MBPS:
            return handle_bitrate_message(fmsg, client_id, is_controlling);
        case MESSAGE_PING:
            return handle_ping_message(fmsg, client_id, is_controlling);
        case MESSAGE_DIMENSIONS:
            return handle_dimensions_message(fmsg, client_id, is_controlling);
        case CMESSAGE_CLIPBOARD:
            return handle_clipboard_message(fmsg, client_id, is_controlling);
        case MESSAGE_AUDIO_NACK:
            return handle_audio_nack_message(fmsg, client_id, is_controlling);
        case MESSAGE_VIDEO_NACK:
            return handle_video_nack_message(fmsg, client_id, is_controlling);
        case MESSAGE_IFRAME_REQUEST:
            return handle_iframe_request_message(fmsg, client_id, is_controlling);
        case CMESSAGE_INTERACTION_MODE:
            return handle_interaction_mode_message(fmsg, client_id, is_controlling);
        case CMESSAGE_QUIT:
            return handle_quit_message(fmsg, client_id, is_controlling);
        case MESSAGE_DISCOVERY_REQUEST:
            return handle_init_message(fmsg, client_id, is_controlling);
        case MESSAGE_MOUSE_INACTIVE:
            return handle_mouse_inactive_message(fmsg, client_id, is_controlling);
        default:
            LOG_WARNING(
                "Unknown FractalClientMessage Received. "
                "(Type: %d)",
                fmsg->type);
            return -1;
    }
}

/*
============================
Private Function Implementations
============================
*/

static int handle_user_input_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling) {
    /*
        Handle a user input message.

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    // Replay user input (keyboard or mouse presses)
    if (is_controlling && input_device) {
        if (!replay_user_input(input_device, fmsg)) {
            LOG_WARNING("Failed to replay input!");
#ifdef _WIN32
            // TODO: Ensure that any password can be used here
            init_desktop(input_device, "password1234567.");
#endif
        }
    }

    if (fmsg->type == MESSAGE_MOUSE_MOTION) {
        fractal_lock_mutex(state_lock);
        clients[client_id].mouse.is_active = true;
        clients[client_id].mouse.x = fmsg->mouseMotion.x_nonrel;
        clients[client_id].mouse.y = fmsg->mouseMotion.y_nonrel;
        fractal_unlock_mutex(state_lock);
    }

    return 0;
}

// TODO: Unix version missing
static int handle_keyboard_state_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling) {
    /*
        Handle a user keyboard state change message. Synchronize client and
        server keyboard state

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    if (!is_controlling) return 0;
    update_keyboard_state(input_device, fmsg);
    return 0;
}

static int handle_bitrate_message(FractalClientMessage *fmsg, int client_id, bool is_controlling) {
    /*
        Handle a user bitrate change message and update MBPS.

        NOTE: idk how to handle this

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    if (!is_controlling) return 0;
    LOG_INFO("MSG RECEIVED FOR MBPS: %f", fmsg->mbps);
    max_mbps = max(fmsg->mbps, MINIMUM_BITRATE / 1024.0 / 1024.0);
    // update_encoder = true;
    return 0;
}

static int handle_ping_message(FractalClientMessage *fmsg, int client_id, bool is_controlling) {
    /*
        Handle a user bitrate change message and update MBPS.

        NOTE: idk how to handle this

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(is_controlling);
    LOG_INFO("Ping Received - Client ID: %d, Ping ID %d", client_id, fmsg->ping_id);

    // Update ping timer
    start_timer(&(clients[client_id].last_ping));

    // Send pong reply
    FractalServerMessage fmsg_response = {0};
    fmsg_response.type = MESSAGE_PONG;
    fmsg_response.ping_id = fmsg->ping_id;
    int ret = 0;
    if (send_udp_packet(&(clients[client_id].UDP_context), PACKET_MESSAGE,
                        (uint8_t *)&fmsg_response, sizeof(fmsg_response), 1, STARTING_BURST_BITRATE,
                        NULL, NULL) < 0) {
        LOG_WARNING("Could not send Ping to Client ID: %d", client_id);
        ret = -1;
    }

    return ret;
}

static int handle_dimensions_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling) {
    /*
        Handle a user dimensions change message.

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    if (!is_controlling) return 0;
    // Update knowledge of client monitor dimensions
    LOG_INFO("Request to use dimensions %dx%d received", fmsg->dimensions.width,
             fmsg->dimensions.height);
    if (client_width != fmsg->dimensions.width || client_height != fmsg->dimensions.height ||
        client_codec_type != fmsg->dimensions.codec_type || client_dpi != fmsg->dimensions.dpi) {
        client_width = fmsg->dimensions.width;
        client_height = fmsg->dimensions.height;
        client_codec_type = fmsg->dimensions.codec_type;
        client_dpi = fmsg->dimensions.dpi;
        // Update device if knowledge changed
        update_device = true;
    }
    return 0;
}

static int handle_clipboard_message(FractalClientMessage *fmsg, int client_id,
                                    bool is_controlling) {
    /*
        Handle a clipboard copy message.

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    if (!is_controlling) return 0;
    // Update clipboard with message
    LOG_INFO("Received Clipboard Data! %d", fmsg->clipboard.type);
    if (!clipboard_synchronizer_set_clipboard(&fmsg->clipboard)) {
        LOG_ERROR("Failed to set local clipboard from client message.");
        return -1;
    }
    return 0;
}

static int handle_audio_nack_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling) {
    /*
        Handle a audio nack message and relay the packet

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    if (!is_controlling) return 0;
    // LOG_INFO("Audio NACK requested for: ID %d Index %d",
    // fmsg->nack_data.id, fmsg->nack_data.index);
    FractalPacket *audio_packet =
        &audio_buffer[fmsg->nack_data.id % AUDIO_BUFFER_SIZE][fmsg->nack_data.index];
    int len =
        audio_buffer_packet_len[fmsg->nack_data.id % AUDIO_BUFFER_SIZE][fmsg->nack_data.index];
    if (audio_packet->id == fmsg->nack_data.id) {
        LOG_INFO(
            "NACKed audio packet %d found of length %d. "
            "Relaying!",
            fmsg->nack_data.id, len);
        replay_packet(&(clients[client_id].UDP_context), audio_packet, len);
    }
    // If we were asked for an invalid index, just ignore it
    else if (fmsg->nack_data.index < audio_packet->num_indices) {
        LOG_WARNING(
            "NACKed audio packet %d %d not found, ID %d %d was "
            "located instead.",
            fmsg->nack_data.id, fmsg->nack_data.index, audio_packet->id, audio_packet->index);
    }
    return 0;
}
static int handle_video_nack_message(FractalClientMessage *fmsg, int client_id,
                                     bool is_controlling) {
    /*
        Handle a video nack message and relay the packet

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    if (!is_controlling) return 0;
    // Video nack received, relay the packet

    // LOG_INFO("Video NACK requested for: ID %d Index %d",
    // fmsg->nack_data.id, fmsg->nack_data.index);
    FractalPacket *video_packet =
        &video_buffer[fmsg->nack_data.id % VIDEO_BUFFER_SIZE][fmsg->nack_data.index];
    int len =
        video_buffer_packet_len[fmsg->nack_data.id % VIDEO_BUFFER_SIZE][fmsg->nack_data.index];
    if (video_packet->id == fmsg->nack_data.id) {
        LOG_INFO(
            "NACKed video packet ID %d Index %d found of "
            "length %d. Relaying!",
            fmsg->nack_data.id, fmsg->nack_data.index, len);
        replay_packet(&(clients[client_id].UDP_context), video_packet, len);
    }

    // If we were asked for an invalid index, just ignore it
    else if (fmsg->nack_data.index < video_packet->num_indices) {
        LOG_WARNING(
            "NACKed video packet %d %d not found, ID %d %d was "
            "located instead.",
            fmsg->nack_data.id, fmsg->nack_data.index, video_packet->id, video_packet->index);
    }
    return 0;
}

static int handle_iframe_request_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling) {
    /*
        Handle an IFrame request message

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    UNUSED(is_controlling);
    LOG_INFO("Request for i-frame found: Creating iframe");
    if (fmsg->reinitialize_encoder) {
        update_encoder = true;
    } else {
        if (USING_FFMPEG_IFRAME_FLAG) {
            wants_iframe = true;
        } else {
            update_encoder = true;
        }
    }
    return 0;
}

static int handle_interaction_mode_message(FractalClientMessage *fmsg, int client_id,
                                           bool is_controlling) {
    /*
        Handle an interaction mode message

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(is_controlling);

    /*
    if (fractal_lock_mutex(state_lock) != 0) {
        LOG_ERROR("Failed to lock client's mouse lock.");
        return -1;
    }
    InteractionMode mode = fmsg->interaction_mode;
    if (mode == CONTROL || mode == EXCLUSIVE_CONTROL) {
        if (!clients[client_id].is_controlling) {
            clients[client_id].is_controlling = true;
            num_controlling_clients++;
        }
    } else if (mode == SPECTATE) {
        if (clients[client_id].is_controlling) {
            clients[client_id].is_controlling = false;
            num_controlling_clients--;
            if (num_controlling_clients == 0 && host_id != -1 &&
                clients[host_id].is_active) {
                clients[host_id].is_controlling = true;
            }
        }
    } else {
        LOG_ERROR("Unrecognized interaction mode (Mode: %d)", (int)mode);
        fractal_unlock_mutex(state_lock);
        return -1;
    }

    if (mode == EXCLUSIVE_CONTROL) {
        for (int id = 0; id < MAX_NUM_CLIENTS; id++) {
            if (clients[id].is_active && clients[id].is_controlling &&
                id != client_id) {
                clients[id].is_controlling = false;
            }
        }
    }
    fractal_unlock_mutex(state_lock);
    */
    // Remove below if uncommenting
    UNUSED(fmsg);
    UNUSED(client_id);

    return 0;
}

static int handle_quit_message(FractalClientMessage *fmsg, int client_id, bool is_controlling) {
    /*
        Handle a user quit message

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(is_controlling);
    UNUSED(fmsg);
    int ret = 0;
    read_unlock(&is_active_rwlock);
    write_lock(&is_active_rwlock);
    fractal_lock_mutex(state_lock);
    if (quit_client(client_id) != 0) {
        LOG_ERROR("Failed to quit client. (ID: %d)", client_id);
        ret = -1;
    }
    fractal_unlock_mutex(state_lock);
    write_unlock(&is_active_rwlock);
    read_lock(&is_active_rwlock);
    if (ret == 0) {
        LOG_INFO("Client successfully quit. (ID: %d)", client_id);
    }
    return ret;
}

static int handle_init_message(FractalClientMessage *cfmsg, int client_id, bool is_controlling) {
    /*
        Handle a user init message

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(client_id);
    UNUSED(is_controlling);
    LOG_INFO("Receiving a message time packet");

    FractalDiscoveryRequestMessage fmsg = cfmsg->discoveryRequest;

    // Handle time
    set_time_data(&fmsg.time_data);

    // Handle init email email
    if (using_sentry) {
        if (client_id == host_id) {
            sentry_value_t user = sentry_value_new_object();
            sentry_value_set_by_key(user, "email", sentry_value_new_string(fmsg.user_email));
            sentry_set_user(user);
        } else {
            sentry_send_bread_crumb("info", "non host email: %s", fmsg.user_email);
        }
    }

    return 0;
}

static int handle_mouse_inactive_message(FractalClientMessage *fmsg, int client_id,
                                         bool is_controlling) {
    /*
        Handle a user mouse change to inactive message

        Arguments:
            fmsg (FractalClientMessage*): message package from client
            client_id (int): which client sent the message
            is_controlling (bool): whether the client is controlling, not spectating

        Returns:
            (int): Returns -1 on failure, 0 on success
    */

    UNUSED(fmsg);
    UNUSED(is_controlling);
    clients[client_id].mouse.is_active = false;
    return 0;
}
