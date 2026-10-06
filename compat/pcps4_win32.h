#ifndef PCPS4_WIN32_H
#define PCPS4_WIN32_H

#include <stdint.h>
#include "pcps4_compat.h"

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#endif

typedef int32_t LONG;

PCPS4_MS_ABI DWORD pcps4_abi_TlsAlloc(void);
PCPS4_MS_ABI BOOL pcps4_abi_TlsFree(DWORD);
PCPS4_MS_ABI LPVOID pcps4_abi_TlsGetValue(DWORD);
PCPS4_MS_ABI BOOL pcps4_abi_TlsSetValue(DWORD, LPVOID);

PCPS4_MS_ABI LONG pcps4_abi_InterlockedExchange(volatile LONG *, LONG);
PCPS4_MS_ABI LONG pcps4_abi_InterlockedCompareExchange(volatile LONG *, LONG, LONG);
PCPS4_MS_ABI LONG pcps4_abi_InterlockedExchangeAdd(volatile LONG *, LONG);

PCPS4_MS_ABI void pcps4_abi_InitializeCriticalSection(void *);
PCPS4_MS_ABI BOOL pcps4_abi_InitializeCriticalSectionAndSpinCount(void *, DWORD);
PCPS4_MS_ABI void pcps4_abi_DeleteCriticalSection(void *);
PCPS4_MS_ABI void pcps4_abi_EnterCriticalSection(void *);
PCPS4_MS_ABI BOOL pcps4_abi_TryEnterCriticalSection(void *);
PCPS4_MS_ABI void pcps4_abi_LeaveCriticalSection(void *);

PCPS4_MS_ABI BOOL pcps4_abi_VirtualProtect(LPVOID, SIZE_T, DWORD, DWORD *);
PCPS4_MS_ABI SIZE_T pcps4_abi_VirtualQuery(const void *, void *, SIZE_T);
PCPS4_MS_ABI BOOL pcps4_abi_FlushInstructionCache(HANDLE, const void *, SIZE_T);

PCPS4_MS_ABI DWORD pcps4_abi_GetCurrentDirectoryA(DWORD, LPSTR);
PCPS4_MS_ABI DWORD pcps4_abi_GetCurrentDirectoryW(DWORD, LPWSTR);
PCPS4_MS_ABI BOOL pcps4_abi_SetCurrentDirectoryA(LPCSTR);
PCPS4_MS_ABI BOOL pcps4_abi_SetCurrentDirectoryW(LPCWSTR);
PCPS4_MS_ABI DWORD pcps4_abi_GetTempPathA(DWORD, LPSTR);
PCPS4_MS_ABI DWORD pcps4_abi_GetTempPathW(DWORD, LPWSTR);
PCPS4_MS_ABI UINT pcps4_abi_GetSystemDirectoryA(LPSTR, UINT);
PCPS4_MS_ABI UINT pcps4_abi_GetSystemDirectoryW(LPWSTR, UINT);
PCPS4_MS_ABI void pcps4_abi_GetSystemTimeAsFileTime(uint64_t *);
PCPS4_MS_ABI void pcps4_abi_GetSystemInfo(void *);
PCPS4_MS_ABI BOOL pcps4_abi_IsProcessorFeaturePresent(DWORD);
PCPS4_MS_ABI BOOL pcps4_abi_IsDebuggerPresent(void);

PCPS4_MS_ABI DWORD pcps4_abi_GetFileAttributesA(LPCSTR);
PCPS4_MS_ABI DWORD pcps4_abi_GetFileAttributesW(LPCWSTR);

