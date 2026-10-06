#define _POSIX_C_SOURCE 200112L
#include "pcps4_winapi.h"

#include "pcps4_compat.h"
#include "pcps4_xinput.h"
#include "pcps4_d3d11_com.h"
#include "pcps4_abi.h"
#include "pcps4_loader.h"
#include "pcps4_dll.h"
#include "pcps4_status.h"
#include "pcps4_win32.h"
#include "pcps4_dxgi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static __thread DWORD g_last_error = 0;
typedef struct pcps4_module_handle {
    uint32_t magic;
    char name[128];
} pcps4_module_handle;

#define PCPS4_MODULE_MAGIC 0x504d4f44u

static pcps4_pe_resolver *g_resolver = NULL;

static int module_name_normalize(
    const char *input,
    char *output,
    size_t output_size);

static void *pcps4_dynamic_resolve(
    void *user,
    const char *dll,
    int by_name,
    const char *name,
    uint16_t ordinal)
{
    char normalized[128];
    char path[1024];
    void *module;
    void *fn;

    (void)user;

    if (dll == NULL)
        return NULL;

    module = pcps4_dll_find(dll);

    /*
     * Windows API-set contracts usually forward to one of the core Win32
     * implementation DLLs. PcPs4 already implements a focused kernel32 /
     * kernelbase set, so consult those built-ins before trying the filesystem.
     */
    if (module == NULL &&
        (strncmp(dll, "api-ms-win-core-", 16) == 0 ||
         strncmp(dll, "ext-ms-win-", 11) == 0)) {
        fn = by_name
            ? pcps4_pe_resolver_lookup(g_resolver, "KERNELBASE.dll", name)
            : pcps4_pe_resolver_lookup_ordinal(g_resolver, "KERNELBASE.dll", ordinal);
        if (fn != NULL)
            return fn;

        fn = by_name
            ? pcps4_pe_resolver_lookup(g_resolver, "KERNEL32.dll", name)
            : pcps4_pe_resolver_lookup_ordinal(g_resolver, "KERNEL32.dll", ordinal);
        if (fn != NULL)
            return fn;
    }

    if (module == NULL) {
        size_t i = 0;
        const char *base = strrchr(dll, '/');
        const char *back = strrchr(dll, 92);
        if (back != NULL && (base == NULL || back > base))
            base = back;
        base = base != NULL ? base + 1 : dll;

        while (base[i] != '\0' && i + 1 < sizeof(normalized)) {
            char ch = base[i];
            if (ch >= 'A' && ch <= 'Z')
                ch = (char)(ch - 'A' + 'a');
            normalized[i] = ch;
            ++i;
        }
        normalized[i] = '\0';

        if (strchr(normalized, '.') == NULL && i + 4 < sizeof(normalized))
            memcpy(normalized + i, ".dll", 5);

        if (snprintf(path, sizeof(path), "/app0/game/%s", normalized) >=
            (int)sizeof(path))
            return NULL;

        module = pcps4_dll_load(path, g_resolver);
        if (module == NULL)
            return NULL;
    }

    fn = pcps4_dll_get_export(module, by_name ? name : NULL, ordinal);
    if (fn == NULL)
        pcps4_status_log(
            "[pcps4] unresolved dynamic export: %s!%s%s",
            dll,
            by_name ? name : "#",
            by_name ? "" : "(ordinal)");

    return fn;
}

