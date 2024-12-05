#include "nvidia_encode.h"
#include "../cudacontext.h"
#include <whist/logging/log_statistic.h>
#include "whist/core/features.h"
#include "whist/utils/string_buffer.h"

#include <dlfcn.h>
#include <string.h>
#include <npp.h>

#define LIB_ENCODEAPI_NAME "libnvidia-encode.so.1"

#define WHIST_PRESET NV_ENC_PRESET_P2_GUID
#define WHIST_TUNING NV_ENC_TUNING_INFO_LOW_LATENCY

static int initialize_preset_config(NvidiaEncoder* encoder, int bitrate, int vbv_size,
                                    CodecType codec, NV_ENC_PRESET_CONFIG* p_preset_config);
static GUID get_codec_guid(CodecType codec);
static int register_resource(NvidiaEncoder* encoder, RegisteredResource* resource_to_register);
static void unregister_resource(NvidiaEncoder* encoder, RegisteredResource registered_resource);

static void try_free_frame(NvidiaEncoder* encoder) {
    /*
        Unlock the bitstream for the encoder so we can clear the encoder's frame in preparation for
       encoding the next frame.

        Arguments:
            encoder (NvidiaEncoder*): encoder to use
    */
    NVENCSTATUS status;

    // If there was a previous frame, we should free it first
    if (encoder->frame != NULL) {
        // Unlock the bitstream
        status = encoder->p_enc_fn.nvEncUnlockBitstream(encoder->internal_nvidia_encoder,
                                                        encoder->output_buffer);
        if (status != NV_ENC_SUCCESS) {
            // If LOG_ERROR is ever desired here in the future,
            // make sure to still UnmapInputResource before returning -1
            LOG_FATAL("Failed to unlock bitstream buffer, status = %d", status);
        }

        encoder->frame = NULL;
    }
}

static void string_buffer_write_nvidia_guid(StringBuffer* sb, const char* name, const GUID* guid) {
    string_buffer_printf(sb, "%s GUID: ", name);
    string_buffer_printf(sb, "%08" PRIx32 "-%04" PRIx16 "-%04" PRIx16, guid->Data1, guid->Data2,
                         guid->Data3);
    for (int i = 0; i < 8; i++) {
        if (i == 0 || i == 2) {
            string_buffer_puts(sb, "-");
        }
        string_buffer_printf(sb, "%02x", guid->Data4[i]);
    }
    string_buffer_puts(sb, "\n");
}

static void string_buffer_write_nvidia_qp(StringBuffer* sb, const char* name, const NV_ENC_QP* qp) {
    string_buffer_printf(sb, "%s QP: { P = %d, B = %d, I = %d }\n", name, qp->qpInterP,
                         qp->qpInterB, qp->qpIntra);
}

static void dump_encode_config(const NV_ENC_CONFIG* config) {
    char config_string[1024];
    StringBuffer buf;
    string_buffer_init(&buf, config_string, sizeof(config_string));

#define DUMP_FIELD(field) string_buffer_printf(&buf, #field ": %d\n", config->field)

    string_buffer_write_nvidia_guid(&buf, "profile", &config->profileGUID);

    DUMP_FIELD(gopLength);
    DUMP_FIELD(frameIntervalP);
    DUMP_FIELD(frameFieldMode);

    string_buffer_write_nvidia_qp(&buf, "const", &config->rcParams.constQP);

    DUMP_FIELD(rcParams.rateControlMode);
    DUMP_FIELD(rcParams.averageBitRate);
    DUMP_FIELD(rcParams.maxBitRate);
    DUMP_FIELD(rcParams.vbvBufferSize);
    DUMP_FIELD(rcParams.vbvInitialDelay);
    DUMP_FIELD(rcParams.enableMinQP);
    DUMP_FIELD(rcParams.enableMaxQP);
    DUMP_FIELD(rcParams.enableInitialRCQP);
    DUMP_FIELD(rcParams.enableAQ);
    DUMP_FIELD(rcParams.enableLookahead);
    DUMP_FIELD(rcParams.enableTemporalAQ);
    DUMP_FIELD(rcParams.zeroReorderDelay);
    DUMP_FIELD(rcParams.enableNonRefP);

    string_buffer_write_nvidia_qp(&buf, "min", &config->rcParams.minQP);
    string_buffer_write_nvidia_qp(&buf, "max", &config->rcParams.maxQP);
    string_buffer_write_nvidia_qp(&buf, "initial", &config->rcParams.initialRCQP);

    DUMP_FIELD(rcParams.targetQuality);
    DUMP_FIELD(rcParams.lookaheadDepth);

#undef DUMP_FIELD

    LOG_WARNING("NVENC config:\n%s", config_string);
}

