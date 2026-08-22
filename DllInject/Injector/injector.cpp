// injector.cpp - Inject SC256_DLL.dll into Notepad.exe and start streaming
#define WIN32_LEAN_AND_MEAN
#define _WINSOCK_DEPRECATED_NO_WARNINGS
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstdint>

struct StreamConfig {
    char     ip[16];
    uint16_t port;
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

    printf("=== SC256 DLL Injector ===\n");
    printf("Target: %s:%d\n\n", target_ip, port);

    DWORD pid = find_process(L"notepad.exe");
    if (!pid) {
        printf("[ERROR] Notepad.exe not found. Start Notepad first.\n");
        printf("Press Enter to exit...");
        getchar();
        return 1;
    }
    printf("[1/5] Found Notepad.exe  PID=%lu\n", pid);

    HANDLE hProcess = OpenProcess(PROCESS_ALL_ACCESS, FALSE, pid);
    if (!hProcess) {
        printf("[ERROR] OpenProcess failed (%lu). Run as Administrator?\n", GetLastError());
        printf("Press Enter to exit...");
        getchar();
        return 1;
    }
    printf("[2/5] Opened process handle\n");

    StreamConfig localCfg = {};
    strncpy_s(localCfg.ip, target_ip, sizeof(localCfg.ip) - 1);
    localCfg.port = port;

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

    wchar_t dllPath[MAX_PATH];
    GetModuleFileNameW(nullptr, dllPath, MAX_PATH);
    wchar_t* sep = wcsrchr(dllPath, L'\\');
    if (sep) *sep = L'\0';
    wcscat_s(dllPath, L"\\SC256_DLL.dll");

    size_t dllPathBytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    wchar_t* remotePath = (wchar_t*)VirtualAllocEx(
        hProcess, nullptr, dllPathBytes,
        MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remotePath) {
        printf("[ERROR] VirtualAllocEx dllpath failed (%lu)\n", GetLastError());
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    WriteProcessMemory(hProcess, remotePath, dllPath, dllPathBytes, nullptr);
    printf("[4/5] DLL path written to target: %S\n", dllPath);

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
    DWORD dllBase = 0;
    GetExitCodeThread(hThread, &dllBase);
    CloseHandle(hThread);

    if (!dllBase) {
        printf("[ERROR] DLL load failed in target process\n");
        VirtualFreeEx(hProcess, remotePath, 0, MEM_RELEASE);
        VirtualFreeEx(hProcess, remoteCfg, 0, MEM_RELEASE);
        CloseHandle(hProcess);
        return 1;
    }
    printf("[4/5] DLL loaded at 0x%08lX in target\n", dllBase);

    HMODULE localDll = LoadLibraryW(dllPath);
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