static PCPS4_MS_ABI void win_SetLastError(DWORD value);
static PCPS4_MS_ABI HMODULE win_LoadLibraryA(LPCSTR name)
{
    pcps4_module_handle *module;
    char normalized[128];
    char path[1024];
    void *loaded;

    if (module_name_normalize(name, normalized, sizeof(normalized)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    loaded = pcps4_dll_find(normalized);
    if (loaded != NULL)
        return (HMODULE)loaded;

    if (g_resolver != NULL) {
        if (snprintf(path, sizeof(path), "/app0/game/%s", normalized) <
            (int)sizeof(path)) {
            loaded = pcps4_dll_load(path, g_resolver);
            if (loaded != NULL)
                return (HMODULE)loaded;
        }
    }

    /*
     * Built-in DLLs such as kernel32/d3d11/xinput do not have a PE image on
     * disk in this runtime. Keep a lightweight module token for them.
     */
    module = (pcps4_module_handle *)calloc(1, sizeof(*module));
    if (module == NULL) {
        win_SetLastError(8);
        return NULL;
    }

    module->magic = PCPS4_MODULE_MAGIC;
    memcpy(module->name, normalized, strlen(normalized) + 1);
    return (HMODULE)module;
}

static PCPS4_MS_ABI HMODULE win_GetModuleHandleA(LPCSTR name);
static PCPS4_MS_ABI HMODULE win_GetModuleHandleW(LPCWSTR name);
static PCPS4_MS_ABI DWORD win_GetEnvironmentVariableA(
    LPCSTR name,
    LPSTR buffer,
    DWORD size);
static PCPS4_MS_ABI DWORD win_GetEnvironmentVariableW(
    LPCWSTR name,
    LPWSTR buffer,
    DWORD size);


static int module_name_normalize(
    const char *input,
    char *output,
    size_t output_size)
{
    const char *base;
    size_t i;
    size_t length;

    if (input == NULL || output == NULL || output_size < 2)
        return -1;

    base = strrchr(input, '/');
    {
        const char *backslash = strrchr(input, 92);
        if (backslash != NULL && (base == NULL || backslash > base))
            base = backslash;
    }
    base = base != NULL ? base + 1 : input;

    length = strlen(base);
    if (length + 1 > output_size)
        return -1;

    for (i = 0; i < length; ++i) {
        char ch = base[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        output[i] = ch;
    }
    output[length] = '\0';

    if (strchr(output, '.') == NULL) {
        static const char suffix[] = ".dll";
        size_t have = strlen(output);
        if (have + sizeof(suffix) > output_size)
            return -1;
        memcpy(output + have, suffix, sizeof(suffix));
    }

    return 0;
}

static PCPS4_MS_ABI HMODULE win_LoadLibraryW(LPCWSTR name)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(name, utf8, sizeof(utf8)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    return win_LoadLibraryA(utf8);
}

static PCPS4_MS_ABI HMODULE win_GetModuleHandleW(LPCWSTR name)
{
    return win_LoadLibraryW(name);
}

static PCPS4_MS_ABI HMODULE win_GetModuleHandleA(LPCSTR name)
{
    pcps4_module_handle *builtin;
    char normalized[128];
    void *loaded;

    if (name == NULL)
        return NULL;

    loaded = pcps4_dll_find(name);
    if (loaded != NULL)
        return (HMODULE)loaded;

    if (module_name_normalize(name, normalized, sizeof(normalized)) != 0)
        return NULL;

    /*
     * Built-in modules have no mapped PE image but their exports are present
     * in the resolver registry. Mirror Windows' GetModuleHandle semantics
     * without forcing a file load.
     */
    builtin = (pcps4_module_handle *)calloc(1, sizeof(*builtin));
    if (builtin == NULL)
        return NULL;

    builtin->magic = PCPS4_MODULE_MAGIC;
    memcpy(builtin->name, normalized, strlen(normalized) + 1);
    return (HMODULE)builtin;
}

static PCPS4_MS_ABI HMODULE win_LoadLibraryExA(
    LPCSTR name,
    HANDLE file,
    DWORD flags)
{
    (void)file;
    (void)flags;
    return win_LoadLibraryA(name);
}

static PCPS4_MS_ABI HMODULE win_LoadLibraryExW(
    LPCWSTR name,
    HANDLE file,
    DWORD flags)
{
    char utf8[1024];

    (void)file;
    (void)flags;

    if (pcps4_wide_to_utf8(name, utf8, sizeof(utf8)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    return win_LoadLibraryExA(utf8, file, flags);
}

static PCPS4_MS_ABI FARPROC win_GetProcAddress(
    HMODULE module,
    LPCSTR name)
{
    pcps4_module_handle *handle;
    void *fn;

    if (module == NULL || name == NULL)
        return NULL;

    if (pcps4_dll_is_module((void *)module)) {
        if (name[0] >= '0' && name[0] <= '9') {
            uint16_t ordinal = (uint16_t)strtoul(name, NULL, 10);
            fn = pcps4_dll_get_export(
                (void *)module, NULL, ordinal);
        } else {
            fn = pcps4_dll_get_export(
                (void *)module, name, 0);
        }
        if (fn == NULL)
            win_SetLastError(127);
        return (FARPROC)fn;
    }

    handle = (pcps4_module_handle *)module;
    if (handle->magic != PCPS4_MODULE_MAGIC || g_resolver == NULL) {
        win_SetLastError(6);
        return NULL;
    }

    if (name[0] >= '0' && name[0] <= '9') {
        uint16_t ordinal = (uint16_t)strtoul(name, NULL, 10);
        fn = pcps4_pe_resolver_lookup_ordinal(
            g_resolver, handle->name, ordinal);
    } else {
        fn = pcps4_pe_resolver_lookup(
            g_resolver, handle->name, name);
    }

    if (fn == NULL)
        win_SetLastError(127);

    return (FARPROC)fn;
}

static PCPS4_MS_ABI DWORD win_GetEnvironmentVariableW(
    LPCWSTR name,
    LPWSTR buffer,
    DWORD size)
{
    char utf8_name[512];
    char utf8_value[1024];
    DWORD result;
    size_t i;

    if (pcps4_wide_to_utf8(
            name, utf8_name, sizeof(utf8_name)) != 0)
        return 0;

    result = win_GetEnvironmentVariableA(
        utf8_name, utf8_value, sizeof(utf8_value));

    if (result == 0 || buffer == NULL || size == 0)
        return result;

    if (result >= size) {
        size_t limit = size > 0 ? (size_t)size - 1 : 0;
        for (i = 0; i < limit && utf8_value[i] != '\0'; ++i)
            buffer[i] = (WCHAR)(unsigned char)utf8_value[i];
        buffer[i] = 0;
        return result;
    }

    for (i = 0; i < (size_t)result; ++i)
        buffer[i] = (WCHAR)(unsigned char)utf8_value[i];
    buffer[result] = 0;
    return result;
}

static PCPS4_MS_ABI BOOL win_FreeLibrary(HMODULE module)
{
    if (module == NULL)
        return FALSE;

    if (pcps4_dll_is_module((void *)module))
        return pcps4_dll_free((void *)module) == 0;

    {
        pcps4_module_handle *handle = (pcps4_module_handle *)module;
        if (handle->magic != PCPS4_MODULE_MAGIC)
            return FALSE;
        handle->magic = 0;
        free(handle);
    }
    return TRUE;
}

static PCPS4_MS_ABI DWORD win_GetLastError(void)
{
    return g_last_error;
}

static PCPS4_MS_ABI void win_SetLastError(DWORD value)
{
    g_last_error = value;
}

static PCPS4_MS_ABI HANDLE win_GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)1;
}

static PCPS4_MS_ABI HANDLE win_GetCurrentThread(void)
{
    return (HANDLE)(intptr_t)1;
}

static PCPS4_MS_ABI void win_ExitProcess(UINT code)
{
    pcps4_pe_exit_process(code);
}

static PCPS4_MS_ABI DWORD win_GetEnvironmentVariableA(
    LPCSTR name,
    LPSTR buffer,
    DWORD size)
{
    const char *value;
    size_t length;

    if (name == NULL) {
        win_SetLastError(87);
        return 0;
    }

    value = getenv(name);
    if (value == NULL) {
        if (buffer != NULL && size != 0)
            buffer[0] = '\0';
        return 0;
    }

    length = strlen(value);

    if (buffer == NULL || size == 0) {
        return (DWORD)(length + 1);
    }

    if (length >= size) {
        size_t copy = size > 0 ? (size_t)size - 1 : 0;
        if (copy != 0)
            memcpy(buffer, value, copy);
        buffer[copy] = '\0';
        return (DWORD)(length + 1);
    }

    memcpy(buffer, value, length + 1);
    return (DWORD)length;
}

static PCPS4_MS_ABI LPSTR win_GetCommandLineA(void)
{
    static char command_line[] = "game.exe";
    return command_line;
}

static PCPS4_MS_ABI DWORD win_GetModuleFileNameA(
    HANDLE module,
    LPSTR filename,
    DWORD size)
{
    const char *path = "/app0/game/game.exe";
    size_t length;

    (void)module;

    if (filename == NULL || size == 0) {
        win_SetLastError(87);
        return 0;
    }

    length = strlen(path);
    if (length + 1 > size) {
        memcpy(filename, path, size - 1);
        filename[size - 1] = '\0';
        win_SetLastError(122);
        return size;
    }

    memcpy(filename, path, length + 1);
    return (DWORD)length;
}

static PCPS4_MS_ABI BOOL win_QueryPerformanceCounter(int64_t *counter)
{
    struct timespec now;

    if (counter == NULL)
        return FALSE;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return FALSE;

    *counter = (int64_t)now.tv_sec * 1000000000LL +
               (int64_t)now.tv_nsec;
    return TRUE;
}

static PCPS4_MS_ABI BOOL win_QueryPerformanceFrequency(int64_t *frequency)
{
    if (frequency == NULL)
        return FALSE;

    *frequency = 1000000000LL;
    return TRUE;
}

static PCPS4_MS_ABI HANDLE win_GetProcessHeap(void)
{
    return (HANDLE)(intptr_t)1;
}

static PCPS4_MS_ABI LPVOID win_HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    (void)heap;
    (void)flags;
    return malloc(size);
}

static PCPS4_MS_ABI LPVOID win_HeapReAlloc(
    HANDLE heap,
    DWORD flags,
    LPVOID memory,
    SIZE_T size)
{
    (void)heap;
    (void)flags;
    return realloc(memory, size);
}

static PCPS4_MS_ABI BOOL win_HeapFree(HANDLE heap, DWORD flags, LPVOID memory)
{
    (void)heap;
    (void)flags;
    free(memory);
    return TRUE;
}

static PCPS4_MS_ABI void win_RtlMoveMemory(PVOID dst, const void *src, SIZE_T length)
{
    memmove(dst, src, length);
}

static void register_kernel32(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define KREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    KREG("GetLastError", win_GetLastError);
    KREG("SetLastError", win_SetLastError);
    KREG("GetCurrentProcess", win_GetCurrentProcess);
    KREG("GetCurrentThread", win_GetCurrentThread);
    KREG("GetCurrentProcessId", pcps4_abi_GetCurrentProcessId);
    KREG("GetCurrentThreadId", pcps4_abi_GetCurrentThreadId);
    KREG("GetTickCount", pcps4_abi_GetTickCount);
    KREG("GetTickCount64", pcps4_abi_GetTickCount64);
    KREG("Sleep", pcps4_abi_Sleep);
    KREG("InterlockedIncrement", pcps4_abi_InterlockedIncrement);
    KREG("InterlockedDecrement", pcps4_abi_InterlockedDecrement);
    KREG("CreateFileA", pcps4_abi_CreateFileA);
    KREG("ReadFile", pcps4_abi_ReadFile);
    KREG("WriteFile", pcps4_abi_WriteFile);
    KREG("GetFileSize", pcps4_abi_GetFileSize);
    KREG("SetFilePointer", pcps4_abi_SetFilePointer);
    KREG("DeleteFileA", pcps4_abi_DeleteFileA);
    KREG("CreateDirectoryA", pcps4_abi_CreateDirectoryA);
    KREG("CloseHandle", pcps4_abi_CloseHandle);
    KREG("CreateThread", pcps4_abi_CreateThread);
    KREG("CreateMutexA", pcps4_abi_CreateMutexA);
    KREG("CreateEventA", pcps4_abi_CreateEventA);
    KREG("SetEvent", pcps4_abi_SetEvent);
    KREG("ResetEvent", pcps4_abi_ResetEvent);
    KREG("WaitForSingleObject", pcps4_abi_WaitForSingleObject);
    KREG("ReleaseMutex", pcps4_abi_ReleaseMutex);
    KREG("VirtualAlloc", pcps4_abi_VirtualAlloc);
    KREG("VirtualFree", pcps4_abi_VirtualFree);
    KREG("GetEnvironmentVariableA", win_GetEnvironmentVariableA);
    KREG("GetCommandLineA", win_GetCommandLineA);
    KREG("GetModuleFileNameA", win_GetModuleFileNameA);
    KREG("QueryPerformanceCounter", win_QueryPerformanceCounter);
    KREG("QueryPerformanceFrequency", win_QueryPerformanceFrequency);
    KREG("GetProcessHeap", win_GetProcessHeap);
    KREG("HeapAlloc", win_HeapAlloc);
    KREG("HeapReAlloc", win_HeapReAlloc);
    KREG("HeapFree", win_HeapFree);
    KREG("RtlMoveMemory", win_RtlMoveMemory);
    KREG("ExitProcess", win_ExitProcess);
    KREG("LoadLibraryA", win_LoadLibraryA);
    KREG("LoadLibraryW", win_LoadLibraryW);
    KREG("LoadLibraryExA", win_LoadLibraryExA);
    KREG("LoadLibraryExW", win_LoadLibraryExW);
    KREG("GetModuleHandleA", win_GetModuleHandleA);
    KREG("GetModuleHandleW", win_GetModuleHandleW);
    KREG("GetProcAddress", win_GetProcAddress);
    KREG("FreeLibrary", win_FreeLibrary);
    KREG("GetEnvironmentVariableW", win_GetEnvironmentVariableW);
    KREG("TlsAlloc", pcps4_abi_TlsAlloc);
    KREG("TlsFree", pcps4_abi_TlsFree);
    KREG("TlsGetValue", pcps4_abi_TlsGetValue);
    KREG("TlsSetValue", pcps4_abi_TlsSetValue);
    KREG("InterlockedExchange", pcps4_abi_InterlockedExchange);
    KREG("InterlockedCompareExchange", pcps4_abi_InterlockedCompareExchange);
    KREG("InterlockedExchangeAdd", pcps4_abi_InterlockedExchangeAdd);
    KREG("InitializeCriticalSection", pcps4_abi_InitializeCriticalSection);
    KREG("InitializeCriticalSectionAndSpinCount", pcps4_abi_InitializeCriticalSectionAndSpinCount);
    KREG("DeleteCriticalSection", pcps4_abi_DeleteCriticalSection);
    KREG("EnterCriticalSection", pcps4_abi_EnterCriticalSection);
    KREG("TryEnterCriticalSection", pcps4_abi_TryEnterCriticalSection);
    KREG("LeaveCriticalSection", pcps4_abi_LeaveCriticalSection);
    KREG("VirtualProtect", pcps4_abi_VirtualProtect);
    KREG("VirtualQuery", pcps4_abi_VirtualQuery);
    KREG("FlushInstructionCache", pcps4_abi_FlushInstructionCache);
    KREG("GetCurrentDirectoryA", pcps4_abi_GetCurrentDirectoryA);
    KREG("GetCurrentDirectoryW", pcps4_abi_GetCurrentDirectoryW);
    KREG("SetCurrentDirectoryA", pcps4_abi_SetCurrentDirectoryA);
    KREG("SetCurrentDirectoryW", pcps4_abi_SetCurrentDirectoryW);
    KREG("GetTempPathA", pcps4_abi_GetTempPathA);
    KREG("GetTempPathW", pcps4_abi_GetTempPathW);
    KREG("GetSystemDirectoryA", pcps4_abi_GetSystemDirectoryA);
    KREG("GetSystemDirectoryW", pcps4_abi_GetSystemDirectoryW);
    KREG("GetSystemTimeAsFileTime", pcps4_abi_GetSystemTimeAsFileTime);
    KREG("GetSystemInfo", pcps4_abi_GetSystemInfo);
    KREG("IsProcessorFeaturePresent", pcps4_abi_IsProcessorFeaturePresent);
    KREG("IsDebuggerPresent", pcps4_abi_IsDebuggerPresent);
    KREG("GetFileAttributesA", pcps4_abi_GetFileAttributesA);
    KREG("GetFileAttributesW", pcps4_abi_GetFileAttributesW);
    KREG("RegisterClassExA", pcps4_abi_RegisterClassExA);
    KREG("RegisterClassExW", pcps4_abi_RegisterClassExW);
    KREG("UnregisterClassA", pcps4_abi_UnregisterClassA);
    KREG("UnregisterClassW", pcps4_abi_UnregisterClassW);
    KREG("CreateWindowExA", pcps4_abi_CreateWindowExA);
    KREG("CreateWindowExW", pcps4_abi_CreateWindowExW);
    KREG("IsWindow", pcps4_abi_IsWindow);
    KREG("DestroyWindow", pcps4_abi_DestroyWindow);
    KREG("ShowWindow", pcps4_abi_ShowWindow);
    KREG("UpdateWindow", pcps4_abi_UpdateWindow);
    KREG("DefWindowProcA", pcps4_abi_DefWindowProcA);
    KREG("DefWindowProcW", pcps4_abi_DefWindowProcW);
    KREG("PeekMessageA", pcps4_abi_PeekMessageA);
    KREG("PeekMessageW", pcps4_abi_PeekMessageW);
    KREG("GetMessageA", pcps4_abi_GetMessageA);
    KREG("TranslateMessage", pcps4_abi_TranslateMessage);
    KREG("DispatchMessageA", pcps4_abi_DispatchMessageA);
    KREG("DispatchMessageW", pcps4_abi_DispatchMessageW);
    KREG("PostQuitMessage", pcps4_abi_PostQuitMessage);
    KREG("GetClientRect", pcps4_abi_GetClientRect);
    KREG("GetWindowRect", pcps4_abi_GetWindowRect);
    KREG("GetDC", pcps4_abi_GetDC);
    KREG("ReleaseDC", pcps4_abi_ReleaseDC);
    KREG("SetForegroundWindow", pcps4_abi_SetForegroundWindow);
    KREG("GetActiveWindow", pcps4_abi_GetActiveWindow);
    KREG("GetFocus", pcps4_abi_GetFocus);
    KREG("SetFocus", pcps4_abi_SetFocus);
    KREG("SetWindowLongPtrA", pcps4_abi_SetWindowLongPtrA);
    KREG("SetWindowLongPtrW", pcps4_abi_SetWindowLongPtrW);
    KREG("GetWindowLongPtrA", pcps4_abi_GetWindowLongPtrA);
    KREG("GetWindowLongPtrW", pcps4_abi_GetWindowLongPtrW);
    KREG("SetWindowTextA", pcps4_abi_SetWindowTextA);
    KREG("SetWindowTextW", pcps4_abi_SetWindowTextW);
#undef KREG
}

static void register_d3d11(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDeviceAndSwapChain",
        (void *)pcps4_abi_D3D11CreateDeviceAndSwapChain);
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDevice",
        (void *)pcps4_abi_D3D11CreateDevice);
}