static void dump_encode_init_params(const NV_ENC_INITIALIZE_PARAMS* params) {
    char params_string[1024];
    StringBuffer buf;
    string_buffer_init(&buf, params_string, sizeof(params_string));

    string_buffer_write_nvidia_guid(&buf, "encode", &params->encodeGUID);
    string_buffer_write_nvidia_guid(&buf, "preset", &params->presetGUID);

    string_buffer_printf(&buf, "encodeSize: %dx%d\n", params->encodeWidth, params->encodeHeight);
    string_buffer_printf(&buf, "darSize: %dx%d\n", params->darWidth, params->darHeight);
    string_buffer_printf(&buf, "frameRate: %d/%d\n", params->frameRateNum, params->frameRateDen);
    string_buffer_printf(&buf, "maxEncodeSize: %dx%d\n", params->maxEncodeWidth,
                         params->maxEncodeHeight);

    LOG_WARNING("NVENC init params:\n%s", params_string);

    dump_encode_config(params->encodeConfig);
}

static void dump_encode_reconfigure_params(const NV_ENC_RECONFIGURE_PARAMS* params) {
    char params_string[1024];
    StringBuffer buf;
    string_buffer_init(&buf, params_string, sizeof(params_string));

    string_buffer_printf(&buf, "resetEncoder: %d\n", params->resetEncoder);
    string_buffer_printf(&buf, "forceIDR: %d\n", params->resetEncoder);

    LOG_WARNING("NVENC reconfigure params:\n%s", params_string);

    dump_encode_init_params(&params->reInitEncodeParams);
}

NvidiaEncoder* create_nvidia_encoder(int bitrate, CodecType codec, int out_width, int out_height,
                                     int vbv_size, CUcontext cuda_context) {
    /*
        Create an Nvidia encoder with the given settings. The encoder will have reconfigurable
       bitrate, codec, width, and height.

        Arguments:
            bitrate (int): Desired encoding bitrate
            codec (CodecType): Desired encoding codec
            out_width (int): Desired width
            out_height (int): Desired height
            cuda_context (CUcontext): CUDA context on which to create encoding session

        Returns:
            (NvidiaEncoder*): New Nvidia encoder with the correct settings
    */
    LOG_INFO("Creating encoder of bitrate %d, codec %d, size = %dx%d, cuda_context %p", bitrate,
             codec, out_width, out_height, cuda_context);
    NVENCSTATUS status;

    // Initialize the encoder
    NvidiaEncoder* encoder = malloc(sizeof(NvidiaEncoder));
    memset(encoder, 0, sizeof(*encoder));
    encoder->codec_type = codec;
    encoder->width = out_width;
    encoder->height = out_height;
    encoder->cuda_context = cuda_context;
    encoder->bitrate = bitrate;
    encoder->vbv_size = vbv_size;

    // Set initial frame pointer to NULL, nvidia will overwrite this later with the framebuffer
    // pointer
    encoder->frame = NULL;
    encoder->frame_idx = -1;

    // Dynamically load the NvEncodeAPI library
    void* lib_enc = dlopen(LIB_ENCODEAPI_NAME, RTLD_NOW);
    if (lib_enc == NULL) {
        LOG_ERROR("Unable to open '%s' (%s)", LIB_ENCODEAPI_NAME, dlerror());
        free(encoder);
        return NULL;
    }

    /*
     * Resolve the 'NvEncodeAPICreateInstance' symbol that will allow us to get
     * the API function pointers.
     */
    typedef NVENCSTATUS(NVENCAPI * NVENCODEAPICREATEINSTANCEPROC)(NV_ENCODE_API_FUNCTION_LIST*);
    NVENCODEAPICREATEINSTANCEPROC nv_encode_api_create_instance_ptr =
        (NVENCODEAPICREATEINSTANCEPROC)dlsym(lib_enc, "NvEncodeAPICreateInstance");
    if (nv_encode_api_create_instance_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'NvEncodeAPICreateInstance'");
        free(encoder);
        return NULL;
    }

    /*
     * Create an NvEncodeAPI instance.
     *
     * API function pointers are accessible through pEncFn.
     */
    memset(&encoder->p_enc_fn, 0, sizeof(encoder->p_enc_fn));

    encoder->p_enc_fn.version = NV_ENCODE_API_FUNCTION_LIST_VER;

    status = nv_encode_api_create_instance_ptr(&encoder->p_enc_fn);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Unable to create NvEncodeAPI instance (status: %d)", status);
        free(encoder);
        return NULL;
    }

    // Create an encoder session
    NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS encode_session_params = {0};

    encode_session_params.version = NV_ENC_OPEN_ENCODE_SESSION_EX_PARAMS_VER;
    encode_session_params.apiVersion = NVENCAPI_VERSION;
    encode_session_params.deviceType = NV_ENC_DEVICE_TYPE_CUDA;
    encode_session_params.device = cuda_context;

    status = encoder->p_enc_fn.nvEncOpenEncodeSessionEx(&encode_session_params,
                                                        &encoder->internal_nvidia_encoder);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to open an encoder session, status = %d", status);
        free(encoder);
        return NULL;
    }

    // Initialize the preset config
    NV_ENC_PRESET_CONFIG preset_config;
    status =
        initialize_preset_config(encoder, bitrate, vbv_size, encoder->codec_type, &preset_config);
    if (status < 0) {
        LOG_ERROR("custom_preset_config failed");
        free(encoder);
        return NULL;
    }

    // Initialize the encode session
    NV_ENC_INITIALIZE_PARAMS init_params = {0};
    init_params.version = NV_ENC_INITIALIZE_PARAMS_VER;
    init_params.encodeGUID = get_codec_guid(encoder->codec_type);
    init_params.presetGUID = WHIST_PRESET;
    init_params.tuningInfo = WHIST_TUNING;
    init_params.encodeConfig = &preset_config.presetCfg;
    init_params.encodeWidth = out_width;
    init_params.encodeHeight = out_height;
    init_params.maxEncodeWidth = MAX_SCREEN_WIDTH;
    init_params.maxEncodeHeight = MAX_SCREEN_HEIGHT;
    init_params.frameRateNum = MAX_FPS;
    init_params.frameRateDen = 1;
    init_params.enablePTD = 1;

    // Copy the init params into the encoder for reconfiguration
    encoder->encoder_params = init_params;

    status =
        encoder->p_enc_fn.nvEncInitializeEncoder(encoder->internal_nvidia_encoder, &init_params);
    if (status != NV_ENC_SUCCESS) {
        dump_encode_init_params(&init_params);
        LOG_ERROR("Failed to initialize the encode session, status = %d", status);
        free(encoder);
        return NULL;
    }

    // Create a bitstream buffer to hold the output
    NV_ENC_CREATE_BITSTREAM_BUFFER bitstream_buffer_params = {0};
    bitstream_buffer_params.version = NV_ENC_CREATE_BITSTREAM_BUFFER_VER;

    status = encoder->p_enc_fn.nvEncCreateBitstreamBuffer(encoder->internal_nvidia_encoder,
                                                          &bitstream_buffer_params);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to create a bitstream buffer, status = %d", status);
        return NULL;
    }
    encoder->output_buffer = bitstream_buffer_params.bitstreamBuffer;

    return encoder;
}

