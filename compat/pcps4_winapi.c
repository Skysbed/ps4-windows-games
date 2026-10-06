#define _POSIX_C_SOURCE 200112L
#include "pcps4_winapi.h"

#include "pcps4_compat.h"
#include "pcps4_xinput.h"
#include "pcps4_d3d11_com.h"

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

static void win_SetLastError(DWORD value);
static HMODULE win_LoadLibraryA(LPCSTR name);
static HMODULE win_LoadLibraryW(LPCWSTR name);
static HMODULE win_GetModuleHandleA(LPCSTR name);
static HMODULE win_GetModuleHandleW(LPCWSTR name);
static DWORD win_GetEnvironmentVariableA(
    LPCSTR name,
    LPSTR buffer,
    DWORD size);
static DWORD win_GetEnvironmentVariableW(
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

static HMODULE win_LoadLibraryW(LPCWSTR name)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(name, utf8, sizeof(utf8)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    return win_LoadLibraryA(utf8);
}

static HMODULE win_GetModuleHandleW(LPCWSTR name)
{
    return win_LoadLibraryW(name);
}

static HMODULE win_LoadLibraryA(LPCSTR name)
{
    pcps4_module_handle *module;
    char normalized[128];

    if (module_name_normalize(name, normalized, sizeof(normalized)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    module = (pcps4_module_handle *)calloc(1, sizeof(*module));
    if (module == NULL) {
        win_SetLastError(8);
        return NULL;
    }

    module->magic = PCPS4_MODULE_MAGIC;
    memcpy(module->name, normalized, strlen(normalized) + 1);
    return (HMODULE)module;
}

static HMODULE win_GetModuleHandleA(LPCSTR name)
{
    return win_LoadLibraryA(name);
}

static FARPROC win_GetProcAddress(
    HMODULE module,
    LPCSTR name)
{
    pcps4_module_handle *handle = (pcps4_module_handle *)module;
    void *fn = NULL;

    if (name == NULL || g_resolver == NULL)
        return NULL;

    if (handle == NULL || handle->magic != PCPS4_MODULE_MAGIC) {
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

static DWORD win_GetEnvironmentVariableW(
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

static BOOL win_FreeLibrary(HMODULE module)
{
    pcps4_module_handle *handle = (pcps4_module_handle *)module;

    if (handle == NULL || handle->magic != PCPS4_MODULE_MAGIC)
        return FALSE;

    handle->magic = 0;
    free(handle);
    return TRUE;
}


static DWORD win_GetLastError(void)
{
    return g_last_error;
}

static void win_SetLastError(DWORD value)
{
    g_last_error = value;
}

static HANDLE win_GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)1;
}

static HANDLE win_GetCurrentThread(void)
{
    return (HANDLE)(intptr_t)1;
}

static void win_ExitProcess(UINT code)
{
    exit((int)code);
}

static DWORD win_GetEnvironmentVariableA(
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

static LPSTR win_GetCommandLineA(void)
{
    static char command_line[] = "game.exe";
    return command_line;
}

static DWORD win_GetModuleFileNameA(
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

static BOOL win_QueryPerformanceCounter(int64_t *counter)
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

static BOOL win_QueryPerformanceFrequency(int64_t *frequency)
{
    if (frequency == NULL)
        return FALSE;

    *frequency = 1000000000LL;
    return TRUE;
}

static HANDLE win_GetProcessHeap(void)
{
    return (HANDLE)(intptr_t)1;
}

static LPVOID win_HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    (void)heap;
    (void)flags;
    return malloc(size);
}

static LPVOID win_HeapReAlloc(
    HANDLE heap,
    DWORD flags,
    LPVOID memory,
    SIZE_T size)
{
    (void)heap;
    (void)flags;
    return realloc(memory, size);
}

static BOOL win_HeapFree(HANDLE heap, DWORD flags, LPVOID memory)
{
    (void)heap;
    (void)flags;
    free(memory);
    return TRUE;
}

static void win_RtlMoveMemory(PVOID dst, const void *src, SIZE_T length)
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
    KREG("GetCurrentProcessId", pcps4_get_current_process_id);
    KREG("GetCurrentThreadId", pcps4_get_current_thread_id);
    KREG("GetTickCount", pcps4_monotonic_ms);
    KREG("GetTickCount64", pcps4_monotonic_ms);
    KREG("Sleep", pcps4_sleep_ms);
    KREG("InterlockedIncrement", pcps4_interlocked_increment);
    KREG("InterlockedDecrement", pcps4_interlocked_decrement);
    KREG("CreateFileA", pcps4_CreateFileA);
    KREG("ReadFile", pcps4_ReadFile);
    KREG("WriteFile", pcps4_WriteFile);
    KREG("GetFileSize", pcps4_GetFileSize);
    KREG("SetFilePointer", pcps4_SetFilePointer);
    KREG("DeleteFileA", pcps4_DeleteFileA);
    KREG("CreateDirectoryA", pcps4_CreateDirectoryA);
    KREG("CloseHandle", pcps4_CloseHandle);
    KREG("CreateThread", pcps4_CreateThread);
    KREG("CreateMutexA", pcps4_CreateMutexA);
    KREG("CreateEventA", pcps4_CreateEventA);
    KREG("SetEvent", pcps4_SetEvent);
    KREG("ResetEvent", pcps4_ResetEvent);
    KREG("WaitForSingleObject", pcps4_WaitForSingleObject);
    KREG("ReleaseMutex", pcps4_ReleaseMutex);
    KREG("VirtualAlloc", pcps4_VirtualAlloc);
    KREG("VirtualFree", pcps4_VirtualFree);
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
    KREG("GetModuleHandleA", win_GetModuleHandleA);
    KREG("GetModuleHandleW", win_GetModuleHandleW);
    KREG("GetProcAddress", win_GetProcAddress);
    KREG("FreeLibrary", win_FreeLibrary);
    KREG("GetEnvironmentVariableW", win_GetEnvironmentVariableW);
#undef KREG
}

static void register_d3d11(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDeviceAndSwapChain",
        (void *)pcps4_d3d11_com_create_device_and_swap_chain);
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDevice",
        (void *)pcps4_d3d11_com_create_device);
}

static void register_winsock(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define WREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    WREG("WSAStartup", pcps4_WSAStartup);
    WREG("WSACleanup", pcps4_WSACleanup);
    WREG("WSAGetLastError", pcps4_WSAGetLastError);
    WREG("socket", pcps4_socket);
    WREG("connect", pcps4_connect);
    WREG("send", pcps4_send);
    WREG("recv", pcps4_recv);
    WREG("shutdown", pcps4_shutdown);
    WREG("setsockopt", pcps4_setsockopt);
    WREG("getsockopt", pcps4_getsockopt);
    WREG("getaddrinfo", pcps4_getaddrinfo);
    WREG("freeaddrinfo", pcps4_freeaddrinfo);
    WREG("htons", pcps4_htons);
    WREG("ntohs", pcps4_ntohs);
    WREG("htonl", pcps4_htonl);
    WREG("ntohl", pcps4_ntohl);
    WREG("inet_addr", pcps4_inet_addr);
    WREG("closesocket", pcps4_closesocket);
#undef WREG
}

static void register_xinput(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define XREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    XREG("XInputGetState", pcps4_XInputGetState);
    XREG("XInputSetState", pcps4_XInputSetState);
    XREG("XInputGetCapabilities", pcps4_XInputGetCapabilities);
    XREG("XInputEnable", pcps4_XInputEnable);
#undef XREG
}

static void register_ucrt(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
#define UREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    UREG("malloc", malloc);
    UREG("calloc", calloc);
    UREG("realloc", realloc);
    UREG("free", free);
    UREG("memcpy", memcpy);
    UREG("memmove", memmove);
    UREG("memset", memset);
    UREG("memcmp", memcmp);
    UREG("strlen", strlen);
    UREG("strcmp", strcmp);
    UREG("strncmp", strncmp);
    UREG("strstr", strstr);
    UREG("strchr", strchr);
    UREG("strrchr", strrchr);
    UREG("atoi", atoi);
    UREG("atol", atol);
    UREG("strtol", strtol);
    UREG("strtoul", strtoul);
    UREG("strtoll", strtoll);
    UREG("strtoull", strtoull);
    UREG("abort", abort);
    UREG("exit", exit);
    UREG("fopen", fopen);
    UREG("fclose", fclose);
    UREG("fread", fread);
    UREG("fwrite", fwrite);
    UREG("fseek", fseek);
    UREG("ftell", ftell);
#undef UREG
}

void pcps4_winapi_build_resolver(pcps4_pe_resolver *resolver)
{
    if (resolver == NULL)
        return;

    pcps4_pe_resolver_init(resolver);
    g_resolver = resolver;

    register_kernel32(resolver, "KERNEL32.dll");
    register_kernel32(resolver, "KERNELBASE.dll");
    register_winsock(resolver, "WS2_32.dll");
    register_d3d11(resolver, "d3d11.dll");
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
