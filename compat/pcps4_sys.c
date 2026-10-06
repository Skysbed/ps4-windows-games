#include "pcps4_sys.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#define PCPS4_HANDLE_MAGIC 0x50345348u

enum {
    PCPS4_HANDLE_FILE = 1,
    PCPS4_HANDLE_THREAD = 2,
    PCPS4_HANDLE_MUTEX = 3
};

struct pcps4_os_handle {
    uint32_t magic;
    uint32_t kind;
    union {
        int fd;
        OrbisPthread thread;
        OrbisPthreadMutex mutex;
    } value;
    uint32_t joined;
};

static uint32_t next_thread_id = 1;

static pcps4_os_handle_t *make_handle(uint32_t kind)
{
    pcps4_os_handle_t *handle =
        (pcps4_os_handle_t *)calloc(1, sizeof(*handle));

    if (handle == NULL) {
        return NULL;
    }

    handle->magic = PCPS4_HANDLE_MAGIC;
    handle->kind = kind;
    return handle;
}

static int valid_handle(pcps4_os_handle_t *handle)
{
    return handle != NULL && handle->magic == PCPS4_HANDLE_MAGIC;
}

uint64_t pcps4_sys_monotonic_ms(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }

    return (uint64_t)now.tv_sec * 1000ULL +
           (uint64_t)now.tv_nsec / 1000000ULL;
}

uint32_t pcps4_sys_process_id(void)
{
    return (uint32_t)getpid();
}

uint32_t pcps4_sys_thread_id(void)
{
    int32_t id = scePthreadGetthreadid();
    return id > 0 ? (uint32_t)id : 0;
}

int pcps4_sys_sleep_ms(uint32_t milliseconds)
{
    return sceKernelUsleep(milliseconds * 1000u);
}

void *pcps4_sys_vm_alloc(void *address, size_t length, uint32_t protection)
{
    int32_t result;
    void *mapped = NULL;

    result = sceKernelMmap(
        address,
        length,
        (int32_t)protection,
        MAP_PRIVATE | MAP_ANONYMOUS,
        -1,
        0,
        &mapped);

    return result == 0 ? mapped : NULL;
}

int pcps4_sys_vm_free(void *address, size_t length)
{
    if (address == NULL || length == 0) {
        return -1;
    }

    return sceKernelMunmap(address, length);
}

pcps4_os_handle_t *pcps4_sys_file_open(const char *path,
                                        uint32_t desired_access,
                                        uint32_t share_mode,
                                        uint32_t creation_disposition,
                                        uint32_t attributes)
{
    int flags;
    int fd;
    pcps4_os_handle_t *handle;

    (void)share_mode;
    (void)attributes;

    if (path == NULL) {
        return NULL;
    }

    if ((desired_access & 0xC0000000u) == 0xC0000000u) {
        flags = O_RDWR;
    } else if ((desired_access & 0x40000000u) != 0) {
        flags = O_WRONLY;
    } else {
        flags = O_RDONLY;
    }

    switch (creation_disposition) {
        case 1: /* CREATE_NEW */
            flags |= O_CREAT | O_EXCL;
            break;
        case 2: /* CREATE_ALWAYS */
            flags |= O_CREAT | O_TRUNC;
            break;
        case 3: /* OPEN_EXISTING */
            break;
        case 4: /* OPEN_ALWAYS */
            flags |= O_CREAT;
            break;
        case 5: /* TRUNCATE_EXISTING */
            flags |= O_TRUNC;
            break;
        default:
            return NULL;
    }

    fd = sceKernelOpen(path, flags, 0644);
    if (fd < 0) {
        return NULL;
    }

    handle = make_handle(PCPS4_HANDLE_FILE);
    if (handle == NULL) {
        (void)sceKernelClose(fd);
        return NULL;
    }

    handle->value.fd = fd;
    return handle;
}

int pcps4_sys_file_read(pcps4_os_handle_t *handle,
                        void *buffer,
                        uint32_t bytes_to_read,
                        uint32_t *bytes_read)
{
    size_t result;

    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_FILE ||
        buffer == NULL) {
        return -1;
    }

    result = sceKernelRead(handle->value.fd, buffer, bytes_to_read);
    if ((ssize_t)result < 0) {
        return -1;
    }

    if (bytes_read != NULL) {
        *bytes_read = (uint32_t)result;
    }

    return 0;
}

int pcps4_sys_file_write(pcps4_os_handle_t *handle,
                         const void *buffer,
                         uint32_t bytes_to_write,
                         uint32_t *bytes_written)
{
    size_t result;

    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_FILE ||
        buffer == NULL) {
        return -1;
    }

    result = sceKernelWrite(handle->value.fd, buffer, bytes_to_write);
    if ((ssize_t)result < 0) {
        return -1;
    }

    if (bytes_written != NULL) {
        *bytes_written = (uint32_t)result;
    }

    return 0;
}