static void register_winsock(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define WREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    WREG("WSAStartup", pcps4_abi_WSAStartup);
    WREG("WSACleanup", pcps4_abi_WSACleanup);
    WREG("WSAGetLastError", pcps4_abi_WSAGetLastError);
    WREG("socket", pcps4_abi_socket);
    WREG("connect", pcps4_abi_connect);
    WREG("send", pcps4_abi_send);
    WREG("recv", pcps4_abi_recv);
    WREG("shutdown", pcps4_abi_shutdown);
    WREG("setsockopt", pcps4_abi_setsockopt);
    WREG("getsockopt", pcps4_abi_getsockopt);
    WREG("getaddrinfo", pcps4_abi_getaddrinfo);
    WREG("freeaddrinfo", pcps4_abi_freeaddrinfo);
    WREG("htons", pcps4_abi_htons);
    WREG("ntohs", pcps4_abi_ntohs);
    WREG("htonl", pcps4_abi_htonl);
    WREG("ntohl", pcps4_abi_ntohl);
    WREG("inet_addr", pcps4_abi_inet_addr);
    WREG("closesocket", pcps4_abi_closesocket);
#undef WREG
}

static void register_dxgi(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
    (void)pcps4_pe_resolver_register(
        resolver, dll, "CreateDXGIFactory",
        (void *)pcps4_abi_CreateDXGIFactory);
    (void)pcps4_pe_resolver_register(
        resolver, dll, "CreateDXGIFactory1",
        (void *)pcps4_abi_CreateDXGIFactory1);
}

