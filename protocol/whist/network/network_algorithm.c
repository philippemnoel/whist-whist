/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file network_algorithm.c
 * @brief This file contains the client's adaptive bitrate code. Any algorithms we are using for
 *        predicting bitrate should be stored here.
============================
Usage
============================
Place to put any predictive/adaptive bitrate algorithms. In the current setup, each algorithm is a
function that takes in inputs through a NetworkStatistics struct. The function is responsible for
maintaining any internal state necessary for the algorithm (possibly through the use of custom types
or helpers), and should update client_max_bitrate when necessary.

To change the algorithm the client uses, set `calculate_new_bitrate` to the desired algorithm.
The function assigned to `calculate_new_bitrate` should have signature such that it takes
one argument of type `NetworkStatistics` and returns type `NetworkSettings`.

For more details about the individual algorithms, please visit
https://www.notion.so/whisthq/Adaptive-Bitrate-Algorithms-a6ea0987adc04e8d84792fc9dcbc7fc7
*/

/*
============================
Includes
============================
*/

#include "network_algorithm.h"

/*
============================
Defines
============================
*/

static NetworkSettings default_network_settings = {
    .bitrate = STARTING_BITRATE,
    .burst_bitrate = STARTING_BURST_BITRATE,
    .desired_codec = CODEC_TYPE_H264,
    .fec_packet_ratio = FEC_PACKET_RATIO,
    .fps = 60,
};

#define BAD_BITRATE 10400000
#define BAD_BURST_BITRATE 31800000

/*
============================
Private Function Declarations
============================
*/

// These functions involve various potential
// implementations of `calculate_new_bitrate`

NetworkSettings fallback_bitrate(NetworkStatistics stats);
NetworkSettings ewma_bitrate(NetworkStatistics stats);
NetworkSettings ewma_ratio_bitrate(NetworkStatistics stats);

/*
============================
Public Function Implementations
============================
*/

NetworkSettings get_desired_network_settings(NetworkStatistics stats) {
    // If there are no statistics stored, just return the default network settings
    if (!stats.statistics_gathered) {
        return default_network_settings;
    }
    NetworkSettings network_settings = ewma_ratio_bitrate(stats);
    network_settings.fps = default_network_settings.fps;
    network_settings.fec_packet_ratio = default_network_settings.fec_packet_ratio;
    network_settings.desired_codec = default_network_settings.desired_codec;
    return network_settings;
}

/*
============================
Private Function Implementations
============================
*/

NetworkSettings fallback_bitrate(NetworkStatistics stats) {
    /*
        Switches between two sets of bitrate/burst bitrate: the default of 16mbps/100mbps and a
        fallback of 10mbps/30mbps. We fall back if we've nacked a lot in the last second.
    */
    static NetworkSettings network_settings;
    if (stats.num_nacks_per_second > 6) {
        network_settings.bitrate = BAD_BITRATE;
        network_settings.burst_bitrate = BAD_BURST_BITRATE;
    } else {
        network_settings.bitrate = STARTING_BITRATE;
        network_settings.burst_bitrate = STARTING_BURST_BITRATE;
    }
    return network_settings;
}

NetworkSettings ewma_bitrate(NetworkStatistics stats) {
    /*
        Keeps an exponentially weighted moving average of the throughput per second the client is
        getting, and uses that to predict a good bitrate to ask the server for.
    */

    // Constants
    const double alpha = 0.8;
    const double bitrate_throughput_ratio = 1.25;

    FATAL_ASSERT(stats.throughput_per_second >= 0);

    // Calculate throughput
    static int throughput = -1;
    if (throughput == -1) {
        throughput = (int)(STARTING_BITRATE / bitrate_throughput_ratio);
    }
    throughput = (int)(alpha * throughput + (1 - alpha) * stats.throughput_per_second);

    // Set network settings
    NetworkSettings network_settings = default_network_settings;
    network_settings.bitrate = (int)(bitrate_throughput_ratio * throughput);
    network_settings.burst_bitrate = STARTING_BURST_BITRATE;
    return network_settings;
}

