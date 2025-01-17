#pragma once
/**
 * Copyright (c) 2021-2022 Whist Technologies, Inc.
 * @file gf256_common.h
 * @brief some common function taken out from gf256.h, in order to fix the SIMD fallback, and make the code clearer
 */

/** \file
    \brief GF(256) Main C API Header
    \copyright Copyright (c) 2017 Christopher A. Taylor.  All rights reserved.

    Redistribution and use in source and binary forms, with or without
    modification, are permitted provided that the following conditions are met:

    * Redistributions of source code must retain the above copyright notice,
      this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright notice,
      this list of conditions and the following disclaimer in the documentation
      and/or other materials provided with the distribution.
    * Neither the name of GF256 nor the names of its contributors may be
      used to endorse or promote products derived from this software without
      specific prior written permission.

    THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
    AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
    IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
    ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
    LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
    CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
    SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
    INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
    CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
    ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
    POSSIBILITY OF SUCH DAMAGE.
*/

#include <stdint.h> // uint32_t etc
#include <string.h> // memcpy, memset

/// Library header version
#define GF256_VERSION 2

//------------------------------------------------------------------------------
// Platform/Architecture

// WHIST_CHANGE
#if defined(__ARM_ARCH) || defined(__ARM_NEON) || defined(__ARM_NEON__)
    #if !defined IOS
        #if defined(__APPLE__)
            #define MACOS_ARM
        #else
            #define LINUX_ARM
        #endif
    #endif
#endif

// WHIST_CHANGE
#if defined(ANDROID) || defined(IOS) || defined(LINUX_ARM) || defined(__powerpc__) || defined(__s390__) || defined(MACOS_ARM)
    #define GF256_TARGET_MOBILE
#endif // ANDROID


#if !defined(GF256_TARGET_MOBILE)
    #define GF256_ALIGN_BYTES 32
#else
    #define GF256_ALIGN_BYTES 16
#endif

// old MSVC doesn't have "immintrin.h", generate an error
#if defined(_MSC_VER) && (_MSC_VER < 1900)
    #error "MSVC version < 1900, too old to support <immintrin.h>"
#endif

#if !defined(GF256_TARGET_MOBILE)
    #include <immintrin.h> // AVX2
    #include <tmmintrin.h> // SSSE3: _mm_shuffle_epi8
    #include <emmintrin.h> // SSE2
#else // GF256_TARGET_MOBILE
    #include <arm_neon.h>
#endif

// Compiler-specific 128-bit SIMD register keyword
#if defined(GF256_TARGET_MOBILE)
    #define GF256_M128 uint8x16_t
#else // GF256_TARGET_MOBILE
    #define GF256_M128 __m128i
#endif // GF256_TARGET_MOBILE

// Compiler-specific 256-bit SIMD register keyword
#if !defined(GF256_TARGET_MOBILE)
    #define GF256_M256 __m256i
#endif

// Compiler-specific C++11 restrict keyword
#define GF256_RESTRICT __restrict

// Compiler-specific force inline keyword
#ifdef _MSC_VER
    #define GF256_FORCE_INLINE inline __forceinline
#else
    #define GF256_FORCE_INLINE inline __attribute__((always_inline))
#endif

// Compiler-specific alignment keyword
// Note: Alignment only matters for ARM NEON where it should be 16
#ifdef _MSC_VER
    #define GF256_ALIGNED __declspec(align(GF256_ALIGN_BYTES))
#else // _MSC_VER
    #define GF256_ALIGNED __attribute__((aligned(GF256_ALIGN_BYTES)))
#endif // _MSC_VER

#ifdef __cplusplus
extern "C" {
#endif // __cplusplus


//------------------------------------------------------------------------------
// Portability

/// Swap two memory buffers in-place
extern void gf256_memswap(void * GF256_RESTRICT vx, void * GF256_RESTRICT vy, int bytes);


//------------------------------------------------------------------------------
// GF(256) Context

#ifdef _MSC_VER
    #pragma warning(push)
    #pragma warning(disable: 4324) // warning C4324: 'gf256_ctx' : structure was padded due to __declspec(align())
#endif // _MSC_VER

/// The context object stores tables required to perform library calculations
typedef struct
{
    /// We require memory to be aligned since the SIMD instructions benefit from
    /// or require aligned accesses to the table data.
    struct
    {
        GF256_ALIGNED GF256_M128 TABLE_LO_Y[256];
        GF256_ALIGNED GF256_M128 TABLE_HI_Y[256];
    } MM128;

#if !defined(GF256_TARGET_MOBILE)
    struct
    {
        GF256_ALIGNED GF256_M256 TABLE_LO_Y[256];
        GF256_ALIGNED GF256_M256 TABLE_HI_Y[256];
    } MM256;
#endif

    /// Mul/Div/Inv/Sqr tables
    uint8_t GF256_MUL_TABLE[256 * 256];
    uint8_t GF256_DIV_TABLE[256 * 256];
    uint8_t GF256_INV_TABLE[256];
    uint8_t GF256_SQR_TABLE[256];

    /// Log/Exp tables
    uint16_t GF256_LOG_TABLE[256];
    uint8_t GF256_EXP_TABLE[512 * 2 + 1];

    /// Polynomial used
    unsigned Polynomial;
} gf256_ctx;

#ifdef _MSC_VER
    #pragma warning(pop)
#endif // _MSC_VER

extern gf256_ctx GF256Ctx;

#ifdef __cplusplus
}
#endif // __cplusplus
