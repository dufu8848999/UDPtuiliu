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
#include <intrin.h>

#include "../../Demo/protocol.h"
#include "../../Demo/dxgi_capture.h"
#include "../../Demo/udp_sender.h"

#pragma comment(lib, "ws2_32.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(lib, "dxgi.lib")

struct StreamConfig {
    char     ip[16];
    uint16_t port;
};

static int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
        steady_clock::now().time_since_epoch()).count();
}

static DWORD WINAPI streaming_thread(LPVOID param) {
    StreamConfig* cfg = (StreamConfig*)param;
    timeBeginPeriod(1);

    AllocConsole();
    freopen("CONOUT$", "w", stdout);
    printf("[SC256_DLL] Streaming to %s:%d\n", cfg->ip, cfg->port);

    DxgiCapture256 cap;
    UdpSender      udp;
    if (!udp.set_target(cfg->ip, cfg->port)) {
        printf("[SC256_DLL] Invalid target IP\n");
        return 1;
    }

    const int64_t frame_interval_us = 1000000 / 60;
    uint32_t frame_id = 0;

    while (true) {
        const int64_t t0 = now_us();
        const uint8_t* raw = cap.capture();
        udp.send_frame(frame_id++, raw, RAW_BGR_SIZE);

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

extern "C" __declspec(dllexport) void StartStreaming(LPVOID config) {
    HANDLE hThread = CreateThread(nullptr, 0, streaming_thread, config, 0, nullptr);
    if (hThread) CloseHandle(hThread);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID lpReserved) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
    }
    return TRUE;
}
