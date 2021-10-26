#ifndef NETWORK_H
#define NETWORK_H
/**
 * Copyright 2021 Fractal Computers, Inc., dba Whist
 * @file network.h
 * @brief This file contains all code that interacts directly with sockets
 *        under-the-hood.
============================
Usage
============================

SocketContext: This type represents a socket.
   - To use a socket, call CreateUDPContext or CreateTCPContext with the desired
     parameters
   - To send data over a socket, call SendTCPPacket or SendUDPPacket
   - To receive data over a socket, call ReadTCPPacket or ReadUDPPacket
   - If there is belief that a packet wasn't sent, you can call ReplayPacket to
     send a packet twice

FractalPacket: This type represents a packet of information
   - Unique packets of a given type will be given unique IDs. IDs are expected
     to be increasing monotonically, with a gap implying that a packet was lost
   - FractalPackets that were thought to have been sent may not arrive, and
     FractalPackets may arrive out-of-order, in the case of UDP. This will not
     be the case for TCP, however TCP sockets may lose connection if there is a
     problem.
   - A given block of data will, during transmission, be split up into packets
     with the same type and ID, but indicies ranging from 0 to num_indices - 1
   - A missing index implies that a packet was lost
   - A FractalPacket is only guaranteed to have data information from 0 to
     payload_size - 1 data[] occurs at the end of the packet, so extra bytes may
     in-fact point to invalid memory to save space and bandwidth
   - A FractalPacket may be sent twice in the case of packet recovery, but any
     two FractalPackets found that are of the same type and ID will be expected
     to have the same data (To be specific, the Client should never legally send
     two distinct packets with same ID/Type, and neither should the Server, but
if the Client and Server happen to both make a PACKET_MESSAGE packet with ID 1
     they can be different)
   - To reconstruct the original datagram from a sequence of FractalPackets,
     concatenated the data[] streams (From 0 to payload_size - 1) for each index
     from 0 to num_indices - 1

-----
Client
-----

SocketContext context;
CreateTCPContext(&context, "10.0.0.5", 5055, 500, 250);

char* msg = "Hello this is a message!";
SendTCPPacket(&context, PACKET_MESSAGE, msg, strlen(msg);

-----
Server
-----

SocketContext context;
CreateTCPContext(&context, NULL, 5055, 500, 250);

FractalPacket* packet = NULL;
while(!packet) {
  packet = ReadTCPPacket(context);
}

LOG_INFO("MESSAGE: %s", packet->data); // Will print "Hello this is a message!"
*/

/*
============================
Includes
============================
*/

// In order to use accept4 we have to allow non-standard extensions
#if !defined(_GNU_SOURCE) && defined(__linux__)
#define _GNU_SOURCE
#endif

#if defined(_WIN32)
#pragma comment(lib, "ws2_32.lib")
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#endif

#include <fractal/core/fractal.h>
#include <fractal/network/throttle.h>

/*
============================
Defines
============================
*/

#if defined(_WIN32)
#define FRACTAL_ETIMEDOUT WSAETIMEDOUT
#define FRACTAL_EWOULDBLOCK WSAEWOULDBLOCK
#define FRACTAL_EAGAIN WSAEWOULDBLOCK
#define FRACTAL_EINPROGRESS WSAEWOULDBLOCK
#define socklen_t int
#define FRACTAL_IOCTL_SOCKET ioctlsocket
#define FRACTAL_CLOSE_SOCKET closesocket
#else
#define SOCKET int
#define INVALID_SOCKET -1
#define closesocket close
#define FRACTAL_IOCTL_SOCKET ioctl
#define FRACTAL_CLOSE_SOCKET close
#define FRACTAL_ETIMEDOUT ETIMEDOUT
#define FRACTAL_EWOULDBLOCK EWOULDBLOCK
#define FRACTAL_EAGAIN EAGAIN
#define FRACTAL_EINPROGRESS EINPROGRESS
#endif

// Note that both the Windows and Linux versions use 2 as the second argument
// to indicate shutting down both ends of the socket
#define FRACTAL_SHUTDOWN_SOCKET(s) shutdown(s, 2)

/*
============================
Constants
============================
*/

#define MAX_PAYLOAD_SIZE 1285

/*
============================
Custom types
============================
*/

typedef struct SocketContext {
    bool is_server;
    bool is_tcp;
    bool udp_is_connected;
    int timeout;
    SOCKET socket;
    struct sockaddr_in addr;
    int ack;
    FractalMutex mutex;
    char binary_aes_private_key[16];
    // Used for reading TCP packets
    int reading_packet_len;
    DynamicBuffer* encrypted_tcp_packet_buffer;
    NetworkThrottleContext* network_throttler;
} SocketContext;

