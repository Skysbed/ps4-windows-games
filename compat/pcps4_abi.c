#include "pcps4_abi.h"

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

PCPS4_MS_ABI void pcps4_abi_ExitProcess(UINT code)
{
    exit((int)code);
}

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
    void **ppImmediateContext)
{
    return pcps4_d3d11_com_create_device_and_swap_chain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion,
        pSwapChainDesc, ppSwapChain, ppDevice,
        pFeatureLevel, ppImmediateContext);
}

#define WRAP(name, ret, args, callargs)     PCPS4_MS_ABI ret pcps4_abi_##name args { return pcps4_##name callargs; }

#define WRAP_VOID(name, args, callargs)     PCPS4_MS_ABI void pcps4_abi_##name args { pcps4_##name callargs; }

WRAP(GetLastError, DWORD, (void), ())
WRAP_VOID(SetLastError, (DWORD value), (value))
WRAP(GetCurrentProcess, HANDLE, (void), ())
WRAP(GetCurrentThread, HANDLE, (void), ())
WRAP(GetCurrentProcessId, DWORD, (void), ())
WRAP(GetCurrentThreadId, DWORD, (void), ())
WRAP(GetTickCount, DWORD, (void), ())
WRAP(GetTickCount64, uint64_t, (void), ())
WRAP_VOID(Sleep, (DWORD milliseconds), (milliseconds))
WRAP(InterlockedIncrement, LONG, (volatile LONG *value), (value))
WRAP(InterlockedDecrement, LONG, (volatile LONG *value), (value))

WRAP(CreateFileA, HANDLE,
     (LPCSTR path, uint32_t desired_access, uint32_t share_mode,
      void *security_attributes, uint32_t creation_disposition,
      uint32_t attributes, void *template_file),
     (path, desired_access, share_mode, security_attributes,
      creation_disposition, attributes, template_file))
WRAP(ReadFile, BOOL,
     (HANDLE handle, void *buffer, DWORD bytes_to_read,
      DWORD *bytes_read, void *overlapped),
     (handle, buffer, bytes_to_read, bytes_read, overlapped))
WRAP(WriteFile, BOOL,
     (HANDLE handle, const void *buffer, DWORD bytes_to_write,
      DWORD *bytes_written, void *overlapped),
     (handle, buffer, bytes_to_write, bytes_written, overlapped))
WRAP(GetFileSize, DWORD, (HANDLE handle, DWORD *high), (handle, high))
WRAP(SetFilePointer, DWORD,
     (HANDLE handle, int32_t distance_low, int32_t *distance_high,
      DWORD move_method),
     (handle, distance_low, distance_high, move_method))
WRAP(DeleteFileA, BOOL, (LPCSTR path), (path))
WRAP(CreateDirectoryA, BOOL, (LPCSTR path, void *security), (path, security))
WRAP(CloseHandle, BOOL, (HANDLE handle), (handle))
WRAP(CreateThread, HANDLE,
     (void *security_attributes, SIZE_T stack_size,
      LPTHREAD_START_ROUTINE start_address, LPVOID parameter,
      DWORD creation_flags, DWORD *thread_id),
     (security_attributes, stack_size, start_address, parameter,
      creation_flags, thread_id))
WRAP(CreateMutexA, HANDLE,
     (void *attributes, BOOL initial_owner, LPCSTR name),
     (attributes, initial_owner, name))
WRAP(CreateEventA, HANDLE,
     (void *attributes, BOOL manual_reset, BOOL initial_state, LPCSTR name),
     (attributes, manual_reset, initial_state, name))
WRAP(SetEvent, BOOL, (HANDLE handle), (handle))
WRAP(ResetEvent, BOOL, (HANDLE handle), (handle))
WRAP(WaitForSingleObject, DWORD,
     (HANDLE handle, DWORD milliseconds), (handle, milliseconds))
WRAP(ReleaseMutex, BOOL, (HANDLE handle), (handle))
WRAP(VirtualAlloc, void *,
     (void *address, SIZE_T size, DWORD allocation_type, DWORD protection),
     (address, size, allocation_type, protection))
WRAP(VirtualFree, BOOL,
     (void *address, SIZE_T size, DWORD free_type),
     (address, size, free_type))

WRAP(GetEnvironmentVariableA, DWORD,
     (LPCSTR name, LPSTR buffer, DWORD size), (name, buffer, size))
WRAP(GetCommandLineA, LPSTR, (void), ())
WRAP(GetModuleFileNameA, DWORD,
     (HANDLE module, LPSTR filename, DWORD size),
     (module, filename, size))
WRAP(QueryPerformanceCounter, BOOL, (int64_t *counter), (counter))
WRAP(QueryPerformanceFrequency, BOOL, (int64_t *frequency), (frequency))
WRAP(GetProcessHeap, HANDLE, (void), ())
WRAP(HeapAlloc, void *,
     (HANDLE heap, DWORD flags, SIZE_T size), (heap, flags, size))
WRAP(HeapReAlloc, void *,
     (HANDLE heap, DWORD flags, void *memory, SIZE_T size),
     (heap, flags, memory, size))
WRAP(HeapFree, BOOL,
     (HANDLE heap, DWORD flags, void *memory), (heap, flags, memory))
