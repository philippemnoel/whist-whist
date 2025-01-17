#include <whist/core/whist.h>
#include <mutex>
#include <thread>
extern "C" {
#include "common.h"
// This is a crutch. Once video is callback-ized we won't need it anymore.
#define FROM_WHIST_PROTOCOL true
#include "interface.h"
#include <whist/utils/queue.h>
#include <whist/network/network_algorithm.h>
#include "../frontend.h"
}

// Just chosen a very large number for events queue size. If required we can optimize/reduce it.
#define MAX_EVENTS_QUEUED 10000

static AVFrame* pending = NULL;
static std::atomic<bool> protocol_alive = false;
static std::atomic<FrontendResizeEvent> latest_resize;

// Whist window management
struct WhistWindowInformation {
    void* ctx;
    OnCursorChangeCallback on_cursor_change_callback_ptr;
    VideoFrameCallback video_frame_callback_ptr;
    OnFileUploadCallback on_file_upload_callback_ptr;
    OnWhistLog on_whist_log_callback_ptr;
    bool playing;
};
static std::mutex whist_window_mutex;
static std::map<int, WhistWindowInformation> whist_windows;
static unsigned int spotlight_expected_id = 0;

static void on_cursor_change_handler(void* ptr, const char* cursor_type, bool relative_mouse_mode) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    // For each window,
    for (auto const& [window_id, window_info] : whist_windows) {
        // If it has a cursor change callback,
        if (window_info.on_cursor_change_callback_ptr) {
            // Feed it the cursor change
            window_info.on_cursor_change_callback_ptr(window_info.ctx, cursor_type,
                                                      relative_mouse_mode);
        }
    }
}

extern "C" {
QueueContext* events_queue = NULL;

void* callback_context = NULL;
void* gpu_context = NULL;
OnCursorChangeCallback on_cursor_change = on_cursor_change_handler;
OnFileUploadCallback on_file_upload = NULL;
OnFileDownloadStart on_file_download_start = NULL;
OnFileDownloadUpdate on_file_download_update = NULL;
OnFileDownloadComplete on_file_download_complete = NULL;
OnNotificationCallback on_notification_callback_ptr = NULL;
GetModifierKeyState get_modifier_key_state = NULL;
OnWhistError on_whist_error = NULL;
OnGpuCommandCallback on_gpu_command = NULL;
}

static WhistSemaphore connection_semaphore = whist_create_semaphore(0);
static std::thread whist_main_thread;

static int vi_api_initialize(int argc, const char* argv[]) {
    // Create variables, if not already existant
    if (events_queue == NULL) {
        events_queue = fifo_queue_create(sizeof(WhistFrontendEvent), MAX_EVENTS_QUEUED);
    }
    // Set the log callback
    whist_log_set_external_logger_callback(+[](unsigned int level, const char* line) -> void {
        std::lock_guard<std::mutex> guard(whist_window_mutex);
        // Go through the windows,
        for (auto const& [window_id, window_info] : whist_windows) {
            // If one of them accepts a log callback,
            if (window_info.on_whist_log_callback_ptr) {
                // Give the log to that one
                window_info.on_whist_log_callback_ptr(window_info.ctx, level, line);
                // And only one of them
                break;
            }
        }
    });
    // Main whist loop
    whist_main_thread = std::thread([=]() -> void {
        while (true) {
            whist_wait_semaphore(connection_semaphore);
            // If the semaphore was hit with protocol marked as dead, exit
            if (protocol_alive == false) {
                break;
            }
            // Start the protocol if valid arguments were given
            if (argc > 0 && argv != NULL) {
                int ret = whist_client_main(argc, argv);
                UNUSED(ret);
            }
            // Mark the protocol as dead when main exits
            protocol_alive = false;
        }
    });
    return 0;
}

static void vi_api_destroy() {
    // Set the protocol to not alive
    protocol_alive = false;
    // Kill the whist main thread by hitting the semaphore while protocol is marked-as-dead
    whist_post_semaphore(connection_semaphore);
    whist_main_thread.join();
    fifo_queue_destroy(events_queue);
    // TODO: Are there other resources to clean up here?
}

static bool vi_api_connect() {
    // Mark the protocol as alive
    bool protocol_was_alive = protocol_alive.exchange(true);
    // But if the protocol wasn't just alive, hit the semaphore to start it
    if (protocol_was_alive == false) {
        // Drain the event queue
        WhistFrontendEvent event;
        while (fifo_queue_dequeue_item(events_queue, &event) == 0)
            ;
        // Remember to reset pertinent global state.
        latest_resize = FrontendResizeEvent();
        // Hit the semaphore to start the protocol again
        whist_post_semaphore(connection_semaphore);
        return true;
    } else {
        // Do nothing, the protocol is already alive
        return false;
    }
}

