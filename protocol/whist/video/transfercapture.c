#include "transfercapture.h"
#include "capture/capture.h"

int transfer_capture(CaptureDevice* device, VideoEncoder* encoder, bool* force_iframe) {
    CaptureDeviceInfos* infos = &device->infos;
    CaptureEncoderHints hints;
    CUcontext cuda_context;

    if (infos->width != encoder->in_width || infos->height != encoder->in_height) {
        LOG_ERROR(
            "Tried to pass in a captured frame of dimension %dx%d, "
            "into an encoder that accepts %dx%d as input",
            infos->width, infos->height, encoder->in_width, encoder->in_height);
        return -1;
    }
    if (transfer_screen(device, &hints)) {
        LOG_ERROR("Unable to transfer screen to CPU buffer.");
        return -1;
    }

    cuda_context = hints.cuda_context;
    if (!cuda_context) cuda_context = *get_video_thread_cuda_context_ptr();

    void* frame_data = NULL;
    int frame_stride = 0;

    if (capture_get_data(device, &frame_data, &frame_stride) < 0) {
        LOG_ERROR("Unable to retrieve capture device data");
    }

#if OS_IS(OS_LINUX)
    // Handle transfer capture for nvidia capture device
    // check if we need to switch our active encoder
    if (encoder->active_encoder == NVIDIA_ENCODER) {
        if (USING_NVIDIA_CAPTURE) {
            // We need to account for switching contexts
            NvidiaEncoder* old_encoder = encoder->nvidia_encoders[encoder->active_encoder_idx];

            if (old_encoder->cuda_context != cuda_context) {
                LOG_INFO("Switching to other Nvidia encoder (cuda=%p)!", cuda_context);
                encoder->active_encoder_idx = (encoder->active_encoder_idx + 1) % 2;

                if (encoder->nvidia_encoders[encoder->active_encoder_idx] == NULL) {
                    encoder->nvidia_encoders[encoder->active_encoder_idx] = create_nvidia_encoder(
                        old_encoder->bitrate, old_encoder->codec_type, old_encoder->width,
                        old_encoder->height, old_encoder->vbv_size, cuda_context);
                }
                *force_iframe = true;
            }
        }

        // capture_get_data
        RegisteredResource resource_to_register = {0};
        resource_to_register.width = infos->width;
        resource_to_register.height = infos->height;
        resource_to_register.pitch = infos->pitch;
        resource_to_register.device_type = device->infos.last_capture_device;
        resource_to_register.texture_pointer = frame_data;
        return nvidia_encoder_frame_intake(encoder->nvidia_encoders[encoder->active_encoder_idx],
                                           resource_to_register);
    }
#endif  // linux

    if (encoder->active_encoder != FFMPEG_ENCODER) {
        LOG_INFO("Switching back to FFmpeg encoder for use with X11 capture!");
        encoder->active_encoder = FFMPEG_ENCODER;
        *force_iframe = true;
    }

    // CPU transfer, if hardware transfer doesn't work
    static int times_measured = 0;
    static double time_spent = 0.0;

    WhistTimer cpu_transfer_timer;
    start_timer(&cpu_transfer_timer);

    if (ffmpeg_encoder_frame_intake(encoder->ffmpeg_encoder, frame_data, frame_stride)) {
        LOG_ERROR("Unable to load data to AVFrame");
        return -1;
    }

    times_measured++;
    time_spent += get_timer(&cpu_transfer_timer);

    if (times_measured == 10) {
        LOG_INFO("Average time transferring frame from capture to encoder frame on CPU: %f",
                 time_spent / times_measured);
        times_measured = 0;
        time_spent = 0.0;
    }

    return 0;
}
