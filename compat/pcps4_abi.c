#include "pcps4_abi.h"
#include "pcps4_loader.h"

#include <stdlib.h>

PCPS4_MS_ABI void pcps4_abi_ExitProcess(UINT code)
{
    pcps4_pe_exit_process(code);
}

PCPS4_MS_ABI HRESULT pcps4_abi_D3D11CreateDeviceAndSwapChain(
    void *pAdapter, D3D_DRIVER_TYPE DriverType, void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    void **ppSwapChain, void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel, void **ppImmediateContext)
{
    return pcps4_d3d11_com_create_device_and_swap_chain(
        pAdapter, DriverType, Software, Flags, pFeatureLevels,
        FeatureLevels, SDKVersion, pSwapChainDesc, ppSwapChain,
        ppDevice, pFeatureLevel, ppImmediateContext);
}

PCPS4_MS_ABI HRESULT pcps4_abi_D3D11CreateDevice(
    void *pAdapter, D3D_DRIVER_TYPE DriverType, void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel, void **ppImmediateContext)
{
    return pcps4_d3d11_com_create_device(
        pAdapter, DriverType, Software, Flags, pFeatureLevels,
        FeatureLevels, SDKVersion, ppDevice, pFeatureLevel,
        ppImmediateContext);
}

#define WRAP(name, ret, args, target, callargs)     PCPS4_MS_ABI ret pcps4_abi_##name args { return target callargs; }

#define WRAP_VOID(name, args, target, callargs)     PCPS4_MS_ABI void pcps4_abi_##name args { target callargs; }

WRAP(GetCurrentProcessId, DWORD, (void), pcps4_get_current_process_id, ())
WRAP(GetCurrentThreadId, DWORD, (void), pcps4_get_current_thread_id, ())
WRAP(GetTickCount64, uint64_t, (void), pcps4_monotonic_ms, ())
WRAP(GetTickCount, DWORD, (void), pcps4_monotonic_ms, ())
WRAP_VOID(Sleep, (DWORD ms), pcps4_sleep_ms, (ms))
WRAP(InterlockedIncrement, int32_t, (volatile int32_t *v), pcps4_interlocked_increment, (v))
WRAP(InterlockedDecrement, int32_t, (volatile int32_t *v), pcps4_interlocked_decrement, (v))

WRAP(CreateFileA, HANDLE,
     (LPCSTR p, uint32_t a, uint32_t s, void *sa, uint32_t d, uint32_t at, void *t),
     pcps4_CreateFileA, (p, a, s, sa, d, at, t))
WRAP(ReadFile, BOOL,
     (HANDLE h, void *b, DWORD n, DWORD *r, void *o),
     pcps4_ReadFile, (h, b, n, r, o))
WRAP(WriteFile, BOOL,
     (HANDLE h, const void *b, DWORD n, DWORD *w, void *o),
     pcps4_WriteFile, (h, b, n, w, o))
WRAP(GetFileSize, DWORD, (HANDLE h, DWORD *hi), pcps4_GetFileSize, (h, hi))
WRAP(SetFilePointer, DWORD,
     (HANDLE h, int32_t lo, int32_t *hi, DWORD m), pcps4_SetFilePointer,
     (h, lo, hi, m))
WRAP(DeleteFileA, BOOL, (LPCSTR p), pcps4_DeleteFileA, (p))
WRAP(CreateDirectoryA, BOOL, (LPCSTR p, void *s), pcps4_CreateDirectoryA, (p, s))
WRAP(CloseHandle, BOOL, (HANDLE h), pcps4_CloseHandle, (h))
/* A Windows thread procedure uses the Microsoft x64 convention (argument in rcx);
 * the runtime's thread layer calls plain System V functions. Bridge with a trampoline. */
typedef DWORD (PCPS4_MS_ABI *pcps4_ms_thread_proc)(LPVOID);
typedef struct {
    pcps4_ms_thread_proc fn;
    LPVOID arg;
} pcps4_ms_thread_start;

static uint32_t pcps4_ms_thread_trampoline(void *opaque)
{
    pcps4_ms_thread_start start = *(pcps4_ms_thread_start *)opaque;
    free(opaque);
    return (uint32_t)start.fn(start.arg);
}

