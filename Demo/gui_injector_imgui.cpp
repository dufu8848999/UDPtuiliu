// gui_injector_imgui.cpp - ImGui-based SC256 DLL Injector
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <psapi.h>
#include <d3d11.h>
#include <dxgi.h>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>
#include <chrono>
#include <thread>
#include <atomic>
#pragma comment(lib, "psapi.lib")
#pragma comment(lib, "d3d11.lib")
#pragma comment(linker, "/subsystem:windows")

#include "imgui/imgui.h"
#include "imgui/imgui_impl_win32.h"
#include "imgui/imgui_impl_dx11.h"

#include "Demo/protocol.h"
#include "Demo/udp_receiver.h"
#include "Demo/display.h"

#define IDI_APPICON 101

// Forward declare Win32 message handler
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

struct StreamConfig {
    char     ip[16];
    uint16_t port;
    uint16_t fps;
    uint16_t width;
    uint16_t height;
};

// ==================== Config save/load ====================

// Config file (relative path, encrypted with XOR)
#define CONFIG_FILE  L"config.dat"
#define XOR_KEY      0xAB

struct AppConfig {
    char ip[32];
    char port[16];
    char fps[16];
    char width[16];
    char height[16];
};

static void xor_encrypt_decrypt(uint8_t* data, size_t len) {
    for (size_t i = 0; i < len; i++)
        data[i] ^= XOR_KEY;
}

static bool save_config(const AppConfig& cfg) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* sep = wcsrchr(path, L'\\');
    if (sep) *sep = L'\0';
    wcscat_s(path, L"\\");
    wcscat_s(path, CONFIG_FILE);

    uint8_t buf[sizeof(AppConfig)];
    memcpy(buf, &cfg, sizeof(AppConfig));
    xor_encrypt_decrypt(buf, sizeof(AppConfig));

    HANDLE hFile = CreateFileW(path, GENERIC_WRITE, 0, nullptr,
        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    DWORD written;
    WriteFile(hFile, buf, sizeof(buf), &written, nullptr);
    CloseHandle(hFile);
    return written == sizeof(buf);
}

static bool load_config(AppConfig& cfg) {
    wchar_t path[MAX_PATH];
    GetModuleFileNameW(nullptr, path, MAX_PATH);
    wchar_t* sep = wcsrchr(path, L'\\');
    if (sep) *sep = L'\0';
    wcscat_s(path, L"\\");
    wcscat_s(path, CONFIG_FILE);

    HANDLE hFile = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile == INVALID_HANDLE_VALUE) return false;
    uint8_t buf[sizeof(AppConfig)];
    DWORD read;
    ReadFile(hFile, buf, sizeof(buf), &read, nullptr);
    CloseHandle(hFile);
    if (read != sizeof(AppConfig)) return false;
    xor_encrypt_decrypt(buf, sizeof(AppConfig));
    memcpy(&cfg, buf, sizeof(AppConfig));
    return true;
}

// ==================== Injection logic ====================

static std::atomic<bool> g_self_exit(false);

