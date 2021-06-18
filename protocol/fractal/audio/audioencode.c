/**
 * Copyright Fractal Computers, Inc. 2020
 * @file audioencode.c
 * @brief This file contains the code to encode AAC-encoded audio using FFmpeg.
============================
Usage
============================

Audio is encoded to AAC via FFmpeg using a FIFO queue. In order for FFmpeg to
be able to encode an audio frame, it needs to be have a certain duration of
data. This is frequently more than a single packet, which is why we have a FIFO
queue. You can initialize the AAC encoder via create_audio_encoder. You then
receive packets into the FIFO queue, which is a data buffer, via
audio_encoder_fifo_intake. You can then encode via audio_encoder_encode.
*/

#include "audioencode.h"

/*
============================
Public Function Implementations
============================
*/

AudioEncoder* create_audio_encoder(int bit_rate, int sample_rate) {
    /*
        Initialize the FFmpeg AAC audio encoder, and set the proper audio parameters
        for receiving from the server

        Arguments:
            bit_rate (int): The amount of bits/seconds that the audio will be encoded
                to (higher means higher quality encoding, and more bandwidth usage)
            sample_rate (int): The sample rate, in Hertz, of the audio to encode

        Returns:
            (AudioEncoder*): The initialized FFmpeg audio encoder struct
    */

    // initialize the audio encoder
    AudioEncoder* encoder = (AudioEncoder*)safe_malloc(sizeof(AudioEncoder));
    memset(encoder, 0, sizeof(*encoder));

    // setup the AVCodec and AVFormatContext
    // avcodec_register_all is deprecated on FFmpeg 4+
    // only Linux uses FFmpeg 3.4.x because of canonical system packages
#if LIBAVCODEC_VERSION_MAJOR < 58
    avcodec_register_all();
#endif

    // allocate the packet - we will set its fields in avcodec_receive_packet.
    encoder->packet = *av_packet_alloc();

    encoder->pCodec = avcodec_find_encoder_by_name("libfdk_aac");
    if (!encoder->pCodec) {
        LOG_WARNING("AVCodec not found.");
        destroy_audio_encoder(encoder);
        return NULL;
    }
    encoder->pCodecCtx = avcodec_alloc_context3(encoder->pCodec);
    if (!encoder->pCodecCtx) {
        LOG_WARNING("Could not allocate AVCodecContext.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    encoder->pCodecCtx->codec_id = AV_CODEC_ID_AAC;
    encoder->pCodecCtx->codec_type = AVMEDIA_TYPE_AUDIO;
    encoder->pCodecCtx->sample_fmt = encoder->pCodec->sample_fmts[0];
    encoder->pCodecCtx->sample_rate = sample_rate;
    encoder->pCodecCtx->channel_layout = AV_CH_LAYOUT_STEREO;
    encoder->pCodecCtx->channels =
        av_get_channel_layout_nb_channels(encoder->pCodecCtx->channel_layout);
    encoder->pCodecCtx->bit_rate = bit_rate;

    if (avcodec_open2(encoder->pCodecCtx, encoder->pCodec, NULL) < 0) {
        LOG_WARNING("Could not open AVCodec.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    // setup the AVFrame

    encoder->pFrame = av_frame_alloc();
    encoder->pFrame->nb_samples = encoder->pCodecCtx->frame_size;
    encoder->pFrame->format = encoder->pCodecCtx->sample_fmt;
    encoder->pFrame->channel_layout = AV_CH_LAYOUT_STEREO;

    encoder->frame_count = 0;

    // initialize the AVFrame buffer

    if (av_frame_get_buffer(encoder->pFrame, 0)) {
        LOG_WARNING("Could not initialize AVFrame buffer.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    // initialize the AVAudioFifo as an empty FIFO

    encoder->pFifo =
        av_audio_fifo_alloc(encoder->pFrame->format,
                            av_get_channel_layout_nb_channels(encoder->pFrame->channel_layout), 1);
    if (!encoder->pFifo) {
        LOG_WARNING("Could not allocate AVAudioFifo.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    // setup the SwrContext for resampling

    encoder->pSwrContext =
        swr_alloc_set_opts(NULL, encoder->pFrame->channel_layout, encoder->pFrame->format,
                           encoder->pCodecCtx->sample_rate,
                           AV_CH_LAYOUT_STEREO,  // should get layout from WASAPI
                           AV_SAMPLE_FMT_FLT,    // should get format from WASAPI
                           sample_rate,  // should use same sample rate as WASAPI, though this just
                           0, NULL);     //       might not work if not same sample size throughout
    if (!encoder->pSwrContext) {
        LOG_WARNING("Could not initialize SwrContext.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    if (swr_init(encoder->pSwrContext)) {
        LOG_WARNING("Could not open SwrContext.");
        destroy_audio_encoder(encoder);
        return NULL;
    }

    // everything set up, so return the encoder

    return encoder;
}

void audio_encoder_fifo_intake(AudioEncoder* encoder, uint8_t* data, int len) {
    /*
        Feeds raw audio data to the FIFO queue, which is pulled from by the encoder
        to encode AAC frames

        Arguments:
            encoder (AudioEncoder*): The audio encoder struct used to AAC encode a frame
            data (uint8_t*): Buffer of the audio data to intake in the encoder FIFO
                queue to encode
            len (int): Length of the buffer of data to intake
    */

    // initialize
    uint8_t** converted_data = calloc(
        av_get_channel_layout_nb_channels(encoder->pFrame->channel_layout), sizeof(uint8_t*));
    if (!converted_data) {
        LOG_WARNING("Could not allocate converted samples channel pointers.");
        return;
    }

    if (av_samples_alloc(converted_data, NULL,
                         av_get_channel_layout_nb_channels(encoder->pFrame->channel_layout), len,
                         encoder->pFrame->format, 0) < 0) {
        LOG_WARNING("Could not allocate converted samples channel arrays.");
        return;
    }

    // convert
    if (swr_convert(encoder->pSwrContext, converted_data, len, (const uint8_t**)&data, len) < 0) {
        LOG_WARNING("Could not convert samples to intake format.");
        return;
    }

    // reallocate fifo
    if (av_audio_fifo_realloc(encoder->pFifo, av_audio_fifo_size(encoder->pFifo) + len) < 0) {
        LOG_WARNING("Could not reallocate AVAudioFifo.");
        return;
    }

    // add
    if (av_audio_fifo_write(encoder->pFifo, (void**)converted_data, len) < len) {
        LOG_WARNING("Could not write all the requested data to the AVAudioFifo.");
        return;
    }

    // cleanup
    if (converted_data) {
        av_freep(&converted_data[0]);
        free(converted_data);
    }
}

int audio_encoder_encode_frame(AudioEncoder* encoder) {
    /*
        Encodes a single AVFrame of audio from the FIFO data to AAC format

        Arguments:
            encoder (AudioEncoder*): The audio encoder struct used to AAC encode a frame

        Returns:
            (int): 0 if success, else -1
    */

    // read from FIFO to AVFrame
    const int len = FFMIN(av_audio_fifo_size(encoder->pFifo), encoder->pCodecCtx->frame_size);

    if (av_audio_fifo_read(encoder->pFifo, (void**)encoder->pFrame->data, len) < len) {
        LOG_WARNING("Could not read all the requested data from the AVAudioFifo.");
        return -1;
    }

    // set frame timestamp
    encoder->pFrame->pts = encoder->frame_count;

    // send frame for encoding
    int res = avcodec_send_frame(encoder->pCodecCtx, encoder->pFrame);
    if (res == AVERROR_EOF) {
        // end of file
        return -1;
    } else if (res < 0) {
        // real error
        LOG_ERROR("Could not send audio AVFrame for encoding: error '%s'.", av_err2str(res));
        return -1;
    }

    // get encoded packet
    // because this always calls av_packet_unref before doing anything,
    // our previous calls to av_packet_unref are unnecessary.
    res = avcodec_receive_packet(encoder->pCodecCtx, &encoder->packet);
    if (res == AVERROR(EAGAIN) || res == AVERROR_EOF) {
        // encoder needs more data or there's nothing left
        av_packet_unref(&encoder->packet);
        return 1;
    } else if (res < 0) {
        // real error
        LOG_ERROR("Could not encode audio frame: error '%s'.\n", av_err2str(res));
        return -1;
    } else {
        // we did it!
        encoder->frame_count += encoder->pFrame->nb_samples;

        encoder->encoded_frame_size = encoder->packet.size;
        encoder->encoded_frame_data = encoder->packet.data;
        return 0;
    }
}

void destroy_audio_encoder(AudioEncoder* encoder) {
    /*
        Destroys and frees the FFmpeg audio encoder

        Arguments:
            encoder (AudioEncoder*): The audio encoder struct to destroy and free
                the memory of
    */

    if (encoder == NULL) {
        LOG_ERROR("Cannot destroy null encoder.\n");
        return;
    }

    // free the ffmpeg contexts
    avcodec_free_context(&encoder->pCodecCtx);

    // free the encoder context and frame
    av_frame_free(&encoder->pFrame);
    LOG_INFO("av_freed frame\n");
    av_free(encoder->pFrame);

    // free swr
    swr_free(&encoder->pSwrContext);
    LOG_INFO("freed swr\n");
    // free the buffer and decoder
    free(encoder);
    LOG_INFO("done destroying decoder!\n");
}
