#ifndef PCPS4_SYS_H
#define PCPS4_SYS_H

/*
 * PcPs4 low-level OS adapter.
 *
 * This is the boundary between translated PC/Win32 code and the PS4
 * platform primitives exposed by OpenOrbis. Keep Windows semantics above
 * this layer; keep Sony/FreeBSD details below it.
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcps4_os_handle pcps4_os_handle_t;

/* Time / process */
uint64_t pcps4_sys_monotonic_ms(void);
uint32_t pcps4_sys_process_id(void);
uint32_t pcps4_sys_thread_id(void);
int pcps4_sys_sleep_ms(uint32_t milliseconds);

/* Virtual memory */
void *pcps4_sys_vm_alloc(void *address, size_t length, uint32_t protection);
int pcps4_sys_vm_free(void *address, size_t length);

/* Files */
pcps4_os_handle_t *pcps4_sys_file_open(const char *path,
                                        uint32_t desired_access,
                                        uint32_t share_mode,
                                        uint32_t creation_disposition,
                                        uint32_t attributes);
int pcps4_sys_file_read(pcps4_os_handle_t *handle,
                        void *buffer,
                        uint32_t bytes_to_read,
                        uint32_t *bytes_read);
int pcps4_sys_file_write(pcps4_os_handle_t *handle,
                         const void *buffer,
                         uint32_t bytes_to_write,
                         uint32_t *bytes_written);

/* Generic handles */
int pcps4_sys_handle_close(pcps4_os_handle_t *handle);
int pcps4_sys_handle_wait(pcps4_os_handle_t *handle, uint32_t milliseconds);
int pcps4_sys_mutex_release(pcps4_os_handle_t *handle);
pcps4_os_handle_t *pcps4_sys_mutex_create(const char *name);
pcps4_os_handle_t *pcps4_sys_event_create(int manual_reset, int initial_state, const char *name);
int pcps4_sys_event_set(pcps4_os_handle_t *handle);
int pcps4_sys_event_reset(pcps4_os_handle_t *handle);

/* Threads */
typedef uint32_t (*pcps4_sys_thread_fn)(void *argument);
pcps4_os_handle_t *pcps4_sys_thread_create(size_t stack_size,
                                           pcps4_sys_thread_fn function,
                                           void *argument,
                                           uint32_t creation_flags,
                                           uint32_t *thread_id);

/* Networking */
int pcps4_sys_socket_startup(uint16_t version, void *startup_data);
int pcps4_sys_socket_cleanup(void);
int pcps4_sys_socket_close(int socket_fd);
int pcps4_sys_socket_error(void);

#ifdef __cplusplus
}
#endif

#endif