static DWORD find_process(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

// Returns log lines; caller must append to the log buffer
static std::vector<std::string> do_inject(const char* ip, uint16_t port, uint16_t fps,
                                          uint16_t width, uint16_t height) {
    std::vector<std::string> log;
    char buf[256];

    auto add_log = [&](const char* msg) { log.push_back(msg); };
    auto add_fmt = [&](const char* fmt, ...) {
        va_list args;
        va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        log.push_back(buf);
    };

    DWORD pid = find_process(L"notepad.exe");
    HANDLE hProcess = nullptr;

    if (!pid) {
        add_log("未找到记事本，正在启动...");
        HINSTANCE hRet = ShellExecuteW(nullptr, L"open", L"notepad.exe", nullptr, nullptr, SW_SHOW);
        if ((INT_PTR)hRet <= 32) {
            add_fmt("[错误] 启动记事本失败 (%lld)", (INT_PTR)hRet);
            return log;
        }
        // Wait up to 5s for Notepad to appear
        for (int i = 0; i < 50; i++) {
            Sleep(100);
            pid = find_process(L"notepad.exe");
            if (pid) break;
        }
        if (!pid) { add_log("[错误] 未找到记事本"); return log; }
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            add_fmt("[错误] 打开进程失败 (%lu)", GetLastError());
            return log;
        }
        add_log("记事本已启动");
    } else {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            add_fmt("[错误] 打开进程失败 (%lu)", GetLastError());
            return log;
        }
        add_log("找到记事本进程");
    }

    // Config in target
    StreamConfig localCfg = {};
    strncpy_s(localCfg.ip, ip, sizeof(localCfg.ip) - 1);
    localCfg.port   = port;
    localCfg.fps    = fps;
    localCfg.width  = width;
    localCfg.height = height;

    StreamConfig* remoteCfg = (StreamConfig*)VirtualAllocEx(
        hProcess, nullptr, sizeof(StreamConfig),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteCfg) {
        add_fmt("[错误] 分配内存失败 (%lu)", GetLastError());
        CloseHandle(hProcess);
        return log;
    }
    WriteProcessMemory(hProcess, remoteCfg, &localCfg, sizeof(StreamConfig), nullptr);

    // Source DLL path (injector's own directory)
    wchar_t srcDllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, srcDllPath, MAX_PATH);
    wchar_t* sep = wcsrchr(srcDllPath, L'\\');
    if (sep) *sep = L'\0';
    wcscat_s(srcDllPath, L"\\winsat.dll");

    // Copy to C:\Windows\System32\winsat.dll (overwrite to ensure latest version)
    wchar_t sysDllPath[MAX_PATH] = L"C:\\Windows\\System32\\winsat.dll";
    if (!CopyFileW(srcDllPath, sysDllPath, FALSE)) {
        add_fmt("[错误] 复制 DLL 到 System32 失败 (%lu)", GetLastError());
        wcscpy_s(sysDllPath, srcDllPath);
    }

    size_t dllPathBytes = (wcslen(sysDllPath) + 1) * sizeof(wchar_t);
    wchar_t* remotePath = (wchar_t*)VirtualAllocEx(
        hProcess, nullptr, dllPathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        add_fmt("[错误] 分配内存失败 (%lu)", GetLastError());
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return log;
    }
    WriteProcessMemory(hProcess, remotePath, sysDllPath, dllPathBytes, nullptr);

    // LoadLibraryW remotely
    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)loadLib, remotePath, 0, nullptr);
    if (!hThread) {
        add_fmt("[错误] 注入失败 (%lu)", GetLastError());
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return log;
    }
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    // Get full 64-bit HMODULE
    uintptr_t dllBase = 0;
    HMODULE remoteMods[1024];
    DWORD needed;
    if (EnumProcessModules(hProcess, remoteMods, sizeof(remoteMods), &needed)) {
        DWORD count = needed / sizeof(HMODULE);
        for (DWORD i = 0; i < count; i++) {
            wchar_t modPath[MAX_PATH];
            if (GetModuleFileNameExW(hProcess, remoteMods[i], modPath, MAX_PATH)) {
                if (_wcsicmp(modPath, sysDllPath) == 0) {
                    dllBase = (uintptr_t)remoteMods[i];
                    break;
                }
            }
        }
    }
    if (!dllBase) {
        add_log("[错误] 注入失败，未找到模块");
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return log;
    }

    // Local LoadLibrary to get offset
    HMODULE localDll = LoadLibraryW(srcDllPath);
    if (!localDll) {
        add_fmt("[错误] 本地加载 DLL 失败 (%lu)", GetLastError());
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return log;
    }
    FARPROC localStart = GetProcAddress(localDll, "StartStreaming");
    if (!localStart) {
        add_fmt("[错误] 获取函数地址失败 (%lu)", GetLastError());
        FreeLibrary(localDll);
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return log;
    }
    uintptr_t remoteStart = (uintptr_t)localStart - (uintptr_t)localDll + (uintptr_t)dllBase;
    FreeLibrary(localDll);

    // Call StartStreaming
    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)remoteStart, remoteCfg, 0, nullptr);
    if (!hThread) {
        add_fmt("[错误] 启动推流失败 (%lu)", GetLastError());
    } else {
        CloseHandle(hThread);
        add_log("");
        add_log("=== 注入成功！ ===");
        add_log("=== 请勿关闭记事本窗口！ ===");
        add_fmt("正在推流到 %s:%d  帧率=%d", ip, port, fps);
        // Self-terminate after 2s
        Sleep(2000);
        g_self_exit = true;
    }

    CloseHandle(hProcess);
    return log;
}