WRAP_VOID(RtlMoveMemory,
          (void *dst, const void *src, SIZE_T length),
          (dst, src, length))

WRAP(LoadLibraryA, HMODULE, (LPCSTR name), (name))
WRAP(LoadLibraryW, HMODULE, (LPCWSTR name), (name))
WRAP(GetModuleHandleA, HMODULE, (LPCSTR name), (name))
WRAP(GetModuleHandleW, HMODULE, (LPCWSTR name), (name))
WRAP(GetProcAddress, FARPROC,
     (HMODULE module, LPCSTR name), (module, name))
WRAP(FreeLibrary, BOOL, (HMODULE module), (module))
WRAP(GetEnvironmentVariableW, DWORD,
     (LPCWSTR name, LPWSTR buffer, DWORD size), (name, buffer, size))

WRAP(WSAStartup, int, (WORD version, WSADATA *data), (version, data))
WRAP_VOID(WSACleanup, (void), ())
WRAP(WSAGetLastError, int, (void), ())
WRAP(socket, int, (int domain, int type, int protocol),
     (domain, type, protocol))
WRAP(connect, int,
     (int socket_fd, const struct sockaddr *address, socklen_t length),
     (socket_fd, address, length))
WRAP(send, int,
     (int socket_fd, const char *buffer, int length, int flags),
     (socket_fd, buffer, length, flags))
WRAP(recv, int,
     (int socket_fd, char *buffer, int length, int flags),
     (socket_fd, buffer, length, flags))
WRAP(shutdown, int, (int socket_fd, int how), (socket_fd, how))
WRAP(setsockopt, int,
     (int socket_fd, int level, int option, const char *value, int length),
     (socket_fd, level, option, value, length))
WRAP(getsockopt, int,
     (int socket_fd, int level, int option, char *value, int *length),
     (socket_fd, level, option, value, length))
WRAP(getaddrinfo, int,
     (const char *node, const char *service,
      const struct addrinfo *hints, struct addrinfo **result),
     (node, service, hints, result))
WRAP_VOID(freeaddrinfo, (struct addrinfo *result), (result))
WRAP(htons, uint16_t, (uint16_t value), (value))
WRAP(ntohs, uint16_t, (uint16_t value), (value))
WRAP(htonl, uint32_t, (uint32_t value), (value))
WRAP(ntohl, uint32_t, (uint32_t value), (value))
WRAP(inet_addr, uint32_t, (const char *value), (value))
WRAP(closesocket, int, (int socket_fd), (socket_fd))

WRAP(XInputGetState, DWORD,
     (DWORD user_index, XINPUT_STATE *state), (user_index, state))
WRAP(XInputSetState, DWORD,
     (DWORD user_index, XINPUT_VIBRATION *vibration),
     (user_index, vibration))
WRAP(XInputGetCapabilities, DWORD,
     (DWORD user_index, DWORD flags, XINPUT_CAPABILITIES *capabilities),
     (user_index, flags, capabilities))
WRAP_VOID(XInputEnable, (BOOL enable), (enable))

WRAP(malloc, void *, (size_t size), (size))
WRAP(calloc, void *, (size_t count, size_t size), (count, size))
WRAP(realloc, void *, (void *memory, size_t size), (memory, size))
WRAP_VOID(free, (void *memory), (memory))
WRAP(memcpy, void *,
     (void *destination, const void *source, size_t length),
     (destination, source, length))
WRAP(memmove, void *,
     (void *destination, const void *source, size_t length),
     (destination, source, length))
WRAP(memset, void *,
     (void *destination, int value, size_t length),
     (destination, value, length))
WRAP(memcmp, int,
     (const void *left, const void *right, size_t length),
     (left, right, length))
WRAP(strlen, size_t, (const char *value), (value))
WRAP(strcmp, int, (const char *left, const char *right), (left, right))
WRAP(strncmp, int,
     (const char *left, const char *right, size_t length),
     (left, right, length))
WRAP(strstr, char *,
     (const char *haystack, const char *needle), (haystack, needle))
WRAP(strchr, char *, (const char *value, int ch), (value, ch))
WRAP(strrchr, char *, (const char *value, int ch), (value, ch))
WRAP(atoi, int, (const char *value), (value))
WRAP(atol, long, (const char *value), (value))
WRAP(strtol, long,
     (const char *value, char **end, int base), (value, end, base))
WRAP(strtoul, unsigned long,
     (const char *value, char **end, int base), (value, end, base))
WRAP(strtoll, long long,
     (const char *value, char **end, int base), (value, end, base))
WRAP(strtoull, unsigned long long,
     (const char *value, char **end, int base), (value, end, base))
WRAP_VOID(abort, (void), ())
WRAP_VOID(exit, (int code), (code))
WRAP(fopen, FILE *,
     (const char *path, const char *mode), (path, mode))
WRAP(fclose, int, (FILE *file), (file))
WRAP(fread, size_t,
     (void *buffer, size_t size, size_t count, FILE *file),
     (buffer, size, count, file))
WRAP(fwrite, size_t,
     (const void *buffer, size_t size, size_t count, FILE *file),
     (buffer, size, count, file))
WRAP(fseek, int, (FILE *file, long offset, int origin),
     (file, offset, origin))
WRAP(ftell, long, (FILE *file), (file))

#undef WRAP
#undef WRAP_VOID