static bool vi_api_is_connected() { return protocol_alive; }

static void vi_api_disconnect() {
    // TODO: Actually force a disconnect when this happens
    LOG_ERROR("Forceful disconnect not implemented yet!");
    protocol_alive = false;
}

static void vi_api_set_on_cursor_change_callback(int window_id, OnCursorChangeCallback cb) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    whist_windows[window_id].on_cursor_change_callback_ptr = cb;
}

static void vi_api_set_on_notification_callback(OnNotificationCallback cb) {
    on_notification_callback_ptr = cb;
}

static void* vi_api_get_frame_ref() {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    // Consume the pending AVFrame, and return it
    void* frame_ref = pending;
    pending = NULL;
    return frame_ref;
}

static void* vi_api_get_handle_from_frame_ref(void* frame_ref) {
    AVFrame* frame = (AVFrame*)frame_ref;
    // Assuming we want CVPixelBufferRef for now
    return frame->data[3];
}

static void vi_api_get_frame_ref_yuv_data(void* frame_ref, uint8_t*** data, int** linesize,
                                          int* width, int* height, int* visible_width,
                                          int* visible_height) {
    AVFrame* frame = (AVFrame*)frame_ref;
    *data = frame->data;
    *linesize = frame->linesize;
    *width = frame->width;
    *height = frame->height;
    // If video width is rounded to the nearest even number, then crop the last pixel
    FrontendResizeEvent cached = latest_resize.load();
    if (frame->width - cached.width == 1) {
        *visible_width = cached.width;
    } else {
        *visible_width = frame->width;
    }
    // If video height is rounded to the nearest even number, then crop the last pixel
    if (frame->height - cached.height == 1) {
        *visible_height = cached.height;
    } else {
        *visible_height = frame->height;
    }
}

static void vi_api_free_frame_ref(void* frame_ref) {
    AVFrame* frame = (AVFrame*)frame_ref;
    av_frame_free(&frame);
}

static unsigned int vi_api_freeze_all_windows() {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    spotlight_expected_id++;
    for (auto& [window_id, window_info] : whist_windows) {
        window_info.playing = false;
    }
    return spotlight_expected_id;
}

static void vi_api_set_video_spotlight(int window_id, unsigned int spotlight_id) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    if (spotlight_id != spotlight_expected_id) return;
    whist_windows[window_id].playing = true;
}

void virtual_interface_send_frame(AVFrame* frame) {
    if (!protocol_alive) return;
    std::lock_guard<std::mutex> guard(whist_window_mutex);

    // Update the pending frame
    av_frame_free(&pending);
    pending = av_frame_clone(frame);

    for (auto const& [window_id, window_info] : whist_windows) {
        if (window_info.playing && window_info.video_frame_callback_ptr) {
            window_info.video_frame_callback_ptr(window_id, av_frame_clone(frame));
        }
    }
}

static void vi_api_set_video_frame_callback(int window_id, VideoFrameCallback callback_ptr) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    // If there's a pending avframe, and that window hasn't been capturing yet,
    // pass the existant frame into the callback ptr
    //
    // When sending the initial frame, only do so if thawed. This line of code can be changed
    // to send, e.g. a black frame or something if desired. Or later, potentially the most
    // recent frame of that tab, for example.
    if (pending != NULL && whist_windows[window_id].video_frame_callback_ptr == NULL &&
        whist_windows[window_id].playing) {
        AVFrame* frame_ref = av_frame_clone(pending);
        callback_ptr(window_id, frame_ref);
    }
    // Store the callback ptr for future frames
    whist_windows[window_id].video_frame_callback_ptr = callback_ptr;
}

static void vi_api_send_event(const WhistFrontendEvent* frontend_event) {
    if (frontend_event->type == FRONTEND_EVENT_RESIZE) {
        // Don't emit duplicate resize events
        // Note: We should refactor to remove duplication between here and
        // impl.c. Currently, there is a synchronization issue if send_event
        // is called from multiple threads -- there's no guarantee that the
        // order of the caches will match the order of the enqueues!
        FrontendResizeEvent cached = latest_resize.load();
        if (cached.width == frontend_event->resize.width &&
            cached.height == frontend_event->resize.height &&
            cached.dpi == frontend_event->resize.dpi) {
            return;
        }
        latest_resize = frontend_event->resize;
    }
    if (fifo_queue_enqueue_item(events_queue, frontend_event) != 0) {
        LOG_ERROR("Virtual event queuing failed");
    }
}

