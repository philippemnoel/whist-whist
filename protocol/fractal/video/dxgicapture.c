/*
 * GPU screen capture on Windows.
 *
 * Copyright Fractal Computers, Inc. 2020
 **/
#include "dxgicapture.h"

#include "dxgicudacapturetransfer.h"

#include <windows.h>

// To link IID_'s
#pragma comment(lib, "dxguid.lib")

void get_bitmap_screenshot(CaptureDevice* device);
ID3D11Texture2D* create_texture(CaptureDevice* device);

#define USE_GPU 0
#define USE_MONITOR 0

int create_capture_device(CaptureDevice* device, UINT width, UINT height, UINT dpi, int bitrate,
                          CodecType codec) {
    // tech debt: don't ignore dpi
    UNUSED(dpi);
    UNUSED(bitrate);
    UNUSED(codec);

    LOG_INFO("Creating capture device for resolution %dx%d...", width, height);
    memset(device, 0, sizeof(CaptureDevice));

    device->hardware = (DisplayHardware*)safe_malloc(sizeof(DisplayHardware));
    memset(device->hardware, 0, sizeof(DisplayHardware));

    DisplayHardware* hardware = device->hardware;

    int num_adapters = 0, num_outputs = 0, i = 0, j = 0;
    IDXGIFactory1* factory;

#define MAX_NUM_ADAPTERS 10
#define MAX_NUM_OUTPUTS 10
    IDXGIOutput* outputs[MAX_NUM_OUTPUTS];
    IDXGIAdapter1* adapters[MAX_NUM_ADAPTERS];
    DXGI_OUTPUT_DESC output_desc = {0};

    HRESULT hr = CreateDXGIFactory1(&IID_IDXGIFactory1, (void**)(&factory));
    if (FAILED(hr)) {
        LOG_WARNING("Failed CreateDXGIFactory1: 0x%X %d", hr, GetLastError());
        return -1;
    }

    // GET ALL GPUS
    while (factory->lpVtbl->EnumAdapters1(factory, num_adapters, &hardware->adapter) !=
           DXGI_ERROR_NOT_FOUND) {
        if (num_adapters == MAX_NUM_ADAPTERS) {
            LOG_WARNING("Too many adapters!");
            break;
        }
        adapters[num_adapters] = hardware->adapter;
        ++num_adapters;
    }

    // GET GPU DESCRIPTIONS
    for (i = 0; i < num_adapters; i++) {
        DXGI_ADAPTER_DESC1 desc;
        hardware->adapter = adapters[i];
        hr = hardware->adapter->lpVtbl->GetDesc1(hardware->adapter, &desc);
        LOG_WARNING("Adapter %d: %S", i, desc.Description);
    }

    // Set used GPU
    if (USE_GPU >= num_adapters) {
        LOG_WARNING("No GPU with ID %d, only %d adapters", USE_GPU, num_adapters);
        return -1;
    }
    hardware->adapter = adapters[USE_GPU];

    LOG_INFO("Monitor Info:");

    // GET ALL MONITORS
    for (i = 0; i < num_adapters; i++) {
        for (j = 0; adapters[i]->lpVtbl->EnumOutputs(adapters[i], j, &hardware->output) !=
                    DXGI_ERROR_NOT_FOUND;
             j++) {
            DXGI_OUTPUT_DESC desc;
            hr = hardware->output->lpVtbl->GetDesc(hardware->output, &desc);
            LOG_INFO("  Found monitor %d on adapter %lu. Monitor %d named %S", j, i, j,
                     desc.DeviceName);
            if (i == USE_GPU) {
                if (j == MAX_NUM_OUTPUTS) {
                    LOG_WARNING("  Too many adapters on adapter %lu!", i);
                    break;
                } else {
                    outputs[j] = hardware->output;
                    num_outputs++;
                }
            }
        }
    }

    // GET MONITOR DESCRIPTIONS
    for (i = 0; i < num_outputs; i++) {
        hardware->output = outputs[i];
        hr = hardware->output->lpVtbl->GetDesc(hardware->output, &output_desc);
        // mprintf("Monitor %d: %s\n", i, output_desc.DeviceName);
    }

    // Set used output
    if (USE_MONITOR >= num_outputs) {
        LOG_WARNING("No Monitor with ID %d, only %d adapters", USE_MONITOR, num_outputs);
        return -1;
    }
    hardware->output = outputs[USE_MONITOR];

    // GET LIST OF VALID RESOLUTIONS
    UINT num_display_modes = 0;
    DXGI_FORMAT format = DXGI_FORMAT_R8G8B8A8_UNORM;
    UINT flags = 0;
    hr = hardware->output->lpVtbl->GetDisplayModeList(hardware->output, format, flags,
                                                      &num_display_modes, 0);
    if (FAILED(hr)) {
        LOG_WARNING("Could not GetDisplayModeList: %X", hr);
    }

    DXGI_MODE_DESC* p_descs = safe_malloc(sizeof(DXGI_MODE_DESC) * num_display_modes);
    hardware->output->lpVtbl->GetDisplayModeList(hardware->output, format, flags,
                                                 &num_display_modes, p_descs);
    if (FAILED(hr)) {
        LOG_WARNING("Could not GetDisplayModeList: %X", hr);
    }

    double ratio_closeness = 100.0;
    UINT set_width = 0;
    UINT set_height = 0;
    LOG_INFO("Target Resolution: %dx%d", width, height);
    LOG_INFO("Number of display modes: %d", num_display_modes);
    for (UINT k = 0; k < num_display_modes; k++) {
        double current_ratio_closeness =
            fabs(1.0 * p_descs[k].Width / p_descs[k].Height - 1.0 * width / height) + 0.001;
        ratio_closeness = min(ratio_closeness, current_ratio_closeness);

        if (p_descs[k].Width == width && p_descs[k].Height == height) {
            LOG_INFO("Exact resolution found!");
            set_width = p_descs[k].Width;
            set_height = p_descs[k].Height;
            ratio_closeness = 0.0;
            LOG_INFO("FPS: %d/%d", p_descs[k].RefreshRate.Numerator,
                     p_descs[k].RefreshRate.Denominator);
        }
    }

    for (UINT k = 0; k < num_display_modes && ratio_closeness > 0.0; k++) {
        LOG_INFO("Possible Resolution: %dx%d", p_descs[k].Width, p_descs[k].Height);

        double current_ratio_closeness =
            fabs(1.0 * p_descs[k].Width / p_descs[k].Height - 1.0 * width / height) + 0.001;
        if (fabs(current_ratio_closeness - ratio_closeness) / ratio_closeness < 0.01) {
            LOG_INFO("Ratio match found with %dx%d!", p_descs[k].Width, p_descs[k].Height);
            LOG_INFO("FPS: %d/%d", p_descs[k].RefreshRate.Numerator,
                     p_descs[k].RefreshRate.Denominator);

            if (set_width == 0) {
                LOG_INFO("Will try using this resolution");
                set_width = p_descs[k].Width;
                set_height = p_descs[k].Height;
            }

            // We'd prefer a higher resolution if possible, if the current
            // resolution still isn't high enough
            if (set_width < p_descs[k].Width && set_width < width) {
                LOG_INFO("This resolution is higher, let's use it");
                set_width = p_descs[k].Width;
                set_height = p_descs[k].Height;
            }

            // We'd prefer a lower resolution if possible, if the potential
            // resolution is indeed high enough
            if (p_descs[k].Width < set_width && width < p_descs[k].Width) {
                LOG_INFO("This resolution is lower, let's use it");
                set_width = p_descs[k].Width;
                set_height = p_descs[k].Height;
            }
        }
    }

    // Update target width and height
    width = set_width;
    height = set_height;
    LOG_INFO("Found Resolution: %dx%d", width, height);

    free(p_descs);

    HMONITOR h_monitor = output_desc.Monitor;
    MONITORINFOEXW monitor_info;
    monitor_info.cbSize = sizeof(MONITORINFOEXW);
    GetMonitorInfoW(h_monitor, (LPMONITORINFO)&monitor_info);
    device->monitorInfo = monitor_info;

    DEVMODE dm;
    memset(&dm, 0, sizeof(dm));
    dm.dmSize = sizeof(dm);
    LOG_INFO("Device Name: %S", monitor_info.szDevice);
    if (0 != EnumDisplaySettingsW(monitor_info.szDevice, ENUM_CURRENT_SETTINGS, &dm)) {
        if (dm.dmPelsWidth != width || dm.dmPelsHeight != height) {
            dm.dmPelsWidth = width;
            dm.dmPelsHeight = height;
            dm.dmFields = DM_PELSWIDTH | DM_PELSHEIGHT;
            // dm.dmDisplayFrequency =

            int ret = ChangeDisplaySettingsExW(monitor_info.szDevice, &dm, NULL,
                                               CDS_SET_PRIMARY | CDS_UPDATEREGISTRY, 0);
            LOG_INFO("ChangeDisplaySettingsCode: %d", ret);
        }
    } else {
        LOG_WARNING("Failed to update DisplaySettings");
    }

    hr = D3D11CreateDevice((IDXGIAdapter*)hardware->adapter, D3D_DRIVER_TYPE_UNKNOWN, NULL, 0, NULL,
                           0, D3D11_SDK_VERSION, &device->D3D11device,
                           NULL,  // implicit D3D Feature Array 11, 10.1, 10, 9
                           &device->D3D11context);

    if (FAILED(hr)) {
        LOG_ERROR("Failed D3D11CreateDevice: 0x%X %d", hr, GetLastError());
        return -1;
    }

    IDXGIOutput1* output1;

    hr = hardware->output->lpVtbl->QueryInterface(hardware->output, &IID_IDXGIOutput1,
                                                  (void**)&output1);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to query interface of output: 0x%X %d", hr, GetLastError());
        return -1;
    }
    hr = output1->lpVtbl->DuplicateOutput(output1, (IUnknown*)device->D3D11device,
                                          &device->duplication);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to duplicate output: 0x%X %d", hr, GetLastError());
        return -1;
    }
    hr = hardware->output->lpVtbl->GetDesc(hardware->output, &hardware->final_output_desc);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to getdesc of output: 0x%X %d", hr, GetLastError());
        return -1;
    }

    if (hardware->final_output_desc.DesktopCoordinates.left != 0) {
        LOG_ERROR("final_output_desc left found: %d",
                  hardware->final_output_desc.DesktopCoordinates.left);
    }

    if (hardware->final_output_desc.DesktopCoordinates.top != 0) {
        LOG_ERROR("final_output_desc top found: %d",
                  hardware->final_output_desc.DesktopCoordinates.top);
    }

    device->width = hardware->final_output_desc.DesktopCoordinates.right;
    device->height = hardware->final_output_desc.DesktopCoordinates.bottom;

    device->released = true;

    get_bitmap_screenshot(device);

    device->screenshot.staging_texture = create_texture(device);

    device->using_nvidia = false;
    device->dxgi_cuda_available = false;

    return 0;
}