PCPS4_MS_ABI HANDLE pcps4_abi_CreateThread(void *sa, SIZE_T st, LPTHREAD_START_ROUTINE fn,
                                           LPVOID p, DWORD f, DWORD *id)
{
    pcps4_ms_thread_start *start = (pcps4_ms_thread_start *)malloc(sizeof(*start));
    HANDLE h;
    if (start == NULL)
        return NULL;
    start->fn = (pcps4_ms_thread_proc)fn;
    start->arg = p;
    h = (HANDLE)pcps4_sys_thread_create(st, pcps4_ms_thread_trampoline, start, f, id);
    if (h == NULL)
        free(start);
    (void)sa;
    return h;
}
WRAP(CreateMutexA, HANDLE, (void *a, BOOL o, LPCSTR n), pcps4_CreateMutexA, (a, o, n))
WRAP(CreateEventA, HANDLE, (void *a, BOOL mr, BOOL is, LPCSTR n), pcps4_CreateEventA, (a, mr, is, n))
WRAP(SetEvent, BOOL, (HANDLE h), pcps4_SetEvent, (h))
WRAP(ResetEvent, BOOL, (HANDLE h), pcps4_ResetEvent, (h))
WRAP(WaitForSingleObject, DWORD, (HANDLE h, DWORD ms), pcps4_WaitForSingleObject, (h, ms))
WRAP(ReleaseMutex, BOOL, (HANDLE h), pcps4_ReleaseMutex, (h))
WRAP(VirtualAlloc, void *,
     (void *a, SIZE_T s, DWORD t, DWORD p), pcps4_VirtualAlloc, (a, s, t, p))
WRAP(VirtualFree, BOOL,
     (void *a, SIZE_T s, DWORD t), pcps4_VirtualFree, (a, s, t))

WRAP(WSAStartup, int, (WORD v, WSADATA *d), pcps4_WSAStartup, (v, d))
WRAP(WSACleanup, int, (void), pcps4_WSACleanup, ())
WRAP(WSAGetLastError, int, (void), pcps4_WSAGetLastError, ())
WRAP(socket, int, (int a, int b, int c), pcps4_socket, (a, b, c))
WRAP(connect, int, (int s, const struct sockaddr *a, socklen_t l), pcps4_connect, (s, a, l))
WRAP(send, int, (int s, const char *b, int l, int f), pcps4_send, (s, b, l, f))
WRAP(recv, int, (int s, char *b, int l, int f), pcps4_recv, (s, b, l, f))
WRAP(shutdown, int, (int s, int h), pcps4_shutdown, (s, h))
WRAP(setsockopt, int, (int s, int l, int o, const char *v, int n), pcps4_setsockopt, (s, l, o, v, n))
WRAP(getsockopt, int, (int s, int l, int o, char *v, int *n), pcps4_getsockopt, (s, l, o, v, n))
WRAP(getaddrinfo, int,
     (const char *n, const char *s, const struct addrinfo *h, struct addrinfo **r),
     pcps4_getaddrinfo, (n, s, h, r))
WRAP_VOID(freeaddrinfo, (struct addrinfo *r), pcps4_freeaddrinfo, (r))
WRAP(htons, uint16_t, (uint16_t v), pcps4_htons, (v))
WRAP(ntohs, uint16_t, (uint16_t v), pcps4_ntohs, (v))
WRAP(htonl, uint32_t, (uint32_t v), pcps4_htonl, (v))
WRAP(ntohl, uint32_t, (uint32_t v), pcps4_ntohl, (v))
WRAP(inet_addr, uint32_t, (const char *v), pcps4_inet_addr, (v))
WRAP(closesocket, int, (int s), pcps4_closesocket, (s))

WRAP(XInputGetState, DWORD, (DWORD i, XINPUT_STATE *s), pcps4_XInputGetState, (i, s))
WRAP(XInputSetState, DWORD, (DWORD i, XINPUT_VIBRATION *v), pcps4_XInputSetState, (i, v))
WRAP(XInputGetCapabilities, DWORD,
     (DWORD i, DWORD f, XINPUT_CAPABILITIES *c),
     pcps4_XInputGetCapabilities, (i, f, c))
WRAP_VOID(XInputEnable, (BOOL e), pcps4_XInputEnable, (e))

#undef WRAP
#undef WRAP_VOID
