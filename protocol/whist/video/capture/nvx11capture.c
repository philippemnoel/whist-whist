#include "x11capture.h"
#include "nvidiacapture.h"
#include "nvx11capture.h"

typedef struct {
    CaptureDeviceImpl base;
    CaptureDeviceImpl* x11impl;
    CaptureDeviceImpl* nvimpl;

    WhistMutex lock;
    volatile bool use_nvidia_device;  // the device currently used for capturing
    volatile bool switch_to_nvidia_device;
    CaptureDeviceType last_capture_device;  // the device used for the last capture, so we can pick
                                            // the right encoder
    bool first_frame;
    volatile bool pending_destruction;
    WhistThread nvidia_manager;
    WhistSemaphore nvidia_device_semaphore;
    WhistSemaphore sleep_semaphore;
    CUcontext nvidia_cuda_context;
} NvX11CaptureDevice;

static int32_t multithreaded_nvidia_device_manager(void* opaque) {
    /*
        Multithreaded function to asynchronously destroy and create the nvidia capture device when
       necessary. nvidia_device_semaphore will be posted to if capture_screen on nvidia fails,
       indicating a need to recreate the capture device, or when the whole device is being torn
       down. In the former case, the thread will keep attempting to create a new nvidia device until
       successful. In the latter case, the thread will exit.

        Arguments:
            opaque (void*): pointer to the capture device

        Returns:
            (int): 0 on exit
    */
    NvX11CaptureDevice* device = (NvX11CaptureDevice*)opaque;
    WhistTimer running_timer;

    start_timer(&running_timer);

    whist_wait_semaphore(device->nvidia_device_semaphore);
    if (device->pending_destruction) return 0;

    // set current context
    CUresult cu_res = cu_ctx_push_current_ptr(device->nvidia_cuda_context);
    if (cu_res != CUDA_SUCCESS) {
        LOG_ERROR("Failed to push current context, status %d!", cu_res);
    }
    cu_ctx_synchronize_ptr();

    // Nvidia requires recreation
    while (device->nvimpl == NULL) {
        LOG_INFO("Creating nvidia capture device...");
        device->nvimpl = create_nvidia_capture_device(device->base.infos, NULL);
        if (device->nvimpl == NULL) {
            /* retry every 200 ms at the beginning and after 4s, retry every 2 seconds */
            uint32_t sleep_delay = 200;
            if (get_timer(&running_timer) > 4.0) sleep_delay = 2 * 1000;

            LOG_DEBUG("failed attempt, sleeping for %d ms", sleep_delay);

            whist_wait_timeout_semaphore(device->sleep_semaphore, sleep_delay);
        }

        if (device->pending_destruction) {
            cu_ctx_pop_current_ptr(&device->nvidia_cuda_context);
            return 0;
        }
    }

    LOG_INFO("Created nvidia capture device!");
    NvidiaCaptureDevice* nv_capture = (NvidiaCaptureDevice*)device->nvimpl;
    LOG_DEBUG("device handle: %" PRIu64, nv_capture->fbc_handle);
    nvidia_release_context((NvidiaCaptureDevice*)device->nvimpl);

    cu_res = cu_ctx_pop_current_ptr(&device->nvidia_cuda_context);
    if (cu_res) {
        LOG_INFO("an error happened when popping the nvidia context (status = %d)", cu_res);
    }

    whist_lock_mutex(device->lock);
    device->switch_to_nvidia_device = true;
    whist_unlock_mutex(device->lock);

    return 0;
}

static int nvx11_capture_device_init(NvX11CaptureDevice* device, int width, int height, int dpi) {
    int ret = device->x11impl->init(device->x11impl, width, height, dpi);
    whist_post_semaphore(device->nvidia_device_semaphore);
    return ret;
}

static int nvx11_reconfigure_capture_device(NvX11CaptureDevice* dev, int width, int height,
                                            int dpi) {
    int ret;

    /* kill the NVidia device */
    dev->pending_destruction = true;
    whist_post_semaphore(dev->sleep_semaphore);
    whist_wait_thread(dev->nvidia_manager, &ret);

    if (dev->nvimpl) dev->nvimpl->destroy(&dev->nvimpl);
    dev->switch_to_nvidia_device = false;
    dev->use_nvidia_device = false;
    dev->pending_destruction = false;
    dev->base.infos->last_capture_device = X11_DEVICE;

    /* reconfigure the X server */
    ret = dev->x11impl->reconfigure(dev->x11impl, width, height, dpi);
    if (ret != 0) return ret;

    /* restart the thread creating the NVidia device */
    dev->nvidia_manager = whist_create_thread(multithreaded_nvidia_device_manager,
                                              "multithreaded_nvidia_manager", dev);
    dev->first_frame = true;
    whist_post_semaphore(dev->nvidia_device_semaphore);
    return ret;
}

