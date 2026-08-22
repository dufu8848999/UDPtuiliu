// screen_capture.h - GDI BitBlt capture of screen-center 256x256 region
// Latency < 1ms. For Win10+ replace with DXGI Desktop Duplication to lower CPU.
#pragma once
#include <windows.h>
#include <cstdint>
#include "protocol.h"

class ScreenCapture256 {
public:
    ScreenCapture256() {
        hDesktopDC_ = GetDC(nullptr);
        hMemDC_     = CreateCompatibleDC(hDesktopDC_);
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = CAPTURE_W;
        bmi.bmiHeader.biHeight      = -CAPTURE_H;   // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;            // BGRA
        bmi.bmiHeader.biCompression = BI_RGB;
        hBitmap_ = CreateDIBSection(hMemDC_, &bmi, DIB_RGB_COLORS,
                                    reinterpret_cast<void**>(&pixels_), nullptr, 0);
        old_bmp_ = static_cast<HBITMAP>(SelectObject(hMemDC_, hBitmap_));
    }

    ~ScreenCapture256() {
        SelectObject(hMemDC_, old_bmp_);
        DeleteObject(hBitmap_);
        DeleteDC(hMemDC_);
        ReleaseDC(nullptr, hDesktopDC_);
    }

    // Capture center 256x256. Returns internal BGRA buffer (do NOT free).
    const uint8_t* capture() {
        const int screen_w = GetSystemMetrics(SM_CXSCREEN);
        const int screen_h = GetSystemMetrics(SM_CYSCREEN);
        const int src_x    = (screen_w - CAPTURE_W) / 2;
        const int src_y    = (screen_h - CAPTURE_H) / 2;
        BitBlt(hMemDC_, 0, 0, CAPTURE_W, CAPTURE_H,
               hDesktopDC_, src_x, src_y, SRCCOPY | CAPTUREBLT);
        GdiFlush();
        return pixels_;
    }

    static constexpr int bytes() { return RAW_BGR_SIZE; }

private:
    HDC     hDesktopDC_ = nullptr;
    HDC     hMemDC_     = nullptr;
    HBITMAP hBitmap_    = nullptr;
    HGDIOBJ old_bmp_    = nullptr;
    uint8_t* pixels_    = nullptr;
};
