#ifndef AES_H
#define AES_H

#include "../include/fractal.h"

int encrypt_packet( struct RTPPacket* plaintext_packet, int packet_len, struct RTPPacket* encrypted_packet, unsigned char* private_key );

int decrypt_packet( struct RTPPacket* encrypted_packet, int packet_len, struct RTPPacket* plaintext_packet, unsigned char* private_key );

#endif
