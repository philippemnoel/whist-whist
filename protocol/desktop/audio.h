#ifndef DESKTOP_AUDIO_H
#define DESKTOP_AUDIO_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file audio.h
 * @brief This file contains all code that interacts directly with processing
 *        audio packets on the client.
============================
Usage
============================

initAudio() must be called first before receiving any audio packets.
updateAudio() gets called immediately after to update the client to the server's
audio format.
*/

/*
============================
Includes
============================
*/

#include <fractal/audio/audiodecode.h>
#include <fractal/core/fractal.h>
#include <fractal/network/network.h>

/*
============================
Public Functions
============================
*/

/**
 * @brief                          This will initialize the FFmpeg AAC audio
 *                                 decoder, and set the proper audio parameters
 *                                 for receiving from the server
 */
void init_audio();

/**
 * @brief                          Receives a FractalPacket into an audio
 *                                 packet, and check if NACKing is necessary
 *
 * @param packet                   Packet of data that gets received over a
 *                                 SocketContext
 *
 * @returns                        Returns -1 if received an incorrect packet,
 *                                 else 0
 */
int32_t receive_audio(FractalPacket* packet);

/**
 * @brief                          Update the audio parameters to new parameters
 *                                 sent from server, if any, by reinitializing,
 *                                 and catch up if needed
 */
void update_audio();

/**
 * @brief                          This will play any queued audio packets
 *                                 NOTE: Is thread-safe, and can be called
 *                                 no matter what other audio calls are being made
 */
void render_audio();

/**
 * @brief                          Close the SDL audio and the FFmpeg audio
 *                                 decoder
 */
void destroy_audio();

#endif  // DESKTOP_AUDIO_H