// TODO: Unique PRIVATE_KEY for every session, so that old packets can't be
// replayed
// TODO: INC integer that must not be used twice

/**
 * @brief                          Data packet description
 */
typedef enum FractalPacketType {
    PACKET_AUDIO,
    PACKET_VIDEO,
    PACKET_MESSAGE,
} FractalPacketType;

/**
 * @brief                          Packet of data to be sent over a
 *                                 SocketContext
 */
typedef struct FractalPacket {
    char hash[16];  // Hash of the rest of the packet
    // hash[16] is a signature for everything below this line

    // Encrypted packet data
    int cipher_len;  // The length of the encrypted segment
    char iv[16];     // One-time pad for encrypted data

    // Everything below this line gets encrypted

    // Metadata
    FractalPacketType type;  // Video, Audio, or Message
    int id;                  // Unique identifier (Two packets with the same type and id, from
                             // the same IP, will be the same)
    short index;             // Handle separation of large datagrams
    short num_indices;       // The total datagram consists of data packets with
                             // indices from 0 to payload_size - 1
    int payload_size;        // size of data[] that is of interest
    bool is_a_nack;          // True if this is a replay'ed packet

    // Data
    uint8_t data[MAX_PAYLOAD_SIZE];  // data at the end of the struct, with invalid
                                     // bytes beyond payload_size / cipher_len
    uint8_t overflow[16];            // The maximum cipher_len is MAX_PAYLOAD_SIZE + 16,
                                     // as the encrypted packet might be slightly larger
                                     // than the unencrypted packet
} FractalPacket;

/**
 * @brief                       Interface describing the avaliable functions
 *                              and socket context of a network protocol
 *
 */
typedef struct NetworkContext {
    // Attributes
    SocketContext* context;

    // Functions common to all network connections
    int (*sendp)(SocketContext* context, void* buf, int len);
    int (*recvp)(SocketContext* context, void* buf, int len);
    int (*ack)(SocketContext* context);
    FractalPacket* (*read_packet)(SocketContext* context, bool should_recvp);
    int (*send_packet_from_payload)(SocketContext* context, FractalPacketType type,
        void* data, int len, int id); // id only valid in UDP contexts
    void (*free_packet)(FractalPacket* packet); //Only Non-NULL in TCP.
} NetworkContext;

#define MAX_PACKET_SIZE (sizeof(FractalPacket))
#define PACKET_HEADER_SIZE (sizeof(FractalPacket) - MAX_PAYLOAD_SIZE - 16)
// Real packet size = PACKET_HEADER_SIZE + FractalPacket.payload_size (If
// Unencrypted)
//                  = PACKET_HEADER_SIZE + cipher_len (If Encrypted)

/*
============================
Public Functions
============================
*/

/*
 * @brief                          Initialize networking system
 *                                 Must be called before calling any other function in this file
 */
void init_networking();

/**
 * @brief                          Get the most recent network error.
 *
 * @returns                        The network error that most recently occured,
 *                                 through WSAGetLastError on Windows or errno
 *                                 on Linux
 */
int get_last_network_error();

/**
 * @brief                          This will send or receive data over a socket
 *
 * @param context                  The socket context to be used
 * @param buf                      The buffer to read or write to
 * @param len                      The length of the buffer to send over the socket
                                   Or, the maximum number of bytes that can be read
 *                                 from the socket

 * @returns                        The number of bytes that have been read or
 *                                 written to or from the buffer
 */
int recvp(SocketContext* context, void* buf, int len);
int sendp(SocketContext* context, void* buf, int len);

/**
 * @brief                          Initialize a UDP/TCP connection between a
 *                                 server and a client
 *
 * @param context                  The socket context that will be initialized
 * @param destination              The server IP address to connect to. Passing
 *                                 NULL will wait for another client to connect
 *                                 to the socket
 * @param port                     The port to connect to. This will be a real
 *                                 port if USING_STUN is false, and it will be a
 *                                 virtual port if USING_STUN is
 *                                 true; (The real port will be some randomly
 *                                 chosen port if USING_STUN is true)
 * @param recvfrom_timeout_s       The timeout that the socketcontext will use
 *                                 after being initialized
 * @param connection_timeout_ms    The timeout that will be used when attempting
 *                                 to connect. The handshake sends a few packets
 *                                 back and forth, so the upper
 *                                 bound of how long CreateXContext will take is
 *                                 some small constant times
 *                                 connection_timeout_ms
 * @param using_stun               True/false for whether or not to use the STUN server for this
 *                                 context
 * @param binary_aes_private_key   The AES private key used to encrypt the socket communication
 *
 * @returns                        Will return -1 on failure, will return 0 on
 *                                 success
 */
int create_udp_context(SocketContext* context, char* destination, int port, int recvfrom_timeout_s,
                       int connection_timeout_ms, bool using_stun, char* binary_aes_private_key);