pcps4_os_handle_t *pcps4_sys_mutex_create(const char *name)
{
    pcps4_os_handle_t *handle;
    int rc;

    handle = make_handle(PCPS4_HANDLE_MUTEX);
    if (handle == NULL) {
        return NULL;
    }

    rc = scePthreadMutexInit(
        &handle->value.mutex,
        NULL,
        name != NULL ? name : "pcps4-mutex");

    if (rc != 0) {
        free(handle);
        return NULL;
    }

    return handle;
}

int pcps4_sys_handle_close(pcps4_os_handle_t *handle)
{
    int result = 0;

    if (!valid_handle(handle)) {
        return -1;
    }

    switch (handle->kind) {
        case PCPS4_HANDLE_FILE:
            result = sceKernelClose(handle->value.fd);
            break;

        case PCPS4_HANDLE_THREAD:
            if (!handle->joined) {
                result = scePthreadDetach(handle->value.thread);
            }
            break;

        case PCPS4_HANDLE_MUTEX:
            result = scePthreadMutexDestroy(&handle->value.mutex);
            break;

        default:
            result = -1;
            break;
    }

    handle->magic = 0;
    free(handle);
    return result;
}

int pcps4_sys_handle_wait(pcps4_os_handle_t *handle, uint32_t milliseconds)
{
    if (!valid_handle(handle)) {
        return -1;
    }

    /*
     * First implementation: support the common infinite wait exactly.
     * Timed waits need a separate semantics adapter because Win32 handles
     * can represent several object kinds while pthread timed APIs differ.
     */
    if (milliseconds != 0xFFFFFFFFu) {
        return 258; /* WAIT_TIMEOUT */
    }

    if (handle->kind == PCPS4_HANDLE_THREAD) {
        void *result = NULL;
        int rc = scePthreadJoin(handle->value.thread, &result);

        if (rc == 0) {
            handle->joined = 1;
            return 0; /* WAIT_OBJECT_0 */
        }

        return rc;
    }

    if (handle->kind == PCPS4_HANDLE_MUTEX) {
        return scePthreadMutexLock(&handle->value.mutex);
    }

    return -1;
}

int pcps4_sys_mutex_release(pcps4_os_handle_t *handle)
{
    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_MUTEX) {
        return -1;
    }

    return scePthreadMutexUnlock(&handle->value.mutex);
}

typedef struct {
    pcps4_sys_thread_fn function;
    void *argument;
} pcps4_thread_start_t;

static void *thread_bootstrap(void *opaque)
{
    pcps4_thread_start_t *start = (pcps4_thread_start_t *)opaque;
    uint32_t exit_code = start->function(start->argument);

    free(start);
    return (void *)(uintptr_t)exit_code;
}

pcps4_os_handle_t *pcps4_sys_thread_create(size_t stack_size,
                                           pcps4_sys_thread_fn function,
                                           void *argument,
                                           uint32_t creation_flags,
                                           uint32_t *thread_id)
{
    pcps4_os_handle_t *handle;
    pcps4_thread_start_t *start;
    OrbisPthreadAttr attr;
    int rc;

    if (function == NULL || (creation_flags & 0x00000004u) != 0) {
        return NULL;
    }

    handle = make_handle(PCPS4_HANDLE_THREAD);
    if (handle == NULL) {
        return NULL;
    }

    start = (pcps4_thread_start_t *)calloc(1, sizeof(*start));
    if (start == NULL) {
        free(handle);
        return NULL;
    }

    start->function = function;
    start->argument = argument;

    if (stack_size != 0) {
        rc = scePthreadAttrInit(&attr);
        if (rc == 0) {
            rc = scePthreadAttrSetstacksize(&attr, stack_size);
        }

        if (rc != 0) {
            (void)scePthreadAttrDestroy(&attr);
            free(start);
            free(handle);
            return NULL;
        }

        rc = scePthreadCreate(
            &handle->value.thread,
            &attr,
            thread_bootstrap,
            start,
            "pcps4-worker");

        (void)scePthreadAttrDestroy(&attr);
    } else {
        rc = scePthreadCreate(
            &handle->value.thread,
            NULL,
            thread_bootstrap,
            start,
            "pcps4-worker");
    }

    if (rc != 0) {
        free(start);
        free(handle);
        return NULL;
    }

    if (thread_id != NULL) {
        *thread_id =
            __atomic_fetch_add(&next_thread_id, 1, __ATOMIC_SEQ_CST);
    }

    return handle;
}

int pcps4_sys_socket_startup(uint16_t version, void *startup_data)
{
    if (startup_data != NULL) {
        uint16_t *fields = (uint16_t *)startup_data;
        fields[0] = version;
        fields[1] = version;
    }

    /*
     * Winsock startup is process-local initialization. PS4 exposes BSD
     * sockets directly, so no equivalent startup syscall is required.
     */
    return 0;
}

int pcps4_sys_socket_cleanup(void)
{
    return 0;
}

int pcps4_sys_socket_close(int socket_fd)
{
    return close(socket_fd);
}

int pcps4_sys_socket_error(void)
{
    return errno;
}
