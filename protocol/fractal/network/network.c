/**
 * Copyright Fractal Computers, Inc. 2020
 * @file network.c
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

#if defined(_WIN32)
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // unportable Windows warnings, need to
                                         // be at the very top
#define _CRT_SECURE_NO_WARNINGS
#endif

#include "network.h"

#include <stdio.h>
#include <fcntl.h>

#ifndef _WIN32
#include "curl/curl.h"
#else
#include <winhttp.h>
#endif

#include "../utils/aes.h"
#include "../utils/json.h"

// Defines

#define STUN_IP "0.0.0.0"
#define STUN_PORT 48800

#define BITS_IN_BYTE 8.0
#define MS_IN_SECOND 1000

// Global data
unsigned short port_mappings[USHRT_MAX + 1];

/*
============================
Private Custom Types
============================
*/

typedef struct {
    unsigned int ip;
    unsigned short private_port;
    unsigned short public_port;
} StunEntry;

typedef enum StunRequestType { ASK_INFO, POST_INFO } StunRequestType;

typedef struct {
    StunRequestType type;
    StunEntry entry;
} StunRequest;

typedef struct {
    char iv[16];
    char signature[32];
} PrivateKeyData;

/**
 * @brief                       Struct to keep response data from a curl
 *                              request as it is being received.
 */
typedef struct {
    char* buffer;       // Buffer that contains received response data
    size_t filled_len;  // How much of the buffer is full so far
    size_t max_len;     // How much the buffer can be filled, at most
} CurlResponseBuffer;

/*
============================
Private Functions
============================
*/

/*
@brief                          This will set the socket s to have timeout
timeout_ms.

@param s                        The SOCKET to be configured
@param timeout_ms               The maximum amount of time that all recv/send
calls will take for that socket (0 to return immediately, -1 to never return)
                                Set 0 to have a non-blocking socket, and -1 for
an indefinitely blocking socket
*/
void set_timeout(SOCKET socket, int timeout_ms);

/*
@brief                          Perform socket syscalls and set fds to
                                use flag FD_CLOEXEC

@returns                        The socket file descriptor, -1 on failure
*/
SOCKET socketp_tcp();
SOCKET socketp_udp();

/*
@brief                          Perform accept syscall and set fd to use flag
                                FD_CLOEXEC

@param sock_fd                  The socket file descriptor
@param sock_addr                The socket address
@param sock_len                 The size of the socket address

@returns                        The new socket file descriptor, -1 on failure
*/
SOCKET acceptp(SOCKET sock_fd, struct sockaddr* sock_addr, socklen_t* sock_len);

/*
@brief                          This will send or receive data over a socket

@param s                        The SOCKET to be used
@param buf                      The buffer to read or write to
@param len                      The length of the buffer to send over the socket
                                Or, the maximum number of bytes that can be read
from the socket

@returns                        The number of bytes that have been read or
written to or from the buffer
*/
int recvp(SocketContext* context, void* buf, int len);
int sendp(SocketContext* context, void* buf, int len);

/*
@brief                          This will prepare the private key data

@param priv_key_data            The private key data buffer
*/
void prepare_private_key_request(PrivateKeyData* priv_key_data);

/*
@brief                          This will sign the other connection's private key data

@param priv_key_data            The private key data buffer
@param recv_size                The length of the buffer
@param private_key              The private key

@returns                        True if the verification succeeds, false if it fails
*/
bool sign_private_key(PrivateKeyData* priv_key_data, int recv_size, void* private_key);

/*
@brief                          This will verify the given private key

@param our_priv_key_data        The private key data buffer
@param our_signed_priv_key_data The signed private key data buffer
@param recv_size                The length of the buffer
@param private_key              The private key

@returns                        True if the verification succeeds, false if it fails
*/
bool confirm_private_key(PrivateKeyData* our_priv_key_data,
                         PrivateKeyData* our_signed_priv_key_data, int recv_size,
                         void* private_key);

/*
============================
Public Function Implementations
============================
*/

/*
@brief                          Initialize default port mappings (i.e. the identity)
*/
void init_networking() {
    for (int i = 0; i <= USHRT_MAX; i++) {
        port_mappings[i] = (unsigned short)i;
    }
}

int get_last_network_error() {
#if defined(_WIN32)
    return WSAGetLastError();
#else
    return errno;
#endif
}

