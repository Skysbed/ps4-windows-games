/* Just enough of the Windows API to run gsring natively on Linux against a reference Vulkan driver. */
#include <dlfcn.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
typedef void *HANDLE, *HMODULE, *HWND;
typedef unsigned int DWORD;
typedef int LONG;
typedef long long LONG64;
typedef int CRITICAL_SECTION;
#define WINAPI
#define CALLBACK
#define INVALID_HANDLE_VALUE ((HANDLE)-1)
#define GENERIC_WRITE 0
#define FILE_SHARE_READ 0
#define CREATE_ALWAYS 0
#define WS_POPUP 0
#define InitializeCriticalSection(x) ((void)0)
#define EnterCriticalSection(x) ((void)0)
#define LeaveCriticalSection(x) ((void)0)
#define CreateFileA(a,b,c,d,e,f,g) ((HANDLE)stdout)
#define WriteFile(h,b,n,d,o) (fwrite((b),1,(n),stdout), *(d)=(n))
#define FlushFileBuffers(h) fflush(stdout)
#define CreateThread(a,b,c,d,e,f) ((void)(c), (HANDLE)0)
#define CloseHandle(h) ((void)(h))
#define Sleep(ms) usleep((ms)*1000)
#define InterlockedExchange(p,v) (*(p)=(v))
#define GetCurrentProcess() ((HANDLE)0)
#define TerminateProcess(p,c) _exit(c)
#define ExitProcess(c) exit(c)
#define GetModuleHandleA(x) ((HMODULE)0)
#define CreateWindowExA(a,b,c,d,e,f,g,h,i,j,k,l) ((HWND)1)
#define LoadLibraryA(x) dlopen("libvulkan.so.1", RTLD_NOW)
#define GetProcAddress(l,n) dlsym((l),(n))
#define _vsnprintf vsnprintf
#define _snprintf snprintf
static inline DWORD GetEnvironmentVariableA(const char *n, char *b, DWORD s)
{
    const char *v = getenv(n);
    if (!v) return 0;
    strncpy(b, v, s - 1); b[s - 1] = 0;
    return (DWORD)strlen(b);
}
