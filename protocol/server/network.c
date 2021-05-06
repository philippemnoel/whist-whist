#include <fractal/core/fractal.h>
#include <fractal/network/network.h>
#include <stdio.h>

#include <fractal/utils/aes.h>

#include "network.h"
#include "client.h"

#define UDP_CONNECTION_WAIT 1000
#define TCP_CONNECTION_WAIT 1000
#define BITS_IN_BYTE 8.0

extern Client clients[MAX_NUM_CLIENTS];

int last_input_id = -1;

int connect_client(int id, bool using_stun, char *binary_aes_private_key) {
    if (create_udp_context(&(clients[id].UDP_context), NULL, clients[id].UDP_port, 1,
                           UDP_CONNECTION_WAIT, using_stun, binary_aes_private_key) < 0) {
        LOG_ERROR("Failed UDP connection with client (ID: %d)", id);
        return -1;
    }

    if (create_tcp_context(&(clients[id].TCP_context), NULL, clients[id].TCP_port, 1,
                           TCP_CONNECTION_WAIT, using_stun, binary_aes_private_key) < 0) {
        LOG_WARNING("Failed TCP connection with client (ID: %d)", id);
        closesocket(clients[id].UDP_context.socket);
        return -1;
    }
    return 0;
}

int disconnect_client(int id) {
    closesocket(clients[id].UDP_context.socket);
    closesocket(clients[id].TCP_context.socket);
    return 0;
}

int disconnect_clients(void) {
    int ret = 0;
    for (int id = 0; id < MAX_NUM_CLIENTS; id++) {
        if (clients[id].is_active) {
            if (disconnect_client(id) != 0) {
                LOG_ERROR("Failed to disconnect client (ID: %d)", id);
                ret = -1;
            } else {
                clients[id].is_active = false;
            }
        }
    }
    return ret;
}

int broadcast_ack(void) {
    int ret = 0;
    for (int id = 0; id < MAX_NUM_CLIENTS; id++) {
        if (clients[id].is_active) {
            ack(&(clients[id].TCP_context));
            ack(&(clients[id].UDP_context));
        }
    }
    return ret;
}

int broadcast_udp_packet(FractalPacketType type, void *data, int len, int id, int burst_bitrate,
                         FractalPacket *packet_buffer, int *packet_len_buffer) {
    if (id <= 0) {
        LOG_WARNING("IDs must be positive!");
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

        int *packet_len = &l_len;
        FractalPacket *packet = &l_packet;

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
        memcpy(packet->data, (uint8_t *)data + curr_index, payload_size);
        packet->index = (short)i;
        packet->payload_size = payload_size;
        packet->id = id;
        packet->num_indices = (short)num_indices;
        packet->is_a_nack = false;
        int packet_size = PACKET_HEADER_SIZE + packet->payload_size;

        // Save the len to nack buffer lens
        *packet_len = packet_size;

        // Send it off
        for (int j = 0; j < MAX_NUM_CLIENTS; j++) {
            if (clients[j].is_active) {
                // Encrypt the packet with AES
                FractalPacket encrypted_packet;
                int encrypt_len =
                    encrypt_packet(packet, packet_size, &encrypted_packet,
                                   (unsigned char *)clients[j].UDP_context.binary_aes_private_key);

                fractal_lock_mutex(clients[j].UDP_context.mutex);
                int sent_size = sendp(&(clients[j].UDP_context), &encrypted_packet, encrypt_len);
                fractal_unlock_mutex(clients[j].UDP_context.mutex);
                if (sent_size < 0) {
                    int error = get_last_network_error();
                    LOG_INFO("Unexpected Packet Error: %d", error);
                    LOG_WARNING("Failed to send UDP packet to client id: %d", j);
                }
            }
        }

        i++;
        curr_index += payload_size;
    }

    // LOG_INFO( "Packet Time: %f", get_timer( packet_timer ) );

    return 0;
}

int broadcast_tcp_packet(FractalPacketType type, void *data, int len) {
    int ret = 0;
    for (int id = 0; id < MAX_NUM_CLIENTS; id++) {
        if (clients[id].is_active) {
            if (send_tcp_packet(&(clients[id].TCP_context), type, (uint8_t *)data, len) < 0) {
                LOG_WARNING("Failed to send TCP packet to client id: %d", id);
                if (ret == 0) ret = -1;
            }
        }
    }
    return ret;
}

clock last_tcp_read;
bool has_read = false;
int try_get_next_message_tcp(int client_id, FractalClientMessage **fcmsg, size_t *fcmsg_size) {
    *fcmsg_size = 0;
    *fcmsg = NULL;

    // Check if 20ms has passed since last TCP recvp, since each TCP recvp read takes 8ms
    bool should_recvp = false;
    if (!has_read || get_timer(last_tcp_read) * 1000.0 > 20.0) {
        should_recvp = true;
        start_timer(&last_tcp_read);
        has_read = true;
    }

    FractalPacket *tcp_packet = read_tcp_packet(&(clients[client_id].TCP_context), should_recvp);
    if (tcp_packet) {
        *fcmsg = (FractalClientMessage *)tcp_packet->data;
        *fcmsg_size = (size_t)tcp_packet->payload_size;

        LOG_INFO("Received TCP Packet (Probably clipboard): Size %d", tcp_packet->payload_size);
        free_tcp_packet(tcp_packet);
    }
    return 0;
}

int try_get_next_message_udp(int client_id, FractalClientMessage *fcmsg, size_t *fcmsg_size) {
    *fcmsg_size = 0;

    memset(fcmsg, 0, sizeof(*fcmsg));

    FractalPacket *packet = read_udp_packet(&(clients[client_id].UDP_context));
    if (packet) {
        memcpy(fcmsg, packet->data, max(sizeof(*fcmsg), (size_t)packet->payload_size));
        if (packet->payload_size != get_fmsg_size(fcmsg)) {
            LOG_WARNING("Packet is of the wrong size!: %d", packet->payload_size);
            LOG_WARNING("Type: %d", fcmsg->type);
            return -1;
        }
        *fcmsg_size = packet->payload_size;

        // Make sure that keyboard events are played in order
        if (fcmsg->type == MESSAGE_KEYBOARD || fcmsg->type == MESSAGE_KEYBOARD_STATE) {
            // Check that id is in order
            if (packet->id > last_input_id) {
                packet->id = last_input_id;
            } else {
                LOG_WARNING("Ignoring out of order keyboard input.");
                *fcmsg_size = 0;
                return 0;
            }
        }
    }
    return 0;
}
