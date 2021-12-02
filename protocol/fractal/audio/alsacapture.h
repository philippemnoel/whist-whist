#ifndef ALSACAPTURE_H
#define ALSACAPTURE_H
/**
 * Copyright 2021 Whist Technologies, Inc.
 * @file alsacapture.h
 * @brief This file contains the code to capture audio on Ubuntu using ALSA.
============================
Usage
============================

Audio is captured as a stream. You first need to create an audio device via
CreateAudioDevice. You can then start it via StartAudioDevice and it will
capture all audio data it finds. It captures nothing if there is no audio
playing. You can use GetNextPacket to retrieve the next packet of audio data
from the stream,, and GetBuffer to grab the data. Packets will keep coming
whether you grab them or not. Once you are done, you need to destroy the audio
device via DestroyAudioDevice.
*/

/*
============================
Includes
============================
*/

#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>

#include <fractal/core/whist.h>

/*
============================
Custom Types
============================
*/

/**
 * @brief                          Audio capture device features
 */
typedef struct AudioDevice {
    snd_pcm_t* handle;
    snd_pcm_uframes_t num_frames;
    unsigned long frames_available;
    unsigned long buffer_size;
    unsigned long frame_size;
    unsigned int channels;
    unsigned int sample_rate;
    enum _snd_pcm_format sample_format;  // NOLINT
    uint8_t* buffer;
    int dummy_state;
} AudioDevice;

#endif  // ALSA_CAPTURE_H
