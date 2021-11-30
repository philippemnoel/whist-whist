/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file aes.c
 * @brief This file contains all code that interacts directly with packets
 *        encryption (using AES encryption).
============================
Usage
============================

The function encrypt_packet gets called when a new packet of data needs to be
sent over the network, while decrypt_packet, which calls decrypt_packet_n, gets
called on the receiving end to re-obtain the data and process it.
*/

#if defined(_WIN32)
#pragma warning(disable : 4706)  // assignment within conditional expression warning
#endif

#include "aes.h"

#include <openssl/conf.h>
#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <time.h>

uint32_t hash(void* buf, size_t len) {
    char* key = buf;

    uint64_t pre_hash = 123456789;
    while (len > 8) {
        pre_hash += *(uint64_t*)(key);
        pre_hash += (pre_hash << 32);
        pre_hash += (pre_hash >> 32);
        pre_hash += (pre_hash << 10);
        pre_hash ^= (pre_hash >> 6);
        pre_hash ^= (pre_hash << 48);
        pre_hash ^= 123456789;
        len -= 8;
        key += 8;
    }

    uint32_t hash = (uint32_t)((pre_hash << 32) ^ pre_hash);
    for (size_t i = 0; i < len; ++i) {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

void handle_errors(void) {
    ERR_print_errors_fp(stderr);
    abort();
}

void gen_iv(void* iv) {
    srand((unsigned int)time(NULL) * rand() + rand());
    (void)rand();
    srand((unsigned int)time(NULL) * rand() + rand());
    (void)rand();
    (void)rand();

    for (int i = 0; i < 16; i++) {
        ((unsigned char*)iv)[i] = (unsigned char)rand();
    }
}

int hmac(void* hash, void* buf, int len, void* key) {
    int hash_len;
    HMAC(EVP_sha256(), key, 16, (const unsigned char*)buf, len, (unsigned char*)hash, (unsigned int*)&hash_len);
    if (hash_len != 32) {
        LOG_WARNING("Incorrect hash length!");
        return -1;
    }
    return hash_len;
}

bool verify_hmac(void* hash, void* buf, int len, void* key) {
    char correct_hash[32];
    hmac(correct_hash, buf, len, key);
    for (int i = 0; i < 16; i++) {
        if (((char*)hash)[i] != correct_hash[i]) {
            return false;
        }
    }
    return true;
}

#define CRYPTO_HEADER_LEN \
    (sizeof(((FractalPacket*)0)->hash) + sizeof(((FractalPacket*)0)->cipher_len) + sizeof(((FractalPacket*)0)->iv))

// NOTE that this function is in the hotpath.
// The hotpath *must* return in under ~10000 assembly instructions.
// Please pass this comment into any non-trivial function that this function calls.
int encrypt_packet(FractalPacket* plaintext_packet, int packet_len, FractalPacket* encrypted_packet,
                   unsigned char* private_key) {
    char* plaintext_buf = (char*)plaintext_packet + CRYPTO_HEADER_LEN;
    int plaintext_buf_len = packet_len - CRYPTO_HEADER_LEN;
    // A unique random number so that all packets are encrypted uniquely
    // (Same plaintext twice gives unique encrypted packets)
    gen_iv((unsigned char*)encrypted_packet->iv);

    char* cipher_buf = (char*)encrypted_packet + CRYPTO_HEADER_LEN;
    int cipher_len = aes_encrypt((unsigned char*)plaintext_buf, plaintext_buf_len, private_key,
                                 (unsigned char*)encrypted_packet->iv, (unsigned char*)cipher_buf);
    encrypted_packet->cipher_len = cipher_len;

    int cipher_packet_len = cipher_len + CRYPTO_HEADER_LEN;

    // LOG_INFO( "HMAC: %d", Hash( encrypted_packet->hash, 16 ) );
    char hash[32];
    // Sign the packet with 32 bytes
    hmac(hash, (char*)encrypted_packet + sizeof(encrypted_packet->hash),
         cipher_packet_len - sizeof(encrypted_packet->hash), (char*)private_key);
    // Only use 16 bytes bc we don't need that long of a signature
    memcpy(encrypted_packet->hash, hash, 16);
    // LOG_INFO( "HMAC: %d", Hash( encrypted_packet->hash, 16 ) );
    // encrypted_packet->hash = Hash( (char*)encrypted_packet + sizeof(
    // encrypted_packet->hash ), cipher_packet_len - sizeof(
    // encrypted_packet->hash ) );

    return cipher_packet_len;
}

int decrypt_packet(FractalPacket* encrypted_packet, int packet_len, FractalPacket* plaintext_packet,
                   unsigned char* private_key) {
    if ((unsigned long)packet_len > MAX_PACKET_SIZE) {
        LOG_WARNING("Encrypted version of Packet is too large!");
        return -1;
    }
    int decrypt_len = decrypt_packet_n(encrypted_packet, packet_len, plaintext_packet,
                                       PACKET_HEADER_SIZE + MAX_PAYLOAD_SIZE, private_key);
    return decrypt_len;
}

int decrypt_packet_n(FractalPacket* encrypted_packet, int packet_len, FractalPacket* plaintext_packet,
                     int plaintext_len, unsigned char* private_key) {
    if ((unsigned long)packet_len < PACKET_HEADER_SIZE) {
        LOG_WARNING("Packet is too small (%d bytes) for metadata!", packet_len);
        return -1;
    }

    if (!verify_hmac(encrypted_packet->hash, (char*)encrypted_packet + sizeof(encrypted_packet->hash),
                     packet_len - sizeof(encrypted_packet->hash), private_key)) {
        LOG_WARNING("Incorrect hmac!");
        return -1;
    }

    char* cipher_buf = (char*)encrypted_packet + CRYPTO_HEADER_LEN;
    char* plaintext_buf = (char*)plaintext_packet + CRYPTO_HEADER_LEN;

    int decrypt_len = aes_decrypt((unsigned char*)cipher_buf, encrypted_packet->cipher_len, private_key,
                                  (unsigned char*)encrypted_packet->iv, (unsigned char*)plaintext_buf);
    decrypt_len += CRYPTO_HEADER_LEN;

    int expected_len = get_packet_size(plaintext_packet);
    if (expected_len != decrypt_len) {
        LOG_WARNING(
            "Packet length is incorrect! Expected %d with payload %d, but got "
            "%d\n",
            expected_len, plaintext_packet->payload_size, decrypt_len);
        return -1;
    }

    if (decrypt_len > plaintext_len) {
        LOG_WARNING("Decrypted version of Packet is too large!");
        return -1;
    }

    return decrypt_len;
}

int aes_encrypt(void* plaintext, int plaintext_len, void* key, void* iv, void* ciphertext) {
    EVP_CIPHER_CTX* ctx;

    int len;

    int ciphertext_len;

    // Create and initialise the context
    if (!(ctx = EVP_CIPHER_CTX_new())) handle_errors();

    // Initialise the encryption operation.
    if (1 != EVP_EncryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv)) handle_errors();

    // Encrypt
    if (1 != EVP_EncryptUpdate(ctx, ciphertext, &len, plaintext, plaintext_len)) handle_errors();
    ciphertext_len = len;

    // Finish encryption (Might add a few bytes)
    if (1 != EVP_EncryptFinal_ex(ctx, (unsigned char*)ciphertext + ciphertext_len, &len)) handle_errors();
    ciphertext_len += len;

    // Free the context
    EVP_CIPHER_CTX_free(ctx);

    return ciphertext_len;
}

int aes_decrypt(void* ciphertext, int ciphertext_len, void* key, void* iv, void* plaintext) {
    EVP_CIPHER_CTX* ctx;

    int len;

    int plaintext_len;

    // Create and initialize the context
    if (!(ctx = EVP_CIPHER_CTX_new())) handle_errors();

    // Initialize decryption
    if (1 != EVP_DecryptInit_ex(ctx, EVP_aes_128_cbc(), NULL, key, iv)) handle_errors();

    // Decrypt
    if (1 != EVP_DecryptUpdate(ctx, plaintext, &len, ciphertext, ciphertext_len)) handle_errors();
    plaintext_len = len;

    // Finish decryption
    if (1 != EVP_DecryptFinal_ex(ctx, (unsigned char*)plaintext + len, &len)) handle_errors();
    plaintext_len += len;

    // Free context
    EVP_CIPHER_CTX_free(ctx);

    return plaintext_len;
}

#if defined(_WIN32)
#pragma warning(default : 4706)
#endif