int nvidia_encoder_frame_intake(NvidiaEncoder* encoder, RegisteredResource resource_to_register) {
    /*
        Send a frame to the encoder in preparation for encoding. For a CUDA buffer, produced by the
       Nvidia capture device, we need to make sure it has been registered with the encoder. For a
       CPU buffer, produced by the X11 capture device, we need to allocate a buffer for the encoder,
       then memcpy the contents of the X11 capture into the encoder's buffer. We keep a cache of
       registered resources and allocated buffers. We reconfigure the encoder on the fly if needed.

        Arguments:
            encoder (NvidiaEncoder*): encoder to use
            resource_to_register (RegisteredResource): resource to send into the encoder
    */
    if (resource_to_register.width != encoder->width ||
        resource_to_register.height != encoder->height) {
        // reconfigure the encoder
        if (!nvidia_reconfigure_encoder(encoder, resource_to_register.width,
                                        resource_to_register.height, encoder->bitrate,
                                        encoder->vbv_size, encoder->codec_type)) {
            LOG_ERROR("Reconfigure failed!");
            return -1;
        }
    }
    if (!resource_to_register.texture_pointer) {
        LOG_ERROR("NULL texture passed into encoder! Doing nothing.");
        return -1;
    }
    bool cache_hit = false;
    // check the cache for a registered resource
    for (int i = 0; i < RESOURCE_CACHE_SIZE; i++) {
        RegisteredResource cached_resource = encoder->resource_cache[i];
        if (cached_resource.texture_pointer == resource_to_register.texture_pointer &&
            cached_resource.width == resource_to_register.width &&
            cached_resource.height == resource_to_register.height &&
            cached_resource.device_type == resource_to_register.device_type) {
            encoder->registered_resource = encoder->resource_cache[i];
            if (LOG_VIDEO) {
                LOG_DEBUG("Using cached resource at entry %d", i);
            }
            cache_hit = true;
            break;
        }
    }
    if (!cache_hit) {
        // no cache hit: we register the resource
        if (encoder->resource_cache[RESOURCE_CACHE_SIZE - 1].texture_pointer != NULL) {
            unregister_resource(encoder, encoder->resource_cache[RESOURCE_CACHE_SIZE - 1]);
        }
        for (int i = RESOURCE_CACHE_SIZE - 1; i > 0; i--) {
            encoder->resource_cache[i] = encoder->resource_cache[i - 1];
        }
        int result = register_resource(encoder, &resource_to_register);
        if (result < 0) {
            LOG_ERROR("Failed to register resource!");
            return -1;
        }
        encoder->resource_cache[0] = resource_to_register;
        encoder->registered_resource = encoder->resource_cache[0];
    }
    if (LOG_VIDEO) {
        LOG_DEBUG("Registered resource data: texture %p, width %d, height %d, pitch %d, device %s",
                  encoder->registered_resource.texture_pointer, encoder->registered_resource.width,
                  encoder->registered_resource.height, encoder->registered_resource.pitch,
                  encoder->registered_resource.device_type == NVIDIA_DEVICE ? "Nvidia" : "X11");
    }
    // If on X11, we need to memcpy the capture into the encoder buffer
    // In accordance with Nvidia documentation, first we lock the buffer, then memcpy, then unlock
    // the buffer
    if (encoder->registered_resource.device_type == X11_DEVICE) {
        NV_ENC_LOCK_INPUT_BUFFER lock_params = {0};
        lock_params.version = NV_ENC_LOCK_INPUT_BUFFER_VER;
        lock_params.inputBuffer = encoder->registered_resource.handle;
        NVENCSTATUS status =
            encoder->p_enc_fn.nvEncLockInputBuffer(encoder->internal_nvidia_encoder, &lock_params);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to lock input buffer, error %d", status);
            return -1;
        }
        encoder->pitch = lock_params.pitch;
        if (LOG_VIDEO) {
            LOG_DEBUG("width: %d, height: %d, pitch: %d (lock pitch %d)",
                      encoder->registered_resource.width, encoder->registered_resource.height,
                      encoder->registered_resource.pitch, lock_params.pitch);
            LOG_DEBUG("Buffer data ptr: %p, texture pointer: %p", lock_params.bufferDataPtr,
                      encoder->registered_resource.texture_pointer);
        }
        // memcpy input data
        // the encoder buffer might have a different pitch, so we have to copy row by row
        for (int i = 0; i < encoder->registered_resource.height; i++) {
            memcpy((char*)lock_params.bufferDataPtr + i * lock_params.pitch,
                   (char*)encoder->registered_resource.texture_pointer +
                       i * encoder->registered_resource.pitch,
                   min(encoder->registered_resource.pitch, (int)lock_params.pitch));
        }
        status = encoder->p_enc_fn.nvEncUnlockInputBuffer(encoder->internal_nvidia_encoder,
                                                          lock_params.inputBuffer);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to unlock input buffer, error %d", status);
            return -1;
        }
    } else {
        encoder->pitch = encoder->registered_resource.pitch;
    }
    return 0;
}

