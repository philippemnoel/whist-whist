/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file network.c
 * @brief This file contains client-specific wrappers to low-level network
 *        functions.
============================
Usage
============================
discover_ports, connect_to_server, close_connections, and send_server_quit_messages are used to
start and end connections to the Whist server. To connect, call discover_ports, then
connect_to_server. To disconnect, send_server_quit_messages and then close_connections.

To communicate with the server, use send_fcmsg to send Whist messages to the server. Large fcmsg's
(e.g. clipboard messages) are sent over TCP; otherwise, messages are sent over UDP. Use update_ping
to ping the server at regular intervals, and receive_pong to receive pongs (ping acknowledgements)
from the server.
*/

/*
============================
Includes
============================
*/

#include "network.h"

#include <fractal/core/fractal.h>
#include <fractal/logging/error_monitor.h>
#include "network.h"
#include "client_utils.h"
#include "audio.h"

// Init information
extern char user_email[WHIST_ARGS_MAXLEN + 1];

// Data
extern volatile char client_binary_aes_private_key[16];
int udp_port = -1;
int tcp_port = -1;
SocketContext packet_udp_context = {0};
SocketContext packet_tcp_context = {0};
extern char *server_ip;
int uid;
extern bool using_stun;

volatile double latency;
extern clock last_ping_timer;
extern volatile int last_ping_id;
extern volatile int ping_failures;
extern volatile int last_pong_id;
const double ping_lambda = 0.8;

extern clock last_tcp_ping_timer;
extern volatile int last_tcp_ping_id;
extern volatile int last_tcp_pong_id;

#define TCP_CONNECTION_WAIT 300  // ms
#define UDP_CONNECTION_WAIT 300  // ms
// Controls the timeouts of read_packet on UDP
// 0ms hurts laptop batteries, but 1ms keeps update_video/update_audio live
#define UDP_CONNECTION_TIMEOUT 1  // ms
// Controls the timeout of read_packet on TCP
#define TCP_CONNECTION_TIMEOUT 1  // ms

/*
============================
Public Function Implementations
============================
*/

int discover_ports(bool *with_stun) {
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
    LOG_INFO("Trying to connect (Using STUN: %s)", *with_stun ? "true" : "false");
    if (!create_tcp_socket_context(&context, server_ip, PORT_DISCOVERY, 1, TCP_CONNECTION_WAIT,
                                   *with_stun, (char *)client_binary_aes_private_key)) {
        /*
                *using_stun = !*using_stun;
                LOG_INFO("Trying to connect (Using STUN: %s)", *using_stun ? "true" : "false");
                if (create_tcp_socket_context(&context, server_ip, PORT_DISCOVERY, 1,
           TCP_CONNECTION_WAIT, *using_stun, (char *)client_binary_aes_private_key) < 0) {
        */
        LOG_WARNING("Failed to connect to server's discovery port.");
        return -1;
        /*
                }
        */
    }

    // Create and send discovery request packet
    FractalClientMessage fcmsg = {0};
    fcmsg.type = MESSAGE_DISCOVERY_REQUEST;
    fcmsg.discoveryRequest.user_id = uid;

    prepare_init_to_server(&fcmsg.discoveryRequest, user_email);

    if (send_packet(&context, PACKET_MESSAGE, (uint8_t *)&fcmsg, (int)sizeof(fcmsg), -1) < 0) {
        LOG_ERROR("Failed to send discovery request message.");
        destroy_socket_context(&context);
        return -1;
    }
    LOG_INFO("Sent discovery packet");

    // Receive discovery packets from server
    FractalPacket *tcp_packet = NULL;
    clock timer;
    start_timer(&timer);
    do {
        tcp_packet = read_packet(&context, true);
        SDL_Delay(5);
    } while (tcp_packet == NULL && get_timer(timer) < 5.0);

    // If no tcp packet was found, just return -1
    // Otherwise, parse the tcp packet's FractalDiscoveryReplyMessage
    if (tcp_packet == NULL) {
        LOG_WARNING("Did not receive discovery packet from server.");
        destroy_socket_context(&context);
        return -1;
    }

    if (tcp_packet->payload_size !=
        sizeof(FractalServerMessage) + sizeof(FractalDiscoveryReplyMessage)) {
        LOG_ERROR(
            "Incorrect discovery reply message size. Expected: %d, Received: "
            "%d",
            sizeof(FractalServerMessage) + sizeof(FractalDiscoveryReplyMessage),
            tcp_packet->payload_size);
        free_packet(&context, tcp_packet);
        destroy_socket_context(&context);
        return -1;
    }

    FractalServerMessage *fsmsg = (FractalServerMessage *)tcp_packet->data;
    if (fsmsg->type != MESSAGE_DISCOVERY_REPLY) {
        LOG_ERROR("Message not of discovery reply type (Type: %d)", fsmsg->type);
        free_packet(&context, tcp_packet);
        destroy_socket_context(&context);
        return -1;
    }

    LOG_INFO("Received discovery info packet from server!");

    // Create and send discovery reply message
    FractalDiscoveryReplyMessage *reply_msg =
        (FractalDiscoveryReplyMessage *)fsmsg->discovery_reply;

    set_audio_frequency(reply_msg->audio_sample_rate);
    udp_port = reply_msg->udp_port;
    tcp_port = reply_msg->tcp_port;
    LOG_INFO("Using UDP Port: %d, TCP Port: %d. Audio frequency: %d.", udp_port, tcp_port,
             reply_msg->audio_sample_rate);

    error_monitor_set_connection_id(reply_msg->connection_id);

    free_packet(&context, tcp_packet);
    destroy_socket_context(&context);
    return 0;
}