// ==================== ImGui App ====================

static std::string get_local_ip() {
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) return "127.0.0.1";
    char hostname[256];
    if (gethostname(hostname, sizeof(hostname)) != 0) { WSACleanup(); return "127.0.0.1"; }
    addrinfo hints = {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_DGRAM;
    addrinfo* result = nullptr;
    if (getaddrinfo(hostname, nullptr, &hints, &result) != 0) { WSACleanup(); return "127.0.0.1"; }
    std::string ip = "127.0.0.1";
    for (addrinfo* p = result; p; p = p->ai_next) {
        auto* sa = reinterpret_cast<sockaddr_in*>(p->ai_addr);
        char buf[INET_ADDRSTRLEN];
        if (inet_ntop(AF_INET, &sa->sin_addr, buf, sizeof(buf))) {
            if (strcmp(buf, "127.0.0.1") != 0) { ip = buf; break; }
        }
    }
    freeaddrinfo(result);
    WSACleanup();
    return ip;
}

static HWND g_hwnd = nullptr;
static ID3D11Device* g_pd3dDevice = nullptr;
static ID3D11DeviceContext* g_pd3dDeviceContext = nullptr;
static IDXGISwapChain* g_pSwapChain = nullptr;
static ID3D11RenderTargetView* g_mainRenderTargetView = nullptr;
static bool g_SwapChainOccluded = false;

// ImGui state
static char g_ip[32] = "127.0.0.1";
static char g_port[16] = "9000";
static char g_fps[16] = "60";
static char g_width[16] = "256";
static char g_height[16] = "256";
static bool g_injecting = false;
static std::vector<std::string> g_log;

// Receiver state
static char g_recv_port[16] = "9000";
static bool g_receiving = false;
static std::vector<std::string> g_recv_log;
static std::atomic<bool> g_recv_stop(false);
static std::atomic<int> g_recv_fps(0);

// Config auto-save: snapshots of previous values to detect changes
static char g_prev_ip[sizeof(g_ip)] = "";
static char g_prev_port[sizeof(g_port)] = "";
static char g_prev_fps[sizeof(g_fps)] = "";
static char g_prev_width[sizeof(g_width)] = "";
static char g_prev_height[sizeof(g_height)] = "";

// ==================== Receiver thread ====================