static int register_resource(NvidiaEncoder* encoder, RegisteredResource* resource_to_register) {
    /*
        Register the given resource with the encoder. If a CUDA buffer (externally allocated), we
       call nvEncRegisterResource. Otherwise, we tell the encoder to allocate a buffer of the given
       width and height using nvEncCreateInputBuffer. On a successful registration,
       resource_to_register->handle will be filled with the encoder's new buffer.

        Arguments:
            encoder (NvidiaEncoder*): encoder to register resource with
            resource_to_register (RegisteredResource*): resource to register.
       resource_to_register->handle will be filled on success.

        Returns:
            (int): 0 on success, -1 on failure
    */
    switch (resource_to_register->device_type) {
        case NVIDIA_DEVICE: {
            LOG_DEBUG("On nvidia, width = %d, height = %d, pitch = %d, texture = %p",
                      resource_to_register->width, resource_to_register->height,
                      resource_to_register->pitch, resource_to_register->texture_pointer);
            if (resource_to_register->texture_pointer == NULL) {
                LOG_ERROR("Tried to register NULL resource, exiting");
                return -1;
            }

            // register the device's resources to the encoder
            NV_ENC_REGISTER_RESOURCE register_params = {0};
            register_params.version = NV_ENC_REGISTER_RESOURCE_VER;
            register_params.resourceType = NV_ENC_INPUT_RESOURCE_TYPE_CUDADEVICEPTR;
            register_params.width = resource_to_register->width;
            register_params.height = resource_to_register->height;
            register_params.pitch = resource_to_register->pitch;
            register_params.resourceToRegister = resource_to_register->texture_pointer;
            register_params.bufferFormat = NV_ENC_BUFFER_FORMAT_ARGB;

            int status = encoder->p_enc_fn.nvEncRegisterResource(encoder->internal_nvidia_encoder,
                                                                 &register_params);
            if (status != NV_ENC_SUCCESS) {
                LOG_ERROR("Failed to register texture, status = %d", status);
                return -1;
            }
            resource_to_register->handle = register_params.registeredResource;
            return 0;
        }
        case X11_DEVICE: {
            LOG_DEBUG("On X11");
            NV_ENC_CREATE_INPUT_BUFFER input_params = {0};
            input_params.version = NV_ENC_CREATE_INPUT_BUFFER_VER;
            input_params.width = resource_to_register->width;
            input_params.height = resource_to_register->height;
            input_params.bufferFmt = NV_ENC_BUFFER_FORMAT_ARGB;
            // input_params.pSysMemBuffer = resource_to_register->texture_pointer;
            int status = encoder->p_enc_fn.nvEncCreateInputBuffer(encoder->internal_nvidia_encoder,
                                                                  &input_params);
            if (status != NV_ENC_SUCCESS) {
                LOG_ERROR("Failed to create input buffer, status = %d", status);
                return -1;
            }
            encoder->buffer_fmt = input_params.bufferFmt;
            resource_to_register->handle = input_params.inputBuffer;
            return 0;
        }
        default:
            LOG_ERROR("Unknown device type: %d", resource_to_register->device_type);
            return -1;
    }
}

