#ifndef PCPS4_WINABI_H
#define PCPS4_WINABI_H

/*
 * Microsoft-x64 entry points for APIs implemented as System-V C functions.
 *
 * The resolver stores raw function pointers in a Windows PE IAT. Every one
 * of those pointers must therefore point at an MS-ABI thunk, not directly at
 * the System-V implementation.
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcps4_winabi_exports {
    const void *get_current_process_id;
    const void *get_current_thread_id;
    const void *get_tick_count;
    const void *get_tick_count64;
    const void *sleep_ms;
    const void *interlocked_increment;
    const void *interlocked_decrement;

    const void *create_file_a;
    const void *create_file_w;
    const void *read_file;
    const void *write_file;
    const void *get_file_size;
    const void *set_file_pointer;
    const void *delete_file_a;
    const void *delete_file_w;
    const void *create_directory_a;
    const void *create_directory_w;
    const void *close_handle;

    const void *create_thread;
    const void *create_mutex_a;
    const void *create_event_a;
    const void *set_event;
    const void *reset_event;
    const void *wait_for_single_object;
    const void *release_mutex;
    const void *virtual_alloc;
    const void *virtual_free;

    const void *wsa_startup;
    const void *wsa_cleanup;
    const void *wsa_get_last_error;
    const void *socket_fn;
    const void *connect_fn;
    const void *send_fn;
    const void *recv_fn;
    const void *shutdown_fn;
    const void *setsockopt_fn;
    const void *getsockopt_fn;
    const void *getaddrinfo_fn;
    const void *freeaddrinfo_fn;
    const void *htons_fn;
    const void *ntohs_fn;
    const void *htonl_fn;
    const void *ntohl_fn;
    const void *inet_addr_fn;
    const void *closesocket_fn;

    const void *xinput_get_state;
    const void *xinput_set_state;
    const void *xinput_get_capabilities;
    const void *xinput_enable;

    const void *malloc_fn;
    const void *calloc_fn;
    const void *realloc_fn;
    const void *free_fn;
    const void *memcpy_fn;
    const void *memmove_fn;
    const void *memset_fn;
    const void *memcmp_fn;
    const void *strlen_fn;
    const void *strcmp_fn;
    const void *strncmp_fn;
    const void *strstr_fn;
    const void *strchr_fn;
    const void *strrchr_fn;
    const void *atoi_fn;
    const void *atol_fn;
    const void *strtol_fn;
    const void *strtoul_fn;
    const void *strtoll_fn;
    const void *strtoull_fn;
    const void *abort_fn;
    const void *exit_fn;
    const void *fopen_fn;
    const void *wfopen_fn;
    const void *fclose_fn;
    const void *fread_fn;
    const void *fwrite_fn;
    const void *fseek_fn;
    const void *ftell_fn;
} pcps4_winabi_exports;

void pcps4_winabi_fill(pcps4_winabi_exports *exports);

#ifdef __cplusplus
}
#endif

#endif
