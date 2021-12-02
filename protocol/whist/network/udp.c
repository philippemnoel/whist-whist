#ifdef _WIN32
#define _WINSOCK_DEPRECATED_NO_WARNINGS  // unportable Windows warnings, needs to
                                         // be at the very top
#endif

#include "udp.h"
#include <whist/utils/aes.h>

#ifndef _WIN32
#include <fcntl.h>
#endif

// Define how many times to retry sending a UDP packet in case of Error 55 (buffer full). The
// current value (5) is an arbitrary choice that was found to work well in practice.
#define RETRIES_ON_BUFFER_FULL 5

extern unsigned short port_mappings[USHRT_MAX + 1];

/*
============================
UDP Implementation of Network.h Interface
============================
*/

int udp_ack(void* raw_context) {
    SocketContextData* context = raw_context;
    return send(context->socket, NULL, 0, 0);
}

WhistPacket* udp_read_packet(void* raw_context, bool should_recv) {
    SocketContextData* context = raw_context;

    if (should_recv == false) {
        LOG_ERROR("should_recv should only be false in TCP contexts");
        return NULL;
    }

    if (context->decrypted_packet_used) {
        LOG_ERROR("Cannot use context->decrypted_packet buffer! Still being used somewhere else!");
        return NULL;
    }

    // Wait to receive packet over TCP, until timing out
    WhistPacket encrypted_packet;
    int encrypted_len =
        recv(context->socket, (char*)&encrypted_packet, sizeof(encrypted_packet), 0);

    // If the packet was successfully received, then decrypt it
    if (encrypted_len > 0) {
        int decrypted_len;
        if (ENCRYPTING_PACKETS) {
            // Decrypt the packet
            decrypted_len =
                decrypt_packet(&encrypted_packet, encrypted_len, &context->decrypted_packet,
                               (unsigned char*)context->binary_aes_private_key);
        } else {
            // The decrypted packet is just the original packet, during dev mode
            decrypted_len = encrypted_len;
            memcpy(&context->decrypted_packet, &encrypted_packet, encrypted_len);
        }
#if LOG_NETWORKING
        LOG_INFO("Received a WhistPacket of size %d over UDP", decrypted_len);
#endif

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

        context->decrypted_packet_used = true;
        return &context->decrypted_packet;
    } else {
        if (encrypted_len < 0) {
            int error = get_last_network_error();
            switch (error) {
                case WHIST_ETIMEDOUT:
                case WHIST_EWOULDBLOCK:
                    // Break on expected network errors
                    break;
                default:
                    LOG_WARNING("Unexpected Packet Error: %d", error);
                    break;
            }
        } else {
            // Ignore packets of size 0
        }
        return NULL;
    }
}