static void unregister_resource(NvidiaEncoder* encoder, RegisteredResource registered_resource) {
    /*
        Unregister the given resource. For a CUDA resource, we call nvEncUnregisterResource; for an
       encoder-allocated buffer, we call nvEncDestroyInputBuffer.

        Arguments:
            encoder (NvidiaEncoder*): encoder to use
            registered_resource (RegisteredResource): registered resource to unregister
    */
    if (!registered_resource.handle) {
        LOG_INFO("Trying to unregister NULL resource - nothing to do!");
        return;
    }
    if (registered_resource.device_type == NVIDIA_DEVICE) {
        int status = encoder->p_enc_fn.nvEncUnregisterResource(encoder->internal_nvidia_encoder,
                                                               registered_resource.handle);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to unregister resource, status = %d", status);
        }
    } else {
        int status = encoder->p_enc_fn.nvEncDestroyInputBuffer(encoder->internal_nvidia_encoder,
                                                               registered_resource.handle);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to destroy input buffer, status = %d", status);
        }
    }
}

int nvidia_encoder_encode(NvidiaEncoder* encoder) {
    /*
        Encode a frame with the encoder. If the encoder's input came from an externally allocated
       buffer (e.g. CUDA), we need to map and unmap the resource. Then we encode and lock the
       bitstream buffer in preparation for sending the frame to the client.

        Arguments:
            encoder (NvidiaEncoder*): encoder to use

        Returns:
            (int): 0 on success, -1 on failure
    */
    NVENCSTATUS status;

    // Register the frame intake
    RegisteredResource registered_resource = encoder->registered_resource;
    if (registered_resource.handle == NULL) {
        LOG_ERROR("Frame intake failed! No resource to map and encode");
        return -1;
    }
    NV_ENC_MAP_INPUT_RESOURCE map_params = {0};
    if (registered_resource.device_type == NVIDIA_DEVICE) {
        // only if we've received a CUDA resource
        map_params.version = NV_ENC_MAP_INPUT_RESOURCE_VER;
        map_params.registeredResource = registered_resource.handle;
        status =
            encoder->p_enc_fn.nvEncMapInputResource(encoder->internal_nvidia_encoder, &map_params);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to map the resource, status = %d\n", status);
            return -1;
        }
        encoder->buffer_fmt = map_params.mappedBufferFmt;
    }

    // Try to free the encoder's previous frame
    try_free_frame(encoder);

    // Fill in the frame encoding information
    NV_ENC_PIC_PARAMS enc_params = {0};
    enc_params.version = NV_ENC_PIC_PARAMS_VER;
    enc_params.pictureStruct = NV_ENC_PIC_STRUCT_FRAME;
    enc_params.inputWidth = encoder->width;
    enc_params.inputHeight = encoder->height;
    enc_params.inputPitch = encoder->width;
    if (registered_resource.device_type == NVIDIA_DEVICE) {
        enc_params.inputBuffer = map_params.mappedResource;
    } else {
        enc_params.inputBuffer = registered_resource.handle;
    }
    enc_params.bufferFmt = encoder->buffer_fmt;
    // frame_idx starts at -1, so first frame has idx 0
    enc_params.frameIdx = ++encoder->frame_idx;
    enc_params.outputBitstream = encoder->output_buffer;

    encoder->frame_type = encoder->ltr_action.frame_type;
    NV_ENC_PIC_PARAMS_H264* h264_pic_params = &enc_params.codecPicParams.h264PicParams;

    if (encoder->wants_iframe || encoder->ltr_action.frame_type == VIDEO_FRAME_TYPE_INTRA) {
        encoder->frame_type = VIDEO_FRAME_TYPE_INTRA;
        enc_params.encodePicFlags = NV_ENC_PIC_FLAG_FORCEIDR | NV_ENC_PIC_FLAG_OUTPUT_SPSPPS;
        if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
            // In H.264 IDR frames must go in long-term slot 0.
            FATAL_ASSERT(encoder->ltr_action.long_term_frame_index == 0);
            h264_pic_params->ltrMarkFrame = 1;
            h264_pic_params->ltrMarkFrameIdx = 0;
        }
    } else if (encoder->ltr_action.frame_type == VIDEO_FRAME_TYPE_NORMAL) {
        // Normal encode, don't set any extra options.
    } else if (encoder->ltr_action.frame_type == VIDEO_FRAME_TYPE_CREATE_LONG_TERM) {
        h264_pic_params->ltrMarkFrame = 1;
        h264_pic_params->ltrMarkFrameIdx = encoder->ltr_action.long_term_frame_index;
    } else if (encoder->ltr_action.frame_type == VIDEO_FRAME_TYPE_REFER_LONG_TERM) {
        h264_pic_params->ltrUseFrames = 1;
        h264_pic_params->ltrUseFrameBitmap = (1 << encoder->ltr_action.long_term_frame_index);
    } else {
        FATAL_ASSERT(false && "Invalid frame_type in LTR action");
    }

    // Encode the frame
    status = encoder->p_enc_fn.nvEncEncodePicture(encoder->internal_nvidia_encoder, &enc_params);
    if (status != NV_ENC_SUCCESS) {
        // TODO: Unmap the frame if needed! Otherwise, memory leaks here
        LOG_ERROR("Failed to encode frame, status = %d", status);
        return -1;
    }

    if (registered_resource.device_type == NVIDIA_DEVICE) {
        // Unmap the input buffer
        status = encoder->p_enc_fn.nvEncUnmapInputResource(encoder->internal_nvidia_encoder,
                                                           map_params.mappedResource);
        if (status != NV_ENC_SUCCESS) {
            // FATAL is chosen over ERROR here to prevent
            // out-of-control runaway memory usage
            LOG_FATAL("Failed to unmap the resource, memory is leaking! status = %d", status);
        }
    }
    encoder->registered_resource.handle = NULL;

    // Lock the bitstream in preparation for frame processing
    NV_ENC_LOCK_BITSTREAM lock_params = {0};

    lock_params.version = NV_ENC_LOCK_BITSTREAM_VER;
    lock_params.outputBitstream = encoder->output_buffer;

    status = encoder->p_enc_fn.nvEncLockBitstream(encoder->internal_nvidia_encoder, &lock_params);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to lock bitstream buffer, status = %d", status);
        return -1;
    }

    log_double_statistic(VIDEO_FRAME_QP, lock_params.frameAvgQP);
    log_double_statistic(VIDEO_FRAME_SATD, lock_params.frameSatd);

    encoder->frame_size = lock_params.bitstreamSizeInBytes;
    encoder->frame = lock_params.bitstreamBufferPtr;

    // encoder->is_reference_frame = lock_params.pictureType != NV_ENC_PIC_TYPE_NONREF_P;
    // LOG_INFO("Is Reference Frame: %d", lock_params.pictureType != NV_ENC_PIC_TYPE_NONREF_P);

    // Untrigger iframe
    encoder->wants_iframe = false;
    return 0;
}