bool handshake_private_key(SocketContext* context) {
    set_timeout(context->socket, 1000);

    PrivateKeyData our_priv_key_data;
    PrivateKeyData our_signed_priv_key_data;
    PrivateKeyData their_priv_key_data;
    int recv_size;
    socklen_t slen = sizeof(context->addr);

    // Generate and send private key request data
    prepare_private_key_request(&our_priv_key_data);
    if (sendp(context, &our_priv_key_data, sizeof(our_priv_key_data)) < 0) {
        LOG_ERROR("sendp(3) failed! Could not send private key request data! %d",
                  get_last_network_error());
        return false;
    }
    fractal_sleep(50);

    // Receive, sign, and send back their private key request data
    while ((recv_size =
                recvfrom(context->socket, (char*)&their_priv_key_data, sizeof(their_priv_key_data),
                         0, (struct sockaddr*)(&context->addr), &slen)) == 0)
        ;
    if (recv_size < 0) {
        LOG_WARNING("Did not receive other connection's private key request: %d",
                    get_last_network_error());
        return false;
    }
    LOG_INFO("Private key request received");
    if (!sign_private_key(&their_priv_key_data, recv_size, context->binary_aes_private_key)) {
        LOG_ERROR("signPrivateKey failed!");
        return false;
    }
    if (sendp(context, &their_priv_key_data, sizeof(their_priv_key_data)) < 0) {
        LOG_ERROR("sendp(3) failed! Could not send signed private key data! %d",
                  get_last_network_error());
        return false;
    }
    fractal_sleep(50);

    // Wait for and verify their signed private key request data
    recv_size = recvp(context, &our_signed_priv_key_data, sizeof(our_signed_priv_key_data));
    if (!confirm_private_key(&our_priv_key_data, &our_signed_priv_key_data, recv_size,
                             context->binary_aes_private_key)) {
        LOG_ERROR("Could not confirmPrivateKey!");
        return false;
    } else {
        LOG_INFO("Private key confirmed");
        set_timeout(context->socket, context->timeout);
        return true;
    }
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int send_tcp_packet(SocketContext* context, FractalPacketType type, void* data, int len) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }

    // Use 256kb static buffer for sending smaller TCP packets
    // But use our block allocator for sending large TCP packets
    // This function fragments the heap too much to use malloc here
    char* packet_buffer = allocate_region(sizeof(FractalPacket) + len + 64);
    char* encrypted_packet_buffer = allocate_region(sizeof(FractalPacket) + len + 128);

    FractalPacket* packet = (FractalPacket*)packet_buffer;

    // Contruct packet metadata
    packet->id = -1;
    packet->type = type;
    packet->index = 0;
    packet->payload_size = len;
    packet->num_indices = 1;
    packet->is_a_nack = false;

    // Copy packet data
    memcpy(packet->data, data, len);

    // Encrypt the packet using aes encryption
    int unencrypted_len = PACKET_HEADER_SIZE + packet->payload_size;
    int encrypted_len = encrypt_packet(packet, unencrypted_len,
                                       (FractalPacket*)(sizeof(int) + encrypted_packet_buffer),
                                       (unsigned char*)context->binary_aes_private_key);

    // Pass the length of the packet as the first byte
    *((int*)encrypted_packet_buffer) = encrypted_len;

    // Send the packet
    LOG_INFO("Sending TCP Packet of length %d", encrypted_len);
    bool failed = false;
    if (sendp(context, encrypted_packet_buffer, sizeof(int) + encrypted_len) < 0) {
        LOG_WARNING("Failed to send packet!");
        failed = true;
    }

    deallocate_region(packet_buffer);
    deallocate_region(encrypted_packet_buffer);

    // Return success code
    return failed ? -1 : 0;
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int send_udp_packet(SocketContext* context, FractalPacketType type, void* data, int len, int id,
                    int burst_bitrate, FractalPacket* packet_buffer, int* packet_len_buffer) {
    if (id <= 0) {
        LOG_WARNING("IDs must be positive!");
        return -1;
    }
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }

    int payload_size;
    int curr_index = 0, i = 0;

    int num_indices = len / MAX_PAYLOAD_SIZE + (len % MAX_PAYLOAD_SIZE == 0 ? 0 : 1);

    double max_bytes_per_second = burst_bitrate / BITS_IN_BYTE;

    /*
    if (type == PACKET_AUDIO) {
        static int ddata = 0;
        static clock last_timer;
        if( ddata == 0 )
        {
            start_timer( &last_timer );
        }
        ddata += len;
        get_timer( last_timer );
        if( get_timer( last_timer ) > 5.0 )
        {
            LOG_INFO( "AUDIO BANDWIDTH: %f kbps", 8 * ddata / get_timer(
    last_timer ) / 1024 ); ddata = 0;
        }
        // LOG_INFO("Video ID %d (Packets: %d)", id, num_indices);
    }
    */

    clock packet_timer;
    start_timer(&packet_timer);

    while (curr_index < len) {
        // Delay distribution of packets as needed
        while (burst_bitrate > 0 &&
               curr_index - 5000 > get_timer(packet_timer) * max_bytes_per_second) {
            fractal_sleep(1);
        }

        // local packet and len for when nack buffer isn't needed
        FractalPacket l_packet = {0};
        int l_len = 0;

        int* packet_len = &l_len;
        FractalPacket* packet = &l_packet;

        // Based on packet type, the packet to one of the buffers to serve later
        // nacks
        if (packet_buffer) {
            packet = &packet_buffer[i];
        }

        if (packet_len_buffer) {
            packet_len = &packet_len_buffer[i];
        }

        payload_size = min(MAX_PAYLOAD_SIZE, (len - curr_index));

        // Construct packet
        packet->type = type;
        memcpy(packet->data, (uint8_t*)data + curr_index, payload_size);
        packet->index = (short)i;
        packet->payload_size = payload_size;
        packet->id = id;
        packet->num_indices = (short)num_indices;
        packet->is_a_nack = false;
        int packet_size = PACKET_HEADER_SIZE + packet->payload_size;

        // Save the len to nack buffer lens
        *packet_len = packet_size;

        // Encrypt the packet with AES
        FractalPacket encrypted_packet;
        int encrypt_len = encrypt_packet(packet, packet_size, &encrypted_packet,
                                         (unsigned char*)context->binary_aes_private_key);

        // Send it off
        fractal_lock_mutex(context->mutex);
        // LOG_INFO("Sending UDP Packet of length %d", encrypt_len);
        int sent_size = sendp(context, &encrypted_packet, encrypt_len);
        fractal_unlock_mutex(context->mutex);

        if (sent_size < 0) {
            int error = get_last_network_error();
            LOG_WARNING("Unexpected Packet Error: %d", error);
            return -1;
        }

        i++;
        curr_index += payload_size;
    }

    // LOG_INFO( "Packet Time: %f\n", get_timer( packet_timer ) );

    return 0;
}

int replay_packet(SocketContext* context, FractalPacket* packet, size_t len) {
    if (len > sizeof(FractalPacket)) {
        LOG_WARNING("Len too long!");
        return -1;
    }
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }
    if (packet == NULL) {
        LOG_WARNING("packet is NULL");
        return -1;
    }

    packet->is_a_nack = true;

    FractalPacket encrypted_packet;
    int encrypt_len = encrypt_packet(packet, (int)len, &encrypted_packet,
                                     (unsigned char*)context->binary_aes_private_key);

    fractal_lock_mutex(context->mutex);
    LOG_INFO("Replay Packet of length %d", encrypt_len);
    int sent_size = sendp(context, &encrypted_packet, encrypt_len);
    fractal_unlock_mutex(context->mutex);

    if (sent_size < 0) {
        LOG_WARNING("Could not replay packet!");
        return -1;
    }

    return 0;
}

SOCKET socketp_tcp() {
    /*
        Create a TCP socket and set the FD_CLOEXEC flag.
        Linux permits atomic FD_CLOEXEC definition via SOCK_CLOEXEC,
        but this is not available on other operating systems yet.

        Return:
            SOCKET: socket fd on success; INVALID_SOCKET on failure
    */

#ifdef SOCK_CLOEXEC
    // Create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, IPPROTO_TCP);
    if (sock_fd <= 0) {
        LOG_WARNING("Could not create socket %d\n", get_last_network_error());
        return INVALID_SOCKET;
    }
#else
    // Create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sock_fd <= 0) {  // Windows & Unix cases
        LOG_WARNING("Could not create socket %d\n", get_last_network_error());
        return INVALID_SOCKET;
    }

#ifndef _WIN32
    // Set socket to close on child exec
    // Not necessary for windows because CreateProcessA creates an independent process
    if (fcntl(sock_fd, F_SETFD, fcntl(sock_fd, F_GETFD) | FD_CLOEXEC) < 0) {
        LOG_WARNING("Could not set fcntl to set socket to close on child exec");
        return INVALID_SOCKET;
    }
#endif
#endif

    return sock_fd;
}

SOCKET socketp_udp() {
    /*
        Create a UDP socket and set the FD_CLOEXEC flag.
        Linux permits atomic FD_CLOEXEC definition via SOCK_CLOEXEC,
        but this is not available on other operating systems yet.

        Return:
            SOCKET: socket fd on success; INVALID_SOCKET on failure
    */

#ifdef SOCK_CLOEXEC
    // Create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_DGRAM | SOCK_CLOEXEC, IPPROTO_UDP);
#else
    // Create socket
    SOCKET sock_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

#ifndef _WIN32
    // Set socket to close on child exec
    // Not necessary for windows because CreateProcessA creates an independent process
    if (fcntl(sock_fd, F_SETFD, fcntl(sock_fd, F_GETFD) | FD_CLOEXEC) < 0) {
        LOG_WARNING("Could not set fcntl to set socket to close on child exec");
        return INVALID_SOCKET;
    }
#endif
#endif

    return sock_fd;
}

