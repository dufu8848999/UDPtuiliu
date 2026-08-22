// main.cpp - Unified entry point for both sender and receiver.
//
// Receiver: Demo.exe --recv [port]
// Sender  : Demo.exe --send IP [fps] [port]
//
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <mmsystem.h>   // timeBeginPeriod
#include <cstdio>
#include <cstdint>
#include <intrin.h>
#include <string>
#include <chrono>
#include <thread>

#include "protocol.h"
#include "dxgi_capture.h"   // tear-free DXGI capture
#include "udp_sender.h"
#include "udp_receiver.h"
#include "display.h"

static inline int64_t now_us() {
    using namespace std::chrono;
    return duration_cast<microseconds>(
               steady_clock::now().time_since_epoch()).count();
}

// ==========================================================
// Sender loop: capture -> UDP packetize -> send (raw BGRA)
// ==========================================================
[[noreturn]] static void run_sender(const char* target_ip, uint16_t port, int target_fps = 60) {
    printf("[Sender] target=%s:%d  %dx%d  fps=%d\n",
           target_ip, port, CAPTURE_W, CAPTURE_H, target_fps);

    DxgiCapture256 cap;
    UdpSender        udp;
    if (!udp.set_target(target_ip, port)) {
        printf("[Sender] invalid target IP: %s\n", target_ip);
        exit(1);
    }

    const int64_t frame_interval_us = 1000000 / target_fps;
    uint32_t frame_id        = 0;
    int      report_counter  = 0;
    int64_t  report_t0       = now_us();
    size_t   report_bytes    = 0;

    while (true) {
        const int64_t t0 = now_us();

        const uint8_t* raw = cap.capture();

        udp.send_frame(frame_id++, raw, RAW_BGR_SIZE);

        const int64_t t1 = now_us();
        report_counter++;
        report_bytes += RAW_BGR_SIZE;

        if (report_counter >= target_fps) {
            const double secs = (t1 - report_t0) / 1e6;
            const int    fps  = static_cast<int>(report_counter / secs);
            const double mbps = (report_bytes * 8.0) / (secs * 1e6);
            printf("[Sender] frame=%u  size=%5db  loop=%3lldus  FPS=%d  bitrate=%.1fMbps\n",
                   frame_id, RAW_BGR_SIZE, t1 - t0, fps, mbps);
            report_counter = 0;
            report_bytes   = 0;
            report_t0      = t1;
        }

        // Precise frame pacing: sleep in 1ms chunks, spin last 1ms.
        const int64_t elapsed = t1 - t0;
        if (elapsed < frame_interval_us) {
            int64_t remaining = frame_interval_us - elapsed;
            // Sleep in small chunks (max 5ms), leaving 1ms for spin
            while (remaining > 1000) {
                int64_t chunk = (remaining - 1000);
                if (chunk > 5000) chunk = 5000;
                std::this_thread::sleep_for(std::chrono::microseconds(chunk));
                remaining = frame_interval_us - (now_us() - t0);
                if (remaining <= 0) break;
            }
            // Spin-wait the final ~1ms for precise timing
            while (now_us() - t0 < frame_interval_us) {
                _mm_pause();
            }
        }
    }
}