static void recv_thread_func(uint16_t port) {
    // Elevate thread priority for low-latency receive
    SetThreadPriority(GetCurrentThread(), THREAD_PRIORITY_ABOVE_NORMAL);
    // Prevent system/display sleep during receiving
    SetThreadExecutionState(ES_CONTINUOUS | ES_SYSTEM_REQUIRED | ES_DISPLAY_REQUIRED);

    HINSTANCE hInst = GetModuleHandleW(nullptr);
    Display256 display;
    HWND hWnd = nullptr;
    bool window_created = false;
    uint16_t curW = 0, curH = 0;

    g_recv_log.push_back("[信息] 接收端已启动，等待数据...");

    std::vector<uint8_t> frame_buf;
    uint32_t frame_id = 0;
    int64_t last_ok_frame = 0;
    bool first_frame = true;

    MSG msg{};
    uint64_t fps_count = 0;
    int64_t fps_timer = GetTickCount64();
    g_recv_fps = 0;

    // Auto-reconnect loop: recreate socket if no data for extended period
    while (!g_recv_stop) {
        UdpReceiver udp(port);
        int64_t no_data_timer = GetTickCount64();

        while (!g_recv_stop) {
            while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
                TranslateMessage(&msg);
                DispatchMessageW(&msg);
            }

            uint16_t w = 0, h = 0;
            if (udp.receive(frame_buf, frame_id, &w, &h)) {
                // (Re)create the window/display if the received size changed.
                if (!window_created || w != curW || h != curH) {
                    if (window_created) {
                        DestroyWindow(hWnd);
                        display.~Display256();
                        new (&display) Display256();
                    }
                    hWnd = create_256_window(hInst, &display, L"SC256 接收端", w, h);
                    if (!hWnd) {
                        g_recv_log.push_back("[错误] 创建接收窗口失败");
                        g_receiving = false;
                        return;
                    }
                    ShowWindow(hWnd, SW_SHOW);
                    UpdateWindow(hWnd);
                    display.attach(hWnd, w, h);
                    curW = w;
                    curH = h;
                    window_created = true;

                    // Show test pattern at the received size
                    std::vector<uint8_t> test(frame_bytes(w, h), 0);
                    for (int y = 0; y < h; ++y)
                        for (int x = 0; x < w; ++x) {
                            uint8_t* p = test.data() + (y * w + x) * 4;
                            int dx = x - w / 2, dy = y - h / 2;
                            if (dx * dx + dy * dy < 64 * 64) {
                                p[0] = 0; p[1] = 0; p[2] = 255; p[3] = 255;
                            } else {
                                p[0] = 0; p[1] = 128; p[2] = 0; p[3] = 255;
                            }
                        }
                    display.present(test.data());
                    g_recv_log.push_back("[信息] 窗口尺寸: " + std::to_string(w) + "x" + std::to_string(h));
                }

                if (first_frame) {
                    g_recv_log.push_back("[信息] 收到第一帧数据！");
                    first_frame = false;
                }
                last_ok_frame = GetTickCount64();
                no_data_timer = GetTickCount64();
                display.present(frame_buf.data());
                fps_count++;
                if (GetTickCount64() - fps_timer >= 1000) {
                    g_recv_fps = (int)fps_count;
                    fps_count = 0;
                    fps_timer = GetTickCount64();
                }
            } else {
                if (!first_frame && GetTickCount64() - last_ok_frame > 3000) {
                    g_recv_log.push_back("[信息] 已3秒未收到数据，请检查推流端是否运行");
                    last_ok_frame = GetTickCount64();
                }
                // Recreate socket if no data for 30s (socket may be stale)
                if (GetTickCount64() - no_data_timer > 30000) {
                    g_recv_log.push_back("[信息] 30秒无数据，重建接收套接字...");
                    break;
                }
            }
        }
    }

    g_recv_log.push_back("[信息] 接收端已停止");
    g_receiving = false;
    if (hWnd) DestroyWindow(hWnd);
}

static void check_and_save_config() {
    if (strcmp(g_ip, g_prev_ip) != 0 ||
        strcmp(g_port, g_prev_port) != 0 ||
        strcmp(g_fps, g_prev_fps) != 0 ||
        strcmp(g_width, g_prev_width) != 0 ||
        strcmp(g_height, g_prev_height) != 0) {
        AppConfig cfg;
        strncpy_s(cfg.ip, g_ip, sizeof(cfg.ip) - 1);
        strncpy_s(cfg.port, g_port, sizeof(cfg.port) - 1);
        strncpy_s(cfg.fps, g_fps, sizeof(cfg.fps) - 1);
        strncpy_s(cfg.width, g_width, sizeof(cfg.width) - 1);
        strncpy_s(cfg.height, g_height, sizeof(cfg.height) - 1);
        if (save_config(cfg)) {
            strncpy_s(g_prev_ip, g_ip, sizeof(g_prev_ip) - 1);
            strncpy_s(g_prev_port, g_port, sizeof(g_prev_port) - 1);
            strncpy_s(g_prev_fps, g_fps, sizeof(g_prev_fps) - 1);
            strncpy_s(g_prev_width, g_width, sizeof(g_prev_width) - 1);
            strncpy_s(g_prev_height, g_height, sizeof(g_prev_height) - 1);
        }
    }
}

