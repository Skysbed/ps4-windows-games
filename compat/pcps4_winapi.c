#include "pcps4_winapi.h"

#include "pcps4_compat.h"
#include "pcps4_xinput.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static __thread DWORD g_last_error = 0;

static DWORD WINAPI_HasNoWinError = 0;

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

static void WINAPI win_ExitProcess(UINT code)
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
#undef KREG
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

    register_kernel32(resolver, "KERNEL32.dll");
    register_kernel32(resolver, "KERNELBASE.dll");
    register_winsock(resolver, "WS2_32.dll");
    register_xinput(resolver, "XINPUT1_4.dll");
    register_xinput(resolver, "XINPUT1_3.dll");
    register_ucrt(resolver, "ucrtbase.dll");
    register_ucrt(resolver, "VCRUNTIME140.dll");
    register_ucrt(resolver, "MSVCRT.dll");

    (void)pcps4_pe_resolver_register_ordinal(
        resolver, "KERNEL32.dll", 1, (void *)win_GetLastError);

    /*
     * Some compilers import the memory functions from the API-set CRT DLL
     * names. They are intentionally added explicitly rather than making the
     * PE resolver guess based on prefixes.
     */
    register_ucrt(resolver, "api-ms-win-crt-runtime-l1-1-0.dll");
    register_ucrt(resolver, "api-ms-win-crt-string-l1-1-0.dll");
    register_ucrt(resolver, "api-ms-win-crt-heap-l1-1-0.dll");

    (void)WINAPI_HasNoWinError;
}
