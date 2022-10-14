#pragma once

#include <optional>
#include "api/units/timestamp.h"
#include "api/units/data_rate.h"
struct CCSharedState
{

    const double k_smaller_clamp_min= 1.f;
    const double k_clamp_min= 6.f;
    const double k_startup_duration=6;
    const double k_increase_ratio=0.12;

    static constexpr double loss_hold_threshold=0.08;
    static constexpr double loss_decrease_threshold=0.10;

    webrtc::Timestamp current_time = webrtc::Timestamp::MinusInfinity();


    bool in_slow_increase= false;

    webrtc::DataRate max_bitrate= webrtc::DataRate::MinusInfinity();
    double current_bitrate_ratio=1;

    std::optional<webrtc::DataRate> ack_bitrate;
    webrtc::Timestamp first_process_time=webrtc::Timestamp::MinusInfinity();

    // count how many samples we have in the estimator
    int est_cnt_=0;
    webrtc::Timestamp last_est_time = webrtc::Timestamp::MinusInfinity();

    double loss_ratio=0;

};

extern CCSharedState cc_shared_state;