static bool CreateRenderTarget() {
    ID3D11Texture2D* backBuffer = nullptr;
    HRESULT hr = g_pSwapChain->GetBuffer(0, IID_PPV_ARGS(&backBuffer));
    if (!SUCCEEDED(hr)) return false;
    hr = g_pd3dDevice->CreateRenderTargetView(backBuffer, nullptr, &g_mainRenderTargetView);
    backBuffer->Release();
    return SUCCEEDED(hr);
}

static void CleanupRenderTarget() {
    if (g_mainRenderTargetView) { g_mainRenderTargetView->Release(); g_mainRenderTargetView = nullptr; }
}

static bool CreateDeviceD3D(HWND hWnd) {
    // Create D3D11 device first (without swap chain)
    UINT createFlags = 0;
    D3D_FEATURE_LEVEL featureLevel;
    D3D_FEATURE_LEVEL featureLevelArray[] = { D3D_FEATURE_LEVEL_11_0 };
    ID3D11Device* device = nullptr;
    ID3D11DeviceContext* ctx = nullptr;

    HRESULT res = D3D11CreateDevice(
        nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, createFlags,
        featureLevelArray, 1, D3D11_SDK_VERSION,
        &device, &featureLevel, &ctx);
    if (!SUCCEEDED(res)) {
        MessageBoxA(hWnd, "D3D11CreateDevice failed", "Error", MB_OK);
        return false;
    }

    // Get DXGI factory from device
    IDXGIDevice* dxgiDevice = nullptr;
    IDXGIAdapter* adapter = nullptr;
    IDXGIFactory* factory = nullptr;
    res = device->QueryInterface(IID_PPV_ARGS(&dxgiDevice));
    if (!SUCCEEDED(res)) { device->Release(); ctx->Release(); return false; }
    res = dxgiDevice->GetAdapter(&adapter);
    if (!SUCCEEDED(res)) { dxgiDevice->Release(); device->Release(); ctx->Release(); return false; }
    res = adapter->GetParent(IID_PPV_ARGS(&factory));
    if (!SUCCEEDED(res)) { adapter->Release(); dxgiDevice->Release(); device->Release(); ctx->Release(); return false; }

    // Create swap chain
    DXGI_SWAP_CHAIN_DESC sd = {};
    sd.BufferCount = 2;
    sd.BufferDesc.Width = 0;
    sd.BufferDesc.Height = 0;
    sd.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    sd.BufferDesc.RefreshRate.Numerator = 60;
    sd.BufferDesc.RefreshRate.Denominator = 1;
    sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    sd.OutputWindow = hWnd;
    sd.SampleDesc.Count = 1;
    sd.SampleDesc.Quality = 0;
    sd.Windowed = TRUE;
    sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING;

    IDXGISwapChain* swapChain = nullptr;
    res = factory->CreateSwapChain(device, &sd, &swapChain);
    if (!SUCCEEDED(res)) {
        // Fallback: try DISCARD (older systems)
        sd.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;
        sd.Flags = 0;
        res = factory->CreateSwapChain(device, &sd, &swapChain);
        if (!SUCCEEDED(res)) {
            MessageBoxA(hWnd, "CreateSwapChain failed", "Error", MB_OK);
            factory->Release(); adapter->Release(); dxgiDevice->Release();
            device->Release(); ctx->Release();
            return false;
        }
    }

    // Release intermediate objects
    factory->Release();
    adapter->Release();
    dxgiDevice->Release();

    g_pd3dDevice = device;
    g_pd3dDeviceContext = ctx;
    g_pSwapChain = swapChain;

    // Create render target view
    if (!CreateRenderTarget()) {
        MessageBoxA(hWnd, "CreateRenderTarget failed", "Error", MB_OK);
        return false;
    }

    return true;
}

static void CleanupDeviceD3D() {
    CleanupRenderTarget();
    if (g_pSwapChain) { g_pSwapChain->Release(); g_pSwapChain = nullptr; }
    if (g_pd3dDeviceContext) { g_pd3dDeviceContext->Release(); g_pd3dDeviceContext = nullptr; }
    if (g_pd3dDevice) { g_pd3dDevice->Release(); g_pd3dDevice = nullptr; }
}

static LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    if (ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam))
        return true;
    switch (msg) {
    case WM_SIZE:
        if (g_pd3dDevice && g_pSwapChain) {
            CleanupRenderTarget();
            g_pSwapChain->ResizeBuffers(0, (UINT)LOWORD(lParam), (UINT)HIWORD(lParam), DXGI_FORMAT_UNKNOWN, 0);
            CreateRenderTarget();
        }
        return 0;
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hWnd, msg, wParam, lParam);
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nCmdShow) {
    // Load application icon from embedded ICON resource
    HICON hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(IDI_APPICON));

    // Create window class with icon support (WNDCLASSEXW for hIconSm)
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(WNDCLASSEXW);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = nullptr;
    wc.lpszClassName = L"SC256_ImGui_Injector";
    if (hIcon) {
        wc.hIcon     = hIcon;
        wc.hIconSm   = hIcon;
    }
    RegisterClassExW(&wc);

    g_hwnd = CreateWindowExW(
        WS_EX_CLIENTEDGE, wc.lpszClassName, L"推流启动器",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 520, 520,
        nullptr, nullptr, hInst, nullptr);
    if (!g_hwnd) return 1;

    // Explicitly set window title bar / taskbar icons (large + small)
    if (hIcon) {
        SendMessageW(g_hwnd, WM_SETICON, ICON_BIG,   (LPARAM)hIcon);
        SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)hIcon);
    }

    // Init D3D11
    if (!CreateDeviceD3D(g_hwnd)) {
        CleanupDeviceD3D();
        return 1;
    }

    ShowWindow(g_hwnd, nCmdShow);
    UpdateWindow(g_hwnd);

    // Init ImGui
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.FontGlobalScale = 1.0f;

    ImGui::StyleColorsDark();

    // Load Chinese font (Microsoft YaHei)
    io.Fonts->AddFontFromFileTTF("C:\\Windows\\Fonts\\msyh.ttc", 16.0f, nullptr, io.Fonts->GetGlyphRangesChineseFull());

    ImGui_ImplWin32_Init(g_hwnd);
    ImGui_ImplDX11_Init(g_pd3dDevice, g_pd3dDeviceContext);

    // Load config from file
    AppConfig loadedCfg;
    if (load_config(loadedCfg)) {
        strncpy_s(g_ip, loadedCfg.ip, sizeof(g_ip) - 1);
        strncpy_s(g_port, loadedCfg.port, sizeof(g_port) - 1);
        strncpy_s(g_fps, loadedCfg.fps, sizeof(g_fps) - 1);
        strncpy_s(g_width, loadedCfg.width, sizeof(g_width) - 1);
        strncpy_s(g_height, loadedCfg.height, sizeof(g_height) - 1);
    }
    // Init prev values so auto-save won't trigger on first frame
    strncpy_s(g_prev_ip, g_ip, sizeof(g_prev_ip) - 1);
    strncpy_s(g_prev_port, g_port, sizeof(g_prev_port) - 1);
    strncpy_s(g_prev_fps, g_fps, sizeof(g_prev_fps) - 1);
    strncpy_s(g_prev_width, g_width, sizeof(g_prev_width) - 1);
    strncpy_s(g_prev_height, g_height, sizeof(g_prev_height) - 1);

    // Main loop
    while (true) {
        if (g_self_exit) ExitProcess(0);

        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (msg.message == WM_QUIT) goto cleanup;
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }

        if (g_SwapChainOccluded && g_pSwapChain->Present(0, DXGI_PRESENT_TEST) == DXGI_STATUS_OCCLUDED) {
            Sleep(10);
            continue;
        }
        g_SwapChainOccluded = false;

        // Get window client size
        RECT clientRect;
        GetClientRect(g_hwnd, &clientRect);
        float winW = (float)(clientRect.right - clientRect.left);
        float winH = (float)(clientRect.bottom - clientRect.top);

        // Clear the render target with dark background
        const float clearColor[4] = { 0.15f, 0.15f, 0.18f, 1.00f };
        g_pd3dDeviceContext->OMSetRenderTargets(1, &g_mainRenderTargetView, nullptr);
        g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, clearColor);

        // Set viewport
        D3D11_VIEWPORT vp;
        vp.Width = winW;
        vp.Height = winH;
        vp.MinDepth = 0.0f;
        vp.MaxDepth = 1.0f;
        vp.TopLeftX = 0;
        vp.TopLeftY = 0;
        g_pd3dDeviceContext->RSSetViewports(1, &vp);

        // Start ImGui frame
        ImGui_ImplDX11_NewFrame();
        ImGui_ImplWin32_NewFrame();
        ImGui::NewFrame();

        // Build UI
        ImGui::SetNextWindowPos(ImVec2(0, 0));
        ImGui::SetNextWindowSize(ImVec2(winW, winH));
        ImGui::Begin("推流", nullptr,
            ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoMove);

        if (ImGui::BeginTabBar("MainTabBar", ImGuiTabBarFlags_None)) {
            // ========== 推流 Tab ==========
            if (ImGui::BeginTabItem("推流")) {
                // Show local IP and UDP receive address
                static std::string local_ip = get_local_ip();
                ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(180, 220, 255, 255));
                ImGui::TextUnformatted(("本机 IP: " + local_ip).c_str());
                ImGui::TextUnformatted(("UDP 接收地址: " + local_ip + ":" + g_port).c_str());
                ImGui::PopStyleColor();
                ImGui::Dummy(ImVec2(0, 2));

                ImGui::TextUnformatted("IP 地址:");
                ImGui::SameLine();
                ImGui::InputText("##ip", g_ip, sizeof(g_ip));
                if (ImGui::IsItemDeactivatedAfterEdit()) check_and_save_config();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("接收端 IP 地址");

                ImGui::TextUnformatted("端口:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                ImGui::InputText("##port", g_port, sizeof(g_port));
                if (ImGui::IsItemDeactivatedAfterEdit()) check_and_save_config();
                ImGui::SameLine();
                ImGui::TextUnformatted("帧率:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(80);
                ImGui::InputText("##fps", g_fps, sizeof(g_fps));
                if (ImGui::IsItemDeactivatedAfterEdit()) check_and_save_config();

                ImGui::Dummy(ImVec2(0, 4));
                ImGui::TextUnformatted("窗口宽度:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                ImGui::InputText("##width", g_width, sizeof(g_width));
                if (ImGui::IsItemDeactivatedAfterEdit()) check_and_save_config();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("捕获区域宽度 (像素)");
                ImGui::SameLine();
                ImGui::TextUnformatted("高度:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(90);
                ImGui::InputText("##height", g_height, sizeof(g_height));
                if (ImGui::IsItemDeactivatedAfterEdit()) check_and_save_config();
                if (ImGui::IsItemHovered()) ImGui::SetTooltip("捕获区域高度 (像素)");

                ImGui::Dummy(ImVec2(0, 6));

                // Inject button
                if (g_injecting) {
                    ImGui::BeginDisabled();
                    ImGui::Button("注入中...", ImVec2(-1, 32));
                    ImGui::EndDisabled();
                } else {
                    if (ImGui::Button("开始推流", ImVec2(-1, 32))) {
                        g_injecting = true;
                        g_log.clear();
                        std::string ip = g_ip;
                        uint16_t port = (uint16_t)atoi(g_port);
                        uint16_t fps  = (uint16_t)atoi(g_fps);
                        uint16_t width  = (uint16_t)atoi(g_width);
                        uint16_t height = (uint16_t)atoi(g_height);
                        if (port == 0) port = 9000;
                        if (fps  == 0) fps  = 60;
                        if (width  == 0) width  = CAPTURE_W;
                        if (height == 0) height = CAPTURE_H;
                        std::thread([ip, port, fps, width, height]() {
                            auto result = do_inject(ip.c_str(), port, fps, width, height);
                            g_log = std::move(result);
                            g_injecting = false;
                        }).detach();
                    }
                    if (ImGui::IsItemHovered()) ImGui::SetTooltip("开始推流");
                }

                // Button to clear log
                ImGui::SameLine();
                if (ImGui::Button("清空日志")) {
                    g_log.clear();
                }

                // Injection status banner
                bool hasSuccess = false;
                for (const auto& line : g_log) {
                    if (line.find("请勿关闭") != std::string::npos) { hasSuccess = true; break; }
                }
                if (hasSuccess) {
                    ImGui::PushStyleColor(ImGuiCol_ChildBg, IM_COL32(0, 80, 0, 255));
                    ImGui::BeginChild("StatusBanner", ImVec2(0, 36), true);
                    ImGui::SetCursorPosY(8);
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 0, 255));
                    ImGui::TextUnformatted("  请勿关闭记事本窗口！关闭将停止推流");
                    ImGui::PopStyleColor();
                    ImGui::EndChild();
                    ImGui::PopStyleColor();
                }

                ImGui::Separator();
                ImGui::TextUnformatted("输出日志:");
                ImGui::BeginChild("Log", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

                for (const auto& line : g_log) {
                    if (line.find("[错误]") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
                    else if (line.find("注入成功") != std::string::npos || line.find("请勿关闭") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 255, 80, 255));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 220, 240, 255));
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopStyleColor();
                }

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            // ========== 接收 Tab ==========
            if (ImGui::BeginTabItem("接收")) {
                ImGui::TextUnformatted("监听端口:");
                ImGui::SameLine();
                ImGui::SetNextItemWidth(120);
                ImGui::InputText("##recv_port", g_recv_port, sizeof(g_recv_port));

                // FPS display
                ImGui::SameLine();
                ImGui::Dummy(ImVec2(20, 0));
                ImGui::SameLine();
                if (g_receiving && g_recv_fps > 0) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(0, 255, 128, 255));
                    ImGui::TextUnformatted(("接收帧率: " + std::to_string(g_recv_fps.load()) + " FPS").c_str());
                    ImGui::PopStyleColor();
                } else if (g_receiving) {
                    ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 200, 200, 255));
                    ImGui::TextUnformatted("接收帧率: 0 FPS");
                    ImGui::PopStyleColor();
                }

                ImGui::Dummy(ImVec2(0, 6));

                if (g_receiving) {
                    if (ImGui::Button("停止接收", ImVec2(-1, 32))) {
                        g_recv_stop = true;
                        g_receiving = false;
                    }
                } else {
                    if (ImGui::Button("开始接收", ImVec2(-1, 32))) {
                        g_recv_log.clear();
                        g_recv_stop = false;
                        uint16_t port = (uint16_t)atoi(g_recv_port);
                        if (port == 0) port = 9000;
                        g_receiving = true;
                        std::thread([port]() {
                            recv_thread_func(port);
                        }).detach();
                    }
                }

                ImGui::SameLine();
                if (ImGui::Button("清空日志##recv")) {
                    g_recv_log.clear();
                }

                ImGui::Separator();
                ImGui::TextUnformatted("接收日志:");
                ImGui::BeginChild("RecvLog", ImVec2(0, 0), true, ImGuiWindowFlags_HorizontalScrollbar);

                for (const auto& line : g_recv_log) {
                    if (line.find("[错误]") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(255, 80, 80, 255));
                    else if (line.find("[信息]") != std::string::npos)
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(80, 200, 255, 255));
                    else
                        ImGui::PushStyleColor(ImGuiCol_Text, IM_COL32(200, 220, 240, 255));
                    ImGui::TextUnformatted(line.c_str());
                    ImGui::PopStyleColor();
                }

                if (ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 20)
                    ImGui::SetScrollHereY(1.0f);

                ImGui::EndChild();
                ImGui::EndTabItem();
            }

            ImGui::EndTabBar();
        }

        ImGui::End();

        // Render
        ImGui::Render();
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // Present (no vsync for responsive UI)
        HRESULT hr = g_pSwapChain->Present(0, 0);
        if (hr == DXGI_STATUS_OCCLUDED) {
            g_SwapChainOccluded = true;
        }
    }

cleanup:
    ImGui_ImplDX11_Shutdown();
    ImGui_ImplWin32_Shutdown();
    ImGui::DestroyContext();
    CleanupDeviceD3D();
    DestroyWindow(g_hwnd);
    return 0;
}