static GUID get_codec_guid(CodecType codec) {
    /*
        Get the Nvidia GUID corresponding to the desired codec.

        Arguments$:
            codec (CodecType): desired encoding codec for nvidia encoder

        Returns:
            (GUID): Nvidia GUID for the codec
    */
    GUID codec_guid = {0};
    if (codec == CODEC_TYPE_H265) {
        codec_guid = NV_ENC_CODEC_HEVC_GUID;
    } else if (codec == CODEC_TYPE_H264) {
        codec_guid = NV_ENC_CODEC_H264_GUID;
    } else {
        LOG_FATAL("Unexpected CodecType: %d", (int)codec);
    }

    return codec_guid;
}

static int initialize_preset_config(NvidiaEncoder* encoder, int bitrate, int vbv_size,
                                    CodecType codec, NV_ENC_PRESET_CONFIG* p_preset_config) {
    /*
        Modify the encoder preset configuration with our desired bitrate and 0 iframes. Right now,
       we are using the low latency high performance preset.

        Arguments$:
            encoder (NvidiaEncoder*): encoder to use
            bitrate (int): desired bitrate
            codec (CodecType): desired codec
            p_preset_config (NV_ENC_PRESET_CONFIG*): pointer to the preset config we will modify

        Returns:
            (int): 0 on success, -1 on failure
    */
    memset(p_preset_config, 0, sizeof(*p_preset_config));
    NVENCSTATUS status;

    p_preset_config->version = NV_ENC_PRESET_CONFIG_VER;
    p_preset_config->presetCfg.version = NV_ENC_CONFIG_VER;
    // fill the preset config with the low latency, high performance config
    status = encoder->p_enc_fn.nvEncGetEncodePresetConfigEx(encoder->internal_nvidia_encoder,
                                                            get_codec_guid(codec), WHIST_PRESET,
                                                            WHIST_TUNING, p_preset_config);
    // Check for bad status
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to obtain preset settings, status = %d", status);
        return -1;
    }

    // Suggestions from
    // https://docs.nvidia.com/video-technologies/video-codec-sdk/nvenc-video-encoder-api-prog-guide/#recommended-nvenc-settings
    p_preset_config->presetCfg.rcParams.multiPass = NV_ENC_TWO_PASS_QUARTER_RESOLUTION;
    p_preset_config->presetCfg.rcParams.enableAQ = 1;

    // Limiting the maxQP to avoid the video pixelation issue at low bitrates.
    p_preset_config->presetCfg.rcParams.enableMaxQP = 1;
    p_preset_config->presetCfg.rcParams.maxQP.qpInterP = MAX_QP;
    p_preset_config->presetCfg.rcParams.maxQP.qpInterB = MAX_QP;
    p_preset_config->presetCfg.rcParams.maxQP.qpIntra = MAX_INTRA_QP;
    // p_preset_config->presetCfg.rcParams.enableTemporalAQ = 1;
    p_preset_config->presetCfg.rcParams.enableNonRefP = 1;
    // Only create IDRs when we request them
    p_preset_config->presetCfg.gopLength = NVENC_INFINITE_GOPLENGTH;
    // LOWDELAY seems to have longer encode times, and the frames are about the same size. So we're
    // sticking with CBR.
    p_preset_config->presetCfg.rcParams.rateControlMode = NV_ENC_PARAMS_RC_CBR;
    p_preset_config->presetCfg.rcParams.averageBitRate = bitrate;
    p_preset_config->presetCfg.rcParams.vbvBufferSize = vbv_size;

    if (FEATURE_ENABLED(LONG_TERM_REFERENCE_FRAMES)) {
        // Enable long-term reference frames.
        // This blindly assumes that NV_ENC_CAPS_NUM_MAX_LTR_FRAMES is
        // at least two, which is true on the Nvidia cards we use but
        // might not be true on older ones.
        if (codec == CODEC_TYPE_H264) {
            NV_ENC_CONFIG_H264* h264 = &p_preset_config->presetCfg.encodeCodecConfig.h264Config;
            h264->enableLTR = 1;
            h264->ltrNumFrames = 2;
        } else if (codec == CODEC_TYPE_H265) {
            NV_ENC_CONFIG_HEVC* h265 = &p_preset_config->presetCfg.encodeCodecConfig.hevcConfig;
            h265->enableLTR = 1;
            h265->ltrNumFrames = 2;
        }
    }

    return 0;
}