static void register_xinput(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define XREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    XREG("XInputGetState", pcps4_abi_XInputGetState);
    XREG("XInputSetState", pcps4_abi_XInputSetState);
    XREG("XInputGetCapabilities", pcps4_abi_XInputGetCapabilities);
    XREG("XInputEnable", pcps4_abi_XInputEnable);
#undef XREG
}

static PCPS4_MS_ABI void *crt_malloc(SIZE_T n) { return malloc(n); }
static PCPS4_MS_ABI void *crt_calloc(SIZE_T n, SIZE_T s) { return calloc(n, s); }
static PCPS4_MS_ABI void *crt_realloc(void *p, SIZE_T n) { return realloc(p, n); }
static PCPS4_MS_ABI void crt_free(void *p) { free(p); }
static PCPS4_MS_ABI void *crt_memcpy(void *d, const void *s, SIZE_T n) { return memcpy(d, s, n); }
static PCPS4_MS_ABI void *crt_memmove(void *d, const void *s, SIZE_T n) { return memmove(d, s, n); }
static PCPS4_MS_ABI void *crt_memset(void *d, int c, SIZE_T n) { return memset(d, c, n); }
static PCPS4_MS_ABI int crt_memcmp(const void *a, const void *b, SIZE_T n) { return memcmp(a, b, n); }
static PCPS4_MS_ABI SIZE_T crt_strlen(const char *s) { return strlen(s); }
static PCPS4_MS_ABI int crt_strcmp(const char *a, const char *b) { return strcmp(a, b); }
static PCPS4_MS_ABI int crt_strncmp(const char *a, const char *b, SIZE_T n) { return strncmp(a, b, n); }
static PCPS4_MS_ABI char *crt_strstr(const char *a, const char *b) { return strstr(a, b); }
static PCPS4_MS_ABI char *crt_strchr(const char *s, int c) { return strchr(s, c); }
static PCPS4_MS_ABI char *crt_strrchr(const char *s, int c) { return strrchr(s, c); }
static PCPS4_MS_ABI int crt_atoi(const char *s) { return atoi(s); }
static PCPS4_MS_ABI long crt_atol(const char *s) { return atol(s); }
static PCPS4_MS_ABI long crt_strtol(const char *s, char **e, int base) { return strtol(s,e,base); }
static PCPS4_MS_ABI unsigned long crt_strtoul(const char *s, char **e, int base) { return strtoul(s,e,base); }
static PCPS4_MS_ABI long long crt_strtoll(const char *s, char **e, int base) { return strtoll(s,e,base); }
static PCPS4_MS_ABI unsigned long long crt_strtoull(const char *s, char **e, int base) { return strtoull(s,e,base); }
static PCPS4_MS_ABI void crt_abort(void) { abort(); }
static PCPS4_MS_ABI void crt_exit(int code) { exit(code); }
static PCPS4_MS_ABI FILE *crt_fopen(const char *p, const char *m) { return fopen(p, m); }
static PCPS4_MS_ABI int crt_fclose(FILE *f) { return fclose(f); }
static PCPS4_MS_ABI SIZE_T crt_fread(void *p, SIZE_T s, SIZE_T n, FILE *f) { return fread(p,s,n,f); }
static PCPS4_MS_ABI SIZE_T crt_fwrite(const void *p, SIZE_T s, SIZE_T n, FILE *f) { return fwrite(p,s,n,f); }
static PCPS4_MS_ABI int crt_fseek(FILE *f, long o, int w) { return fseek(f,o,w); }
static PCPS4_MS_ABI long crt_ftell(FILE *f) { return ftell(f); }
static PCPS4_MS_ABI int crt_rand(void) { return rand(); }
static PCPS4_MS_ABI void crt_srand(unsigned int s) { srand(s); }

