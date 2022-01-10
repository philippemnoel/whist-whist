#ifndef CLIENT_BITRATE_H
#define CLIENT_BITRATE_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file network_algorithm.h
 * @brief     This file contains the client's network algorithm code.
 *            It takes in any network statistics,
 *            and returns a network settings decision based on those statistics.
============================
Usage
============================
The client should periodically call calculate_new_bitrate with any new network statistics, such as
throughput or number of nacks. This will return a new network settings as needed
*/

/*
============================
Includes
============================
*/

#include <whist/core/whist.h>

/*
============================
Defines
============================
*/

typedef struct {
    int num_nacks_per_second;
    int num_received_packets_per_second;
    int num_skipped_frames_per_second;
    int num_rendered_frames_per_second;
    int throughput_per_second;
    // True so that a NetworkStatistics struct can be marked as valid/invalid
    bool statistics_gathered;
} NetworkStatistics;

/*
============================
Public Functions
============================
*/

/**
 * @brief               This function will calculate what network settings
 *                      are desired, in response to any network statistics given
 *
 * @param stats         A struct containing any information we might need
 *                      when deciding what bitrate we want
 *
 * @returns             A network settings struct
 */
NetworkSettings get_desired_network_settings(NetworkStatistics stats);

#endif