void send_ping(int ping_id) {
    /*
        Send a ping to the server with the given ping_id.

        Arguments:
            ping_id (int): Ping ID to send to the server
    */
    FractalClientMessage fcmsg = {0};
    fcmsg.type = MESSAGE_PING;
    fcmsg.ping_id = ping_id;

    LOG_INFO("Ping! %d", ping_id);
    if (send_fcmsg(&fcmsg) != 0) {
        LOG_WARNING("Failed to ping server! (ID: %d)", ping_id);
    }
    last_ping_id = ping_id;
    start_timer(&last_ping_timer);
}

void send_tcp_ping(int ping_id) {
    /*
        Send a TCP ping to the server with the given ping_id.

        Arguments:
            ping_id (int): Ping ID to send to the server
    */

    FractalClientMessage fcmsg = {0};
    fcmsg.type = MESSAGE_TCP_PING;
    fcmsg.ping_id = ping_id;

    LOG_INFO("TCP Ping! %d", ping_id);
    if (send_fcmsg(&fcmsg) != 0) {
        LOG_WARNING("Failed to TCP ping server! (ID: %d)", ping_id);
    }
    last_tcp_ping_id = ping_id;
    start_timer(&last_tcp_ping_timer);
}

void receive_pong(int pong_id) {
    /*
        Mark the ping with ID pong_id as received, and warn if pong_id is outdated.

        Arguments:
            pong_id (int): ID of pong to receive
    */
    if (pong_id == last_ping_id) {
        // the server received the last ping we sent!
        double ping_time = get_timer(last_ping_timer);
        LOG_INFO("Pong %d received: took %f seconds", pong_id, ping_time);
        LOG_METRIC("\"UDP_RTT\" : %d", (int)(ping_time * 1000.0));  // In milliseconds

        latency = ping_lambda * latency + (1 - ping_lambda) * ping_time;
        ping_failures = 0;
        last_pong_id = pong_id;
    } else {
        LOG_WARNING("Received old pong (ID %d), expected ID %d", pong_id, last_ping_id);
    }
}

void receive_tcp_pong(int pong_id) {
    /*
        Mark the TCP ping with ID pong_id as received, and warn if pong_id is outdated.

        Arguments:
            pong_id (int): ID of pong to receive
    */
    if (pong_id == last_tcp_ping_id) {
        // the server received the last TCP ping we sent!
        double ping_time = get_timer(last_tcp_ping_timer);
        LOG_INFO("TCP Pong %d received: took %f seconds", pong_id, ping_time);

        last_tcp_pong_id = pong_id;
    } else {
        LOG_WARNING("Received old TCP pong (ID %d), expected ID %d", pong_id, last_tcp_ping_id);
    }
}