NetworkSettings ewma_ratio_bitrate(NetworkStatistics stats) {
    /*
        Keeps an exponentially weighted moving average of the throughput per second the client is
        getting, and uses that to predict a good bitrate to ask the server for. The throughput
        per second that the client is getting is estimated by the ratio of successful packets to
        total packets (successful + NACKed) multiplied by the active throughput. Due to the usage
        of a heuristic for finding current throughput, every `same_throughput_count_threshold`
        periods, we boost the throughput estimate if the calculated throughput has remained the
        same for `same_throughput_count_threshold` periods. If the boosted throughput results in
        NACKs, then we revert to the previous throughput and increase the value of
        `same_throughput_count_threshold` so that the successful throughput stays constant
        for longer and longer periods of time before we try higher network_settings. We follow a
       similar logic flow for the burst bitrate, except we use skipped renders instead of NACKs.
    */
    const double alpha = 0.8;
    // Because the max bitrate of the encoder is usually larger than the actual amount of data we
    //     get from the server
    const double bitrate_throughput_ratio = 1.25;
    // Multiplier when boosting throughput/bitrate after continuous success
    const double boost_multiplier = 1.05;

    // Hacky way of allowing constant assignment to static variable (cannot assign `const` to
    //     `static`)
    // In order:
    //     > the minimum setting for threshold for meeting throughput expectations before boosting
    //     > the multiplier for threshold after successfully meeting throughput expectations
    //     > the maximum setting for threshold for meeting throughput expectations before boosting
    enum {
        meet_expectations_min = 5,         // NOLINT(readability-identifier-naming)
        meet_expectations_multiplier = 2,  // NOLINT(readability-identifier-naming)
        meet_expectations_max = 1024       // NOLINT(readability-identifier-naming)
    };

    // Target throughput (used to calculate bitrate)
    static int expected_throughput = -1;
    // How many times in a row that the real throughput has met expectations
    static int met_throughput_expectations_count = 0;
    // The threshold that the current throughput needs to meet to be boosted
    static int meet_throughput_expectations_threshold = meet_expectations_min;
    // The latest throughput that met expectations up to threshold
    static int latest_successful_throughput = -1;
    // The threshold that the latest successful throughput needs to meet to be boosted
    static int latest_successful_throughput_threshold = meet_expectations_min;

    // How many times in a row that the burst has met expectations
    static int met_burst_expectations_count = 0;
    // The threshold that the current burst needs to meet to be boosted
    static int meet_burst_expectations_threshold = meet_expectations_min;
    // The latest burst that met expectations up to threshold
    static int latest_successful_burst = -1;
    // The threshold that the latest successful burst needs to meet to be boosted
    static int latest_successful_burst_threshold = meet_expectations_min;

    static NetworkSettings network_settings;
    if (expected_throughput == -1) {
        expected_throughput = (int)(STARTING_BITRATE / bitrate_throughput_ratio);
        network_settings.burst_bitrate = STARTING_BURST_BITRATE;
    }

    // Make sure throughput is not negative and also that the client has received frames at all
    //     i.e., we don't want to recalculate bitrate if the video is static and server is sending
    //      zero packets
    if (stats.num_received_packets_per_second + stats.num_nacks_per_second > 0) {
        int real_throughput =
            (int)(expected_throughput * (double)stats.num_received_packets_per_second /
                  (stats.num_nacks_per_second + stats.num_received_packets_per_second));

        if (real_throughput == expected_throughput) {
            // If this throughput meets expectations, then increment the met expectation count
            met_throughput_expectations_count++;

            // If we have met expectations for the current threshold number of iterations,
            //     then set the latest successful throughput to the active throughput and boost the
            //     expected throughput to test a higher bitrate.
            if (met_throughput_expectations_count >= meet_throughput_expectations_threshold) {
                latest_successful_throughput = real_throughput;
                met_throughput_expectations_count = 0;
                latest_successful_throughput_threshold = meet_throughput_expectations_threshold;
                meet_throughput_expectations_threshold = meet_expectations_min;
                expected_throughput = (int)(expected_throughput * boost_multiplier);
            }
        } else {
            if (expected_throughput > latest_successful_throughput &&
                latest_successful_throughput != -1) {
                // If the expected throughput that yielded lost packets was higher than the
                //     last continuously successful throughput, then set the expected
                //     throughput to the last continuously successful throughput to try it
                //     again for longer. Increase the threshold for boosting the expected throughput
                //     after continuous success because it is more likely that we have found
                //     our successful throughput for now.
                expected_throughput = latest_successful_throughput;
                latest_successful_throughput_threshold =
                    min(latest_successful_throughput_threshold * meet_expectations_multiplier,
                        meet_expectations_max);
                meet_throughput_expectations_threshold = latest_successful_throughput_threshold;
            } else {
                // If the expected throughput that yielded lost packets was lower than the last
                //     continuously successful throughput, then set the expected throughput to an
                //     EWMA-calculated new value and reset the threshold for boosting
                //     the expected throughput after meeting threshold success.
                expected_throughput =
                    (int)(alpha * expected_throughput + (1 - alpha) * real_throughput);
                meet_throughput_expectations_threshold = meet_expectations_min;
            }

            met_throughput_expectations_count = 0;
        }

        network_settings.bitrate = (int)(bitrate_throughput_ratio * expected_throughput);

        if (network_settings.bitrate > MAXIMUM_BITRATE) {
            network_settings.bitrate = MAXIMUM_BITRATE;
            expected_throughput = (int)(MAXIMUM_BITRATE / bitrate_throughput_ratio);
        } else if (network_settings.bitrate < MINIMUM_BITRATE) {
            network_settings.bitrate = MINIMUM_BITRATE;
            expected_throughput = (int)(MINIMUM_BITRATE / bitrate_throughput_ratio);
        }
    }

    // Make sure that frames have been recieved before trying to update the burst bitrate
    if (stats.num_rendered_frames_per_second > 0) {
        int current_burst_heuristic =
            (int)(network_settings.burst_bitrate * (double)stats.num_rendered_frames_per_second /
                  stats.num_rendered_frames_per_second);

        if (current_burst_heuristic == network_settings.burst_bitrate) {
            // If this burst bitrate is the same as the burst bitrate from the last period, then
            //     increment the same burst bitrate count
            met_burst_expectations_count++;

            // If we have had the same burst bitrate for the current threshold number of iterations,
            //     then set the latest successful burst bitrate to the active burst bitrate and
            //     boost the burst bitrate to test a higher burst bitrate. Reset the threshold for
            //     boosting the burst bitrate after continuous success since we may have entered an
            //     area with higher bandwidth.
            if (met_burst_expectations_count >= meet_burst_expectations_threshold) {
                latest_successful_burst = network_settings.burst_bitrate;
                met_burst_expectations_count = 0;
                latest_successful_burst_threshold = meet_throughput_expectations_threshold;
                meet_burst_expectations_threshold = meet_expectations_min;
                network_settings.burst_bitrate =
                    (int)(network_settings.burst_bitrate * boost_multiplier);
            }
        } else {
            if (network_settings.burst_bitrate > latest_successful_burst &&
                latest_successful_burst != -1) {
                // If the burst bitrate that yielded lost packets was higher than the last
                //     continuously successful burst bitrate, then set the burst bitrate to
                //     the last continuously successful burst bitrate to try it again for longer.
                //     Increase the threshold for boosting the burst bitrate after continuous
                //     success because it is more likely that we have found our successful
                //     burst bitrate for now.
                network_settings.burst_bitrate = latest_successful_burst;
                latest_successful_burst_threshold =
                    min(latest_successful_burst_threshold * meet_expectations_multiplier,
                        meet_expectations_max);
                meet_burst_expectations_threshold = latest_successful_burst_threshold;
            } else {
                // If the burst bitrate that yielded lost packets was lower than the last
                //     continuously successful burst bitrate, then set the burst bitrate to an
                //     EWMA-calculated new value and reset the threshold for boosting
                //     the burst bitrate after continuous success.
                network_settings.burst_bitrate = (int)(alpha * network_settings.burst_bitrate +
                                                       (1 - alpha) * current_burst_heuristic);
                meet_burst_expectations_threshold = meet_expectations_min;
            }

            met_burst_expectations_count = 0;
        }

        if (network_settings.burst_bitrate > STARTING_BURST_BITRATE) {
            network_settings.burst_bitrate = STARTING_BURST_BITRATE;
        } else if (network_settings.burst_bitrate < MINIMUM_BITRATE) {
            network_settings.burst_bitrate = MINIMUM_BITRATE;
        }
    }
    return network_settings;
}