static void register_ucrt(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define UREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    UREG("malloc", crt_malloc);
    UREG("calloc", crt_calloc);
    UREG("realloc", crt_realloc);
    UREG("free", crt_free);
    UREG("memcpy", crt_memcpy);
    UREG("memmove", crt_memmove);
    UREG("memset", crt_memset);
    UREG("memcmp", crt_memcmp);
    UREG("strlen", crt_strlen);
    UREG("strcmp", crt_strcmp);
    UREG("strncmp", crt_strncmp);
    UREG("strstr", crt_strstr);
    UREG("strchr", crt_strchr);
    UREG("strrchr", crt_strrchr);
    UREG("atoi", crt_atoi);
    UREG("atol", crt_atol);
    UREG("strtol", crt_strtol);
    UREG("strtoul", crt_strtoul);
    UREG("strtoll", crt_strtoll);
    UREG("strtoull", crt_strtoull);
    UREG("abort", crt_abort);
    UREG("exit", crt_exit);
    UREG("fopen", crt_fopen);
    UREG("fclose", crt_fclose);
    UREG("fread", crt_fread);
    UREG("fwrite", crt_fwrite);
    UREG("fseek", crt_fseek);
    UREG("ftell", crt_ftell);
    UREG("rand", crt_rand);
    UREG("srand", crt_srand);
#undef UREG
}