int connect_to_server(bool with_stun) {
    /*
        Connect to the server. Must be called after `discover_ports()`.

        Arguments:
            using_stun (bool): whether we are using the STUN server

        Return:
            (int): 0 on success, -1 on failure
    */

    LOG_INFO("using stun is %d", with_stun);
    if (udp_port < 0) {
        LOG_ERROR("Trying to connect UDP but port not set.");
        return -1;
    }
    if (tcp_port < 0) {
        LOG_ERROR("Trying to connect TCP but port not set.");
        return -1;
    }

    if (!create_udp_socket_context(&packet_udp_context, server_ip, udp_port, UDP_CONNECTION_TIMEOUT,
                                   UDP_CONNECTION_WAIT, with_stun,
                                   (char *)client_binary_aes_private_key)) {
        LOG_WARNING("Failed to establish UDP connection from server");
        return -1;
    }

    LOG_INFO("create_udp_socket_context() done, current time = %s", current_time_str());

    if (!create_tcp_socket_context(&packet_tcp_context, server_ip, tcp_port, TCP_CONNECTION_TIMEOUT,
                                   TCP_CONNECTION_WAIT, with_stun,
                                   (char *)client_binary_aes_private_key)) {
        LOG_WARNING("Failed to establish TCP connection with server.");
        destroy_socket_context(&packet_udp_context);
        return -1;
    }

    LOG_INFO("create_tcp_socket_context() done, current time = %s", current_time_str());

    return 0;
}

int send_tcp_reconnect_message() {
    /*
        Send a TCP socket reset message to the server, regardless of the initiator of the lost
        connection.

        Returns:
            0 on success, -1 on failure
    */

    FractalClientMessage fcmsg;
    fcmsg.type = MESSAGE_TCP_RECOVERY;

    SocketContext discovery_context;
    if (!create_tcp_socket_context(&discovery_context, (char *)server_ip, PORT_DISCOVERY, 1, 300,
                                   using_stun, (char *)client_binary_aes_private_key)) {
        LOG_WARNING("Failed to connect to server's discovery port.");
        return -1;
    }

    if (send_packet(&discovery_context, PACKET_MESSAGE, (uint8_t *)&fcmsg, (int)sizeof(fcmsg), -1) <
        0) {
        LOG_ERROR("Failed to send discovery request message.");
        destroy_socket_context(&discovery_context);
        return -1;
    }
    destroy_socket_context(&discovery_context);

    // We wouldn't have called closesocket on this socket before, so we can safely call
    //     close regardless of what caused the socket failure without worrying about
    //     undefined behavior.
    destroy_socket_context(&packet_tcp_context);
    if (!create_tcp_socket_context(&packet_tcp_context, (char *)server_ip, tcp_port, 1, 1000,
                                   using_stun, (char *)client_binary_aes_private_key)) {
        LOG_WARNING("Failed to connect to server's TCP port.");
        return -1;
    }

    return 0;
}

int close_connections(void) {
    /*
        Close all connections between client and server

        Return:
            (int): 0 on success
    */

    destroy_socket_context(&packet_udp_context);
    destroy_socket_context(&packet_tcp_context);
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

    FractalClientMessage fcmsg = {0};
    fcmsg.type = CMESSAGE_QUIT;
    int retval = 0;
    for (; num_messages > 0; num_messages--) {
        SDL_Delay(50);
        if (send_fcmsg(&fcmsg) != 0) {
            retval = -1;
        }
    }
    return retval;
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int send_fcmsg(FractalClientMessage *fcmsg) {
    /*
        We send large fcmsg's over TCP. At the moment, this is only CLIPBOARD;
        Currently, sending FractalClientMessage packets over UDP that require multiple
        sub-packets to send is not supported (if low latency large
        FractalClientMessage packets are needed, then this will have to be
        implemented)

        Arguments:
            fcmsg (FractalClientMessage*): pointer to FractalClientMessage to be send

        Return:
            (int): 0 on success, -1 on failure
    */

    // Shouldn't overflow, will take 50 days at 1000 fcmsg/second to overflow
    static unsigned int fcmsg_id = 0;
    fcmsg->id = fcmsg_id;
    fcmsg_id++;

    if (fcmsg->type == CMESSAGE_CLIPBOARD || fcmsg->type == MESSAGE_DISCOVERY_REQUEST ||
        fcmsg->type == MESSAGE_TCP_PING) {
        return send_packet(&packet_tcp_context, PACKET_MESSAGE, fcmsg, get_fcmsg_size(fcmsg), -1);
    } else {
        if ((size_t)get_fcmsg_size(fcmsg) > MAX_PACKET_SIZE) {
            LOG_ERROR(
                "Attempting to send FMSG that is too large for UDP, and only CLIPBOARD, TIME, and "
                "TCP_PING is "
                "presumed to be over TCP");
            return -1;
        }
        static int sent_packet_id = 0;
        sent_packet_id++;

        return send_packet(&packet_udp_context, PACKET_MESSAGE, fcmsg, get_fcmsg_size(fcmsg),
                           sent_packet_id);
    }
}
