#ifndef PCPS4_COMPAT_H
#define PCPS4_COMPAT_H

/*
 * Win32/MSVC compatibility façade.
 *
 * Translated PC code talks to these Windows-shaped names. The implementation
 * routes them through pcps4_sys.* which contains the actual PS4/OpenOrbis
 * bindings.
 */

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <netdb.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "pcps4_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;
typedef HANDLE HMODULE;
typedef void *FARPROC;
typedef uint32_t DWORD;
typedef uint32_t UINT;
typedef int32_t HRESULT;
typedef uintptr_t ULONG_PTR;
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef uint8_t BYTE;
typedef uint16_t WCHAR;
typedef char CHAR;
typedef const char *LPCSTR;
typedef char *LPSTR;
typedef const WCHAR *LPCWSTR;
typedef WCHAR *LPWSTR;
typedef void *LPVOID;
typedef void *PVOID;
typedef size_t SIZE_T;
typedef int BOOL;
typedef int SOCKET;

typedef DWORD (*LPTHREAD_START_ROUTINE)(LPVOID);

typedef struct {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    WORD iMaxSockets;
    WORD iMaxUdpDg;
    char *lpVendorInfo;
} WSADATA;

#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_SOCKET (-1)
#define SOCKET_ERROR (-1)

#define INFINITE 0xFFFFFFFFu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u

#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u

#define CREATE_NEW 1u
#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u

#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define CREATE_SUSPENDED 0x00000004u

#define MEM_COMMIT 0x00001000u
#define MEM_RESERVE 0x00002000u
#define MEM_RELEASE 0x00008000u

#define PAGE_NOACCESS 0x01u
#define PAGE_READONLY 0x02u
#define PAGE_READWRITE 0x04u
#define PAGE_EXECUTE_READ 0x20u
#define PAGE_EXECUTE_READWRITE 0x40u

static inline uint64_t pcps4_monotonic_ms(void)
{
    return pcps4_sys_monotonic_ms();
}

static inline uint32_t pcps4_get_current_process_id(void)
{
    return pcps4_sys_process_id();
}

static inline uint32_t pcps4_get_current_thread_id(void)
{
    return pcps4_sys_thread_id();
}

static inline uint32_t pcps4_sleep_ms(uint32_t milliseconds)
{
    return (uint32_t)pcps4_sys_sleep_ms(milliseconds);
}

