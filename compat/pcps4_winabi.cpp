#include "pcps4_winabi.h"

#include "pcps4_compat.h"
#include "pcps4_xinput.h"
#include "pcps4_msabi.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* C++ exposes const/non-const overloads for these libc names; use explicit
 * adapters so PCPS4_MSW receives one unambiguous function type. */
static const char *pcps4_winabi_strstr(const char *a, const char *b)
{
    return strstr(a, b);
}
static const char *pcps4_winabi_strchr(const char *a, int c)
{
    return strchr(a, c);
}
static const char *pcps4_winabi_strrchr(const char *a, int c)
{
    return strrchr(a, c);
}

static void PCPS4_MS_ABI pcps4_winabi_abort(void)
{
    abort();
}

static void PCPS4_MS_ABI pcps4_winabi_exit(int code)
{
    exit(code);
}

static FILE *pcps4_winabi_fopen(
    const char *path, const char *mode)
{
    char translated[1024];
    int writing = 0;

    if (mode == NULL)
        return NULL;

    if (strchr(mode, 'w') != NULL ||
        strchr(mode, 'a') != NULL ||
        strchr(mode, '+') != NULL)
        writing = 1;

    if (pcps4_sys_translate_path(
            path, writing, translated, sizeof(translated)) != 0)
        return NULL;

    return fopen(translated, mode);
}

/*
 * Each assignment below is a compiler-generated MS-x64 thunk. The original
 * implementation remains System-V so translated PcPs4 code can continue
 * calling it normally.
 */
void pcps4_winabi_fill(pcps4_winabi_exports *exports)
{
    if (exports == NULL)
        return;

#define WABI(member, fn) exports->member = PCPS4_MSW(fn)
#define MS_DIRECT(member, fn) exports->member = (const void *)(fn)

    WABI(get_current_process_id, pcps4_get_current_process_id);
    WABI(get_current_thread_id, pcps4_get_current_thread_id);
    WABI(get_tick_count, pcps4_monotonic_ms);
    WABI(get_tick_count64, pcps4_monotonic_ms);
    WABI(sleep_ms, pcps4_sleep_ms);
    WABI(interlocked_increment, pcps4_interlocked_increment);
    WABI(interlocked_decrement, pcps4_interlocked_decrement);

    WABI(create_file_a, pcps4_CreateFileA);
    WABI(create_file_w, pcps4_CreateFileW);
    WABI(read_file, pcps4_ReadFile);
    WABI(write_file, pcps4_WriteFile);
    WABI(get_file_size, pcps4_GetFileSize);
    WABI(set_file_pointer, pcps4_SetFilePointer);
    WABI(delete_file_a, pcps4_DeleteFileA);
    WABI(delete_file_w, pcps4_DeleteFileW);
    WABI(create_directory_a, pcps4_CreateDirectoryA);
    WABI(create_directory_w, pcps4_CreateDirectoryW);
    WABI(close_handle, pcps4_CloseHandle);

    WABI(create_thread, pcps4_CreateThread);
    WABI(create_mutex_a, pcps4_CreateMutexA);
    WABI(create_event_a, pcps4_CreateEventA);
    WABI(set_event, pcps4_SetEvent);
    WABI(reset_event, pcps4_ResetEvent);
    WABI(wait_for_single_object, pcps4_WaitForSingleObject);
    WABI(release_mutex, pcps4_ReleaseMutex);
    WABI(virtual_alloc, pcps4_VirtualAlloc);
    WABI(virtual_free, pcps4_VirtualFree);

    WABI(wsa_startup, pcps4_WSAStartup);
    WABI(wsa_cleanup, pcps4_WSACleanup);
    WABI(wsa_get_last_error, pcps4_WSAGetLastError);
    WABI(socket_fn, pcps4_socket);
    WABI(connect_fn, pcps4_connect);
    WABI(send_fn, pcps4_send);
    WABI(recv_fn, pcps4_recv);
    WABI(shutdown_fn, pcps4_shutdown);
    WABI(setsockopt_fn, pcps4_setsockopt);
    WABI(getsockopt_fn, pcps4_getsockopt);
    WABI(getaddrinfo_fn, pcps4_getaddrinfo);
    WABI(freeaddrinfo_fn, pcps4_freeaddrinfo);
    WABI(htons_fn, pcps4_htons);
    WABI(ntohs_fn, pcps4_ntohs);
    WABI(htonl_fn, pcps4_htonl);
    WABI(ntohl_fn, pcps4_ntohl);
    WABI(inet_addr_fn, pcps4_inet_addr);
    WABI(closesocket_fn, pcps4_closesocket);

    WABI(xinput_get_state, pcps4_XInputGetState);
    WABI(xinput_set_state, pcps4_XInputSetState);
    WABI(xinput_get_capabilities, pcps4_XInputGetCapabilities);
    WABI(xinput_enable, pcps4_XInputEnable);

    WABI(malloc_fn, malloc);
    WABI(calloc_fn, calloc);
    WABI(realloc_fn, realloc);
    WABI(free_fn, free);
    WABI(memcpy_fn, memcpy);
    WABI(memmove_fn, memmove);
    WABI(memset_fn, memset);
    WABI(memcmp_fn, memcmp);
    WABI(strlen_fn, strlen);
    WABI(strcmp_fn, strcmp);
    WABI(strncmp_fn, strncmp);
    WABI(strstr_fn, pcps4_winabi_strstr);
    WABI(strchr_fn, pcps4_winabi_strchr);
    WABI(strrchr_fn, pcps4_winabi_strrchr);
    WABI(atoi_fn, atoi);
    WABI(atol_fn, atol);
    WABI(strtol_fn, strtol);
    WABI(strtoul_fn, strtoul);
    WABI(strtoll_fn, strtoll);
    WABI(strtoull_fn, strtoull);
    MS_DIRECT(abort_fn, pcps4_winabi_abort);
    MS_DIRECT(exit_fn, pcps4_winabi_exit);
    WABI(fopen_fn, pcps4_winabi_fopen);
    WABI(fclose_fn, fclose);
    WABI(fread_fn, fread);
    WABI(fwrite_fn, fwrite);
    WABI(fseek_fn, fseek);
    WABI(ftell_fn, ftell);

#undef MS_DIRECT
#undef WABI
}
