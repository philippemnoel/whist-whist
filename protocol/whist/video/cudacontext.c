/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file cudacontext.c
 * @brief This file contains the code to create a CUDA context
============================
Usage
============================
The CUDA context is required for the NVIDIA encoder. First call
`cuda_init()` and then use the initialized CUDA context by
calling `get_active_cuda_context_ptr()`.
*/

/*
============================
Includes
============================
*/
#include "cudacontext.h"

#if !OS_IS(OS_WIN32)
#include <dlfcn.h>
#endif /* OS_WIN32 */

/*
============================
Defines
============================
*/

#define LIB_CUDA_NAME "libcuda.so.1"

/*
 * CUDA entry points
 */
typedef CUresult (*CUINITPROC)(unsigned int flags);
typedef CUresult (*CUDEVICEGETPROC)(CUdevice* device, int ordinal);
typedef CUresult (*CUCTXCREATEV2PROC)(CUcontext* pctx, unsigned int flags, CUdevice dev);
typedef CUresult (*CUCTXDESTROYV2PROC)(CUcontext ctx);

static CUINITPROC cu_init_ptr = NULL;
static CUDEVICEGETPROC cu_device_get_ptr = NULL;
static CUCTXCREATEV2PROC cu_ctx_create_v2_ptr = NULL;
static CUCTXDESTROYV2PROC cu_ctx_destroy_v2_ptr = NULL;
static bool cuda_initialized = false;

CUcontext video_thread_cuda_context = NULL;
CUCTXSETCURRENTPROC cu_ctx_set_current_ptr = NULL;
CUCTXGETCURRENTPROC cu_ctx_get_current_ptr = NULL;
CUCTXPUSHCURRENTPROC cu_ctx_push_current_ptr = NULL;
CUCTXPOPCURRENTPROC cu_ctx_pop_current_ptr = NULL;
CUCTXSYNCHRONIZEPROC cu_ctx_synchronize_ptr = NULL;
CUCMEMALLOCPROC cu_mem_alloc_ptr = NULL;
CUMEMFREEPROC cu_mem_free_ptr = NULL;
CUMEMCPYDTOHV2PROC cu_memcpy_dtoh_v2_ptr = NULL;
CUMEMCPY2DV2PROC cu_memcpy_2d_v2_ptr = NULL;

/*
============================
Private Function Implementations
============================
*/

/**
 * Dynamically opens the CUDA library and resolves the symbols that are
 * needed for this application.
 *
 * \param [out] lib_cuda
 *   A pointer to the opened CUDA library.
 *
 * \return
 *   NVFBC_TRUE in case of success, NVFBC_FALSE otherwise.
 */
static NVFBC_BOOL cuda_load_library(void* lib_cuda) {
#if !OS_IS(OS_WIN32)
    lib_cuda = dlopen(LIB_CUDA_NAME, RTLD_NOW);
    if (lib_cuda == NULL) {
        LOG_ERROR("Unable to open '%s'\n", LIB_CUDA_NAME);
        return NVFBC_FALSE;
    }

    cu_init_ptr = (CUINITPROC)dlsym(lib_cuda, "cuInit");
    if (cu_init_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuInit'\n");
        return NVFBC_FALSE;
    }

    cu_device_get_ptr = (CUDEVICEGETPROC)dlsym(lib_cuda, "cuDeviceGet");
    if (cu_device_get_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuDeviceGet'\n");
        return NVFBC_FALSE;
    }

    cu_ctx_create_v2_ptr = (CUCTXCREATEV2PROC)dlsym(lib_cuda, "cuCtxCreate_v2");
    if (cu_ctx_create_v2_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxCreate_v2'\n");
        return NVFBC_FALSE;
    }

    cu_ctx_destroy_v2_ptr = (CUCTXDESTROYV2PROC)dlsym(lib_cuda, "cuCtxDestroy_v2");
    if (cu_ctx_destroy_v2_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxDestroy_v2'\n");
        return NVFBC_FALSE;
    }

    cu_memcpy_dtoh_v2_ptr = (CUMEMCPYDTOHV2PROC)dlsym(lib_cuda, "cuMemcpyDtoH_v2");
    if (cu_memcpy_dtoh_v2_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuMemcpyDtoH_v2'\n");
        return NVFBC_FALSE;
    }

    cu_memcpy_2d_v2_ptr = (CUMEMCPY2DV2PROC)dlsym(lib_cuda, "cuMemcpy2D_v2");
    if (cu_memcpy_2d_v2_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuMemcpy2D_v2'\n");
        return NVFBC_FALSE;
    }

    cu_ctx_set_current_ptr = (CUCTXSETCURRENTPROC)dlsym(lib_cuda, "cuCtxSetCurrent");
    if (cu_ctx_set_current_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxSetCurrent'\n");
        return NVFBC_FALSE;
    }

    cu_ctx_get_current_ptr = (CUCTXGETCURRENTPROC)dlsym(lib_cuda, "cuCtxGetCurrent");
    if (cu_ctx_get_current_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxGetCurrent'\n");
        return NVFBC_FALSE;
    }
    cu_ctx_pop_current_ptr = (CUCTXPOPCURRENTPROC)dlsym(lib_cuda, "cuCtxPopCurrent");
    if (cu_ctx_pop_current_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxPopCurrent'\n");
        return NVFBC_FALSE;
    }
    cu_ctx_push_current_ptr = (CUCTXPUSHCURRENTPROC)dlsym(lib_cuda, "cuCtxPushCurrent");
    if (cu_ctx_push_current_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxPushCurrent'\n");
        return NVFBC_FALSE;
    }
    cu_ctx_synchronize_ptr = (CUCTXSYNCHRONIZEPROC)dlsym(lib_cuda, "cuCtxSynchronize");
    if (cu_ctx_synchronize_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuCtxSychronizeCurrent'\n");
        return NVFBC_FALSE;
    }
    cu_mem_alloc_ptr = (CUCMEMALLOCPROC)dlsym(lib_cuda, "cuMemAlloc_v2");
    if (cu_mem_alloc_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuMemAlloc_v2'\n");
        return NVFBC_FALSE;
    }
    cu_mem_free_ptr = (CUMEMFREEPROC)dlsym(lib_cuda, "cuMemFree_v2");
    if (cu_mem_free_ptr == NULL) {
        LOG_ERROR("Unable to resolve symbol 'cuMemFree_v2'\n");
        return NVFBC_FALSE;
    }
#endif
    return NVFBC_TRUE;
}