bool nvidia_reconfigure_encoder(NvidiaEncoder* encoder, int width, int height, int bitrate,
                                int vbv_size, CodecType codec) {
    /*
        Using Nvidia's Reconfigure API, update encoder bitrate and codec without
       destruction/recreation.

        Arguments:
            encoder (NvidiaEncoder*): encoder to update
            codec (CodecType): new codec
            bitrate (int): new bitrate

        Returns:
            (int): 0 on success, -1 on failure
    */

    LOG_INFO("Reconfiguring encoder to width %d, height %d, bitrate %d, codec %d", width, height,
             bitrate, codec);
    NVENCSTATUS status;
    // If dimensions or codec type changes,
    // then we need to send an IDR frame
    bool needs_idr_frame =
        width != encoder->width || height != encoder->height || codec != encoder->codec_type;

    // Create reconfigure params
    NV_ENC_RECONFIGURE_PARAMS reconfigure_params = {0};
    reconfigure_params.version = NV_ENC_RECONFIGURE_PARAMS_VER;

    // Copy over init params
    reconfigure_params.reInitEncodeParams = encoder->encoder_params;

    // Check that it's possible to reconfigure into those dimensions
    int max_width = reconfigure_params.reInitEncodeParams.maxEncodeWidth;
    int max_height = reconfigure_params.reInitEncodeParams.maxEncodeHeight;
    if (width > max_width || height > max_height) {
        LOG_ERROR(
            "Trying to reconfigure into dimensions %dx%d,"
            "but the max reconfigure dimensions are %dx%d",
            width, height, max_width, max_height);
    }

    // Reinitialization takes a new set of NV_ENC_INITIALIZE_PARAMS; we use the encoder's current
    // parameters and modify the width, height, codec, and bitrate accordingly. Initialize
    // preset_config
    NV_ENC_PRESET_CONFIG preset_config;
    if (initialize_preset_config(encoder, bitrate, vbv_size, codec, &preset_config) < 0) {
        LOG_ERROR("Failed to reconfigure encoder!");
        return false;
    }
    reconfigure_params.reInitEncodeParams.encodeConfig = &preset_config.presetCfg;

    // Adjust the init params with new configure,
    // And then copy over these new init params
    reconfigure_params.reInitEncodeParams.encodeWidth = width;
    reconfigure_params.reInitEncodeParams.encodeHeight = height;
    reconfigure_params.reInitEncodeParams.encodeGUID = get_codec_guid(codec);
    NV_ENC_INITIALIZE_PARAMS new_init_params = reconfigure_params.reInitEncodeParams;
    // Reset the encoder stream if the codec has changed,
    // Since the decoder will need an iframe
    if (needs_idr_frame) {
        reconfigure_params.resetEncoder = 1;
        reconfigure_params.forceIDR = 1;
    } else {
        reconfigure_params.resetEncoder = 0;
        reconfigure_params.forceIDR = 0;
    }

    // Set encode_config params, since this is the bitrate and codec stuff we really want to change
    status = encoder->p_enc_fn.nvEncReconfigureEncoder(encoder->internal_nvidia_encoder,
                                                       &reconfigure_params);
    if (status != NV_ENC_SUCCESS) {
        dump_encode_reconfigure_params(&reconfigure_params);
        LOG_ERROR("Failed to reconfigure the encoder, status = %d", status);
        return false;
    }

    // Only update the encoder struct here!
    // Since this is after all possible "return false"'s'

    if (needs_idr_frame) {
        // Trigger an i-frame on the next iframe
        encoder->wants_iframe = true;
    }

    encoder->encoder_params = new_init_params;
    encoder->codec_type = codec;
    encoder->width = width;
    encoder->height = height;

    return true;
}

