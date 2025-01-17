#ifndef NVIDIA_ENCODE_H
#define NVIDIA_ENCODE_H

#include "../nvidia-linux/NvFBCUtils.h"
#include "../nvidia-linux/nvEncodeAPI.h"
#include <whist/core/whist.h>
#include "whist/video/ltr.h"
#include "../cudacontext.h"

#define RESOURCE_CACHE_SIZE 4

typedef struct {
    NV_ENC_REGISTERED_PTR handle;
    CaptureDeviceType device_type;
    int width;
    int height;
    int pitch;
    void* texture_pointer;
} RegisteredResource;

typedef struct {
    NV_ENCODE_API_FUNCTION_LIST p_enc_fn;
    void* internal_nvidia_encoder;
    NV_ENC_INITIALIZE_PARAMS encoder_params;

    RegisteredResource resource_cache[RESOURCE_CACHE_SIZE];
    RegisteredResource registered_resource;

    NV_ENC_OUTPUT_PTR output_buffer;
    NV_ENC_BUFFER_FORMAT buffer_fmt;
    CUcontext cuda_context;
    CodecType codec_type;
    int bitrate;
    int vbv_size;
    uint32_t frame_idx;
    int width;
    int height;
    int pitch;
    bool wants_iframe;
    LTRAction ltr_action;
    // Output
    void* frame;
    unsigned int frame_size;
    VideoFrameType frame_type;
} NvidiaEncoder;

/**
 * @brief                          Will create a new nvidia encoder
 *
 * @param bitrate                  The bitrate to encode at (In bits per second)
 * @param codec                    Which codec type (h264 or h265) to use
 * @param out_width                Width of the output frame
 * @param out_height               Height of the output frame
 * @param vbv_size                 VBV size in bits
 * @param cuda_context             The CUDA context to use for the encoding session
 *
 * @returns                        The newly created nvidia encoder
 */
NvidiaEncoder* create_nvidia_encoder(int bitrate, CodecType codec, int out_width, int out_height,
                                     int vbv_size, CUcontext cuda_context);

/**
 * @brief                          Will reconfigure an nvidia encoder
 *
 * @param encoder                  The nvidia encoder to reconfigure
 * @param bitrate                  The new bitrate
 * @param vbv_size                 The new VBV size in bits
 * @param codec                    The new codec
 * @param out_width                The new output width
 * @param out_height               The new output height
 *
 * @returns                        true on success, false on failure
 */
bool nvidia_reconfigure_encoder(NvidiaEncoder* encoder, int out_width, int out_height, int bitrate,
                                int vbv_size, CodecType codec);

/**
 * @brief                          Put the input data into the nvidia encoder
 *
 * @param encoder                  The encoder to encode with
 * @param resource_to_register     The resource to register - contains data about capture device,
 *                                 width, height, and texture
 *
 * @returns                        0 on success, else -1
 *                                 This function will return -1 if width/height do not match
 *                                 out_width/out_height, as the nvidia encoder does not support
 *                                 serverside scaling yet.
 */
int nvidia_encoder_frame_intake(NvidiaEncoder* encoder, RegisteredResource resource_to_register);
/**
 * @brief                          Set the next frame to be an IDR-frame,
 *                                 with SPS/PPS headers included as well.
 *
 * @param encoder                  Encoder to be updated
 */
void nvidia_set_iframe(NvidiaEncoder* encoder);

/**
 * @brief                          Encode the most recently intake'd frame
 *
 * @param encoder                  The encoder to encode with
 *
 * @returns                        0 on success, else -1
 */
int nvidia_encoder_encode(NvidiaEncoder* encoder);

/**
 * @brief                          Destroy the nvidia encoder
 *
 * @param encoder                  The encoder to destroy
 */
void destroy_nvidia_encoder(NvidiaEncoder* encoder);

#endif
