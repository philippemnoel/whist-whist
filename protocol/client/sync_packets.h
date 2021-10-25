#ifndef CLIENT_SYNC_PACKETS_H
#define CLIENT_SYNC_PACKETS_H
/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file sync_packets.h
 * @brief This file contains code for high-level communication with the server
============================
Usage
============================
Use multithreaded_sync_udp_packets to send and receive UDP messages to and from the server, such as
audio and video packets. Use multithreaded_sync_tcp_packets to send and receive TCP messages, mostly
clipboard packets.
*/

/*
============================
Public Functions
============================
*/

/**
 * @brief                   Receive and send UDP packets from and to the server. Includes bitrate,
 * dimension, nacks, and audio/video packets.
 * @param opaque            A socket context for receiving packets
 */
int multithreaded_sync_udp_packets(void* opaque);

/**
 * @brief                   Receive and send TCP packets, mostly clipboard
 *
 * @param opaque            Unused - necessary for creating fractal threads.
 */
int multithreaded_sync_tcp_packets(void* opaque);
#endif
