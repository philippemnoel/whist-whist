/**
 * Copyright Fractal Computers, Inc. 2020
 * @file network.c
 * @brief This file contains client-specific wrappers to low-level network
 *        functions.
============================
Usage
============================

Use these functions for any client-specific networking needs.
*/

/*
============================
Includes
============================
*/

#include "network.h"

#include <fractal/core/fractal.h>
#include "network.h"
#include "client_utils.h"

// Init information
extern volatile int audio_frequency;
extern char user_email[FRACTAL_ARGS_MAXLEN + 1];

// Data
extern volatile char binary_aes_private_key[16];
extern char filename[300];
extern char username[50];
extern int udp_port;
extern int tcp_port;
extern int client_id;
extern SocketContext packet_send_context;
extern SocketContext packet_receive_context;
extern SocketContext packet_tcp_context;
extern char *server_ip;
extern int uid;

#define TCP_CONNECTION_WAIT 300  // ms
#define UDP_CONNECTION_WAIT 300  // ms

/*
============================
Public Function Implementations
============================
*/

int discover_ports(bool *using_stun) {
    /*
        Send a discovery packet to the server to determine which TCP
        and UDP packets are assigned to the client. Must be called
        before `connect_to_server()`.

        Arguments:
            using_stun (bool*): whether we are using the STUN server

        Return:
            (int): 0 on success, -1 on failure
    */

    // Create TCP context
    SocketContext context;
    LOG_INFO("Trying to connect (Using STUN: %s)", *using_stun ? "true" : "false");
    if (create_tcp_context(&context, server_ip, PORT_DISCOVERY, 1, TCP_CONNECTION_WAIT, *using_stun,
                           (char *)binary_aes_private_key) < 0) {
        *using_stun = !*using_stun;
        LOG_INFO("Trying to connect (Using STUN: %s)", *using_stun ? "true" : "false");
        if (create_tcp_context(&context, server_ip, PORT_DISCOVERY, 1, TCP_CONNECTION_WAIT,
                               *using_stun, (char *)binary_aes_private_key) < 0) {
            LOG_WARNING("Failed to connect to server's discovery port.");
            return -1;
        }
    }

    // Create and send discovery request packet
    FractalClientMessage fcmsg = {0};
    fcmsg.type = MESSAGE_DISCOVERY_REQUEST;
    fcmsg.discoveryRequest.user_id = uid;

    prepare_init_to_server(&fcmsg.discoveryRequest, user_email);

    if (send_tcp_packet(&context, PACKET_MESSAGE, (uint8_t *)&fcmsg, (int)sizeof(fcmsg)) < 0) {
        LOG_ERROR("Failed to send discovery request message.");
        closesocket(context.s);
        return -1;
    }
    LOG_INFO("Sent discovery packet");

    // Receive discovery packets from server
    FractalPacket *packet;
    clock timer;
    start_timer(&timer);
    do {
        packet = read_tcp_packet(&context, true);
        SDL_Delay(5);
    } while (packet == NULL && get_timer(timer) < 5.0);
    closesocket(context.s);

    if (packet == NULL) {
        LOG_WARNING("Did not receive discovery packet from server.");
        return -1;
    }

    FractalServerMessage *fsmsg = (FractalServerMessage *)packet->data;
    if (packet->payload_size !=
        sizeof(FractalServerMessage) + sizeof(FractalDiscoveryReplyMessage)) {
        LOG_ERROR(
            "Incorrect discovery reply message size. Expected: %d, Received: "
            "%d",
            sizeof(FractalServerMessage) + sizeof(FractalDiscoveryReplyMessage),
            packet->payload_size);
        return -1;
    }
    if (fsmsg->type != MESSAGE_DISCOVERY_REPLY) {
        LOG_ERROR("Message not of discovery reply type (Type: %d)", fsmsg->type);
        return -1;
    }

    LOG_INFO("Received discovery info packet from server!");

    // Create and send discovery reply message
    FractalDiscoveryReplyMessage *reply_msg =
        (FractalDiscoveryReplyMessage *)fsmsg->discovery_reply;
    if (reply_msg->client_id == -1) {
        LOG_ERROR("Not awarded a client id from server.");
        return -1;
    }

    client_id = reply_msg->client_id;
    audio_frequency = reply_msg->audio_sample_rate;
    udp_port = reply_msg->UDP_port;
    tcp_port = reply_msg->TCP_port;
    LOG_INFO("Assigned client ID: %d. UDP Port: %d, TCP Port: %d", client_id, udp_port, tcp_port);

    memcpy(filename, reply_msg->filename, min(sizeof(filename), sizeof(reply_msg->filename)));
    memcpy(username, reply_msg->username, min(sizeof(username), sizeof(reply_msg->username)));

    if (log_connection_id(reply_msg->connection_id) < 0) {
        LOG_ERROR("Failed to log connection ID.");
        return -1;
    }

    return 0;
}