SOCKET acceptp(SOCKET sock_fd, struct sockaddr* sock_addr, socklen_t* sock_len) {
    /*
        Accept a connection on `sock_fd` and return a new socket fd

        Arguments:
            sock_fd (SOCKET): file descriptor of socket that we are accepting a connection on
            sock_addr (struct sockaddr*): the address of the socket
            sock_len (socklen_t*): the length of the socket address struct

        Return:
            SOCKET: new fd for socket on success; INVALID_SOCKET on failure
    */

#if defined(_GNU_SOURCE) && defined(SOCK_CLOEXEC)
    SOCKET new_socket = accept4(sock_fd, sock_addr, sock_len, SOCK_CLOEXEC);
#else
    // Accept connection from client
    SOCKET new_socket = accept(sock_fd, sock_addr, sock_len);
    if (new_socket < 0) {
        LOG_WARNING("Did not receive response from client! %d\n", get_last_network_error());
        return INVALID_SOCKET;
    }

#ifndef _WIN32
    // Set socket to close on child exec
    // Not necessary for windows because CreateProcessA creates an independent process
    if (fcntl(new_socket, F_SETFD, fcntl(new_socket, F_GETFD) | FD_CLOEXEC) < 0) {
        LOG_WARNING("Could not set fcntl to set socket to close on child exec");
        return INVALID_SOCKET;
    }
#endif
#endif

    return new_socket;
}

int recvp(SocketContext* context, void* buf, int len) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }
    return recv(context->socket, buf, len, 0);
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int sendp(SocketContext* context, void* buf, int len) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }
    if (len != 0 && buf == NULL) {
        LOG_ERROR("Passed non zero length and a NULL pointer to sendto");
        return -1;
    }
    // cppcheck-suppress nullPointer
    if (context->is_tcp) {
        return send(context->socket, buf, len, 0);
    } else {
        return sendto(context->socket, buf, len, 0, (struct sockaddr*)(&context->addr),
                      sizeof(context->addr));
    }
}

int ack(SocketContext* context) { return sendp(context, NULL, 0); }

bool tcp_connect(SOCKET socket, struct sockaddr_in addr, int timeout_ms) {
    // Connect to TCP server
    int ret;
    set_timeout(socket, 0);
    if ((ret = connect(socket, (struct sockaddr*)(&addr), sizeof(addr))) < 0) {
        bool worked = get_last_network_error() == FRACTAL_EINPROGRESS;

        if (!worked) {
            LOG_WARNING(
                "Could not connect() over TCP to server: Returned %d, Error "
                "Code %d",
                ret, get_last_network_error());
            closesocket(socket);
            return false;
        }
    }

    // Select connection
    fd_set set;
    FD_ZERO(&set);
    FD_SET(socket, &set);
    struct timeval tv;
    tv.tv_sec = timeout_ms / MS_IN_SECOND;
    tv.tv_usec = (timeout_ms % MS_IN_SECOND) * MS_IN_SECOND;
    if ((ret = select((int)socket + 1, NULL, &set, NULL, &tv)) <= 0) {
        if (ret == 0) {
            LOG_INFO("No TCP Connection Retrieved, ending TCP connection attempt.");
        } else {
            LOG_WARNING(
                "Could not select() over TCP to server: Returned %d, Error Code "
                "%d\n",
                ret, get_last_network_error());
        }
        closesocket(socket);
        return false;
    }

    set_timeout(socket, timeout_ms);
    return true;
}

FractalPacket* read_udp_packet(SocketContext* context) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return NULL;
    }

    // Wait to receive packet over TCP, until timing out
    FractalPacket encrypted_packet;
    int encrypted_len = recvp(context, &encrypted_packet, sizeof(encrypted_packet));

    static FractalPacket decrypted_packet;

    // If the packet was successfully received, then decrypt it
    if (encrypted_len > 0) {
        int decrypted_len = decrypt_packet(&encrypted_packet, encrypted_len, &decrypted_packet,
                                           (unsigned char*)context->binary_aes_private_key);

        // If there was an issue decrypting it, post warning and then
        // ignore the problem
        if (decrypted_len < 0) {
            if (encrypted_len == sizeof(StunEntry)) {
                StunEntry* e;
                e = (void*)&encrypted_packet;
                LOG_INFO("Maybe a map from public %d to private %d?", ntohs(e->private_port),
                         ntohs(e->private_port));
            }
            LOG_WARNING("Failed to decrypt packet");
            return NULL;
        }

        return &decrypted_packet;
    } else {
        if (encrypted_len < 0) {
            int error = get_last_network_error();
            switch (error) {
                case FRACTAL_ETIMEDOUT:
                    // LOG_ERROR("Read UDP Packet error: Timeout");
                case FRACTAL_EWOULDBLOCK:
                    // LOG_ERROR("Read UDP Packet error: Blocked");
                    // Break on expected network errors
                    break;
                default:
                    LOG_WARNING("Unexpected Packet Error: %d", error);
                    break;
            }
        }
        return NULL;
    }
}

FractalPacket* read_tcp_packet(SocketContext* context, bool should_recvp) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return NULL;
    }
    if (!context->is_tcp) {
        LOG_WARNING("TryReadingTCPPacket received a context that is NOT TCP!");
        return NULL;
    }
    // The dynamically sized buffer to read into
    DynamicBuffer* encrypted_tcp_packet_buffer = context->encrypted_tcp_packet_buffer;

#define TCP_SEGMENT_SIZE 4096

    int len = TCP_SEGMENT_SIZE;
    while (should_recvp && len == TCP_SEGMENT_SIZE) {
        // Make the tcp buffer larger if needed
        resize_dynamic_buffer(encrypted_tcp_packet_buffer,
                              context->reading_packet_len + TCP_SEGMENT_SIZE);
        // Try to fill up the buffer, in chunks of TCP_SEGMENT_SIZE
        len = recvp(context, encrypted_tcp_packet_buffer->buf + context->reading_packet_len,
                    TCP_SEGMENT_SIZE);

        if (len < 0) {
            int err = get_last_network_error();
            if (err == FRACTAL_ETIMEDOUT || err == FRACTAL_EAGAIN) {
            } else {
                LOG_WARNING("Network Error %d", err);
            }
        } else if (len > 0) {
            // LOG_INFO( "READ LEN: %d", len );
            context->reading_packet_len += len;
        }

        // If the previous recvp was maxed out, ie == TCP_SEGMENT_SIZE,
        // then try pulling some more from recvp
    };

    if ((unsigned long)context->reading_packet_len >= sizeof(int)) {
        // The amount of data bytes read (actual len), and the amount of bytes
        // we're looking for (target len), respectively
        int actual_len = context->reading_packet_len - sizeof(int);
        int target_len = *((int*)encrypted_tcp_packet_buffer->buf);

        // If the target len is valid, and actual len > target len, then we're
        // good to go
        if (target_len >= 0 && actual_len >= target_len) {
            FractalPacket* decrypted_packet_buffer = allocate_region(target_len);
            // Decrypt it
            int decrypted_len =
                decrypt_packet_n((FractalPacket*)(encrypted_tcp_packet_buffer->buf + sizeof(int)),
                                 target_len, decrypted_packet_buffer, target_len,
                                 (unsigned char*)context->binary_aes_private_key);

            // Move the rest of the read bytes to the beginning of the buffer to
            // continue
            int start_next_bytes = sizeof(int) + target_len;
            for (unsigned long i = start_next_bytes; i < sizeof(int) + actual_len; i++) {
                encrypted_tcp_packet_buffer->buf[i - start_next_bytes] =
                    encrypted_tcp_packet_buffer->buf[i];
            }
            context->reading_packet_len = actual_len - target_len;

            // Realloc the buffer smaller if we have room to
            resize_dynamic_buffer(encrypted_tcp_packet_buffer, context->reading_packet_len);

            if (decrypted_len < 0) {
                // A warning not an error, since it doesn't simply we did something wrong
                // Someone else in the same coffeeshop as you could make you generate these
                // by sending bad TCP packets. After this point though, the packet is authenticated,
                // and problems with its data should be LOG_ERROR'ed
                LOG_WARNING("Could not decrypt TCP message");
                deallocate_region(decrypted_packet_buffer);
                return NULL;
            } else {
                // Return the decrypted packet
                return decrypted_packet_buffer;
            }
        }
    }

    return NULL;
}