static int nvx11_capture_screen(NvX11CaptureDevice* device, int* width, int* height, int* pitch) {
    CaptureDeviceImpl* impl = device->x11impl;

    whist_lock_mutex(device->lock);
    if (!device->first_frame && device->switch_to_nvidia_device) {
        impl = device->nvimpl;

        LOG_INFO("switching to NVidia capture");
        CUcontext tmp_cuda_context = NULL;
        CUresult cu_res = cu_ctx_pop_current_ptr(&tmp_cuda_context);
        if (cu_res != CUDA_SUCCESS) {
            LOG_ERROR("Failed to pop current context, status=%d", cu_res);
        }
        LOG_DEBUG("popped last CUDA context %p", tmp_cuda_context);

        cu_res = cu_ctx_push_current_ptr(device->nvidia_cuda_context);
        if (cu_res != CUDA_SUCCESS) {
            LOG_ERROR("Failed to swap contexts!");
        }

        cu_res = cu_ctx_synchronize_ptr();
        if (cu_res != CUDA_SUCCESS) {
            LOG_ERROR("Failed to cu_ctx_synchronize_ptr!");
        }

        nvidia_bind_context((NvidiaCaptureDevice*)impl);

        device->switch_to_nvidia_device = false;
        device->use_nvidia_device = true;
    }
    whist_unlock_mutex(device->lock);

    if (device->use_nvidia_device) {
        int new_w, new_h, new_pitch;
        CaptureDeviceInfos* infos = device->base.infos;

        impl = device->nvimpl;
        int ret = impl->capture_screen(impl, &new_w, &new_h, &new_pitch);
        if (LOG_VIDEO) LOG_INFO("Captured with Nvidia (ret=%d)", ret);

        if (ret >= 0) {
            if ((new_w != infos->width) || (new_h != infos->height) ||
                (new_pitch != infos->pitch)) {
                LOG_INFO("Captured sizes changed from (%dx%d p=%d) to (%dx%d p=%d)", infos->width,
                         infos->height, infos->pitch, new_w, new_h, new_pitch);
            }
            infos->last_capture_device = NVIDIA_DEVICE;
            return ret;
        }

        /* if capture via NVidia didn't work let's kill the NVidia instance, switch back to X11
         * and retry the thread that creates NVidia backend */
        device->nvimpl->destroy(&device->nvimpl);

        int thread_exit_code;
        whist_wait_thread(device->nvidia_manager, &thread_exit_code);

        device->nvidia_manager = whist_create_thread(multithreaded_nvidia_device_manager,
                                                     "multithreaded_nvidia_manager", device);
        device->use_nvidia_device = false;
        device->first_frame = true;
        whist_post_semaphore(device->nvidia_device_semaphore);
    }

    return device->x11impl->capture_screen(device->x11impl, width, height, pitch);
}

static int nvx11_get_dimensions(NvX11CaptureDevice* device, int* w, int* h, int* dpi) {
    return device->x11impl->get_dimensions(device->x11impl, w, h, dpi);
}

static int nvx11_capture_getdata(NvX11CaptureDevice* device, void** buf, int* stride) {
    CaptureDeviceImpl* impl = device->use_nvidia_device ? device->nvimpl : device->x11impl;
    int ret = impl->capture_get_data(impl, buf, stride);
    return ret;
}

static int nvx11_capture_transfer_screen(NvX11CaptureDevice* device, CaptureEncoderHints* hints) {
    int ret = 0;
    CaptureDeviceImpl* impl = device->use_nvidia_device ? device->nvimpl : device->x11impl;

    memset(hints, 0, sizeof(*hints));
    if (impl && impl->transfer_screen) ret = impl->transfer_screen(impl, hints);

    if (device->use_nvidia_device && !device->first_frame)
        hints->cuda_context = device->nvidia_cuda_context;

    device->first_frame = false;
    return ret;
}

static void nvx11_destroy_capture_device(NvX11CaptureDevice** pdevice) {
    NvX11CaptureDevice* device = *pdevice;
    int ret;

    device->pending_destruction = true;
    whist_post_semaphore(device->nvidia_device_semaphore);
    whist_post_semaphore(device->sleep_semaphore);
    whist_wait_thread(device->nvidia_manager, &ret);

    device->x11impl->destroy(&device->x11impl);
    if (device->nvimpl) device->nvimpl->destroy(&device->nvimpl);

    whist_destroy_mutex(device->lock);
    free(device);
    *pdevice = NULL;
}

CaptureDeviceImpl* create_nvx11_capture_device(CaptureDeviceInfos* infos, const char* display) {
    NvX11CaptureDevice* dev = (NvX11CaptureDevice*)safe_zalloc(sizeof(X11CaptureDevice));
    CaptureDeviceImpl* impl = &dev->base;

    impl->device_type = NVX11_DEVICE;
    impl->infos = infos;
    impl->init = (CaptureDeviceInitFn)nvx11_capture_device_init;
    impl->reconfigure = (CaptureDeviceReconfigureFn)nvx11_reconfigure_capture_device;
    impl->capture_screen = (CaptureDeviceCaptureScreenFn)nvx11_capture_screen;
    impl->capture_get_data = (CaptureDeviceGetDataFn)nvx11_capture_getdata;
    impl->transfer_screen = (CaptureDeviceTransferScreenFn)nvx11_capture_transfer_screen;
    impl->get_dimensions = (CaptureDeviceGetDimensionsFn)nvx11_get_dimensions;
    impl->destroy = (CaptureDeviceDestroyFn)nvx11_destroy_capture_device;

    dev->x11impl = create_x11_capture_device(infos, display);
    if (!dev->x11impl) goto error_x11;

    bool res = cuda_init(&dev->nvidia_cuda_context, false);
    if (!res) {
        LOG_ERROR("Failed to initialize second cuda context!");
    }

    dev->lock = whist_create_mutex();

    LOG_DEBUG("Nvidia context: %p, Video context: %p", dev->nvidia_cuda_context,
              *get_video_thread_cuda_context_ptr());
    // set up semaphore and nvidia manager
    dev->sleep_semaphore = whist_create_semaphore(0);
    dev->nvidia_device_semaphore = whist_create_semaphore(0);
    dev->nvidia_manager = whist_create_thread(multithreaded_nvidia_device_manager,
                                              "multithreaded_nvidia_manager", dev);
    dev->first_frame = true;
    return impl;

error_x11:
    free(dev);
    return NULL;
}

X11CaptureDevice* nvx11_capture_get_x11(CaptureDeviceImpl* impl) {
    NvX11CaptureDevice* nvx11 = (NvX11CaptureDevice*)impl;
    return (X11CaptureDevice*)nvx11->x11impl;
}