int connect_to_server(bool using_stun) {
    /*
        Connect to the server. Must be called after `discover_ports()`.

        Arguments:
            using_stun (bool): whether we are using the STUN server

        Return:
            (int): 0 on success, -1 on failure
    */

    LOG_INFO("using stun is %d", using_stun);
    if (udp_port < 0) {
        LOG_ERROR("Trying to connect UDP but port not set.");
        return -1;
    }
    if (tcp_port < 0) {
        LOG_ERROR("Trying to connect TCP but port not set.");
        return -1;
    }

    if (create_udp_context(&packet_send_context, server_ip, udp_port, 10, UDP_CONNECTION_WAIT,
                           using_stun, (char *)binary_aes_private_key) < 0) {
        LOG_WARNING("Failed establish UDP connection from server");
        return -1;
    }

    // socket options = TCP_NODELAY IPTOS_LOWDELAY SO_RCVBUF=65536
    // Windows Socket 65535 Socket options apply to all sockets.
    int a = 65535;
    if (setsockopt(packet_send_context.s, SOL_SOCKET, SO_RCVBUF, (const char *)&a, sizeof(int)) ==
        -1) {
        LOG_ERROR("Error setting socket opts: %d", get_last_network_error());
        return -1;
    }

    if (create_tcp_context(&packet_tcp_context, server_ip, tcp_port, 1, TCP_CONNECTION_WAIT,
                           using_stun, (char *)binary_aes_private_key) < 0) {
        LOG_ERROR("Failed to establish TCP connection with server.");
        closesocket(packet_send_context.s);
        return -1;
    }

    packet_receive_context = packet_send_context;

    return 0;
}

int close_connections(void) {
    /*
        Close all connections between client and server

        Return:
            (int): 0 on success
    */

    closesocket(packet_send_context.s);
    closesocket(packet_receive_context.s);
    closesocket(packet_tcp_context.s);
    return 0;
}

int send_server_quit_messages(int num_messages) {
    /*
        Send quit message to the server

        Arguments:
            num_messages (int): the number of quit packets
                to send to the server

        Return:
            (int): 0 on success, -1 on failure
    */

    FractalClientMessage fmsg = {0};
    fmsg.type = CMESSAGE_QUIT;
    int retval = 0;
    for (; num_messages > 0; num_messages--) {
        SDL_Delay(50);
        if (send_fmsg(&fmsg) != 0) {
            retval = -1;
        }
    }
    return retval;
}

int send_fmsg(FractalClientMessage *fmsg) {
    /*
        We send large fmsg's over TCP. At the moment, this is only CLIPBOARD;
        Currently, sending FractalClientMessage packets over UDP that require multiple
        sub-packets to send is not supported (if low latency large
        FractalClientMessage packets are needed, then this will have to be
        implemented)

        Arguments:
            fmsg (FractalClientMessage*): pointer to FractalClientMessage to be send

        Return:
            (int): 0 on success, -1 on failure
    */

    // Shouldn't overflow, will take 50 days at 1000 fmsg/second to overflow
    static unsigned int fmsg_id = 0;
    fmsg->id = fmsg_id;
    fmsg_id++;

    if (fmsg->type == CMESSAGE_CLIPBOARD || fmsg->type == MESSAGE_DISCOVERY_REQUEST) {
        return send_tcp_packet(&packet_tcp_context, PACKET_MESSAGE, fmsg, get_fmsg_size(fmsg));
    } else {
        if ((size_t)get_fmsg_size(fmsg) > MAX_PACKET_SIZE) {
            LOG_ERROR(
                "Attempting to send FMSG that is too large for UDP, and only CLIPBOARD and TIME is "
                "presumed to be over TCP");
            return -1;
        }
        static int sent_packet_id = 0;
        sent_packet_id++;
        return send_udp_packet(&packet_send_context, PACKET_MESSAGE, fmsg, get_fmsg_size(fmsg),
                               sent_packet_id, -1, NULL, NULL);
    }
}