int create_tcp_context(SocketContext* context, char* destination, int port, int recvfrom_timeout_s,
                       int connection_timeout_ms, bool using_stun, char* binary_aes_private_key);

/**
 * @brief                          Split a payload into several packets approprately-sized
 *                                 for UDP transport, and write those files to a buffer.
 *
 * @param payload                  The payload data to be split into packets
 * @param payload_size             The size of the payload, in bytes
 * @param payload_id               An ID for the UDP data (must be positive)
 * @param packet_type              The FractalPacketType (video, audio, or message)
 * @param packet_buffer            The buffer to write the packets to
 * @param packet_buffer_length     The length of the packet buffer
 *
 * @returns                        The number of packets that were written to the buffer,
 *                                 or -1 on failure
 *
 * @note                           This function should be removed and replaced with
 *                                 a more general packet splitter/joiner context, which
 *                                 will enable us to use forward error correction, etc.
 */
int write_payload_to_packets(uint8_t* payload, size_t payload_size, int payload_id,
                             FractalPacketType packet_type, FractalPacket* packet_buffer,
                             size_t packet_buffer_length);

/**
 * @brief                          Get the size of a FractalPacket
 *
 * @param packet                   The packet to get the size of
 *
 * @returns                        The size of the packet, or -1 on error
 */
int get_packet_size(FractalPacket* packet);

/**
 * @brief                          This will send a FractalPacket over TCP to
 *                                 the SocketContext context. A
 *                                 FractalPacketType is also provided to
 *                                 describe the packet
 *
 * @param context                  The socket context
 * @param type                     The FractalPacketType, either VIDEO, AUDIO,
 *                                 or MESSAGE
 * @param data                     A pointer to the data to be sent
 * @param len                      The number of bytes to send
 *
 * @returns                        Will return -1 on failure, will return 0 on
 *                                 success
 */
int send_tcp_packet_from_payload(SocketContext* context, FractalPacketType type, void* data,
                                 int len, int id);

/**
 * @brief                          This will send a FractalPacket over UDP to
 *                                 the SocketContext context. A
 *                                 FractalPacketType is also provided to the
 *                                 receiving end
 *
 * @param context                  The socket context
 * @param type                     The FractalPacketType, either VIDEO, AUDIO,
 *                                 or MESSAGE
 * @param data                     A pointer to the data to be sent
 * @param len                      The number of bytes to send
 * @param id                       An ID for the UDP data.
 *
 * @returns                        Will return -1 on failure, will return 0 on
 *                                 success
 */
int send_udp_packet_from_payload(SocketContext* context, FractalPacketType type, void* data,
                                 int len, int id);

/**
 * @brief                          This will send a FractalPacket over UDP to
 *                                 the SocketContext context. This function does
 *                                 not create the packet from raw data, but
 *                                 assumes that the packet has been prepared by
 *                                 the caller (e.g. fragmented into
 *                                 appropriately-sized chunks by a fragmenter).
 *                                 This function assumes and checks that the
 *                                 packet is small enough to send without
 *                                 further breaking into smaller packets.
 *
 * @param context                  The socket context
 * @param packet                   A pointer to the packet to be sent
 * @param packet_size              The size of the packet to be sent
 *
 * @returns                        Will return -1 on failure, will return 0 on
 *                                 success
 */
int send_udp_packet(SocketContext* context, FractalPacket* packet, size_t packet_size);

/**
 * @brief                          Send a 0-length packet over the socket. Used
 *                                 to keep-alive over NATs, and to check on the
 *                                 validity of the socket
 *
 * @param context                  The socket context
 *
 * @returns                        Will return -1 on failure, will return 0 on
 *                                 success Failure implies that the socket is
 *                                 broken or the TCP connection has ended, use
 *                                 GetLastNetworkError() to learn more about the
 *                                 error
 */
int ack(SocketContext* context);

/**
 * @brief                          Receive a FractalPacket from a SocketContext,
 *                                 if any such packet exists
 *
 * @param context                  The socket context
 * @param should_recvp             Only valid in TCP contexts. Adding in so that functions can
 *                                 be generalized. 
 *                                 If false, this function will only pop buffered packets
 *                                 If true, this function will pull data from the TCP socket,
 *                                 but that might take a while
 *
 * @returns                        A pointer to the FractalPacket on success,
 *                                 NULL on failure
 */
FractalPacket* read_tcp_packet(SocketContext* context, bool should_recvp);
FractalPacket* read_udp_packet(SocketContext* context, bool should_recvp);

/**
 * @brief                          Frees a TCP packet created by read_tcp_packet
 *
 * @param tcp_packet               The TCP packet to free
 */
void free_tcp_packet(FractalPacket* tcp_packet);

#endif  // NETWORK_H