static inline int32_t pcps4_interlocked_increment(volatile int32_t *value)
{
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

static inline int32_t pcps4_interlocked_decrement(volatile int32_t *value)
{
    return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}

static inline HANDLE pcps4_CreateFileA(const char *path,
                                       uint32_t desired_access,
                                       uint32_t share_mode,
                                       void *security_attributes,
                                       uint32_t creation_disposition,
                                       uint32_t attributes,
                                       void *template_file)
{
    (void)security_attributes;
    (void)template_file;

    pcps4_os_handle_t *handle = pcps4_sys_file_open(
        path,
        desired_access,
        share_mode,
        creation_disposition,
        attributes);

    return handle != NULL ? (HANDLE)handle : INVALID_HANDLE_VALUE;
}

static inline BOOL pcps4_ReadFile(HANDLE handle,
                                  void *buffer,
                                  DWORD bytes_to_read,
                                  DWORD *bytes_read,
                                  void *overlapped)
{
    (void)overlapped;

    return pcps4_sys_file_read(
               (pcps4_os_handle_t *)handle,
               buffer,
               bytes_to_read,
               bytes_read) == 0;
}

static inline BOOL pcps4_WriteFile(HANDLE handle,
                                   const void *buffer,
                                   DWORD bytes_to_write,
                                   DWORD *bytes_written,
                                   void *overlapped)
{
    (void)overlapped;

    return pcps4_sys_file_write(
               (pcps4_os_handle_t *)handle,
               buffer,
               bytes_to_write,
               bytes_written) == 0;
}

static inline BOOL pcps4_CloseHandle(HANDLE handle)
{
    if (handle == NULL || handle == INVALID_HANDLE_VALUE) {
        return FALSE;
    }

    return pcps4_sys_handle_close((pcps4_os_handle_t *)handle) == 0;
}

static inline HANDLE pcps4_CreateMutexA(void *attributes,
                                        BOOL initial_owner,
                                        const char *name)
{
    (void)attributes;
    (void)initial_owner;
    return (HANDLE)pcps4_sys_mutex_create(name);
}

static inline DWORD pcps4_WaitForSingleObject(HANDLE handle,
                                              DWORD milliseconds)
{
    int rc = pcps4_sys_handle_wait(
        (pcps4_os_handle_t *)handle,
        milliseconds);

    if (rc == (int)WAIT_OBJECT_0) {
        return WAIT_OBJECT_0;
    }

    if (rc == (int)WAIT_TIMEOUT) {
        return WAIT_TIMEOUT;
    }

    return (DWORD)rc;
}

static inline HANDLE pcps4_CreateEventA(
    void *attributes, BOOL manual_reset, BOOL initial_state, const char *name)
{
    (void)attributes;
    return (HANDLE)pcps4_sys_event_create(
        manual_reset, initial_state, name);
}

static inline BOOL pcps4_SetEvent(HANDLE handle)
{
    return pcps4_sys_event_set((pcps4_os_handle_t *)handle) == 0;
}

static inline BOOL pcps4_ResetEvent(HANDLE handle)
{
    return pcps4_sys_event_reset((pcps4_os_handle_t *)handle) == 0;
}

static inline BOOL pcps4_ReleaseMutex(HANDLE handle)
{
    return pcps4_sys_mutex_release(
               (pcps4_os_handle_t *)handle) == 0;
}

static inline HANDLE pcps4_CreateThread(void *security_attributes,
                                        SIZE_T stack_size,
                                        LPTHREAD_START_ROUTINE start_address,
                                        LPVOID parameter,
                                        DWORD creation_flags,
                                        DWORD *thread_id)
{
    (void)security_attributes;

    return (HANDLE)pcps4_sys_thread_create(
        stack_size,
        (pcps4_sys_thread_fn)start_address,
        parameter,
        creation_flags,
        thread_id);
}

static inline void *pcps4_VirtualAlloc(void *address,
                                       SIZE_T size,
                                       DWORD allocation_type,
                                       DWORD protection)
{
    uint32_t prot = PROT_NONE;

    (void)allocation_type;

    switch (protection) {
        case PAGE_READONLY:
            prot = PROT_READ;
            break;
        case PAGE_READWRITE:
            prot = PROT_READ | PROT_WRITE;
            break;
        case PAGE_EXECUTE_READ:
            prot = PROT_READ | PROT_EXEC;
            break;
        case PAGE_EXECUTE_READWRITE:
            prot = PROT_READ | PROT_WRITE | PROT_EXEC;
            break;
        default:
            break;
    }

    return pcps4_sys_vm_alloc(address, size, prot);
}

static inline BOOL pcps4_VirtualFree(void *address,
                                     SIZE_T size,
                                     DWORD free_type)
{
    (void)free_type;
    return pcps4_sys_vm_free(address, size) == 0;
}

static inline int pcps4_WSAStartup(WORD version, WSADATA *data)
{
    return pcps4_sys_socket_startup(
        version,
        data != NULL ? (void *)&data->wVersion : NULL);
}

static inline int pcps4_WSACleanup(void)
{
    return pcps4_sys_socket_cleanup();
}

static inline int pcps4_WSAGetLastError(void)
{
    return pcps4_sys_socket_error();
}

static inline int pcps4_closesocket(SOCKET socket_fd)
{
    return pcps4_sys_socket_close(socket_fd);
}

#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define CopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))
#define MoveMemory(Destination, Source, Length) memmove((Destination), (Source), (Length))

static inline DWORD pcps4_GetFileSize(HANDLE handle, DWORD *high)
{
    int64_t size = pcps4_sys_file_size((pcps4_os_handle_t *)handle);
    if (size < 0) {
        if (high != NULL) *high = 0xFFFFFFFFu;
        return 0xFFFFFFFFu;
    }
    if (high != NULL) *high = (DWORD)((uint64_t)size >> 32);
    return (DWORD)((uint64_t)size & 0xFFFFFFFFu);
}

static inline DWORD pcps4_SetFilePointer(
    HANDLE handle, int32_t distance_low, int32_t *distance_high, DWORD move_method)
{
    int64_t offset = (int64_t)(int32_t)distance_low;
    if (distance_high != NULL)
        offset |= (int64_t)(*distance_high) << 32;
    int64_t result = pcps4_sys_file_seek(
        (pcps4_os_handle_t *)handle, offset, (int)move_method);
    if (result < 0) return 0xFFFFFFFFu;
    if (distance_high != NULL)
        *distance_high = (int32_t)((uint64_t)result >> 32);
    return (DWORD)((uint64_t)result & 0xFFFFFFFFu);
}

static inline BOOL pcps4_DeleteFileA(const char *path)
{
    return pcps4_sys_file_delete(path) == 0;
}

static inline BOOL pcps4_CreateDirectoryA(const char *path, void *security)
{
    (void)security;
    return pcps4_sys_file_mkdir(path) == 0;
}

static inline int pcps4_wide_to_utf8(
    LPCWSTR input,
    LPSTR output,
    size_t output_size)
{
    size_t i = 0;
    size_t used = 0;

    if (input == NULL || output == NULL || output_size == 0)
        return -1;

    while (input[i] != 0) {
        uint32_t cp = input[i++];

        if (cp >= 0xD800u && cp <= 0xDBFFu && input[i] != 0) {
            uint32_t low = input[i];

            if (low >= 0xDC00u && low <= 0xDFFFu) {
                ++i;
                cp = 0x10000u +
                     ((cp - 0xD800u) << 10) +
                     (low - 0xDC00u);
            }
        }

        if (cp < 0x80u) {
            if (used + 1 >= output_size)
                return -1;
            output[used++] = (char)cp;
        } else if (cp < 0x800u) {
            if (used + 2 >= output_size)
                return -1;
            output[used++] = (char)(0xC0u | (cp >> 6));
            output[used++] = (char)(0x80u | (cp & 0x3Fu));
        } else if (cp < 0x10000u) {
            if (used + 3 >= output_size)
                return -1;
            output[used++] = (char)(0xE0u | (cp >> 12));
            output[used++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            output[used++] = (char)(0x80u | (cp & 0x3Fu));
        } else {
            if (used + 4 >= output_size)
                return -1;
            output[used++] = (char)(0xF0u | (cp >> 18));
            output[used++] = (char)(0x80u | ((cp >> 12) & 0x3Fu));
            output[used++] = (char)(0x80u | ((cp >> 6) & 0x3Fu));
            output[used++] = (char)(0x80u | (cp & 0x3Fu));
        }
    }

    output[used] = '\0';
    return 0;
}

static inline BOOL pcps4_CreateFileW(
    LPCWSTR path,
    uint32_t desired_access,
    uint32_t share_mode,
    void *security_attributes,
    uint32_t creation_disposition,
    uint32_t attributes,
    void *template_file)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(path, utf8, sizeof(utf8)) != 0)
        return FALSE;

    return (pcps4_CreateFileA(
        utf8,
        desired_access,
        share_mode,
        security_attributes,
        creation_disposition,
        attributes,
        template_file) != INVALID_HANDLE_VALUE)
        ? TRUE
        : FALSE;
}