void nvidia_set_iframe(NvidiaEncoder* encoder) {
    /*
        Tell the encoder to send an i-frame as the next frame.

        Arguments:
            encoder (NvidiaEncoder*): encoder to use
    */
    if (encoder == NULL) {
        LOG_ERROR("nvidia_set_iframe received NULL encoder!");
        return;
    }
    encoder->wants_iframe = true;
}

void destroy_nvidia_encoder(NvidiaEncoder* encoder) {
    /*
        Destroy the given encoder. We flush the encoder, then destroy it.

        Arguments:
            encoder (NvidiaEncoder*): encoder to destroy
    */
    // Try to free the encoder's frame
    LOG_INFO("Trying to free frame...");
    try_free_frame(encoder);
    LOG_INFO("Freed frame!");

    NVENCSTATUS status;

    NV_ENC_PIC_PARAMS enc_params = {0};
    enc_params.version = NV_ENC_PIC_PARAMS_VER;
    enc_params.encodePicFlags = NV_ENC_PIC_FLAG_EOS;

    LOG_INFO("Sending EOS to encoder...");
    status = encoder->p_enc_fn.nvEncEncodePicture(encoder->internal_nvidia_encoder, &enc_params);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to flush the encoder, status = %d", status);
    }
    LOG_INFO("Sent EOS!");

    if (encoder->output_buffer) {
        LOG_INFO("Destroying bitstream buffer...");
        status = encoder->p_enc_fn.nvEncDestroyBitstreamBuffer(encoder->internal_nvidia_encoder,
                                                               encoder->output_buffer);
        if (status != NV_ENC_SUCCESS) {
            LOG_ERROR("Failed to destroy buffer, status = %d", status);
        }
        encoder->output_buffer = NULL;
        LOG_INFO("Destroyed bitstream buffer!");
    }

    // Destroy the encode session
    LOG_INFO("Destroying nvenc device...");
    status = encoder->p_enc_fn.nvEncDestroyEncoder(encoder->internal_nvidia_encoder);
    if (status != NV_ENC_SUCCESS) {
        LOG_ERROR("Failed to destroy encoder, status = %d", status);
    }
    LOG_INFO("Destroyed nvenc device!");

    // Free the encoder struct itself
    free(encoder);
}