PCPS4_MS_ABI WORD pcps4_abi_RegisterClassExA(const void *);
PCPS4_MS_ABI WORD pcps4_abi_RegisterClassExW(const void *);
PCPS4_MS_ABI BOOL pcps4_abi_UnregisterClassA(LPCSTR, HANDLE);
PCPS4_MS_ABI BOOL pcps4_abi_UnregisterClassW(LPCWSTR, HANDLE);
PCPS4_MS_ABI HANDLE pcps4_abi_CreateWindowExA(DWORD, LPCSTR, LPCSTR, DWORD, int, int, int, int, HANDLE, HANDLE, HANDLE, LPVOID);
PCPS4_MS_ABI HANDLE pcps4_abi_CreateWindowExW(DWORD, LPCWSTR, LPCWSTR, DWORD, int, int, int, int, HANDLE, HANDLE, HANDLE, LPVOID);
PCPS4_MS_ABI BOOL pcps4_abi_IsWindow(HANDLE);
PCPS4_MS_ABI BOOL pcps4_abi_DestroyWindow(HANDLE);
PCPS4_MS_ABI BOOL pcps4_abi_ShowWindow(HANDLE, int);
PCPS4_MS_ABI BOOL pcps4_abi_UpdateWindow(HANDLE);
PCPS4_MS_ABI intptr_t pcps4_abi_DefWindowProcA(HANDLE, DWORD, uintptr_t, intptr_t);
PCPS4_MS_ABI intptr_t pcps4_abi_DefWindowProcW(HANDLE, DWORD, uintptr_t, intptr_t);
PCPS4_MS_ABI BOOL pcps4_abi_PeekMessageA(void *, HANDLE, DWORD, DWORD, DWORD);
PCPS4_MS_ABI BOOL pcps4_abi_PeekMessageW(void *, HANDLE, DWORD, DWORD, DWORD);
PCPS4_MS_ABI int pcps4_abi_GetMessageA(void *, HANDLE, DWORD, DWORD);
PCPS4_MS_ABI BOOL pcps4_abi_TranslateMessage(const void *);
PCPS4_MS_ABI intptr_t pcps4_abi_DispatchMessageA(const void *);
PCPS4_MS_ABI intptr_t pcps4_abi_DispatchMessageW(const void *);
PCPS4_MS_ABI void pcps4_abi_PostQuitMessage(int);
PCPS4_MS_ABI BOOL pcps4_abi_GetClientRect(HANDLE, void *);
PCPS4_MS_ABI BOOL pcps4_abi_GetWindowRect(HANDLE, void *);
PCPS4_MS_ABI HANDLE pcps4_abi_GetDC(HANDLE);
PCPS4_MS_ABI int pcps4_abi_ReleaseDC(HANDLE, HANDLE);
PCPS4_MS_ABI BOOL pcps4_abi_SetForegroundWindow(HANDLE);
PCPS4_MS_ABI HANDLE pcps4_abi_GetActiveWindow(void);
PCPS4_MS_ABI HANDLE pcps4_abi_GetFocus(void);
PCPS4_MS_ABI HANDLE pcps4_abi_SetFocus(HANDLE);
PCPS4_MS_ABI intptr_t pcps4_abi_SetWindowLongPtrA(HANDLE, int, intptr_t);
PCPS4_MS_ABI intptr_t pcps4_abi_SetWindowLongPtrW(HANDLE, int, intptr_t);
PCPS4_MS_ABI intptr_t pcps4_abi_GetWindowLongPtrA(HANDLE, int);
PCPS4_MS_ABI intptr_t pcps4_abi_GetWindowLongPtrW(HANDLE, int);
PCPS4_MS_ABI BOOL pcps4_abi_SetWindowTextA(HANDLE, LPCSTR);
PCPS4_MS_ABI BOOL pcps4_abi_SetWindowTextW(HANDLE, LPCWSTR);

PCPS4_MS_ABI HRESULT pcps4_abi_CoInitializeEx(LPVOID, DWORD);
PCPS4_MS_ABI void pcps4_abi_CoUninitialize(void);
PCPS4_MS_ABI LPVOID pcps4_abi_CoTaskMemAlloc(SIZE_T);
PCPS4_MS_ABI LPVOID pcps4_abi_CoTaskMemRealloc(LPVOID, SIZE_T);
PCPS4_MS_ABI void pcps4_abi_CoTaskMemFree(LPVOID);

PCPS4_MS_ABI UINT pcps4_abi_timeGetTime(void);
PCPS4_MS_ABI UINT pcps4_abi_timeBeginPeriod(UINT);
PCPS4_MS_ABI UINT pcps4_abi_timeEndPeriod(UINT);

#endif
