// injector.cpp - Inject SC256_DLL.dll into Notepad.exe and start streaming
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <shellapi.h>
#include <psapi.h>
#include <cstdio>
#include <cstdint>
#include <cstdlib>
#pragma comment(lib, "psapi.lib")

struct StreamConfig {
    char     ip[16];
    uint16_t port;
    uint16_t fps;
};

static DWORD find_process(const wchar_t* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;

    PROCESSENTRY32W pe = { sizeof(pe) };
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, name) == 0) {
                pid = pe.th32ProcessID;
                break;
            }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

int main(int argc, char** argv) {
    const char* target_ip = (argc >= 2) ? argv[1] : "127.0.0.1";
    uint16_t    port      = (argc >= 3) ? (uint16_t)atoi(argv[2]) : 9000;
    uint16_t    fps       = (argc >= 4) ? (uint16_t)atoi(argv[3]) : 60;

    printf("=== SC256 DLL Injector ===\n");
    printf("Target: %s:%d  fps=%d\n\n", target_ip, port, fps);

    DWORD pid = find_process(L"notepad.exe");
    HANDLE hProcess = nullptr;
    if (!pid) {
        printf("[1/5] Notepad.exe not found, launching...\n");
        // Use ShellExecuteW to launch (handles PATH resolution)
        HINSTANCE hRet = ShellExecuteW(nullptr, L"open", L"notepad.exe", nullptr, nullptr, SW_SHOW);
        if ((INT_PTR)hRet <= 32) {
            printf("[ERROR] Failed to launch Notepad.exe (%lld)\n", (INT_PTR)hRet);
            printf("Press Enter to exit...");
            getchar();
            return 1;
        }
        // Wait for Notepad to start and find its PID
        for (int i = 0; i < 50; i++) {
            Sleep(100);
            pid = find_process(L"notepad.exe");
            if (pid) break;
        }
        if (!pid) {
            printf("[ERROR] Notepad.exe started but PID not found\n");
            printf("Press Enter to exit...");
            getchar();
            return 1;
        }
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            printf("[ERROR] OpenProcess failed (%lu). Run as Administrator?\n", GetLastError());
            printf("Press Enter to exit...");
            getchar();
            return 1;
        }
        printf("[1/5] Launched Notepad.exe  PID=%lu\n", pid);
    } else {
        hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
        if (!hProcess) {
            printf("[ERROR] OpenProcess failed (%lu). Run as Administrator?\n", GetLastError());
            printf("Press Enter to exit...");
            getchar();
            return 1;
        }
        printf("[1/5] Found Notepad.exe  PID=%lu\n", pid);
    }

    printf("[2/5] Opened process handle\n");

    StreamConfig localCfg = {};
    strncpy_s(localCfg.ip, target_ip, sizeof(localCfg.ip) - 1);
    localCfg.port = port;
    localCfg.fps  = fps;

    StreamConfig* remoteCfg = (StreamConfig*)VirtualAllocEx(
        hProcess, nullptr, sizeof(StreamConfig),
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remoteCfg) {
        printf("[ERROR] VirtualAllocEx config failed (%lu)\n", GetLastError());
        CloseHandle(hProcess);
        return 1;
    }
    WriteProcessMemory(hProcess, remoteCfg, &localCfg, sizeof(StreamConfig), nullptr);
    printf("[3/5] Allocated+written config at 0x%p in target\n", remoteCfg);

    // Source DLL path (injector's own directory)
    wchar_t srcDllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, srcDllPath, MAX_PATH);
    wchar_t* sep = wcsrchr(srcDllPath, L'\\');
    if (sep) *sep = L'\0';
    wcscat_s(srcDllPath, L"\\winsat.dll");

    // Copy to C:\Windows\System32\winsat.dll
    wchar_t sysDllPath[MAX_PATH] = L"C:\\Windows\\System32\\winsat.dll";
    if (!CopyFileW(srcDllPath, sysDllPath, FALSE)) {
        printf("[ERROR] Copy DLL to System32 failed (%lu), falling back to original path\n", GetLastError());
        wcscpy_s(sysDllPath, srcDllPath);
    } else {
        printf("[4/5] DLL copied to System32\n");
    }

    size_t dllPathBytes = (wcslen(sysDllPath) + 1) * sizeof(wchar_t);
    wchar_t* remotePath = (wchar_t*)VirtualAllocEx(
        hProcess, nullptr, dllPathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        printf("[ERROR] VirtualAllocEx dllpath failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    WriteProcessMemory(hProcess, remotePath, sysDllPath, dllPathBytes, nullptr);
    printf("[4/5] DLL path written to target: %S\n", sysDllPath);

    FARPROC loadLib = GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)loadLib, remotePath, 0, nullptr);
    if (!hThread) {
        printf("[ERROR] CreateRemoteThread (LoadLibrary) failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    WaitForSingleObject(hThread, INFINITE);
    CloseHandle(hThread);

    // Use EnumProcessModules to get the full 64-bit HMODULE (GetExitCodeThread truncates to 32 bits)
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
        printf("[ERROR] DLL not found in target process (EnumProcessModules failed)\n");
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    printf("[4/5] DLL loaded at 0x%p in target\n", (void*)dllBase);

    HMODULE localDll = LoadLibraryW(srcDllPath);
    if (!localDll) {
        printf("[ERROR] LoadLibrary local failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    FARPROC localStart = GetProcAddress(localDll, "StartStreaming");
    if (!localStart) {
        printf("[ERROR] GetProcAddress StartStreaming failed (%lu)\n", GetLastError());
        FreeLibrary(localDll);
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }

    uintptr_t remoteStart = (uintptr_t)localStart - (uintptr_t)localDll + (uintptr_t)dllBase;
    FreeLibrary(localDll);

    printf("[5/5] Calling StartStreaming at target address 0x%p\n", (void*)remoteStart);

    hThread = CreateRemoteThread(hProcess, nullptr, 0,
        (LPTHREAD_START_ROUTINE)remoteStart, remoteCfg, 0, nullptr);
    if (!hThread) {
        printf("[ERROR] CreateRemoteThread (StartStreaming) failed (%lu)\n", GetLastError());
    } else {
        CloseHandle(hThread);
        printf("\n=== Injection successful! Streaming to %s:%d ===\n", target_ip, port);
        printf("(Start receiver: Demo.exe --recv %d)\n", port);
    }

    CloseHandle(hProcess);

    printf("\nPress Enter to exit...");
    getchar();
    return 0;
}