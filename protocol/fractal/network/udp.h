#ifndef WHIST_UDP_H
#define WHIST_UDP_H
/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file udp.h
 * @brief This file contains the network interface code for UDP protocol
 *
 *
============================
Usage
============================

To create the context:
udp_context = create_udp_network_context(...);

To send a packet:
udp_context->send_packet(...);

To read a packet:
udp_context->read_packet(...);

To free a packet:
udp_context->free_packet(...);
 */
#include <fractal/core/whist.h>

/**
 * @brief Creates a udp network context and initializes a UDP connection
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
 * @return                          The UDP network context on success, NULL
 *                                  on failure
 */
bool create_udp_socket_context(SocketContext* context, char* destination, int port,
                               int recvfrom_timeout_s, int connection_timeout_ms, bool using_stun,
                               char* binary_aes_private_key);

/**
 * @brief Creates a udp listen socket, that can be used in SocketContext
 *
 * @param sock                      The socket that will be initialized
 * @param port                      The port to listen on
 * @param timeout_ms                The timeout for socket
 * @return                          0 on success, otherwise failure.
 */
int create_udp_listen_socket(SOCKET* sock, int port, int timeout_ms);

/**
 * @brief                          Sets the burst bitrate for the given UDP SocketContext
 *
 * @param context                  The SocketContext that we'll be adjusting the burst bitrate for
 * @param burst_bitrate            The new burst bitrate, in MBPS
 * @param fec_packet_ratio         The percentage of packets that should be FEC
 */
void udp_update_bitrate_settings(SocketContext* context, int burst_bitrate,
                                 double fec_packet_ratio);

/**
 * @brief                          Registers a nack buffer, so that future nacks can be handled.
 *                                 It will be able to respond to nacks from the most recent
 *                                 `buffer_size` ID's that have been send via send_packet
 *                                 NOTE: This function is not thread-safe on SocketContext
 *
 * @param context                  The SocketContext that will have a nack buffer
 * @param type                     The WhistPacketType that this nack buffer will be used for
 * @param max_payload_size         The largest payload that will be saved in the nack buffer
 * @param num_buffers              The number of buffers that will be stored in the nack buffer
 */
void udp_register_nack_buffer(SocketContext* context, WhistPacketType type, int max_payload_size,
                              int num_buffers);

/**
 * @brief                          Respond to a nack for a given ID/Index
 *                                 NOTE: This function is thread-safe with send_packet
 *
 * @param context                  The SocketContext to nack from
 * @param type                     The WhistPacketType of the nack'ed packet
 * @param id                       The ID of the nack'ed packet
 * @param index                    The index of the nack'ed packet
 *                                 (The UDP packet index into the larger WhistPacket)
 */
int udp_nack(SocketContext* context, WhistPacketType type, int id, int index);

#endif  // WHIST_UDP_H
