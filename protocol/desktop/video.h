#ifndef DESKTOP_VIDEO_H
#define DESKTOP_VIDEO_H
/**
 * Copyright Fractal Computers, Inc. 2020
 * @file video.h
 * @brief This file contains all code that interacts directly with receiving and
 *        processing video packets on the client.
============================
Usage
============================

initVideo() gets called before any video packet can be received. The video
packets are received as standard FractalPackets by ReceiveVideo(FractalPacket*
packet), before being saved in a proper video frame format.
*/

/*
============================
Includes
============================
*/

#include "../fractal/core/fractal.h"
#include "../fractal/network/network.h"
#include "../fractal/video/videodecode.h"

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
 * @brief                          Free the video thread and VideContext data to
 *                                 exit
 *
 * @param packet                   Packet received from the server, which gets
 *                                 sorted as video packet with proper parameters
 *
 * @returns                        Return -1 if failed to receive packet into
 *                                 video frame, else 0
 */
int32_t receive_video(FractalPacket* packet);

/**
 * @brief                          Calculate statistics about bitrate, I-Frame,
 *                                 etc. and request video update from the server
 */
void update_video();

/**
 * @brief                          Free the video thread and VideContext data to
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

#endif  // DESKTOP_VIDEO_H
