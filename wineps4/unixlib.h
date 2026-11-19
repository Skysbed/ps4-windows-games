/*
 * unixlib.h for wineps4 - PS4 audio driver
 * Based on mmdevapi's unixlib.h
 */

#ifndef __WINEPS4_UNIXLIB_H
#define __WINEPS4_UNIXLIB_H

#include <stdint.h>
#include "ntstatus.h"
#include "winternl.h"

typedef UINT64 unixlib_handle_t;
typedef UINT64 unixlib_module_t;
typedef UINT64 stream_handle;

enum driver_priority
{
    Priority_Unavailable = 0,
    Priority_Low,
    Priority_Neutral,
    Priority_Preferred
};

#ifdef WINE_UNIX_LIB

typedef NTSTATUS (*unixlib_entry_t)( void *args );

/* helper to create a thread on the Unix side */
static inline NTSTATUS create_unix_thread( HANDLE *handle, const WCHAR *name,
                                           void (*entry)(void *), void *param )
{
    DWORD priority = THREAD_PRIORITY_TIME_CRITICAL;
    THREAD_NAME_INFORMATION info;
    NTSTATUS status;

    if (!(status = PsCreateSystemThread( handle, THREAD_ALL_ACCESS, NULL, 0, NULL, entry, param )))
    {
        RtlInitUnicodeString( &info.ThreadName, name );
        NtSetInformationThread( *handle, ThreadNameInformation, &info, sizeof(info) );
        NtSetInformationThread( *handle, ThreadBasePriority, &priority, sizeof(priority) );
    }
    return status;
}

#endif  /* WINE_UNIX_LIB */

#endif /* __WINEPS4_UNIXLIB_H */