void udp_free_packet(void* raw_context, WhistPacket* udp_packet) {
    SocketContextData* context = raw_context;

    if (!context->decrypted_packet_used) {
        LOG_ERROR("Called udp_free_packet, but there was no udp_packet to free!");
        return;
    }
    if (udp_packet != &context->decrypted_packet) {
        LOG_ERROR("The wrong pointer was passed into udp_free_packet!");
    }

    // Free the one buffer
    context->decrypted_packet_used = false;
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int udp_send_constructed_packet(void* raw_context, WhistPacket* packet, size_t packet_size) {
    SocketContextData* context = raw_context;
    if (context == NULL) {
        LOG_ERROR("SocketContextData is NULL");
        return -1;
    }

    // Use MAX_PACKET_SIZE here since we are checking the size of the packet itself.
    if (packet_size > MAX_PACKET_SIZE) {
        LOG_ERROR("Packet too large to send over UDP: %d", packet_size);
        return -1;
    }

    WhistPacket encrypted_packet;
    size_t encrypted_len = (size_t)encrypt_packet(packet, (int)packet_size, &encrypted_packet,
                                                  (unsigned char*)context->binary_aes_private_key);
    network_throttler_wait_byte_allocation(context->network_throttler, (size_t)encrypted_len);

    // If sending fails because of no buffer space available on the system, retry a few times.
    for (int i = 0; i < RETRIES_ON_BUFFER_FULL; i++) {
        whist_lock_mutex(context->mutex);
        int ret;
#if LOG_NETWORKING
        LOG_INFO("Sending a WhistPacket of size %d over UDP", packet_size);
#endif
        if (ENCRYPTING_PACKETS) {
            // Send encrypted during normal usage
            ret = send(context->socket, (const char*)&encrypted_packet, (int)encrypted_len, 0);
        } else {
            // Send unencrypted during dev mode
            ret = send(context->socket, (const char*)packet, (int)packet_size, 0);
        }
        whist_unlock_mutex(context->mutex);
        if (ret < 0) {
            int error = get_last_network_error();
            if (error == ENOBUFS) {
                LOG_WARNING("Unexpected UDP Packet Error: %d, retrying to send packet!", error);
                continue;
            } else {
                LOG_WARNING("Unexpected UDP Packet Error: %d", error);
                return -1;
            }
        } else {
            break;
        }
    }

    return 0;
}

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int udp_send_packet(void* raw_context, WhistPacketType packet_type, void* payload, int payload_size,
                    int packet_id) {
    SocketContextData* context = raw_context;
    if (context == NULL) {
        LOG_ERROR("SocketContextData is NULL");
        return -1;
    }

    // Get the nack_buffer, if there is one for this type of packet
    WhistPacket* nack_buffer = NULL;

    int type_index = (int)packet_type;
    if (type_index >= NUM_PACKET_TYPES) {
        LOG_ERROR("Type is out of bounds! Something wrong happened");
        return -1;
    }
    if (context->nack_buffers[type_index] != NULL) {
        nack_buffer =
            context->nack_buffers[type_index][packet_id % context->nack_num_buffers[type_index]];
    }

    // Calculate number of packets needed to send the payload, rounding up.
    int num_indices = payload_size == 0 ? 1
                                        : (int)(payload_size / MAX_PAYLOAD_SIZE +
                                                (payload_size % MAX_PAYLOAD_SIZE == 0 ? 0 : 1));

    // If nack buffer can't hold a packet that large,
    // Or there's no nack buffer but it's a packet that must be split,
    // Then there's a problem and we LOG_ERROR
    if (nack_buffer && num_indices > context->nack_buffer_max_indices[type_index] ||
        nack_buffer && payload_size > context->nack_buffer_max_payload_size[type_index] ||
        !nack_buffer && num_indices > 1) {
        LOG_ERROR("Packet is too large to send the payload! %d", num_indices);
        return -1;
    }

    // Write all the packets into the packet buffer and send them all
    size_t current_position = 0;
    for (int packet_index = 0; packet_index < num_indices; packet_index++) {
        if (nack_buffer) {
            // Lock on a per-loop basis to not starve nack() calls
            whist_lock_mutex(context->nack_mutex[type_index]);
        }
        WhistPacket local_packet;
        // Construct the packet, potentially into the nack buffer
        WhistPacket* packet = nack_buffer ? &nack_buffer[packet_index] : &local_packet;
        packet->type = packet_type;
        packet->payload_size = (int)min(payload_size - current_position, MAX_PAYLOAD_SIZE);
        packet->index = (short)packet_index;
        packet->id = packet_id;
        packet->num_indices = (short)num_indices;
        packet->is_a_nack = false;
        memcpy(packet->data, (char*)payload + current_position, packet->payload_size);
        current_position += packet->payload_size;
        // Send the packet,
        // ignoring the return code since maybe a subset of the packets were sent
        udp_send_constructed_packet(context, packet, get_packet_size(packet));
        if (nack_buffer) {
            whist_unlock_mutex(context->nack_mutex[type_index]);
        }
    }

    return 0;
}

void udp_update_bitrate_settings(SocketContext* socket_context, int burst_bitrate,
                                 double fec_packet_ratio) {
    SocketContextData* context = socket_context->context;

    if (context->network_throttler == NULL) {
        LOG_ERROR("Tried to set the burst bitrate, but there's no network throttler!");
        return;
    }

    if (fec_packet_ratio > 0.0) {
        LOG_ERROR("Asked for a larger FEC ratio, but FEC isn't implemented yet!");
    }
    context->fec_packet_ratio = fec_packet_ratio;

    network_throttler_set_burst_bitrate(context->network_throttler, burst_bitrate);
}

void udp_register_nack_buffer(SocketContext* socket_context, WhistPacketType type,
                              int max_payload_size, int num_buffers) {
    SocketContextData* context = socket_context->context;

    int type_index = (int)type;
    if (type_index >= NUM_PACKET_TYPES) {
        LOG_ERROR("Type is out of bounds! Something wrong happened");
        return;
    }
    if (context->nack_buffers[type_index] != NULL) {
        LOG_ERROR("Nack Buffer has already been initialized!");
        return;
    }

    int max_num_ids = max_payload_size / MAX_PAYLOAD_SIZE + 2;

    context->nack_buffers[type_index] = malloc(sizeof(WhistPacket*) * num_buffers);
    context->nack_mutex[type_index] = whist_create_mutex();
    context->nack_num_buffers[type_index] = num_buffers;
    context->nack_buffer_max_payload_size[type_index] = max_payload_size;
    context->nack_buffer_max_indices[type_index] = max_num_ids;

    // Allocate each nack buffer, based on num_buffers
    for (int i = 0; i < num_buffers; i++) {
        // Allocate a buffer of max_num_ids WhistPacket's
        context->nack_buffers[type_index][i] = allocate_region(sizeof(WhistPacket) * max_num_ids);
        // Set just the ID, but don't memset the entire region to 0,
        // Or you'll make the kernel allocate all of the memory
        for (int j = 0; j < max_num_ids; j++) {
            context->nack_buffers[type_index][i][j].id = 0;
        }
    }
}

int udp_nack(SocketContext* socket_context, WhistPacketType type, int packet_id, int packet_index) {
    SocketContextData* context = socket_context->context;

    int type_index = (int)type;
    if (type_index >= NUM_PACKET_TYPES) {
        LOG_ERROR("Type is out of bounds! Something wrong happened");
        return -1;
    }
    if (context->nack_buffers[type_index] == NULL) {
        LOG_ERROR("Nack Buffer has not been initialized!");
        return -1;
    }
    if (packet_index >= context->nack_buffer_max_indices[type_index]) {
        LOG_ERROR("Nacked Index %d is >= num indices %d!", packet_index,
                  context->nack_buffer_max_indices[type_index]);
        return -1;
    }

    whist_lock_mutex(context->nack_mutex[type_index]);
    WhistPacket* packet =
        &context->nack_buffers[type_index][packet_id % context->nack_num_buffers[type_index]]
                              [packet_index];

    int ret;
    if (packet->id == packet_id) {
        int len = get_packet_size(packet);
        packet->is_a_nack = true;
        LOG_INFO(
            "NACKed %s packet ID %d Index %d found of "
            "length %d. Relaying!",
            type == PACKET_VIDEO ? "video" : "audio", packet_id, packet_index, len);
        ret = udp_send_constructed_packet(context, packet, len);
    } else {
        LOG_WARNING(
            "NACKed %s packet %d %d not found, ID %d was "
            "located instead.",
            type == PACKET_VIDEO ? "video" : "audio", packet_id, packet_index, packet->id);
        ret = -1;
    }

    whist_unlock_mutex(context->nack_mutex[type_index]);
    return ret;
}

void udp_destroy_socket_context(void* raw_context) {
    SocketContextData* context = raw_context;

    if (context->decrypted_packet_used) {
        LOG_ERROR("Destroyed the socket context, but didn't free the most recent UDP packet!");
    }

    // Deallocate the nack buffers
    for (int type_id = 0; type_id < NUM_PACKET_TYPES; type_id++) {
        if (context->nack_buffers[type_id] != NULL) {
            for (int i = 0; i < context->nack_num_buffers[type_id]; i++) {
                deallocate_region(context->nack_buffers[type_id][i]);
            }
            free(context->nack_buffers[type_id]);
            context->nack_buffers[type_id] = NULL;
        }
    }

    closesocket(context->socket);
    if (context->network_throttler != NULL) {
        network_throttler_destroy(context->network_throttler);
    }
    whist_destroy_mutex(context->mutex);
    free(context);
}

/*
============================
Private Function Implementations
============================
*/

int create_udp_server_context(void* raw_context, int port, int recvfrom_timeout_ms,
                              int stun_timeout_ms) {
    SocketContextData* context = raw_context;

    socklen_t slen = sizeof(context->addr);
    int recv_size;
    if ((recv_size = recvfrom(context->socket, NULL, 0, 0, (struct sockaddr*)(&context->addr),
                              &slen)) != 0) {
        LOG_WARNING("Failed to receive ack! %d %d", recv_size, get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

    if (connect(context->socket, (struct sockaddr*)&context->addr, sizeof(context->addr)) == -1) {
        LOG_WARNING("Failed to connect()!");
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

int create_udp_server_context_stun(SocketContextData* context, int port, int recvfrom_timeout_ms,
                                   int stun_timeout_ms) {
    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);
    set_tos(context->socket, TOS_DSCP_EXPEDITED_FORWARDING);

    // Server connection protocol

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
            (get_last_network_error() == WHIST_ETIMEDOUT ||
             get_last_network_error() == WHIST_EAGAIN)) {
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

    if (connect(context->socket, (struct sockaddr*)&context->addr, sizeof(context->addr)) == -1) {
        LOG_WARNING("Failed to connect()!");
        closesocket(context->socket);
        return -1;
    }

    // Open up the port
    if (send(context->socket, NULL, 0, 0) < 0) {
        LOG_ERROR("send(4) failed! Could not open up port! %d", get_last_network_error());
        return false;
    }
    whist_sleep(150);

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

int create_udp_client_context(SocketContextData* context, char* destination, int port,
                              int recvfrom_timeout_ms, int stun_timeout_ms) {
    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Client connection protocol
    context->addr.sin_family = AF_INET;
    context->addr.sin_addr.s_addr = inet_addr(destination);
    context->addr.sin_port = htons((unsigned short)port);
    if (connect(context->socket, (struct sockaddr*)&context->addr, sizeof(context->addr)) == -1) {
        LOG_WARNING("Failed to connect()!");
        closesocket(context->socket);
        return -1;
    }

    LOG_INFO("Connecting to server...");

    // Send Ack
    if (udp_ack(context) < 0) {
        LOG_WARNING("Could not send ack to server %d\n", get_last_network_error());
        closesocket(context->socket);
        return -1;
    }

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

int create_udp_client_context_stun(SocketContextData* context, char* destination, int port,
                                   int recvfrom_timeout_ms, int stun_timeout_ms) {
    // Create UDP socket
    if ((context->socket = socketp_udp()) == INVALID_SOCKET) {
        return -1;
    }

    set_timeout(context->socket, stun_timeout_ms);

    // Client connection protocol

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
    if ((recv_size = recv(context->socket, (char*)&entry, sizeof(entry), 0)) < 0) {
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
    if (connect(context->socket, (struct sockaddr*)&context->addr, sizeof(context->addr)) == -1) {
        LOG_WARNING("Failed to connect()!");
        closesocket(context->socket);
        return -1;
    }

    // Open up the port
    if (send(context->socket, NULL, 0, 0) < 0) {
        LOG_ERROR("send(4) failed! Could not open up port! %d", get_last_network_error());
        return false;
    }
    whist_sleep(150);

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

/*
============================
Public Function Implementations
============================
*/

bool create_udp_socket_context(SocketContext* network_context, char* destination, int port,
                               int recvfrom_timeout_ms, int connection_timeout_ms, bool using_stun,
                               char* binary_aes_private_key) {
    /*
        Create a UDP socket context

        Arguments:
            context (SocketContext*): pointer to the SocketContext struct to initialize
            destination (char*): the destination address, NULL means act as a server
            port (int): the port to bind over
            recvfrom_timeout_ms (int): timeout, in milliseconds, for recvfrom
            connection_timeout_ms (int): timeout, in milliseconds, for socket connection
            using_stun (bool): Whether or not to use STUN
            binary_aes_private_key (char*): The 16byte AES key to use

        Returns:
            (int): -1 on failure, 0 on success
    */

    // Populate function pointer table
    network_context->ack = udp_ack;
    network_context->read_packet = udp_read_packet;
    network_context->free_packet = udp_free_packet;
    network_context->send_packet = udp_send_packet;
    network_context->destroy_socket_context = udp_destroy_socket_context;

    // Create the SocketContextData, and set to zero
    SocketContextData* context = safe_malloc(sizeof(SocketContextData));
    memset(context, 0, sizeof(SocketContextData));
    network_context->context = context;

    // if dest is NULL, it means the context will be listening for income connections
    if (destination == NULL) {
        if (network_context->listen_socket == NULL) {
            LOG_ERROR("listen_socket not provided");
            return false;
        }
        /*
            for udp, transfer the ownership to SocketContextData.
            when SocketContextData is destoryed, the transferred listen_socket should be closed.
        */
        context->socket = *network_context->listen_socket;
        *network_context->listen_socket = INVALID_SOCKET;
    }

    // Map Port
    if ((int)((unsigned short)port) != port) {
        LOG_ERROR("Port invalid: %d", port);
    }
    port = port_mappings[port];

    context->timeout = recvfrom_timeout_ms;
    context->mutex = whist_create_mutex();
    memcpy(context->binary_aes_private_key, binary_aes_private_key,
           sizeof(context->binary_aes_private_key));

    if (destination == NULL) {
        // On the server, we create a network throttler to limit the
        // outgoing bitrate.
        context->network_throttler = network_throttler_create();
    } else {
        context->network_throttler = NULL;
    }
    context->burst_bitrate = -1;
    context->fec_packet_ratio = 0.0;

    int ret;
    if (using_stun) {
        if (destination == NULL)
            ret = create_udp_server_context_stun(context, port, recvfrom_timeout_ms,
                                                 connection_timeout_ms);
        else
            ret = create_udp_client_context_stun(context, destination, port, recvfrom_timeout_ms,
                                                 connection_timeout_ms);
    } else {
        if (destination == NULL)
            ret = create_udp_server_context(context, port, recvfrom_timeout_ms,
                                            connection_timeout_ms);
        else
            ret = create_udp_client_context(context, destination, port, recvfrom_timeout_ms,
                                            connection_timeout_ms);
    }

    if (ret == 0) {
        // socket options = TCP_NODELAY IPTOS_LOWDELAY SO_RCVBUF=65536
        // Windows Socket 65535 Socket options apply to all sockets.
        // this is set to stop the kernel from buffering too much, thereby
        // getting the data to us faster for lower latency
        int a = 65535;
        if (setsockopt(context->socket, SOL_SOCKET, SO_RCVBUF, (const char*)&a, sizeof(int)) ==
            -1) {
            LOG_ERROR("Error setting socket opts: %d", get_last_network_error());
            closesocket(context->socket);
            return false;
        }
        return true;
    } else {
        return false;
    }
}

int create_udp_listen_socket(SOCKET* sock, int port, int timeout_ms) {
    LOG_INFO("Creating listen UDP Socket");
    *sock = socketp_udp();
    if (*sock == INVALID_SOCKET) {
        LOG_ERROR("Failed to create UDP listen socket");
        return -1;
    }
    set_timeout(*sock, timeout_ms);
    set_tos(*sock, TOS_DSCP_EXPEDITED_FORWARDING);
    // Server connection protocol

    // Bind the server port to the advertized public port
    struct sockaddr_in origin_addr;
    origin_addr.sin_family = AF_INET;
    origin_addr.sin_addr.s_addr = htonl(INADDR_ANY);
    origin_addr.sin_port = htons((unsigned short)port);

    if (bind(*sock, (struct sockaddr*)(&origin_addr), sizeof(origin_addr)) < 0) {
        LOG_ERROR("Failed to bind to port %d! errno=%d\n", port, get_last_network_error());
        closesocket(*sock);
        return -1;
    }

    LOG_INFO("Waiting for client to connect to %s:%d...\n", "localhost", port);

    return 0;
}