// ==========================================================
// Receiver loop: poll UDP -> assemble frame -> display (raw BGRA)
// ==========================================================
[[noreturn]] static void run_receiver(uint16_t port) {
    printf("[Receiver] listening UDP 0.0.0.0:%d for %dx%d raw stream...\n",
           port, CAPTURE_W, CAPTURE_H);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    Display256 display;
    HWND hWnd = create_256_window(hInst, &display, L"SC256 Receiver (Raw BGRA over UDP)");
    ShowWindow(hWnd, SW_SHOW);
    UpdateWindow(hWnd);

    display.attach(hWnd);

    // ---- Show a test pattern to verify display works ----
    {
        std::vector<uint8_t> test(RAW_BGR_SIZE, 0);
        for (int y = 0; y < CAPTURE_H; ++y) {
            for (int x = 0; x < CAPTURE_W; ++x) {
                uint8_t* p = test.data() + (y * CAPTURE_W + x) * 4;
                int dx = x - CAPTURE_W / 2, dy = y - CAPTURE_H / 2;
                if (dx * dx + dy * dy < 64 * 64) {
                    p[0] = 0;   p[1] = 0;   p[2] = 255; p[3] = 255; // blue circle
                } else {
                    p[0] = 0;   p[1] = 128; p[2] = 0;   p[3] = 255; // green bg
                }
            }
        }
        display.present(test.data());
        printf("[Receiver] test pattern displayed (blue circle on green).\n");
    }

    printf("[Receiver] waiting for incoming frames...\n");
    UdpReceiver     udp(port);

    std::vector<uint8_t> frame_buf;
    frame_buf.reserve(RAW_BGR_SIZE);
    uint32_t frame_id = 0;

    int      report_counter  = 0;
    int64_t  report_t0       = now_us();
    int64_t  report_decode   = 0;
    size_t   report_bytes    = 0;
    uint32_t report_last_id  = 0;
    int      report_drops    = 0;
    int64_t  last_ok_frame   = now_us();

    MSG msg{};
    while (true) {
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
            if (msg.message == WM_QUIT) {
                ExitProcess(0);
            }
        }

        if (udp.receive(frame_buf, frame_id)) {
            // First frame received?
            if (report_last_id == 0) {
                printf("[Receiver] first frame received! frame_id=%u, size=%zu\n",
                       frame_id, frame_buf.size());
            }
            last_ok_frame = now_us();

            // Track frame drops (gap in frame id means intermediate frames lost)
            if (report_last_id != 0 && frame_id > report_last_id + 1) {
                report_drops += static_cast<int>(frame_id - report_last_id - 1);
            }
            report_last_id = frame_id;

            display.present(frame_buf.data());

            const int64_t t1 = now_us();
            report_decode  += t1 - last_ok_frame;
            report_bytes   += frame_buf.size();
            report_counter++;

            if (report_counter >= 30) {
                const double secs = (t1 - report_t0) / 1e6;
                const int    fps  = static_cast<int>(report_counter / secs);
                const int    avg  = static_cast<int>(report_decode / report_counter);
                const double mbps = (report_bytes * 8.0) / (secs * 1e6);
                printf("[Receiver] frame=%u  size=%5zub  decode_avg=%dus  FPS=%d  bitrate=%.1fMbps  drops=%d\n",
                       frame_id, frame_buf.size(), avg, fps, mbps, report_drops);
                report_counter = 0;
                report_decode  = 0;
                report_bytes   = 0;
                report_drops   = 0;
                report_t0      = t1;
            }
        } else {
            // Warn if no frame received for 3 seconds
            if (now_us() - last_ok_frame > 3'000'000) {
                printf("[Receiver] WARNING: no frame received for 3+ seconds (port=%d).\n"
                       "  Make sure the sender is running:\n"
                       "    Demo.exe --send <SERVER_IP> %d\n",
                       port, port);
                last_ok_frame = now_us();
            }
            // Tight loop: when data is available, recvfrom returns immediately.
            // When no data (sender idle), SO_RCVTIMEO=1ms naturally throttles.
            // Do NOT sleep here — it drops packets and prevents frame assembly.
        }
    }
}

int main(int argc, char** argv) {
    SetPriorityClass(GetCurrentProcess(), HIGH_PRIORITY_CLASS);
    timeBeginPeriod(1);  // 1ms timer resolution for accurate sleep_for

    if (argc >= 2 && std::string(argv[1]) == "--send") {
        if (argc < 3) {
            printf("Missing target IP. Usage:\n");
            printf("  Demo.exe --send <IP> [fps] [port]\n");
            return 1;
        }
        const char* ip = argv[2];
        int fps = 60;
        uint16_t port = UDP_PORT;
        if (argc >= 4) port = static_cast<uint16_t>(atoi(argv[3]));
        if (argc >= 5) fps = atoi(argv[4]);
        if (fps < 1) fps = 60;
        if (port == 0) port = UDP_PORT;
        run_sender(ip, port, fps);
    } else if (argc >= 2 && std::string(argv[1]) == "--recv") {
        uint16_t port = UDP_PORT;
        if (argc >= 3) port = static_cast<uint16_t>(atoi(argv[2]));
        if (port == 0) port = UDP_PORT;
        run_receiver(port);
    } else {
        printf("LAN UDP 256x256 Screen Streamer (ultra-low-latency)\n");
        printf("Usage:\n");
        printf("  Receiver : Demo.exe --recv [port]              default port=9000\n");
        printf("  Sender   : Demo.exe --send <IP> [port] [fps]   default port=9000 fps=60\n");
        printf("\nExamples:\n");
        printf("  Demo.exe --recv 9000\n");
        printf("  Demo.exe --send 192.168.1.100 9000\n");
        printf("  Demo.exe --send 192.168.1.100 9000 120\n");
        printf("  start \"\" Demo.exe --recv 9001 & Demo.exe --send 127.0.0.1 9001\n");
    }
    return 0;
}
