#ifndef CLIENT_VIDEO_H
#define CLIENT_VIDEO_H
/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file video.h
 * @brief This file contains all code that interacts directly with receiving and
 *        processing video packets on the client.
============================
Usage
============================

initVideo() gets called before any video packet can be received. The video
packets are received as standard WhistPackets by ReceiveVideo(WhistPacket*
packet), before being saved in a proper video frame format.
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>
#include <whist/network/network.h>
#include <whist/video/codec/decode.h>
#include <whist/utils/avpacket_buffer.h>

/*
============================
Public Functions
============================
*/

/**
 * @brief                          Create the SDL video thread
 */
void init_video();

/**
 * @brief                          Receive video packet
 *
 * @param packet                   Packet received from the server, which gets
 *                                 stored as video packet with proper parameters
 *
 * @returns                        Return -1 if failed to receive packet into
 *                                 video frame, else 0
 */
int32_t receive_video(WhistPacket* packet);

/**
 * @brief                          Calculate statistics about bitrate, I-Frame,
 *                                 etc. and request video update from the server
 */
void update_video();

/**
 * @brief                          Initialize the video renderer
 */
int init_video_renderer();

/**
 * @brief                          Render the video frame (If any are available to render)
 */
int render_video();

/**
 * @brief                          Free the video thread and VideoContext data to
 *                                 exit
 */
void destroy_video();

/**
 * @brief                          Set the global variable 'resizing' to true if
 *                                 the SDL window is being resized, else false
 *
 * @param is_resizing              Boolean indicating whether or not the SDL
 *                                 window is being resized
 */
void set_video_active_resizing(bool is_resizing);

#endif  // CLIENT_VIDEO_H