/*
============================
Public Function Implementations
============================
*/

NVFBC_BOOL cuda_destroy(CUcontext* cuda_context_ptr) {
    FATAL_ASSERT(cuda_context_ptr != NULL);
    CUresult cu_res = cu_ctx_destroy_v2_ptr(*cuda_context_ptr);
    if (cu_res != CUDA_SUCCESS) {
        LOG_ERROR("Unable to destroy CUDA context (result: %d)\n", cu_res);
        return NVFBC_FALSE;
    }
    *cuda_context_ptr = NULL;
    return NVFBC_TRUE;
}

/**
 * Initializes CUDA and creates a CUDA context.
 *
 * \return
 *   NVFBC_TRUE in case of success, NVFBC_FALSE otherwise.
 */
NVFBC_BOOL cuda_init(CUcontext* cuda_context, bool make_current) {
    void* lib_cuda = NULL;
    if (*cuda_context) {
        LOG_DEBUG("Cuda context %p already exists! Doing nothing.", *cuda_context);
        return NVFBC_TRUE;
    }
    CUresult cu_res;
    CUdevice cu_dev;
    if (!cuda_initialized) {
        if (cuda_load_library(lib_cuda) != NVFBC_TRUE) {
            LOG_ERROR("Failed to load CUDA library!");
            return NVFBC_FALSE;
        }

        cu_res = cu_init_ptr(0);
        if (cu_res != CUDA_SUCCESS) {
            LOG_ERROR("Unable to initialize CUDA (result: %d)\n", cu_res);
            return NVFBC_FALSE;
        }
        cuda_initialized = true;
    }

    cu_res = cu_device_get_ptr(&cu_dev, 0);
    if (cu_res != CUDA_SUCCESS) {
        LOG_ERROR("Unable to get CUDA device (result: %d)\n", cu_res);
        return NVFBC_FALSE;
    }

    cu_res = cu_ctx_create_v2_ptr(cuda_context, CU_CTX_SCHED_AUTO, cu_dev);
    if (cu_res != CUDA_SUCCESS) {
        LOG_ERROR("Unable to create CUDA context (result: %d)\n", cu_res);
        return NVFBC_FALSE;
    }

    if (!make_current) {
        cu_res = cu_ctx_pop_current_ptr(cuda_context);
        if (cu_res != CUDA_SUCCESS) {
            LOG_ERROR("Error popping back the created context (result: %d)\n", cu_res);
        }
    }

    return NVFBC_TRUE;
}

CUcontext* get_video_thread_cuda_context_ptr(void) {
    /*
        Return a pointer to the CUDA context that will be used next by the video thread.

        Returns:
            (CUcontext*): pointer to the video thread CUDA context
    */

    return &video_thread_cuda_context;
}