void get_bitmap_screenshot(CaptureDevice* device) {
    HDC h_screen_dc = CreateDCW(device->monitorInfo.szDevice, NULL, NULL, NULL);
    HDC h_memory_dc = CreateCompatibleDC(h_screen_dc);

    HBITMAP h_bitmap = CreateCompatibleBitmap(h_screen_dc, device->width, device->height);
    HBITMAP h_old_bitmap = (HBITMAP)SelectObject(h_memory_dc, h_bitmap);

    BitBlt(h_memory_dc, 0, 0, device->width, device->height, h_screen_dc, 0, 0, SRCCOPY);
    h_bitmap = (HBITMAP)SelectObject(h_memory_dc, h_old_bitmap);

    DeleteDC(h_memory_dc);
    DeleteDC(h_screen_dc);

    int bitmap_size = 10000000;
    if (!device->bitmap) {
        device->bitmap = safe_malloc(bitmap_size);
    }
    GetBitmapBits(h_bitmap, bitmap_size, device->bitmap);

    DeleteObject(h_bitmap);

    device->frame_data = device->bitmap;
    device->pitch = device->width * 4;
    device->texture_on_gpu = false;
}

ID3D11Texture2D* create_texture(CaptureDevice* device) {
    HRESULT hr;

    DisplayHardware* hardware = device->hardware;

    D3D11_TEXTURE2D_DESC t_desc;

    // Texture to store GPU pixels
    t_desc.Width = hardware->final_output_desc.DesktopCoordinates.right;
    t_desc.Height = hardware->final_output_desc.DesktopCoordinates.bottom;
    t_desc.MipLevels = 1;
    t_desc.ArraySize = 1;
    t_desc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
    t_desc.SampleDesc.Count = 1;
    t_desc.SampleDesc.Quality = 0;
    t_desc.Usage = D3D11_USAGE_STAGING;
    t_desc.BindFlags = 0;
    t_desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    t_desc.MiscFlags = 0;

    device->Box.top = hardware->final_output_desc.DesktopCoordinates.top;
    device->Box.left = hardware->final_output_desc.DesktopCoordinates.left;
    device->Box.right = hardware->final_output_desc.DesktopCoordinates.right;
    device->Box.bottom = hardware->final_output_desc.DesktopCoordinates.bottom;
    device->Box.front = 0;
    device->Box.back = 1;

    ID3D11Texture2D* texture;
    hr = device->D3D11device->lpVtbl->CreateTexture2D(device->D3D11device, &t_desc, NULL, &texture);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to create Texture2D 0x%X %d", hr, GetLastError());
        return NULL;
    }

    device->duplication->lpVtbl->GetDesc(device->duplication, &device->duplication_desc);

    return texture;
}