static void vi_api_set_on_file_upload_callback(int window_id, OnFileUploadCallback cb) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    // Update that whist window's file upload callback
    whist_windows[window_id].on_file_upload_callback_ptr = cb;
    // Set globals
    callback_context = whist_windows[window_id].ctx;
    on_file_upload = cb;
}

static void vi_api_set_on_file_download_start_callback(OnFileDownloadStart cb) {
    on_file_download_start = cb;
}

static void vi_api_set_on_file_download_update_callback(OnFileDownloadUpdate cb) {
    on_file_download_update = cb;
}

static void vi_api_set_on_file_download_complete_callback(OnFileDownloadComplete cb) {
    on_file_download_complete = cb;
}

static void vi_api_set_get_modifier_key_state(GetModifierKeyState cb) {
    get_modifier_key_state = cb;
}

static void vi_api_set_on_whist_error_callback(OnWhistError cb) { on_whist_error = cb; }

static void vi_api_set_on_whist_log_callback(int window_id, OnWhistLog cb) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    whist_windows[window_id].on_whist_log_callback_ptr = cb;
}

static int vi_api_create_window() {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    // Use serial window IDs, so that each window gets a unique ID
    static int serial_window_ids = 1;
    int next_window_id = serial_window_ids++;
    // Store window info, and return the new window ID
    whist_windows[next_window_id] = {};
    return next_window_id;
}

static void vi_api_register_context(int window_id, void* ctx) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    whist_windows[window_id].ctx = ctx;
}

static void vi_api_destroy_window(int window_id) {
    std::lock_guard<std::mutex> guard(whist_window_mutex);
    whist_windows.erase(window_id);
    if (whist_windows.size() == 0) {
        callback_context = NULL;
        on_file_upload = NULL;
    } else {
        callback_context = whist_windows.begin()->second.ctx;
        on_file_upload = whist_windows.begin()->second.on_file_upload_callback_ptr;
    }
}

static void* virtual_malloc(size_t size) { return safe_malloc(size); }

static void virtual_free(void* ptr) { free(ptr); }

static void vi_api_set_gpu_command_callback(void* opaque, OnGpuCommandCallback cb) {
    gpu_context = opaque;
    on_gpu_command = cb;
}

static const VirtualInterface vi = {
    .lifecycle =
        {
            .initialize = vi_api_initialize,
            .destroy = vi_api_destroy,

            .connect = vi_api_connect,
            .is_connected = vi_api_is_connected,
            .disconnect = vi_api_disconnect,

            .create_window = vi_api_create_window,
            .register_context = vi_api_register_context,
            .destroy_window = vi_api_destroy_window,
        },
    .video =
        {
            .get_frame_ref = vi_api_get_frame_ref,
            .get_handle_from_frame_ref = vi_api_get_handle_from_frame_ref,
            .get_frame_ref_yuv_data = vi_api_get_frame_ref_yuv_data,
            .free_frame_ref = vi_api_free_frame_ref,
            .set_on_cursor_change_callback = vi_api_set_on_cursor_change_callback,
            .set_video_frame_callback = vi_api_set_video_frame_callback,
            .freeze_all_windows = vi_api_freeze_all_windows,
            .set_video_spotlight = vi_api_set_video_spotlight,
        },
    .events =
        {
            .send = vi_api_send_event,
            .set_get_modifier_key_state = vi_api_set_get_modifier_key_state,
            .set_on_whist_error_callback = vi_api_set_on_whist_error_callback,
            .set_on_whist_log_callback = vi_api_set_on_whist_log_callback,
        },
    .file =
        {
            .set_on_file_upload_callback = vi_api_set_on_file_upload_callback,
            .set_on_file_download_start_callback = vi_api_set_on_file_download_start_callback,
            .set_on_file_download_update_callback = vi_api_set_on_file_download_update_callback,
            .set_on_file_download_complete_callback = vi_api_set_on_file_download_complete_callback,
            .set_on_notification_callback = vi_api_set_on_notification_callback,
        },
    .utils =
        {
            .malloc = virtual_malloc,
            .free = virtual_free,
        },
    .gpu =
        {
            .set_on_gpu_command_callback = vi_api_set_gpu_command_callback,
        },
};

const VirtualInterface* get_virtual_interface() { return &vi; }
