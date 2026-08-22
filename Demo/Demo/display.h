// display.h - Receiver display with GDI double-buffering.
// Uses CreateDIBSection + BitBlt to avoid tearing.
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>
#include "protocol.h"

class Display256 {
public:
    bool attach(HWND hWnd, int w = CAPTURE_W, int h = CAPTURE_H) {
        w_ = w;
        h_ = h;
        hWnd_ = hWnd;
        hDC_  = GetDC(hWnd);
        if (!hDC_) return false;

        // Create a memory DC for double-buffering
        memDC_ = CreateCompatibleDC(hDC_);
        if (!memDC_) return false;

        // Create a DIB section (system memory bitmap) as the backbuffer
        BITMAPINFO bmi{};
        bmi.bmiHeader.biSize        = sizeof(BITMAPINFOHEADER);
        bmi.bmiHeader.biWidth       = w_;
        bmi.bmiHeader.biHeight      = -h_;   // top-down
        bmi.bmiHeader.biPlanes      = 1;
        bmi.bmiHeader.biBitCount    = 32;
        bmi.bmiHeader.biCompression = BI_RGB;

        dib_ = CreateDIBSection(memDC_, &bmi, DIB_RGB_COLORS, &dibBits_, nullptr, 0);
        if (!dib_) return false;

        // Select the DIB into the memory DC
        oldBmp_ = (HBITMAP)SelectObject(memDC_, dib_);
        if (!oldBmp_ || oldBmp_ == (HBITMAP)GDI_ERROR) return false;

        bmpBytes_ = static_cast<size_t>(w_) * h_ * 4;
        return true;
    }

    ~Display256() {
        if (memDC_ && oldBmp_) SelectObject(memDC_, oldBmp_);
        if (memDC_) DeleteDC(memDC_);
        if (dib_)   DeleteObject(dib_);
        if (hDC_)   ReleaseDC(hWnd_, hDC_);
    }

    // Copy frame into the DIB section and mark window for repaint.
    void present(const uint8_t* bgra_data) {
        memcpy(dibBits_, bgra_data, bmpBytes_);
        InvalidateRect(hWnd_, nullptr, FALSE);
    }

    // Called from WM_PAINT handler. Fast BitBlt from the offscreen DC.
    void on_paint() {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hWnd_, &ps);
        BitBlt(hdc, 0, 0, w_, h_, memDC_, 0, 0, SRCCOPY);
        EndPaint(hWnd_, &ps);
    }

private:
    HWND    hWnd_    = nullptr;
    int     w_       = CAPTURE_W;
    int     h_       = CAPTURE_H;
    HDC     hDC_     = nullptr;   // window DC (owned)
    HDC     memDC_   = nullptr;   // offscreen DC
    HBITMAP dib_     = nullptr;   // DIB section (backbuffer bitmap)
    HBITMAP oldBmp_  = nullptr;   // saved old bitmap for SelectObject restore
    void*   dibBits_ = nullptr;   // pointer to the DIB pixel buffer
    size_t  bmpBytes_= 0;         // size of the pixel buffer in bytes
};

// Window procedure: handles WM_PAINT by delegating to Display256.
static LRESULT CALLBACK sc256_wndproc(HWND hWnd, UINT msg,
                                      WPARAM wParam, LPARAM lParam) {
    if (msg == WM_PAINT) {
        auto* disp = reinterpret_cast<Display256*>(
            GetWindowLongPtrW(hWnd, GWLP_USERDATA));
        if (disp) disp->on_paint();
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

// Create a window whose client area is exactly w x h.
// Stores Display256 pointer in GWLP_USERDATA for WM_PAINT.
inline HWND create_256_window(HINSTANCE hInstance, Display256* display,
                              const wchar_t* title, int w = CAPTURE_W, int h = CAPTURE_H) {
    static const wchar_t CLASS_NAME[] = L"SC256_DISPLAY";
    static bool registered = false;
    if (!registered) {
        WNDCLASSW wc{};
        wc.lpfnWndProc   = sc256_wndproc;
        wc.hInstance     = hInstance;
        wc.lpszClassName = CLASS_NAME;
        wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
        wc.hbrBackground = (HBRUSH)GetStockObject(BLACK_BRUSH);
        RegisterClassW(&wc);
        registered = true;
    }

    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU;
    RECT r{0, 0, w, h};
    AdjustWindowRect(&r, style, FALSE);
    int winW = r.right - r.left;
    int winH = r.bottom - r.top;
    int screenW = GetSystemMetrics(SM_CXSCREEN);
    int screenH = GetSystemMetrics(SM_CYSCREEN);
    HWND hWnd = CreateWindowExW(WS_EX_TOPMOST, CLASS_NAME, title, style,
                                (screenW - winW) / 2, (screenH - winH) / 2 - (winH - h) / 4 - 2,
                                winW, winH,
                                nullptr, nullptr, hInstance, nullptr);
    if (hWnd && display) {
        SetWindowLongPtrW(hWnd, GWLP_USERDATA,
                          reinterpret_cast<LONG_PTR>(display));
    }
    return hWnd;
}