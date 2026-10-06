#ifndef PCPS4_ABI_H
#define PCPS4_ABI_H

/*
 * Microsoft x64 ABI entry points exposed to native Windows PE binaries.
 * The PS4 application itself is built with the normal System V x86-64 ABI.
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
    void *pAdapter, D3D_DRIVER_TYPE DriverType, void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    void **ppSwapChain, void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel, void **ppImmediateContext);

PCPS4_MS_ABI HRESULT pcps4_abi_D3D11CreateDevice(
    void *pAdapter, D3D_DRIVER_TYPE DriverType, void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel, void **ppImmediateContext);

#define PCPS4_ABI_DECLARE(ret, name, args) PCPS4_MS_ABI ret pcps4_abi_##name args

PCPS4_ABI_DECLARE(DWORD, GetCurrentProcessId, (void));
PCPS4_ABI_DECLARE(DWORD, GetCurrentThreadId, (void));
PCPS4_ABI_DECLARE(uint64_t, GetTickCount64, (void));
PCPS4_ABI_DECLARE(DWORD, GetTickCount, (void));
PCPS4_ABI_DECLARE(void, Sleep, (DWORD milliseconds));
PCPS4_ABI_DECLARE(int32_t, InterlockedIncrement, (volatile int32_t *value));
PCPS4_ABI_DECLARE(int32_t, InterlockedDecrement, (volatile int32_t *value));

PCPS4_ABI_DECLARE(HANDLE, CreateFileA,
    (LPCSTR, uint32_t, uint32_t, void *, uint32_t, uint32_t, void *));
PCPS4_ABI_DECLARE(BOOL, ReadFile,
    (HANDLE, void *, DWORD, DWORD *, void *));
PCPS4_ABI_DECLARE(BOOL, WriteFile,
    (HANDLE, const void *, DWORD, DWORD *, void *));
PCPS4_ABI_DECLARE(DWORD, GetFileSize, (HANDLE, DWORD *));
PCPS4_ABI_DECLARE(DWORD, SetFilePointer, (HANDLE, int32_t, int32_t *, DWORD));
PCPS4_ABI_DECLARE(BOOL, DeleteFileA, (LPCSTR));
PCPS4_ABI_DECLARE(BOOL, CreateDirectoryA, (LPCSTR, void *));
PCPS4_ABI_DECLARE(BOOL, CloseHandle, (HANDLE));
PCPS4_ABI_DECLARE(HANDLE, CreateThread,
    (void *, SIZE_T, LPTHREAD_START_ROUTINE, LPVOID, DWORD, DWORD *));
PCPS4_ABI_DECLARE(HANDLE, CreateMutexA, (void *, BOOL, LPCSTR));
PCPS4_ABI_DECLARE(HANDLE, CreateEventA, (void *, BOOL, BOOL, LPCSTR));
PCPS4_ABI_DECLARE(BOOL, SetEvent, (HANDLE));
PCPS4_ABI_DECLARE(BOOL, ResetEvent, (HANDLE));
PCPS4_ABI_DECLARE(DWORD, WaitForSingleObject, (HANDLE, DWORD));
PCPS4_ABI_DECLARE(BOOL, ReleaseMutex, (HANDLE));
PCPS4_ABI_DECLARE(void *, VirtualAlloc, (void *, SIZE_T, DWORD, DWORD));
PCPS4_ABI_DECLARE(BOOL, VirtualFree, (void *, SIZE_T, DWORD));

PCPS4_ABI_DECLARE(int, WSAStartup, (WORD, WSADATA *));
PCPS4_ABI_DECLARE(int, WSACleanup, (void));
PCPS4_ABI_DECLARE(int, WSAGetLastError, (void));
PCPS4_ABI_DECLARE(int, socket, (int, int, int));
PCPS4_ABI_DECLARE(int, connect, (int, const struct sockaddr *, socklen_t));
PCPS4_ABI_DECLARE(int, send, (int, const char *, int, int));
PCPS4_ABI_DECLARE(int, recv, (int, char *, int, int));
PCPS4_ABI_DECLARE(int, shutdown, (int, int));
PCPS4_ABI_DECLARE(int, setsockopt, (int, int, int, const char *, int));
PCPS4_ABI_DECLARE(int, getsockopt, (int, int, int, char *, int *));
PCPS4_ABI_DECLARE(int, getaddrinfo,
    (const char *, const char *, const struct addrinfo *, struct addrinfo **));
PCPS4_ABI_DECLARE(void, freeaddrinfo, (struct addrinfo *));
PCPS4_ABI_DECLARE(uint16_t, htons, (uint16_t));
PCPS4_ABI_DECLARE(uint16_t, ntohs, (uint16_t));
PCPS4_ABI_DECLARE(uint32_t, htonl, (uint32_t));
PCPS4_ABI_DECLARE(uint32_t, ntohl, (uint32_t));
PCPS4_ABI_DECLARE(uint32_t, inet_addr, (const char *));
PCPS4_ABI_DECLARE(int, closesocket, (int));

PCPS4_ABI_DECLARE(DWORD, XInputGetState, (DWORD, XINPUT_STATE *));
PCPS4_ABI_DECLARE(DWORD, XInputSetState, (DWORD, XINPUT_VIBRATION *));
PCPS4_ABI_DECLARE(DWORD, XInputGetCapabilities,
    (DWORD, DWORD, XINPUT_CAPABILITIES *));
PCPS4_ABI_DECLARE(void, XInputEnable, (BOOL));

#undef PCPS4_ABI_DECLARE

#ifdef __cplusplus
}
#endif

#endif
