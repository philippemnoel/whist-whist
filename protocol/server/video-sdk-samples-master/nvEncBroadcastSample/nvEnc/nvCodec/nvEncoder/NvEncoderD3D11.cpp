/*
* Copyright (c) 2018, NVIDIA CORPORATION. All rights reserved.
*
* Permission is hereby granted, free of charge, to any person obtaining a
* copy of this software and associated documentation files (the "Software"),
* to deal in the Software without restriction, including without limitation
* the rights to use, copy, modify, merge, publish, distribute, sublicense,
* and/or sell copies of the Software, and to permit persons to whom the
* Software is furnished to do so, subject to the following conditions:
*
* The above copyright notice and this permission notice shall be included in
* all copies or substantial portions of the Software.
*
* THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
* IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
* FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
* THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
* LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
* FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
* DEALINGS IN THE SOFTWARE.
*/

#ifndef WIN32
#include <dlfcn.h>
#endif
#include "NvEncoder/NvEncoderD3D11.h"
#include <D3D9Types.h>

#ifndef MAKEFOURCC
#define MAKEFOURCC(a,b,c,d) (((unsigned int)a) | (((unsigned int)b)<< 8) | (((unsigned int)c)<<16) | (((unsigned int)d)<<24) )
#endif

DXGI_FORMAT GetD3D11Format(NV_ENC_BUFFER_FORMAT eBufferFormat)
{
    switch (eBufferFormat)
    {
    case NV_ENC_BUFFER_FORMAT_NV12:
        return DXGI_FORMAT_NV12;
    case NV_ENC_BUFFER_FORMAT_ARGB:
        return DXGI_FORMAT_B8G8R8A8_UNORM;
    case NV_ENC_BUFFER_FORMAT_ABGR:
        return DXGI_FORMAT_R8G8B8A8_UNORM;
    default:
        return DXGI_FORMAT_UNKNOWN;
    }
}

NvEncoderD3D11::NvEncoderD3D11(ID3D11Device* pD3D11Device, uint32_t nWidth, uint32_t nHeight,
    NV_ENC_BUFFER_FORMAT eBufferFormat,  uint32_t nExtraOutputDelay, bool bMotionEstimationOnly) :
    NvEncoder(NV_ENC_DEVICE_TYPE_DIRECTX, pD3D11Device, nWidth, nHeight, eBufferFormat, nExtraOutputDelay, bMotionEstimationOnly)
{
    std::cout << "Nv 1" << std::endl;
    if (!pD3D11Device)
    {
        std::cout << "Nv 1.1" << std::endl;
        NVENC_THROW_ERROR("Bad d3d11device ptr", NV_ENC_ERR_INVALID_PTR);
        return;
    }

    if (GetD3D11Format(GetPixelFormat()) == DXGI_FORMAT_UNKNOWN)
    {
        std::cout << "Nv 1.2" << std::endl;
        NVENC_THROW_ERROR("Unsupported Buffer format", NV_ENC_ERR_INVALID_PARAM);
    }

    if (!m_hEncoder)
    {
        std::cout << "Nv 1.3" << std::endl;
        NVENC_THROW_ERROR("Encoder Initialization failed", NV_ENC_ERR_INVALID_DEVICE);
    }

    std::cout << "Nv 1.3.1" << std::endl;
    m_pD3D11Device = pD3D11Device;
    m_pD3D11Device->AddRef();
    std::cout << "Nv 1.4" << std::endl;
    m_pD3D11Device->GetImmediateContext(&m_pD3D11DeviceContext);
    std::cout << "Nv 2" << std::endl;
}

NvEncoderD3D11::~NvEncoderD3D11() 
{
    ReleaseD3D11Resources();
}

void NvEncoderD3D11::AllocateInputBuffers(int32_t numInputBuffers)
{
    if (!IsHWEncoderInitialized())
    {
        NVENC_THROW_ERROR("Encoder intialization failed", NV_ENC_ERR_ENCODER_NOT_INITIALIZED);
    }

    // for MEOnly mode we need to allocate seperate set of buffers for reference frame
    int numCount = m_bMotionEstimationOnly ? 2 : 1;
    for (int count = 0; count < numCount; count++)
    {
        std::vector<void*> inputFrames;
        for (int i = 0; i < numInputBuffers; i++)
        {
            ID3D11Texture2D *pInputTextures = NULL;
            D3D11_TEXTURE2D_DESC desc;
            ZeroMemory(&desc, sizeof(D3D11_TEXTURE2D_DESC));
            desc.Width = GetMaxEncodeWidth();
            desc.Height = GetMaxEncodeHeight();
            desc.MipLevels = 1;
            desc.ArraySize = 1;
            desc.Format = GetD3D11Format(GetPixelFormat());
            desc.SampleDesc.Count = 1;
            desc.Usage = D3D11_USAGE_DEFAULT;
            desc.BindFlags = D3D11_BIND_RENDER_TARGET;
            desc.CPUAccessFlags = 0;
            if (m_pD3D11Device->CreateTexture2D(&desc, NULL, &pInputTextures) != S_OK)
            {
                NVENC_THROW_ERROR("Failed to create d3d11textures", NV_ENC_ERR_OUT_OF_MEMORY);
            }
            inputFrames.push_back(pInputTextures);
        }
        RegisterResources(inputFrames, NV_ENC_INPUT_RESOURCE_TYPE_DIRECTX, 
            GetMaxEncodeWidth(), GetMaxEncodeHeight(), 0, GetPixelFormat(), count == 1 ? true : false);
    }
}

void NvEncoderD3D11::ReleaseInputBuffers()
{
    ReleaseD3D11Resources();
}

void NvEncoderD3D11::ReleaseD3D11Resources()
{
    if (!m_hEncoder)
    {
        return;
    }

    UnregisterResources();

    if (m_bOwnInputFrames)
    {
        for (uint32_t i = 0; i < m_vInputFrames.size(); ++i)
        {
            if (m_vInputFrames[i].inputPtr)
            {
                reinterpret_cast<ID3D11Texture2D*>(m_vInputFrames[i].inputPtr)->Release();
            }
        }
    }
    m_vInputFrames.clear();

    for (uint32_t i = 0; i < m_vReferenceFrames.size(); ++i)
    {
        if (m_vReferenceFrames[i].inputPtr)
        {
            reinterpret_cast<ID3D11Texture2D*>(m_vReferenceFrames[i].inputPtr)->Release();
        }
    }
    m_vReferenceFrames.clear();

    if (m_pD3D11DeviceContext)
    {
        m_pD3D11DeviceContext->Release();
        m_pD3D11DeviceContext = nullptr;
    }

    if (m_pD3D11Device)
    {
        m_pD3D11Device->Release();
        m_pD3D11Device = nullptr;
    }
}

