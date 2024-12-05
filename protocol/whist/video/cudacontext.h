#ifndef CUDA_CONTEXT_H
#define CUDA_CONTEXT_H
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file cudacontext.h
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

#include "nvidia-linux/NvFBCUtils.h"
#include "nvidia-linux/cuda_drvapi_dynlink_cuda.h"
#include <whist/core/whist.h>

typedef CUresult (*CUCTXSETCURRENTPROC)(CUcontext ctx);
extern CUCTXSETCURRENTPROC cu_ctx_set_current_ptr;
typedef CUresult (*CUCTXGETCURRENTPROC)(CUcontext* pctx);
extern CUCTXGETCURRENTPROC cu_ctx_get_current_ptr;
typedef CUresult (*CUCTXPOPCURRENTPROC)(CUcontext* pctx);
extern CUCTXPOPCURRENTPROC cu_ctx_pop_current_ptr;
typedef CUresult (*CUCTXPUSHCURRENTPROC)(CUcontext ctx);
extern CUCTXPUSHCURRENTPROC cu_ctx_push_current_ptr;
typedef CUresult (*CUCTXSYNCHRONIZEPROC)(void);
extern CUCTXSYNCHRONIZEPROC cu_ctx_synchronize_ptr;
typedef CUresult (*CUCMEMALLOCPROC)(CUdeviceptr* dptr, size_t bytesize);
extern CUCMEMALLOCPROC cu_mem_alloc_ptr;
typedef CUresult (*CUMEMFREEPROC)(CUdeviceptr dptr);
extern CUMEMFREEPROC cu_mem_free_ptr;
typedef CUresult (*CUMEMCPYDTOHV2PROC)(void* dst_host, CUdeviceptr src_device, size_t byte_count);
extern CUMEMCPYDTOHV2PROC cu_memcpy_dtoh_v2_ptr;
typedef CUresult (*CUMEMCPY2DV2PROC)(const CUDA_MEMCPY2D* pCopy);
extern CUMEMCPY2DV2PROC cu_memcpy_2d_v2_ptr;

/*
============================
Public Functions
============================
*/

/**
 * Initializes CUDA and creates a CUDA context.
 *
 * \return
 *   NVFBC_TRUE in case of success, NVFBC_FALSE otherwise.
 */
NVFBC_BOOL cuda_init(CUcontext* cuda_context, bool make_current);

/**
 * @brief                          Returns a pointer to the CUDA context that will be used next by
 * the video thread
 *
 * @returns                        Pointer to the video thread CUDA context
 */
CUcontext* get_video_thread_cuda_context_ptr(void);

/**
 * @brief                           Destroys the CUDA context given.
 *
 * @param cuda_context_ptr          Pointer to the CUDA context to destroy. The context
 *                                  value will be set to NULL.
 *
 * @returns                         NVFBC_TRUE on success, NVFBC_FALSE otherwise.
 */
NVFBC_BOOL cuda_destroy(CUcontext* cuda_context_ptr);
#endif