void pcps4_winapi_build_resolver(pcps4_pe_resolver *resolver)
{
    if (resolver == NULL)
        return;

    pcps4_pe_resolver_init(resolver);
    g_resolver = resolver;
    resolver->fallback = pcps4_dynamic_resolve;
    resolver->fallback_user = NULL;

    register_kernel32(resolver, "KERNEL32.dll");
    register_kernel32(resolver, "KERNELBASE.dll");
    register_winsock(resolver, "WS2_32.dll");
    register_d3d11(resolver, "d3d11.dll");
    register_dxgi(resolver, "dxgi.dll");
    register_xinput(resolver, "XINPUT1_4.dll");
    register_xinput(resolver, "XINPUT1_3.dll");
    register_ucrt(resolver, "ucrtbase.dll");
    register_ucrt(resolver, "VCRUNTIME140.dll");
    register_ucrt(resolver, "MSVCRT.dll");


    /*
     * Some compilers import the memory functions from the API-set CRT DLL
     * names. They are intentionally added explicitly rather than making the
     * PE resolver guess based on prefixes.
     */
    register_ucrt(resolver, "api-ms-win-crt-runtime-l1-1-0.dll");
    register_ucrt(resolver, "api-ms-win-crt-string-l1-1-0.dll");
    register_ucrt(resolver, "api-ms-win-crt-heap-l1-1-0.dll");
}
