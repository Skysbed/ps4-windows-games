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

#include "pcps4_sys.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef void *HANDLE;
typedef uint32_t DWORD;
typedef uint16_t WORD;
typedef int16_t SHORT;
typedef uint8_t BYTE;
typedef uint16_t WCHAR;
typedef char CHAR;
typedef const char *LPCSTR;
typedef char *LPSTR;
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

#ifdef __cplusplus
}
#endif

#endif