void free_tcp_packet(FractalPacket* tcp_packet) { deallocate_region(tcp_packet); }

int create_tcp_server_context(SocketContext* context, int port, int recvfrom_timeout_ms,
                              int stun_timeout_ms) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }

    context->is_tcp = true;

    int opt;

    // Create TCP socket
    LOG_INFO("Creating TCP Socket");
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);
    // Server connection protocol
    context->is_server = true;

    // Reuse addr
    opt = 1;
    if (setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        LOG_WARNING("Could not setsockopt SO_REUSEADDR");
        return -1;
    }

    struct sockaddr_in origin_addr;
    origin_addr.sin_family = AF_INET;
    origin_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    origin_addr.sin_port = htons((unsigned short)port);

    // Bind to port
    if (bind(context->socket, (struct sockaddr*)(&origin_addr), sizeof(origin_addr)) < 0) {
        LOG_WARNING("Failed to bind to port! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    // Set listen queue
    LOG_INFO("Waiting for TCP Connection");
    if (listen(context->socket, 3) < 0) {
        LOG_WARNING("Could not listen(2)! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    fd_set fd_read, fd_write;
    FD_ZERO(&fd_read);
    FD_ZERO(&fd_write);
    FD_SET(context->socket, &fd_read);
    FD_SET(context->socket, &fd_write);

    struct timeval tv;
    tv.tv_sec = stun_timeout_ms / MS_IN_SECOND;
    tv.tv_usec = (stun_timeout_ms % MS_IN_SECOND) * 1000;

    int ret;
    if ((ret = select((int)context->socket + 1, &fd_read, &fd_write, NULL,
                      stun_timeout_ms > 0 ? &tv : NULL)) <= 0) {
        if (ret == 0) {
            LOG_INFO("No TCP Connection Retrieved, ending TCP connection attempt.");
        } else {
            LOG_WARNING("Could not select! %d", get_last_network_error());
        }
        closesocket(context->socket);
        return -1;
    }

    // Accept connection from client
    LOG_INFO("Accepting TCP Connection");
    socklen_t slen = sizeof(context->addr);
    SOCKET new_socket;
    if ((new_socket = acceptp(context->socket, (struct sockaddr*)(&context->addr), &slen)) ==
        INVALID_SOCKET) {
        return -1;
    }

    LOG_INFO("PORT: %d", ntohs(context->addr.sin_port));

    closesocket(context->socket);
    context->socket = new_socket;

    LOG_INFO("Client received at %s:%d!\n", inet_ntoa(context->addr.sin_addr),
             ntohs(context->addr.sin_port));

    set_timeout(context->socket, recvfrom_timeout_ms);

    return 0;
}

int create_tcp_server_context_stun(SocketContext* context, int port, int recvfrom_timeout_ms,
                                   int stun_timeout_ms) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }

    context->is_tcp = true;

    // Init stun_addr
    struct sockaddr_in stun_addr;
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_addr.s_addr = inet_addr(STUN_IP);
    stun_addr.sin_port = htons(STUN_PORT);
    int opt;

    // Create TCP socket
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Create UDP socket
    SOCKET udp_s = socketp_udp();
    if (udp_s == INVALID_SOCKET) {
        return -1;
    }

    // cppcheck-suppress nullPointer
    sendto(udp_s, NULL, 0, 0, (struct sockaddr*)&stun_addr, sizeof(stun_addr));
    closesocket(udp_s);

    // Server connection protocol
    context->is_server = true;

    // Reuse addr
    opt = 1;
    if (setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        LOG_WARNING("Could not setsockopt SO_REUSEADDR");
        return -1;
    }

    struct sockaddr_in origin_addr;
    // Connect over TCP to STUN
    LOG_INFO("Connecting to STUN TCP...");
    if (!tcp_connect(context->socket, stun_addr, stun_timeout_ms)) {
        LOG_WARNING("Could not connect to STUN Server over TCP");
        return -1;
    }

    socklen_t slen = sizeof(origin_addr);
    if (getsockname(context->socket, (struct sockaddr*)&origin_addr, &slen) < 0) {
        LOG_WARNING("Could not get sock name");
        closesocket(context->socket);
        return -1;
    }

    // Send STUN request
    StunRequest stun_request = {0};
    stun_request.type = POST_INFO;
    stun_request.entry.public_port = htons((unsigned short)port);

    if (sendp(context, &stun_request, sizeof(stun_request)) < 0) {
        LOG_WARNING("Could not send STUN request to connected STUN server!");
        closesocket(context->socket);
        return -1;
    }

    // Receive STUN response
    clock t;
    start_timer(&t);

    int recv_size = 0;
    StunEntry entry = {0};

    while (recv_size < (int)sizeof(entry) && get_timer(t) < stun_timeout_ms) {
        int single_recv_size;
        if ((single_recv_size = recvp(context, ((char*)&entry) + recv_size,
                                      max(0, (int)sizeof(entry) - recv_size))) < 0) {
            LOG_WARNING("Did not receive STUN response %d\n", get_last_network_error());
            closesocket(context->socket);
            return -1;
        }
        recv_size += single_recv_size;
    }

    if (recv_size != sizeof(entry)) {
        LOG_WARNING("TCP STUN Response packet of wrong size! %d\n", recv_size);
        closesocket(context->socket);
        return -1;
    }

    // Print STUN response
    struct sockaddr_in client_addr;
    client_addr.sin_family = AF_INET;
    client_addr.sin_addr.s_addr = entry.ip;
    client_addr.sin_port = entry.private_port;
    LOG_INFO("TCP STUN notified of desired request from %s:%d\n", inet_ntoa(client_addr.sin_addr),
             ntohs(client_addr.sin_port));

    closesocket(context->socket);

    // Create TCP socket
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    if (context->socket <= 0) {  // Windows & Unix cases
        LOG_WARNING("Could not create TCP socket %d\n", get_last_network_error());
        return -1;
    }

    opt = 1;
    if (setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        LOG_WARNING("Could not setsockopt SO_REUSEADDR");
        return -1;
    }

    // Bind to port
    if (bind(context->socket, (struct sockaddr*)(&origin_addr), sizeof(origin_addr)) < 0) {
        LOG_WARNING("Failed to bind to port! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("WAIT");

    // Connect to client
    if (!tcp_connect(context->socket, client_addr, stun_timeout_ms)) {
        LOG_WARNING("Could not connect to Client over TCP");
        return -1;
    }

    context->addr = client_addr;
    LOG_INFO("Client received at %s:%d!\n", inet_ntoa(context->addr.sin_addr),
             ntohs(context->addr.sin_port));
    set_timeout(context->socket, recvfrom_timeout_ms);
    return 0;
}

int create_tcp_client_context(SocketContext* context, char* destination, int port,
                              int recvfrom_timeout_ms, int stun_timeout_ms) {
    UNUSED(stun_timeout_ms);
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }
    if (destination == NULL) {
        LOG_WARNING("destiniation is NULL");
        return -1;
    }
    context->is_tcp = true;

    // Create TCP socket
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Client connection protocol
    context->is_server = false;

    context->addr.sin_family = AF_INET;
    context->addr.sin_addr.s_addr = inet_addr(destination);
    context->addr.sin_port = htons((unsigned short)port);

    LOG_INFO("Connecting to server...");

    fractal_sleep(200);

    // Connect to TCP server
    if (!tcp_connect(context->socket, context->addr, stun_timeout_ms)) {
        LOG_WARNING("Could not connect to server over TCP");
        return -1;
    }

    LOG_INFO("Connected on %s:%d!\n", destination, port);

    set_timeout(context->socket, recvfrom_timeout_ms);

    return 0;
}

int create_tcp_client_context_stun(SocketContext* context, char* destination, int port,
                                   int recvfrom_timeout_ms, int stun_timeout_ms) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }
    if (destination == NULL) {
        LOG_WARNING("destiniation is NULL");
        return -1;
    }
    context->is_tcp = true;

    // Init stun_addr
    struct sockaddr_in stun_addr;
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_addr.s_addr = inet_addr(STUN_IP);
    stun_addr.sin_port = htons(STUN_PORT);
    int opt;

    // Create TCP socket
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Tell the STUN to use UDP
    SOCKET udp_s = socketp_udp();
    if (udp_s == INVALID_SOCKET) {
        return -1;
    }

    // cppcheck-suppress nullPointer
    sendto(udp_s, NULL, 0, 0, (struct sockaddr*)&stun_addr, sizeof(stun_addr));
    closesocket(udp_s);
    // Client connection protocol
    context->is_server = false;

    // Reuse addr
    opt = 1;
    if (setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        LOG_WARNING("Could not setsockopt SO_REUSEADDR");
        return -1;
    }

    // Connect to STUN server
    if (!tcp_connect(context->socket, stun_addr, stun_timeout_ms)) {
        LOG_WARNING("Could not connect to STUN Server over TCP");
        return -1;
    }

    struct sockaddr_in origin_addr;
    socklen_t slen = sizeof(origin_addr);
    if (getsockname(context->socket, (struct sockaddr*)&origin_addr, &slen) < 0) {
        LOG_WARNING("Could not get sock name");
        closesocket(context->socket);
        return -1;
    }

    // Make STUN request
    StunRequest stun_request = {0};
    stun_request.type = ASK_INFO;
    stun_request.entry.ip = inet_addr(destination);
    stun_request.entry.public_port = htons((unsigned short)port);

    if (sendp(context, &stun_request, sizeof(stun_request)) < 0) {
        LOG_WARNING("Could not send STUN request to connected STUN server!");
        closesocket(context->socket);
        return -1;
    }

    // Receive STUN response
    clock t;
    start_timer(&t);

    int recv_size = 0;
    StunEntry entry = {0};

    while (recv_size < (int)sizeof(entry) && get_timer(t) < stun_timeout_ms) {
        int single_recv_size;
        if ((single_recv_size = recvp(context, ((char*)&entry) + recv_size,
                                      max(0, (int)sizeof(entry) - recv_size))) < 0) {
            LOG_WARNING("Did not receive STUN response %d\n", get_last_network_error());
            closesocket(context->socket);
            return -1;
        }
        recv_size += single_recv_size;
    }

    if (recv_size != sizeof(entry)) {
        LOG_WARNING("STUN Response of wrong size! %d", recv_size);
        closesocket(context->socket);
        return -1;
    } else if (entry.ip != stun_request.entry.ip ||
               entry.public_port != stun_request.entry.public_port) {
        LOG_WARNING("STUN Response IP and/or Public Port is incorrect!");
        closesocket(context->socket);
        return -1;
    } else if (entry.private_port == 0) {
        LOG_WARNING("STUN reported no such IP Address");
        closesocket(context->socket);
        return -1;
    } else {
        LOG_WARNING("Received STUN response! Public %d is mapped to private %d\n",
                    ntohs((unsigned short)entry.public_port),
                    ntohs((unsigned short)entry.private_port));
        context->addr.sin_family = AF_INET;
        context->addr.sin_addr.s_addr = entry.ip;
        context->addr.sin_port = entry.private_port;
    }

    // Print STUN response
    struct in_addr a;
    a.s_addr = entry.ip;
    LOG_WARNING("TCP STUN responded that the TCP server is located at %s:%d\n", inet_ntoa(a),
                ntohs(entry.private_port));

    closesocket(context->socket);

    // Create TCP socket
    if ((context->socket = socketp_tcp()) == INVALID_SOCKET) {
        return -1;
    }

    // Reuse addr
    opt = 1;
    if (setsockopt(context->socket, SOL_SOCKET, SO_REUSEADDR, (const char*)&opt, sizeof(opt)) < 0) {
        LOG_WARNING("Could not setsockopt SO_REUSEADDR");
        return -1;
    }

    // Bind to port
    if (bind(context->socket, (struct sockaddr*)(&origin_addr), sizeof(origin_addr)) < 0) {
        LOG_WARNING("Failed to bind to port! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }
    set_timeout(context->socket, stun_timeout_ms);

    LOG_INFO("Connecting to server...");

    // Connect to TCP server
    if (!tcp_connect(context->socket, context->addr, stun_timeout_ms)) {
        LOG_WARNING("Could not connect to server over TCP");
        return -1;
    }

    LOG_INFO("Connected on %s:%d!\n", destination, port);

    set_timeout(context->socket, recvfrom_timeout_ms);
    return 0;
}

int create_tcp_context(SocketContext* context, char* destination, int port, int recvfrom_timeout_ms,
                       int stun_timeout_ms, bool using_stun, char* binary_aes_private_key) {
    if ((int)((unsigned short)port) != port) {
        LOG_ERROR("Port invalid: %d", port);
    }
    if (context == NULL) {
        LOG_ERROR("Context is NULL");
        return -1;
    }
    port = port_mappings[port];

    context->timeout = recvfrom_timeout_ms;
    context->mutex = fractal_create_mutex();
    memcpy(context->binary_aes_private_key, binary_aes_private_key,
           sizeof(context->binary_aes_private_key));
    context->reading_packet_len = 0;
    context->encrypted_tcp_packet_buffer = init_dynamic_buffer(true);
    resize_dynamic_buffer(context->encrypted_tcp_packet_buffer, 0);

    int ret;

    if (using_stun) {
        if (destination == NULL)
            ret =
                create_tcp_server_context_stun(context, port, recvfrom_timeout_ms, stun_timeout_ms);
        else
            ret = create_tcp_client_context_stun(context, destination, port, recvfrom_timeout_ms,
                                                 stun_timeout_ms);
    } else {
        if (destination == NULL)
            ret = create_tcp_server_context(context, port, recvfrom_timeout_ms, stun_timeout_ms);
        else
            ret = create_tcp_client_context(context, destination, port, recvfrom_timeout_ms,
                                            stun_timeout_ms);
    }

    if (ret == -1) {
        return -1;
    }

    if (!handshake_private_key(context)) {
        LOG_WARNING("Could not complete handshake!");
        closesocket(context->socket);
        return -1;
    }

    return ret;
}

int create_udp_server_context(SocketContext* context, int port, int recvfrom_timeout_ms,
                              int stun_timeout_ms) {
    if (context == NULL) {
        LOG_WARNING("Context is NULL");
        return -1;
    }

    context->is_tcp = false;
    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);
    // Server connection protocol
    context->is_server = true;

    // Bind the server port to the advertized public port
    struct sockaddr_in origin_addr;
    origin_addr.sin_family = AF_INET;
    origin_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    origin_addr.sin_port = htons((unsigned short)port);

    if (bind(context->socket, (struct sockaddr*)(&origin_addr), sizeof(origin_addr)) < 0) {
        LOG_WARNING("Failed to bind to port! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Waiting for client to connect to %s:%d...\n", "localhost", port);

    socklen_t slen = sizeof(context->addr);
    int recv_size;
    if ((recv_size = recvfrom(context->socket, NULL, 0, 0, (struct sockaddr*)(&context->addr),
                              &slen)) != 0) {
        LOG_WARNING("Failed to receive ack! %d %d", recv_size, get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    if (!handshake_private_key(context)) {
        LOG_WARNING("Could not complete handshake!");
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Client received at %s:%d!\n", inet_ntoa(context->addr.sin_addr),
             ntohs(context->addr.sin_port));

    set_timeout(context->socket, recvfrom_timeout_ms);

    return 0;
}

int create_udp_server_context_stun(SocketContext* context, int port, int recvfrom_timeout_ms,
                                   int stun_timeout_ms) {
    context->is_tcp = false;

    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Server connection protocol
    context->is_server = true;

    // Tell the STUN to log our requested virtual port
    struct sockaddr_in stun_addr;
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_addr.s_addr = inet_addr(STUN_IP);
    stun_addr.sin_port = htons(STUN_PORT);

    StunRequest stun_request = {0};
    stun_request.type = POST_INFO;
    stun_request.entry.public_port = htons((unsigned short)port);

    LOG_INFO("Sending stun entry to STUN...");
    if (sendto(context->socket, (const char*)&stun_request, sizeof(stun_request), 0,
               (struct sockaddr*)&stun_addr, sizeof(stun_addr)) < 0) {
        LOG_WARNING("Could not send message to STUN %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Waiting for client to connect to %s:%d...\n", "localhost", port);

    // Receive client's connection attempt
    // Update the STUN every 100ms
    set_timeout(context->socket, 100);

    // But keep track of time to compare against stun_timeout_ms
    clock recv_timer;
    start_timer(&recv_timer);

    socklen_t slen = sizeof(context->addr);
    StunEntry entry = {0};
    int recv_size;
    while ((recv_size = recvfrom(context->socket, (char*)&entry, sizeof(entry), 0,
                                 (struct sockaddr*)(&context->addr), &slen)) < 0) {
        // If we haven't spent too much time waiting, and our previous 100ms
        // poll failed, then send another STUN update
        if (get_timer(recv_timer) * MS_IN_SECOND < stun_timeout_ms &&
            (get_last_network_error() == FRACTAL_ETIMEDOUT ||
             get_last_network_error() == FRACTAL_EAGAIN)) {
            if (sendto(context->socket, (const char*)&stun_request, sizeof(stun_request), 0,
                       (struct sockaddr*)&stun_addr, sizeof(stun_addr)) < 0) {
                LOG_WARNING("Could not send message to STUN %d\n", get_last_network_error());
                closesocket(context->socket);
                return -1;
            }
            continue;
        }
        LOG_WARNING("Did not receive response from client! %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    set_timeout(context->socket, 350);

    if (recv_size != sizeof(entry)) {
        LOG_WARNING("STUN response was not the size of an entry!");
        closesocket(context->socket);
        return -1;
    }

    // Setup addr to open up port
    context->addr.sin_family = AF_INET;
    context->addr.sin_addr.s_addr = entry.ip;
    context->addr.sin_port = entry.private_port;

    LOG_INFO("Received STUN response, client connection desired from %s:%d\n",
             inet_ntoa(context->addr.sin_addr), ntohs(context->addr.sin_port));

    // Open up the port
    if (sendp(context, NULL, 0) < 0) {
        LOG_ERROR("sendp(3) failed! Could not open up port! %d", get_last_network_error());
        return false;
    }
    fractal_sleep(150);

    if (!handshake_private_key(context)) {
        LOG_WARNING("Could not complete handshake!");
        closesocket(context->socket);
        return -1;
    }
    set_timeout(context->socket, recvfrom_timeout_ms);

    // Check that confirmation matches STUN's claimed client
    if (context->addr.sin_addr.s_addr != entry.ip || context->addr.sin_port != entry.private_port) {
        LOG_WARNING(
            "Connection did not match STUN's claimed client, got %s:%d "
            "instead\n",
            inet_ntoa(context->addr.sin_addr), ntohs(context->addr.sin_port));
        context->addr.sin_addr.s_addr = entry.ip;
        context->addr.sin_port = entry.private_port;
        LOG_WARNING("Should have been %s:%d!\n", inet_ntoa(context->addr.sin_addr),
                    ntohs(context->addr.sin_port));
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Client received at %s:%d!\n", inet_ntoa(context->addr.sin_addr),
             ntohs(context->addr.sin_port));

    return 0;
}

int create_udp_client_context(SocketContext* context, char* destination, int port,
                              int recvfrom_timeout_ms, int stun_timeout_ms) {
    context->is_tcp = false;

    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Client connection protocol
    context->is_server = false;
    context->addr.sin_family = AF_INET;
    context->addr.sin_addr.s_addr = inet_addr(destination);
    context->addr.sin_port = htons((unsigned short)port);

    LOG_INFO("Connecting to server...");

    // Send Ack
    if (ack(context) < 0) {
        LOG_WARNING("Could not send ack to server %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    fractal_sleep(stun_timeout_ms);

    if (!handshake_private_key(context)) {
        LOG_WARNING("Could not complete handshake!");
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Connected to server on %s:%d! (Private %d)\n", inet_ntoa(context->addr.sin_addr),
             port, ntohs(context->addr.sin_port));

    set_timeout(context->socket, recvfrom_timeout_ms);

    return 0;
}

int create_udp_client_context_stun(SocketContext* context, char* destination, int port,
                                   int recvfrom_timeout_ms, int stun_timeout_ms) {
    context->is_tcp = false;

    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Client connection protocol
    context->is_server = false;

    struct sockaddr_in stun_addr;
    stun_addr.sin_family = AF_INET;
    stun_addr.sin_addr.s_addr = inet_addr(STUN_IP);
    stun_addr.sin_port = htons(STUN_PORT);

    StunRequest stun_request = {0};
    stun_request.type = ASK_INFO;
    stun_request.entry.ip = inet_addr(destination);
    stun_request.entry.public_port = htons((unsigned short)port);

    LOG_INFO("Sending info request to STUN...");
    if (sendto(context->socket, (const char*)&stun_request, sizeof(stun_request), 0,
               (struct sockaddr*)&stun_addr, sizeof(stun_addr)) < 0) {
        LOG_WARNING("Could not send message to STUN %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    StunEntry entry = {0};
    int recv_size;
    if ((recv_size = recvp(context, &entry, sizeof(entry))) < 0) {
        LOG_WARNING("Could not receive message from STUN %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    if (recv_size != sizeof(entry)) {
        LOG_WARNING("STUN Response of wrong size! %d", recv_size);
        closesocket(context->socket);
        return -1;
    } else if (entry.ip != stun_request.entry.ip ||
               entry.public_port != stun_request.entry.public_port) {
        LOG_WARNING("STUN Response IP and/or Public Port is incorrect!");
        closesocket(context->socket);
        return -1;
    } else if (entry.private_port == 0) {
        LOG_WARNING("STUN reported no such IP Address");
        closesocket(context->socket);
        return -1;
    } else {
        LOG_WARNING("Received STUN response! Public %d is mapped to private %d\n",
                    ntohs((unsigned short)entry.public_port),
                    ntohs((unsigned short)entry.private_port));
        context->addr.sin_family = AF_INET;
        context->addr.sin_addr.s_addr = entry.ip;
        context->addr.sin_port = entry.private_port;
    }

    LOG_INFO("Connecting to server...");

    // Open up the port
    if (sendp(context, NULL, 0) < 0) {
        LOG_ERROR("sendp(3) failed! Could not open up port! %d", get_last_network_error());
        return false;
    }
    fractal_sleep(150);

    if (!handshake_private_key(context)) {
        LOG_WARNING("Could not complete handshake!");
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Connected to server on %s:%d! (Private %d)\n", inet_ntoa(context->addr.sin_addr),
             port, ntohs(context->addr.sin_port));
    set_timeout(context->socket, recvfrom_timeout_ms);

    return 0;
}

int create_udp_context(SocketContext* context, char* destination, int port, int recvfrom_timeout_ms,
                       int stun_timeout_ms, bool using_stun, char* binary_aes_private_key) {
    if ((int)((unsigned short)port) != port) {
        LOG_ERROR("Port invalid: %d", port);
    }
    if (context == NULL) {
        LOG_ERROR("Context is NULL");
        return -1;
    }
    port = port_mappings[port];

    context->timeout = recvfrom_timeout_ms;
    context->mutex = fractal_create_mutex();
    memcpy(context->binary_aes_private_key, binary_aes_private_key,
           sizeof(context->binary_aes_private_key));

    if (using_stun) {
        if (destination == NULL)
            return create_udp_server_context_stun(context, port, recvfrom_timeout_ms,
                                                  stun_timeout_ms);
        else
            return create_udp_client_context_stun(context, destination, port, recvfrom_timeout_ms,
                                                  stun_timeout_ms);
    } else {
        if (destination == NULL)
            return create_udp_server_context(context, port, recvfrom_timeout_ms, stun_timeout_ms);
        else
            return create_udp_client_context(context, destination, port, recvfrom_timeout_ms,
                                             stun_timeout_ms);
    }
}

size_t write_curl_response_callback(char* ptr, size_t size, size_t nmemb, CurlResponseBuffer* crb) {
    /*
    Writes CURL request response data to the CurlResponseBuffer buffer up to `crb->max_len`

    Arguments:
        ptr (char*): pointer to the delivered response data
        size (size_t): always 1 - size of each `nmemb` pointed to by `ptr`
        nmemb (size_t): the size of the buffer pointed to by `ptr`
        crb (CurlResponseBuffer*): the response buffer object that contains full response data

    Returns:
        size (size_t): returns `size * nmemb` if successful (this function just always
            returns success state)
    */

    if (!crb || !crb->buffer || crb->filled_len > crb->max_len) {
        return size * nmemb;
    }

    size_t copy_bytes = size * nmemb;
    if (copy_bytes + crb->filled_len > crb->max_len) {
        copy_bytes = crb->max_len - crb->filled_len;
    }

    memcpy(crb->buffer + crb->filled_len, ptr, copy_bytes);

    return size * nmemb;
}

bool send_http_request(char* type, char* host_s, char* path, char* payload, char** response_body,
                       size_t max_response_size) {
    /*
    Send an HTTP (over HTTPS protocol) to a host

    Arguments:
        type (char*): type of HTTP request (POST, GET, etc.)
        host_s (char*): hostname (e.g. URL) for HTTP request target
        path (char*): path of request (full request URL would be host_s/path)
        payload (char*): content of the request body
        response_body (char**): pointer to the buffer where request response should be stored
        max_response_size (size_t): max size of the response buffer

    Returns:
        bool: true on success, false on failure
        => ARG `response_body` is loaded with request response data
    */

    // verify that we're requesting from a valid host
    struct hostent* host;
    host = gethostbyname(host_s);
    if (host == NULL) {
        LOG_ERROR("Error %d: Could not resolve host %s", h_errno, host_s);
        return false;
    }

#ifndef _WIN32
    // use CURL to send a request and process response buffer
    CURL* curl;
    curl = curl_easy_init();

    if (!curl) {
        return false;
    }

    // set request type (POST/GET/etc.) and protocol to https
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, type);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_DEFAULT_PROTOCOL, "https");

    // create target URL
#ifdef CURLUPART_URL
    // when curl's urlapi is not available
    CURLU* curl_url_handle = curl_url();
    if (!curl_url_handle) {
        return false;
    }
    curl_url_set(curl_url_handle, CURLUPART_URL, host_s, CURLU_DEFAULT_SCHEME);
    curl_url_set(curl_url_handle, CURLUPART_PATH, path, CURLU_DEFAULT_SCHEME);
    curl_url_cleanup(curl_url_handle);
    curl_easy_setopt(curl, CURLOPT_CURLU, curl_url_handle);
#else
    // with no urlapi, build our own URL (path must begin with '/' when passed in)
    char* full_url = safe_malloc(strlen(host_s) + strlen(path) + 2);
    sprintf(full_url, "%s%s", host_s, path);
    curl_easy_setopt(curl, CURLOPT_URL, full_url);
    free(full_url);
#endif  // CURL urlapi

    if (payload) {
        // add request headers:
        //       "Content-Type: application/json"
        //       "Content-Length: payload_len"
        struct curl_slist* headers = NULL;
        headers = curl_slist_append(headers, "Content-Type: application/json");
        // create content-length header
        char* content_length_header = safe_malloc(64);
        sprintf(content_length_header, "Content-Length: %lu", strlen(payload));
        headers = curl_slist_append(headers, content_length_header);
        free(content_length_header);
        curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);

        // add request payload
        curl_easy_setopt(curl, CURLOPT_POSTFIELDS, payload);
    }

    // if a response is expected (response_body != NULL), have libcurl return response body
    CurlResponseBuffer crb;
    crb.buffer = NULL;
    if (response_body) {
        crb.buffer = safe_malloc(max_response_size);
        crb.filled_len = 0;
        crb.max_len = max_response_size;
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_curl_response_callback);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &crb);
    }

    // LOG_ERROR curl's error if it fails
    char* error_buf = safe_malloc(CURL_ERROR_SIZE);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error_buf);
    if (curl_easy_perform(curl) != 0) {
        LOG_ERROR("curl to %s/%s failed: %s", host_s, path, error_buf);
        free(error_buf);
        return false;
    }
    free(error_buf);

    // if response is expected and a response buffer was created, copy over
    if (response_body && crb.buffer) {
        *response_body = crb.buffer;
    }

    curl_easy_cleanup(curl);

#else

    HINTERNET http_session = NULL;
    HINTERNET http_connect = NULL;
    HINTERNET http_request = NULL;

    // open session handle
    http_session = WinHttpOpen(L"Fractal Protocol", WINHTTP_ACCESS_TYPE_NO_PROXY,
                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);

    // specify http server
    if (http_session) {
        http_connect =
            WinHttpConnect(http_session, (LPCWSTR)host_s, INTERNET_DEFAULT_HTTPS_PORT, 0);
    }

    // create http request handl
    if (http_connect) {
        http_request =
            WinHttpOpenRequest(http_connect, (LPCWSTR)type, (LPCWSTR)path, NULL, WINHTTP_NO_REFERER,
                               WINHTTP_DEFAULT_ACCEPT_TYPES, WINHTTP_FLAG_SECURE);
    }

    // send the request
    if (http_request) {
        DWORD payload_size = 0;
        if (payload) {
            payload_size = (DWORD)strlen(payload);

            // add request headers:
            //       "Content-Type: application/json\r\n"
            //       "Content-Length: %d\r\n"
            char* headers = safe_malloc(128);
            sprintf(headers,
                    "Content-Type: application/json\r\n"
                    "Content-Length: %d",
                    (int)payload_size);
            if (!WinHttpAddRequestHeaders(http_request, (LPCWSTR)headers, (DWORD)strlen(headers),
                                          0)) {
                LOG_ERROR("WinHttpAddRequestHeaders failed with error %u", GetLastError());
            }
        }
        if (!WinHttpSendRequest(http_request, WINHTTP_NO_ADDITIONAL_HEADERS, 0, (LPVOID)payload,
                                payload_size, payload_size, 0)) {
            LOG_ERROR("WinHttpSendRequest failed with error %u", GetLastError());
            return false;
        }
    }

    // end request
    WinHttpReceiveResponse(http_request, NULL);

    DWORD read_size = 0;
    DWORD total_read_size = 0;
    DWORD downloaded_size;
    DWORD size_to_download = 0;
    // keep checking for data while there is still data
    if (response_body) {
        *response_body = safe_malloc(max_response_size);

        do {
            // check for available data
            read_size = 0;
            if (!WinHttpQueryDataAvailable(http_request, &read_size)) {
                LOG_ERROR("WinHttpQueryDataAvailable failed with error %u", GetLastError());
                free(*response_body);
                return false;
            }

            // allocate space for the buffer
            size_to_download = read_size;
            if (read_size + total_read_size > max_response_size) {
                size_to_download = (DWORD)max_response_size - total_read_size;
            }

            if (!WinHttpReadData(http_request, (LPVOID)*response_body, read_size,
                                 &downloaded_size)) {
                LOG_ERROR("WinHttpReadData failed with error %u", GetLastError());
                free(*response_body);
                return false;
            }

        } while (read_size > 0);
    }

    if (http_request) {
        WinHttpCloseHandle(http_request);
    }
    if (http_connect) {
        WinHttpCloseHandle(http_connect);
    }
    if (http_session) {
        WinHttpCloseHandle(http_session);
    }

#endif  // not _WIN32

    return true;
}

bool send_post_request(char* host_s, char* path, char* payload, char** response_body,
                       size_t max_response_size) {
    /*
        Send post request to `host_s` with body `payload`

        Arguments:
            host_s (char*): host for request to be sent to
            path (char*): path in host to send request to
            payload (char*): content to be sent in request body
            response_body (char**): pointer to buffer for request response to be
                copied into
            max_response_size (size_t): max size of received response to be
                copied into `response_body`

        Return:
            bool: return true on success (or 0-length host), false on failure
            => ARG `response_body` is loaded with request response data
    */

    // assume that no host means no post request needs to be sent,
    //  so treat this as an auto-success
    if (strlen(host_s) == 0) {
        return true;
    }

    return send_http_request("POST", host_s, path, payload, response_body, max_response_size);
}

bool send_get_request(char* host_s, char* path, char** response_body, size_t max_response_size) {
    /*
        Send get request to `host_s`

        Arguments:
            host_s (char*): host for request to be sent to
            path (char*): path in host to send request to
            response_body (char**): pointer to buffer for request response to be
                copied into
            max_response_size (size_t): max size of received response to be
                copied into `response_body`

        Return:
            bool: return true on success (or 0-length host), false on failure
            => ARG `response_body` is loaded with request response data
    */

    // assume that no host means no post request needs to be sent,
    //  so treat this as an auto-success
    if (strlen(host_s) == 0) {
        return true;
    }

    return send_http_request("GET", host_s, path, NULL, response_body, max_response_size);
}

void set_timeout(SOCKET socket, int timeout_ms) {
    // Sets the timeout for SOCKET s to be timeout_ms in milliseconds
    // Any recv calls will wait this long before timing out
    // -1 means that it will block indefinitely until a packet is received
    // 0 means that it will immediately return with whatever data is waiting in
    // the buffer
    if (timeout_ms < 0) {
        LOG_WARNING(
            "WARNING: This socket will blocking indefinitely. You will not be "
            "able to recover if a packet is never received");
        unsigned long mode = 0;

        if (FRACTAL_IOCTL_SOCKET(socket, FIONBIO, &mode) != 0) {
            LOG_FATAL("Failed to make socket blocking.");
        }

    } else if (timeout_ms == 0) {
        unsigned long mode = 1;
        if (FRACTAL_IOCTL_SOCKET(socket, FIONBIO, &mode) != 0) {
            LOG_FATAL("Failed to make socket return immediately.");
        }
    } else {
        // Set to blocking when setting a timeout
        unsigned long mode = 0;
        if (FRACTAL_IOCTL_SOCKET(socket, FIONBIO, &mode) != 0) {
            LOG_FATAL("Failed to make socket blocking.");
            return;
        }

        clock read_timeout = create_clock(timeout_ms);

        if (setsockopt(socket, SOL_SOCKET, SO_RCVTIMEO, (const char*)&read_timeout,
                       sizeof(read_timeout)) < 0) {
            int err = get_last_network_error();
            LOG_WARNING("Failed to set timeout: %d. Msg: %s\n", err, strerror(err));

            return;
        }
    }
}

void prepare_private_key_request(PrivateKeyData* priv_key_data) {
    // Generate the IV, so that someone else can sign it
    gen_iv(priv_key_data->iv);
    // Clear priv_key_data so that PrivateKeyData is entirely initialized
    // (For valgrind)
    memset(priv_key_data->signature, 0, sizeof(priv_key_data->signature));
}

typedef struct {
    char iv[16];
    char private_key[16];
} SignatureData;

bool sign_private_key(PrivateKeyData* priv_key_data, int recv_size, void* private_key) {
    if (recv_size == sizeof(PrivateKeyData)) {
        SignatureData sig_data;
        memcpy(sig_data.iv, priv_key_data->iv, sizeof(priv_key_data->iv));
        memcpy(sig_data.private_key, private_key, sizeof(sig_data.private_key));
        hmac(priv_key_data->signature, &sig_data, sizeof(sig_data), private_key);
        return true;
    } else {
        LOG_ERROR("Recv Size was not equal to PrivateKeyData: %d instead of %d", recv_size,
                  sizeof(PrivateKeyData));
        return false;
    }
}

bool confirm_private_key(PrivateKeyData* our_priv_key_data,
                         PrivateKeyData* our_signed_priv_key_data, int recv_size,
                         void* private_key) {
    if (recv_size == sizeof(PrivateKeyData)) {
        if (memcmp(our_priv_key_data->iv, our_signed_priv_key_data->iv, 16) != 0) {
            LOG_ERROR("IV is incorrect!");
            return false;
        } else {
            SignatureData sig_data;
            memcpy(sig_data.iv, our_signed_priv_key_data->iv, sizeof(our_signed_priv_key_data->iv));
            memcpy(sig_data.private_key, private_key, sizeof(sig_data.private_key));
            if (!verify_hmac(our_signed_priv_key_data->signature, &sig_data, sizeof(sig_data),
                             private_key)) {
                LOG_ERROR("Verify HMAC Failed");
                return false;
            } else {
                return true;
            }
        }
    } else {
        LOG_ERROR("Recv Size was not equal to PrivateKeyData: %d instead of %d", recv_size,
                  sizeof(PrivateKeyData));
        return false;
    }
}
