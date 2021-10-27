#ifndef FRACTAL_TCP_H
#define FRACTAL_TCP_H
/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file tcp.h
 * @brief This file contains the network interface code for TCP protocol
 *
 *
============================
Usage
============================

To create the context:
tcp_context = create_tcp_network_context(...);

To send a packet from payload:
tcp_context->send_packet_from_payload(...);

To read a packet:
tcp_context->read_packet(...);

To free a packet:
tcp_context->free_packet(...);
 */
#include "network.h"

/**
 * @brief Creates a tcp network context and initializes a TCP connection
 *        between a server and a client
 *
 * @param context                   The socket context that will be initialized
 * @param destination               The server IP address to connect to. Passing
 *                                  NULL will wait for another client to connect
 *                                  to the socket
 * @param port                      The port to connect to. It will be a virtual
 *                                  port.
 * @param recvfrom_timeout_s        The timeout that the socketcontext will use
 *                                  after being initialized
 * @param connection_timeout_ms     The timeout that will be used when attempting
 *                                  to connect. The handshake sends a few packets
 *                                  back and forth, so the upper bound of how
 *                                  long CreateUDPSocketContext will take is
 * @param using_stun                True/false for whether or not to use the STUN server for this
 *                                  context
 *                                  some small constant times connection_timeout_ms
 * @param binary_aes_private_key    The AES private key used to encrypt the socket
 *                                  communication
 * @return                          The TCP network context on success, NULL
 *                                  on failure
 */
bool create_tcp_socket_context(SocketContext* context, char* destination, int port,
                               int recvfrom_timeout_s, int connection_timeout_ms, bool using_stun,
                               char* binary_aes_private_key);

#endif  // FRACTAL_TCP_H
