// dll_main.cpp - SC256 streaming DLL (injectable)
// Export StartStreaming() - injector calls this via CreateRemoteThread
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <chrono>
#include <intrin.h>
#include <atomic>

#include "protocol.h"
#include "dxgi_capture.h"
#include "udp_sender.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

struct StreamConfig {
    char     ip[16];
    uint16_t port;
    uint16_t fps;
    uint16_t width;
    uint16_t height;
};

// Write log to a file in temp directory for crash diagnosis
static void dbg_print(const char* msg) {
    OutputDebugStringA(msg);
    // Also write to a log file in case DebugView is not available
    HANDLE hFile = CreateFileA("C:\\Temp\\SC256_DLL.log", FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, nullptr);
        CloseHandle(hFile);
    }
}

static int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

// Separate the actual work into a function without __try so C++ objects work
static DWORD WINAPI streaming_thread_impl(LPVOID param) {
    StreamConfig* cfg = (StreamConfig*)param;
    dbg_print("[SC256_DLL] Thread started\n");

    timeBeginPeriod(1);
    // Prevent system/display sleep during streaming
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);
    dbg_print("[SC256_DLL] SetThreadExecutionState OK\n");

    // Elevate thread priority for consistent frame timing
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    dbg_print("[SC256_DLL] Thread priority set\n");

    char buf[256];
    sprintf_s(buf, "[SC256_DLL] Streaming to %s:%d  fps=%d\n", cfg->ip, cfg->port, cfg->fps);
    dbg_print(buf);

    dbg_print("[SC256_DLL] Creating DxgiCapture256...\n");
    DxgiCapture256 cap;
    cap.set_size(cfg->width, cfg->height);
    dbg_print("[SC256_DLL] DxgiCapture256 OK\n");

    dbg_print("[SC256_DLL] Creating UdpSender...\n");
    UdpSender      udp;
    dbg_print("[SC256_DLL] UdpSender OK\n");

    if (!udp.set_target(cfg->ip, cfg->port)) {
        dbg_print("[SC256_DLL] Invalid target IP\n");
        return 1;
    }
    dbg_print("[SC256_DLL] set_target OK\n");

    const int64_t frame_interval_us = 1000000 / (cfg->fps ? cfg->fps : 60);
    uint32_t frame_id = 0;
    int capture_fail_count = 0;
    dbg_print("[SC256_DLL] Entering main loop\n");

    while (true) {
        const int64_t t0 = now_us();
        const uint8_t* raw = cap.capture();

        if (!raw) {
            // Capture failed, retry with backoff
            if (++capture_fail_count > 100) {
                dbg_print("[SC256_DLL] Capture failed 100 times, reinitializing...\n");
                cap.~DxgiCapture256();
                new (&cap) DxgiCapture256();
                capture_fail_count = 0;
                Sleep(500);
            }
            continue;
        }
        capture_fail_count = 0;

        udp.send_frame(frame_id++, raw, cap.size_bytes(),
                       (uint16_t)cap.width(), (uint16_t)cap.height());

        const int64_t elapsed = now_us() - t0;
        if (elapsed < frame_interval_us) {
            int64_t remaining = frame_interval_us - elapsed;
            while (remaining > 1000) {
                int64_t chunk = (remaining - 1000);
                if (chunk > 5000) chunk = 5000;
                Sleep((DWORD)(chunk / 1000));
                remaining = frame_interval_us - (now_us() - t0);
                if (remaining <= 0) break;
            }
            while (now_us() - t0 < frame_interval_us) {
                _mm_pause();
            }
        }
    }
    return 0;
}

// SEH wrapper to catch crashes and signal watchdog for restart
static std::atomic<bool> g_stream_alive(false);
static StreamConfig* g_cfg = nullptr;

static DWORD WINAPI streaming_thread(LPVOID param) {
    g_stream_alive = true;
    __try {
        DWORD ret = streaming_thread_impl(param);
        g_stream_alive = false;
        return ret;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) {
        dbg_print("[SC256_DLL] Streaming thread crashed! Watchdog will restart.\n");
        g_stream_alive = false;
    }
    return 0;
}

// Watchdog thread: restarts streaming if it dies
static DWORD WINAPI watchdog_thread(LPVOID param) {
    dbg_print("[SC256_DLL] Watchdog started\n");
    while (true) {
        HANDLE hThread = CreateThread(nullptr, 0, streaming_thread, g_cfg, 0, nullptr);
        if (hThread) {
            WaitForSingleObject(hThread, INFINITE);
            CloseHandle(hThread);
        }
        // Streaming thread exited (crash or error), wait before restart
        dbg_print("[SC256_DLL] Watchdog: restarting streaming in 2s...\n");
        Sleep(2000);
    }
    return 0;
}

extern "C" __declspec(dllexport) void StartStreaming(LPVOID config) {
    HANDLE hFile = CreateFileA("C:\\Temp\\SC256_DLL.log", FILE_APPEND_DATA,
        FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE) {
        const char* msg = "[SC256_DLL] StartStreaming called\n";
        DWORD written;
        WriteFile(hFile, msg, (DWORD)strlen(msg), &written, nullptr);
        CloseHandle(hFile);
    }

    g_cfg = (StreamConfig*)config;
    HANDLE hWatchdog = CreateThread(nullptr, 0, watchdog_thread, nullptr, 0, nullptr);
    if (hWatchdog) CloseHandle(hWatchdog);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        // Log from DllMain to verify the DLL is actually loaded
        HANDLE hFile = CreateFileA("C:\\Temp\\SC256_DLL.log", FILE_APPEND_DATA,
            FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (hFile != INVALID_HANDLE_VALUE) {
            const char* msg = "[SC256_DLL] DllMain DLL_PROCESS_ATTACH\n";
            DWORD written;
            WriteFile(hFile, msg, (DWORD)strlen(msg), &written, nullptr);
            CloseHandle(hFile);
        }
    }
    return TRUE;
}