void release_screenshot(ScreenshotContainer* screenshot) {
    if (screenshot->final_texture != NULL) {
        screenshot->final_texture->lpVtbl->Release(screenshot->final_texture);
        screenshot->final_texture = NULL;
    }

    if (screenshot->desktop_resource != NULL) {
        screenshot->desktop_resource->lpVtbl->Release(screenshot->desktop_resource);
        screenshot->desktop_resource = NULL;
    }

    if (screenshot->surface != NULL) {
        screenshot->surface->lpVtbl->Release(screenshot->surface);
        screenshot->surface = NULL;
    }
}

int capture_screen(CaptureDevice* device) {
    release_screen(device);

    HRESULT hr;

    ScreenshotContainer* screenshot = &device->screenshot;

    hr = device->duplication->lpVtbl->ReleaseFrame(device->duplication);

    IDXGIResource* desktop_resource;
    hr = device->duplication->lpVtbl->AcquireNextFrame(device->duplication, 1, &device->frame_info,
                                                       &desktop_resource);

    if (FAILED(hr)) {
        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            return 0;
        } else if (hr == DXGI_ERROR_ACCESS_LOST || hr == DXGI_ERROR_INVALID_CALL) {
            LOG_WARNING(
                "CaptureScreen returned DXGI_ERROR_ACCESS_LOST or "
                "DXGI_ERROR_INVALID_CALL (0x%X)! Recreating device",
                hr);
            SDL_Delay(1);
            return -1;
        } else {
            LOG_ERROR("Failed to Acquire Next Frame! 0x%X %d", hr, GetLastError());
            return -1;
        }
    }

    release_screenshot(screenshot);
    screenshot->desktop_resource = desktop_resource;

    hr = screenshot->desktop_resource->lpVtbl->QueryInterface(
        screenshot->desktop_resource, &IID_ID3D11Texture2D, (void**)&screenshot->final_texture);

    if (FAILED(hr)) {
        LOG_WARNING("Query Interface Failed!");
        return -1;
    }

    int accumulated_frames = device->frame_info.AccumulatedFrames;

    if (accumulated_frames > 0 && device->bitmap) {
        free(device->bitmap);
        device->bitmap = NULL;
        device->texture_on_gpu = true;
    }

    device->D3D11context->lpVtbl->CopySubresourceRegion(
        device->D3D11context, (ID3D11Resource*)screenshot->staging_texture, 0, 0, 0, 0,
        (ID3D11Resource*)screenshot->final_texture, 0, &device->Box);

    return accumulated_frames;
}

