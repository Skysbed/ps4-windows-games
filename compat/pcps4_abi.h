#ifndef PCPS4_ABI_H
#define PCPS4_ABI_H

/*
 * Microsoft x64 ABI entry points used by native Windows PE binaries.
 *
 * The OpenOrbis application itself is built for the normal x86-64 System V
 * ABI. Native PE code, however, follows the Microsoft x64 ABI. Functions
 * exposed to the PE resolver therefore need an explicit ms_abi boundary.
 */
#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#endif

#include "pcps4_compat.h"
#include "pcps4_d3d11_com.h"
#include "pcps4_xinput.h"

#ifdef __cplusplus
extern "C" {
#endif

PCPS4_MS_ABI void pcps4_abi_ExitProcess(UINT code);
PCPS4_MS_ABI HRESULT pcps4_abi_D3D11CreateDeviceAndSwapChain(
    void *pAdapter,
    D3D_DRIVER_TYPE DriverType,
    void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    void **ppSwapChain,
    void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    void **ppImmediateContext);

#define PCPS4_ABI_DECLARE(ret, name, args) PCPS4_MS_ABI ret pcps4_abi_##name args

PCPS4_ABI_DECLARE(DWORD, GetLastError, (void));
PCPS4_ABI_DECLARE(void, SetLastError, (DWORD value));
PCPS4_ABI_DECLARE(HANDLE, GetCurrentProcess, (void));
PCPS4_ABI_DECLARE(HANDLE, GetCurrentThread, (void));
PCPS4_ABI_DECLARE(DWORD, GetCurrentProcessId, (void));
PCPS4_ABI_DECLARE(DWORD, GetCurrentThreadId, (void));
PCPS4_ABI_DECLARE(DWORD, GetTickCount, (void));
PCPS4_ABI_DECLARE(uint64_t, GetTickCount64, (void));
PCPS4_ABI_DECLARE(void, Sleep, (DWORD milliseconds));
PCPS4_ABI_DECLARE(LONG, InterlockedIncrement, (volatile LONG *value));
PCPS4_ABI_DECLARE(LONG, InterlockedDecrement, (volatile LONG *value));

PCPS4_ABI_DECLARE(HANDLE, CreateFileA,
                  (LPCSTR path, uint32_t desired_access,
                   uint32_t share_mode, void *security_attributes,
                   uint32_t creation_disposition, uint32_t attributes,
                   void *template_file));
PCPS4_ABI_DECLARE(BOOL, ReadFile,
                  (HANDLE handle, void *buffer, DWORD bytes_to_read,
                   DWORD *bytes_read, void *overlapped));
PCPS4_ABI_DECLARE(BOOL, WriteFile,
                  (HANDLE handle, const void *buffer, DWORD bytes_to_write,
                   DWORD *bytes_written, void *overlapped));
PCPS4_ABI_DECLARE(DWORD, GetFileSize, (HANDLE handle, DWORD *high));
PCPS4_ABI_DECLARE(DWORD, SetFilePointer,
                  (HANDLE handle, int32_t distance_low,
                   int32_t *distance_high, DWORD move_method));
PCPS4_ABI_DECLARE(BOOL, DeleteFileA, (LPCSTR path));
PCPS4_ABI_DECLARE(BOOL, CreateDirectoryA, (LPCSTR path, void *security));
PCPS4_ABI_DECLARE(BOOL, CloseHandle, (HANDLE handle));
PCPS4_ABI_DECLARE(HANDLE, CreateThread,
                  (void *security_attributes, SIZE_T stack_size,
                   LPTHREAD_START_ROUTINE start_address, LPVOID parameter,
                   DWORD creation_flags, DWORD *thread_id));
PCPS4_ABI_DECLARE(HANDLE, CreateMutexA,
                  (void *attributes, BOOL initial_owner, LPCSTR name));
PCPS4_ABI_DECLARE(HANDLE, CreateEventA,
                  (void *attributes, BOOL manual_reset, BOOL initial_state,
                   LPCSTR name));
PCPS4_ABI_DECLARE(BOOL, SetEvent, (HANDLE handle));
PCPS4_ABI_DECLARE(BOOL, ResetEvent, (HANDLE handle));
PCPS4_ABI_DECLARE(DWORD, WaitForSingleObject,
                  (HANDLE handle, DWORD milliseconds));
PCPS4_ABI_DECLARE(BOOL, ReleaseMutex, (HANDLE handle));
PCPS4_ABI_DECLARE(void *, VirtualAlloc,
                  (void *address, SIZE_T size, DWORD allocation_type,
                   DWORD protection));
PCPS4_ABI_DECLARE(BOOL, VirtualFree,
                  (void *address, SIZE_T size, DWORD free_type));

PCPS4_ABI_DECLARE(DWORD, GetEnvironmentVariableA,
                  (LPCSTR name, LPSTR buffer, DWORD size));
PCPS4_ABI_DECLARE(LPSTR, GetCommandLineA, (void));
PCPS4_ABI_DECLARE(DWORD, GetModuleFileNameA,
                  (HANDLE module, LPSTR filename, DWORD size));
PCPS4_ABI_DECLARE(BOOL, QueryPerformanceCounter, (int64_t *counter));
PCPS4_ABI_DECLARE(BOOL, QueryPerformanceFrequency, (int64_t *frequency));
PCPS4_ABI_DECLARE(HANDLE, GetProcessHeap, (void));
PCPS4_ABI_DECLARE(void *, HeapAlloc,
                  (HANDLE heap, DWORD flags, SIZE_T size));
PCPS4_ABI_DECLARE(void *, HeapReAlloc,
                  (HANDLE heap, DWORD flags, void *memory, SIZE_T size));
PCPS4_ABI_DECLARE(BOOL, HeapFree,
                  (HANDLE heap, DWORD flags, void *memory));
PCPS4_ABI_DECLARE(void, RtlMoveMemory,
                  (void *dst, const void *src, SIZE_T length));

PCPS4_ABI_DECLARE(HMODULE, LoadLibraryA, (LPCSTR name));
PCPS4_ABI_DECLARE(HMODULE, LoadLibraryW, (LPCWSTR name));
PCPS4_ABI_DECLARE(HMODULE, GetModuleHandleA, (LPCSTR name));
PCPS4_ABI_DECLARE(HMODULE, GetModuleHandleW, (LPCWSTR name));
PCPS4_ABI_DECLARE(FARPROC, GetProcAddress, (HMODULE module, LPCSTR name));
PCPS4_ABI_DECLARE(BOOL, FreeLibrary, (HMODULE module));
PCPS4_ABI_DECLARE(DWORD, GetEnvironmentVariableW,
                  (LPCWSTR name, LPWSTR buffer, DWORD size));

PCPS4_ABI_DECLARE(int, WSAStartup, (WORD version, WSADATA *data));
PCPS4_ABI_DECLARE(int, WSACleanup, (void));
PCPS4_ABI_DECLARE(int, WSAGetLastError, (void));
PCPS4_ABI_DECLARE(int, socket, (int domain, int type, int protocol));
PCPS4_ABI_DECLARE(int, connect,
                  (int socket_fd, const struct sockaddr *address, socklen_t length));
PCPS4_ABI_DECLARE(int, send,
                  (int socket_fd, const char *buffer, int length, int flags));
PCPS4_ABI_DECLARE(int, recv,
                  (int socket_fd, char *buffer, int length, int flags));
PCPS4_ABI_DECLARE(int, shutdown, (int socket_fd, int how));
PCPS4_ABI_DECLARE(int, setsockopt,
                  (int socket_fd, int level, int option,
                   const char *value, int length));
PCPS4_ABI_DECLARE(int, getsockopt,
                  (int socket_fd, int level, int option,
                   char *value, int *length));
PCPS4_ABI_DECLARE(int, getaddrinfo,
                  (const char *node, const char *service,
                   const struct addrinfo *hints, struct addrinfo **result));
PCPS4_ABI_DECLARE(void, freeaddrinfo, (struct addrinfo *result));
PCPS4_ABI_DECLARE(uint16_t, htons, (uint16_t value));
PCPS4_ABI_DECLARE(uint16_t, ntohs, (uint16_t value));
PCPS4_ABI_DECLARE(uint32_t, htonl, (uint32_t value));
PCPS4_ABI_DECLARE(uint32_t, ntohl, (uint32_t value));
PCPS4_ABI_DECLARE(uint32_t, inet_addr, (const char *value));
PCPS4_ABI_DECLARE(int, closesocket, (int socket_fd));

PCPS4_ABI_DECLARE(DWORD, XInputGetState,
                  (DWORD user_index, XINPUT_STATE *state));
PCPS4_ABI_DECLARE(DWORD, XInputSetState,
                  (DWORD user_index, XINPUT_VIBRATION *vibration));
PCPS4_ABI_DECLARE(DWORD, XInputGetCapabilities,
                  (DWORD user_index, DWORD flags,
                   XINPUT_CAPABILITIES *capabilities));
PCPS4_ABI_DECLARE(void, XInputEnable, (BOOL enable));

PCPS4_ABI_DECLARE(void *, malloc, (size_t size));
PCPS4_ABI_DECLARE(void *, calloc, (size_t count, size_t size));
PCPS4_ABI_DECLARE(void *, realloc, (void *memory, size_t size));
PCPS4_ABI_DECLARE(void, free, (void *memory));
PCPS4_ABI_DECLARE(void *, memcpy,
                  (void *destination, const void *source, size_t length));
PCPS4_ABI_DECLARE(void *, memmove,
                  (void *destination, const void *source, size_t length));
PCPS4_ABI_DECLARE(void *, memset,
                  (void *destination, int value, size_t length));
PCPS4_ABI_DECLARE(int, memcmp,
                  (const void *left, const void *right, size_t length));
PCPS4_ABI_DECLARE(size_t, strlen, (const char *value));
PCPS4_ABI_DECLARE(int, strcmp, (const char *left, const char *right));
PCPS4_ABI_DECLARE(int, strncmp,
                  (const char *left, const char *right, size_t length));
PCPS4_ABI_DECLARE(char *, strstr, (const char *haystack, const char *needle));
PCPS4_ABI_DECLARE(char *, strchr, (const char *value, int ch));
PCPS4_ABI_DECLARE(char *, strrchr, (const char *value, int ch));
PCPS4_ABI_DECLARE(int, atoi, (const char *value));
PCPS4_ABI_DECLARE(long, atol, (const char *value));
PCPS4_ABI_DECLARE(long, strtol,
                  (const char *value, char **end, int base));
PCPS4_ABI_DECLARE(unsigned long, strtoul,
                  (const char *value, char **end, int base));
PCPS4_ABI_DECLARE(long long, strtoll,
                  (const char *value, char **end, int base));
PCPS4_ABI_DECLARE(unsigned long long, strtoull,
                  (const char *value, char **end, int base));
PCPS4_ABI_DECLARE(void, abort, (void));
PCPS4_ABI_DECLARE(void, exit, (int code));
PCPS4_ABI_DECLARE(FILE *, fopen, (const char *path, const char *mode));
PCPS4_ABI_DECLARE(int, fclose, (FILE *file));
PCPS4_ABI_DECLARE(size_t, fread,
                  (void *buffer, size_t size, size_t count, FILE *file));
PCPS4_ABI_DECLARE(size_t, fwrite,
                  (const void *buffer, size_t size, size_t count, FILE *file));
PCPS4_ABI_DECLARE(int, fseek, (FILE *file, long offset, int origin));
PCPS4_ABI_DECLARE(long, ftell, (FILE *file));

#undef PCPS4_ABI_DECLARE

#ifdef __cplusplus
}
#endif

#endif
