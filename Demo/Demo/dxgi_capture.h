// dxgi_capture.h - Tear-free screen capture using DXGI Desktop Duplication API.
// Captures the center WxH region of the primary monitor.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstdio>
#include <vector>
#include <d3d11.h>
#include <dxgi1_2.h>
#include "protocol.h"

#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

class DxgiCapture256 {
public:
    DxgiCapture256() {
        buffer_.resize(frame_bytes(CAPTURE_W, CAPTURE_H), 0);

        // Create D3D11 device (BGRA support required for DuplicateOutput)
        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        HRESULT hr = D3D11CreateDevice(
            nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
            levels, 2, D3D11_SDK_VERSION, &device_, nullptr, nullptr);
        if (FAILED(hr)) {
            printf("[DxgiCapture] D3D11CreateDevice failed (0x%08X)\n", hr);
            return;
        }

        // Get DXGI device -> adapter -> output
        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)))
            dxgiDev->GetAdapter(&adapter);
        if (dxgiDev) dxgiDev->Release();

        if (!adapter) {
            printf("[DxgiCapture] GetAdapter failed\n");
            return;
        }

        IDXGIOutput* output = nullptr;
        adapter->EnumOutputs(0, &output);
        adapter->Release();

        if (!output) {
            printf("[DxgiCapture] EnumOutputs(0) failed (no monitor?)\n");
            return;
        }

        // DuplicateOutput requires IDXGIOutput1
        IDXGIOutput1* output1 = nullptr;
        if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&output1))) {
            hr = output1->DuplicateOutput(device_, &duplication_);
            output1->Release();
        }
        output->Release();

        if (FAILED(hr) || !duplication_) {
            printf("[DxgiCapture] DuplicateOutput failed (0x%08X)\n", hr);
            printf("  Fall back to GDI BitBlt capture.\n");
            return;
        }

        // Get device context
        device_->GetImmediateContext(&context_);
        ready_ = true;
        printf("[DxgiCapture] DXGI Desktop Duplication ready (tear-free)\n");
    }

    ~DxgiCapture256() {
        close_duplication();
        if (staging_)   staging_->Release();
        if (context_)   context_->Release();
        if (device_)    device_->Release();
    }

    // Set the capture region size (centered on the primary monitor).
    void set_size(int w, int h) {
        if (w <= 0) w = CAPTURE_W;
        if (h <= 0) h = CAPTURE_H;
        captureW_ = w;
        captureH_ = h;
        buffer_.resize(frame_bytes(w, h), 0);
    }

    // Returns the internal BGRA buffer (center WxH of primary monitor).
    const uint8_t* capture() {
        if (!ready_) return buffer_.data();

        // Try to acquire the next desktop frame
        IDXGIResource* res = nullptr;
        DXGI_OUTDUPL_FRAME_INFO info;
        HRESULT hr = duplication_->AcquireNextFrame(0, &info, &res);

        if (hr == DXGI_ERROR_WAIT_TIMEOUT) {
            // No new frame yet — return the last captured frame
            return buffer_.data();
        }

        if (hr == DXGI_ERROR_ACCESS_LOST) {
            printf("[DxgiCapture] Access lost, reinitializing...\n");
            close_duplication();
            try_reinit();
            return buffer_.data();
        }

        if (FAILED(hr)) {
            duplication_->ReleaseFrame();
            return buffer_.data();
        }

        // Get the desktop texture
        ID3D11Texture2D* tex = nullptr;
        hr = res->QueryInterface(__uuidof(ID3D11Texture2D), (void**)&tex);
        res->Release();
        if (FAILED(hr)) { duplication_->ReleaseFrame(); return buffer_.data(); }

        D3D11_TEXTURE2D_DESC desc;
        tex->GetDesc(&desc);

        // (Re)create staging texture if desktop size changed
        if (!staging_ || stagingW_ != (int)desc.Width || stagingH_ != (int)desc.Height) {
            if (staging_) staging_->Release();
            staging_ = nullptr;
            D3D11_TEXTURE2D_DESC sd = desc;
            sd.Usage          = D3D11_USAGE_STAGING;
            sd.BindFlags      = 0;
            sd.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
            sd.MiscFlags      = 0;
            sd.MipLevels      = 1;
            sd.ArraySize      = 1;
            sd.SampleDesc.Count   = 1;
            sd.SampleDesc.Quality = 0;
            hr = device_->CreateTexture2D(&sd, nullptr, &staging_);
            if (FAILED(hr)) {
                tex->Release(); duplication_->ReleaseFrame();
                return buffer_.data();
            }
            stagingW_ = (int)desc.Width;
            stagingH_ = (int)desc.Height;
        }

        // Copy full desktop to staging, then release the DXGI frame
        context_->CopyResource(staging_, tex);
        tex->Release();
        duplication_->ReleaseFrame();

        // Map staging texture for CPU read
        D3D11_MAPPED_SUBRESOURCE map;
        hr = context_->Map(staging_, 0, D3D11_MAP_READ, 0, &map);
        if (FAILED(hr)) return buffer_.data();

        // Extract center region
        const int srcX = (stagingW_ - captureW_) / 2;
        const int srcY = (stagingH_ - captureH_) / 2;
        const uint8_t* src = (const uint8_t*)map.pData + srcY * map.RowPitch + srcX * 4;
        uint8_t* dst = buffer_.data();
        for (int y = 0; y < captureH_; ++y) {
            memcpy(dst, src, captureW_ * 4);
            src += map.RowPitch;
            dst += captureW_ * 4;
        }
        context_->Unmap(staging_, 0);
        return buffer_.data();
    }

    int size_bytes() const { return (int)buffer_.size(); }
    int width()  const { return captureW_; }
    int height() const { return captureH_; }

private:
    void close_duplication() {
        if (duplication_) { duplication_->Release(); duplication_ = nullptr; }
        if (staging_)     { staging_->Release();     staging_ = nullptr; }
        ready_ = false;
    }

    void try_reinit() {
        // Recreate D3D device and duplication
        if (device_)  { device_->Release();  device_ = nullptr; }
        if (context_) { context_->Release(); context_ = nullptr; }

        UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
        D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0 };
        if (FAILED(D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags,
                                     levels, 2, D3D11_SDK_VERSION, &device_, nullptr, nullptr)))
            return;
        device_->GetImmediateContext(&context_);

        IDXGIDevice* dxgiDev = nullptr;
        IDXGIAdapter* adapter = nullptr;
        if (SUCCEEDED(device_->QueryInterface(__uuidof(IDXGIDevice), (void**)&dxgiDev)))
            dxgiDev->GetAdapter(&adapter);
        if (dxgiDev) dxgiDev->Release();
        if (!adapter) return;

        IDXGIOutput* output = nullptr;
        adapter->EnumOutputs(0, &output);
        adapter->Release();
        if (!output) return;

        IDXGIOutput1* out1 = nullptr;
        if (SUCCEEDED(output->QueryInterface(__uuidof(IDXGIOutput1), (void**)&out1))) {
            if (SUCCEEDED(out1->DuplicateOutput(device_, &duplication_))) ready_ = true;
            out1->Release();
        }
        output->Release();
    }

    bool    ready_ = false;
    int     stagingW_ = 0;
    int     stagingH_ = 0;
    int     captureW_ = CAPTURE_W;
    int     captureH_ = CAPTURE_H;
    std::vector<uint8_t> buffer_;

    ID3D11Device*           device_      = nullptr;
    ID3D11DeviceContext*    context_     = nullptr;
    IDXGIOutputDuplication* duplication_ = nullptr;
    ID3D11Texture2D*        staging_     = nullptr;
};