static inline HANDLE pcps4_CreateFileW_handle(
    LPCWSTR path,
    uint32_t desired_access,
    uint32_t share_mode,
    void *security_attributes,
    uint32_t creation_disposition,
    uint32_t attributes,
    void *template_file)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(path, utf8, sizeof(utf8)) != 0)
        return INVALID_HANDLE_VALUE;

    return pcps4_CreateFileA(
        utf8,
        desired_access,
        share_mode,
        security_attributes,
        creation_disposition,
        attributes,
        template_file);
}

static inline BOOL pcps4_DeleteFileW(LPCWSTR path)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(path, utf8, sizeof(utf8)) != 0)
        return FALSE;

    return pcps4_DeleteFileA(utf8);
}

static inline BOOL pcps4_CreateDirectoryW(
    LPCWSTR path,
    void *security)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(path, utf8, sizeof(utf8)) != 0)
        return FALSE;

    return pcps4_CreateDirectoryA(utf8, security);
}

static inline SOCKET pcps4_socket(int domain, int type, int protocol) { return socket(domain, type, protocol); }
static inline int pcps4_connect(SOCKET s, const struct sockaddr *a, socklen_t l) { return connect(s, a, l); }
static inline int pcps4_send(SOCKET s, const char *b, int l, int f) { return (int)send(s, b, (size_t)l, f); }
static inline int pcps4_recv(SOCKET s, char *b, int l, int f) { return (int)recv(s, b, (size_t)l, f); }
static inline int pcps4_shutdown(SOCKET s, int how) { return shutdown(s, how); }
static inline int pcps4_setsockopt(SOCKET s, int level, int opt, const char *v, int l) { return setsockopt(s, level, opt, v, (socklen_t)l); }
static inline int pcps4_getsockopt(SOCKET s, int level, int opt, char *v, int *l) { socklen_t n=l?(socklen_t)*l:0; int rc=getsockopt(s,level,opt,v,&n); if(l)*l=(int)n; return rc; }
static inline int pcps4_getaddrinfo(const char *n,const char *s,const struct addrinfo *h,struct addrinfo **r){return getaddrinfo(n,s,h,r);}
static inline void pcps4_freeaddrinfo(struct addrinfo *r){freeaddrinfo(r);}
static inline uint16_t pcps4_htons(uint16_t v){return htons(v);}
static inline uint16_t pcps4_ntohs(uint16_t v){return ntohs(v);}
static inline uint32_t pcps4_htonl(uint32_t v){return htonl(v);}
static inline uint32_t pcps4_ntohl(uint32_t v){return ntohl(v);}
static inline uint32_t pcps4_inet_addr(const char *v){return inet_addr(v);}


#ifdef __cplusplus
}
#endif

#endif