int transfer_screen(CaptureDevice* device) {
    HRESULT hr;
    ScreenshotContainer* screenshot = &device->screenshot;

    hr = screenshot->staging_texture->lpVtbl->QueryInterface(
        screenshot->staging_texture, &IID_IDXGISurface, (void**)&screenshot->surface);
    if (FAILED(hr)) {
        LOG_ERROR("Query Interface Failed! 0x%X %d", hr, GetLastError());
        return -1;
    }

    hr = screenshot->surface->lpVtbl->Map(screenshot->surface, &screenshot->mapped_rect,
                                          DXGI_MAP_READ);

    if (FAILED(hr)) {
        LOG_ERROR("Map Failed!");
        return -1;
    }

    if (!device->bitmap) {
        device->frame_data = (char*)screenshot->mapped_rect.pBits;
        device->pitch = screenshot->mapped_rect.Pitch;
    }

    device->released = false;
    return 0;
}

void release_screen(CaptureDevice* device) {
    if (device->released) {
        return;
    }
    HRESULT hr;

    ScreenshotContainer* screenshot = &device->screenshot;
    hr = screenshot->surface->lpVtbl->Unmap(screenshot->surface);
    if (FAILED(hr)) {
        LOG_ERROR("Failed to unmap screenshot surface 0x%X %d", hr, GetLastError());
    }

    device->released = true;
}

void destroy_capture_device(CaptureDevice* device) {
    // need to reinitialize this, so close it
    dxgi_cuda_close_transfer_context();

    HRESULT hr;

    hr = device->duplication->lpVtbl->ReleaseFrame(device->duplication);

    release_screenshot(&device->screenshot);

    if (device->screenshot.staging_texture != NULL) {
        device->screenshot.staging_texture->lpVtbl->Release(device->screenshot.staging_texture);
        device->screenshot.staging_texture = NULL;
    }

    if (device->duplication) {
        device->duplication->lpVtbl->Release(device->duplication);
        device->duplication = NULL;
    }
    if (device->hardware) {
        free(device->hardware);
        device->hardware = NULL;
    }
}

void update_capture_encoder(CaptureDevice* device, int bitrate, CodecType codec) {
    UNUSED(device);
    UNUSED(bitrate);
    UNUSED(codec);
}
