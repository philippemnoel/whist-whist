/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file renderer.c
 * @brief This file contains the video/audio renderer
 */

/*
============================
Includes
============================
*/

#include "renderer.h"
#include "client_utils.h"
#include "client_statistic.h"
#include "sdl_event_handler.h"

/*
============================
Defines
============================
*/

#define LOG_RENDERER_THREAD_USAGE false

/*
============================
Private Function Declarations
============================
*/

/**
 * @brief                          Renders audio/video on a separate thread,
 *                                 if necessary
 *
 * @note                           This will be used if `renderer_try_render`
 *                                 doesn't get called often enough otherwise.
 */
int32_t multithreaded_renderer(void* opaque);

/*
============================
Public Function Implementations
============================
*/

WhistRenderer* init_renderer() {
    WhistRenderer* whist_renderer = (WhistRenderer*)safe_malloc(sizeof(WhistRenderer));
    memset(whist_renderer, 0, sizeof(WhistRenderer));

    // Initialize audio and video systems
    whist_renderer->audio_context = init_audio();
    whist_renderer->video_context = init_video();

    // These mutex/sem/timer pass work to the renderer thread when necessary
    start_timer(&whist_renderer->last_try_render_timer);
    whist_renderer->renderer_mutex = whist_create_mutex();
    whist_renderer->renderer_semaphore = whist_create_semaphore(0);
    // Used to log renderer thread usage
    whist_renderer->using_renderer_thread = false;
    whist_renderer->render_is_on_renderer_thread = false;

    // We use the renderer thread to do work,
    // If someone else hasn't called try_render recently
    whist_renderer->run_renderer_thread = true;
    whist_renderer->renderer_thread =
        whist_create_thread(multithreaded_renderer, "multithreaded_renderer", whist_renderer);

    // Return the struct
    return whist_renderer;
}

void renderer_receive_packet(WhistRenderer* whist_renderer, WhistPacket* packet) {
    clock statistics_timer;

    // Pass the receive packet into the video or audio context
    switch (packet->type) {
        case PACKET_AUDIO: {
            TIME_RUN(receive_audio(whist_renderer->audio_context, packet), AUDIO_RECEIVE_TIME,
                     statistics_timer);
            break;
        }
        case PACKET_VIDEO: {
            TIME_RUN(receive_video(whist_renderer->video_context, packet), VIDEO_RECEIVE_TIME,
                     statistics_timer);
            break;
        }
        default: {
            LOG_FATAL("Unknown packet type! %d", packet->type);
        }
    }
}

void renderer_update(WhistRenderer* whist_renderer) {
    clock statistics_timer;

    // Update video/audio
    TIME_RUN(update_audio(whist_renderer->audio_context), AUDIO_UPDATE_TIME, statistics_timer);
    TIME_RUN(update_video(whist_renderer->video_context), VIDEO_UPDATE_TIME, statistics_timer);

    // If it's been 2 ms since the last time someone else called try_render,
    // let's ping our renderer thread to do the work instead

    // IF try_lock confirms we aren't rendering right now... [While also protecting
    // `last_try_render_timer`]
    if (whist_try_lock_mutex(whist_renderer->renderer_mutex) == 0) {
        // AND the timer confirms we haven't rendered recently... [Last ~2ms]
        double last_try_render_time_ms =
            get_timer(whist_renderer->last_try_render_timer) * MS_IN_SECOND;
        if (last_try_render_time_ms > 2.0) {
            // Then..., we hit the semaphore so the renderer thread can do the rendering
            whist_post_semaphore(whist_renderer->renderer_semaphore);
        }

        // Unlock the mutex
        whist_unlock_mutex(whist_renderer->renderer_mutex);
    }
}

void renderer_try_render(WhistRenderer* whist_renderer) {
    // Use a mutex to prevent multiple threads from rendering at once
    whist_lock_mutex(whist_renderer->renderer_mutex);

#if LOG_RENDERER_THREAD_USAGE
    // Log render thread usage
    if (!whist_renderer->using_renderer_thread && whist_renderer->render_is_on_renderer_thread) {
        LOG_INFO(
            "try_render has not been called externally recently, "
            "so defaulting to renderer thread usage now!");
        whist_renderer->using_renderer_thread = true;
    }
    if (whist_renderer->using_renderer_thread && !whist_renderer->render_is_on_renderer_thread) {
        LOG_INFO(
            "try_render has been called from somewhere else! "
            "renderer thread will no longer be used now.");
        whist_renderer->using_renderer_thread = false;
    }
#endif

    // If the audio device is pending an update,
    // refresh the audio device, but only if we are in a connected state,
    // otherwise the audio device will crash
    if (sdl_pending_audio_device_update() && connected) {
        refresh_audio_device(whist_renderer->audio_context);
    }

    // Render out any pending audio or video
    render_audio(whist_renderer->audio_context);
    render_video(whist_renderer->video_context);

    // Mark as recently rendered, and unlock
    start_timer(&whist_renderer->last_try_render_timer);
    whist_unlock_mutex(whist_renderer->renderer_mutex);
}

void destroy_renderer(WhistRenderer* whist_renderer) {
    // Wait to close the renderer thread
    whist_renderer->run_renderer_thread = false;
    whist_post_semaphore(whist_renderer->renderer_semaphore);
    whist_wait_thread(whist_renderer->renderer_thread, NULL);

    // Destroy the audio/video context
    destroy_audio(whist_renderer->audio_context);
    destroy_video(whist_renderer->video_context);

    // Free the whist renderer struct
    whist_destroy_semaphore(whist_renderer->renderer_semaphore);
    whist_destroy_mutex(whist_renderer->renderer_mutex);
    free(whist_renderer);
}

/*
============================
Private Function Implementations
============================
*/

int32_t multithreaded_renderer(void* opaque) {
    WhistRenderer* whist_renderer = (WhistRenderer*)opaque;

    while (true) {
        // Wait until we're told to render on this thread
        whist_wait_semaphore(whist_renderer->renderer_semaphore);

        // If this thread should no longer exist, exit accordingly
        if (!whist_renderer->run_renderer_thread) {
            break;
        }

        // Otherwise, try to render
        whist_renderer->render_is_on_renderer_thread = true;
        renderer_try_render(whist_renderer);
        whist_renderer->render_is_on_renderer_thread = false;
    }

    return 0;
}
