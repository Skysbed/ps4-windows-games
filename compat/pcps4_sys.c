#define _POSIX_C_SOURCE 200112L
#include "pcps4_sys.h"
#include "pcps4_fault.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <time.h>
#include <sys/stat.h>
#include <unistd.h>

#include <orbis/libkernel.h>

#define PCPS4_HANDLE_MAGIC 0x50345348u

enum {
    PCPS4_HANDLE_FILE = 1,
    PCPS4_HANDLE_THREAD = 2,
    PCPS4_HANDLE_MUTEX = 3,
    PCPS4_HANDLE_EVENT = 4
};

struct pcps4_os_handle {
    uint32_t magic;
    uint32_t kind;
    union {
        int fd;
        OrbisPthread thread;
        OrbisPthreadMutex mutex;
        struct {
            volatile int signaled;
            int manual_reset;
        } event;
    } value;
    uint32_t joined;
};

static uint32_t next_thread_id = 1;

/*
 * Low-level Win32 compatibility primitives used by the API façade.
 * PS4 mappings use a 16 KiB page granule; keep the actual mapped length so
 * VirtualFree can release the correct region even when the requested length
 * was smaller or Windows supplied size == 0 for MEM_RELEASE.
 */
#define PCPS4_VM_PAGE_SIZE ((size_t)0x4000u)

typedef struct pcps4_vm_region {
    void *address;
    size_t length;
    struct pcps4_vm_region *next;
} pcps4_vm_region_t;

static pcps4_vm_region_t *vm_regions;
static volatile int vm_regions_lock;
static __thread uint32_t current_thread_id;

extern int32_t sceKernelMmap(void *addr, size_t len, int32_t prot,
                             int32_t flags, int32_t fd, off_t offset,
                             void **res);
extern int32_t sceKernelMunmap(void *addr, size_t len);

static void vm_lock(void)
{
    while (__atomic_exchange_n(&vm_regions_lock, 1, __ATOMIC_ACQUIRE) != 0) {
        (void)sceKernelUsleep(50);
    }
}

static void vm_unlock(void)
{
    __atomic_store_n(&vm_regions_lock, 0, __ATOMIC_RELEASE);
}

static int vm_round_length(size_t length, size_t *rounded)
{
    if (length == 0 || rounded == NULL ||
        length > SIZE_MAX - (PCPS4_VM_PAGE_SIZE - 1u))
        return -1;
    *rounded = (length + PCPS4_VM_PAGE_SIZE - 1u) &
               ~(PCPS4_VM_PAGE_SIZE - 1u);
    return *rounded == 0 ? -1 : 0;
}

uint64_t pcps4_sys_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000u +
           (uint64_t)now.tv_nsec / 1000000u;
}

uint32_t pcps4_sys_process_id(void)
{
    pid_t pid = getpid();
    return pid > 0 ? (uint32_t)pid : 1u;
}

uint32_t pcps4_sys_thread_id(void)
{
    if (current_thread_id == 0) {
        uint32_t id = __atomic_fetch_add(
            &next_thread_id, 1u, __ATOMIC_SEQ_CST);
        if (id == 0)
            id = __atomic_fetch_add(&next_thread_id, 1u, __ATOMIC_SEQ_CST);
        current_thread_id = id;
    }
    return current_thread_id;
}

int pcps4_sys_sleep_ms(uint32_t milliseconds)
{
    uint64_t remaining_us = (uint64_t)milliseconds * 1000u;

    while (remaining_us != 0) {
        uint32_t chunk = remaining_us > 1000000u
            ? 1000000u : (uint32_t)remaining_us;
        int rc = sceKernelUsleep(chunk);
        if (rc != 0)
            return rc;
        remaining_us -= chunk;
    }
    return 0;
}

void *pcps4_sys_vm_alloc(void *address, size_t length, uint32_t protection)
{
    size_t map_length;
    void *mapped = NULL;
    pcps4_vm_region_t *region;
    int32_t rc;

    if (vm_round_length(length, &map_length) != 0)
        return NULL;

    region = (pcps4_vm_region_t *)calloc(1, sizeof(*region));
    if (region == NULL)
        return NULL;

    rc = sceKernelMmap(address, map_length, (int32_t)protection,
                       MAP_PRIVATE | MAP_ANON, -1, 0, &mapped);
    if (rc != 0 || mapped == NULL) {
        free(region);
        return NULL;
    }

    region->address = mapped;
    region->length = map_length;
    vm_lock();
    region->next = vm_regions;
    vm_regions = region;
    vm_unlock();
    return mapped;
}

int pcps4_sys_vm_free(void *address, size_t length)
{
    pcps4_vm_region_t **link;
    pcps4_vm_region_t *region;
    int32_t rc;

    (void)length; /* release uses the original mapping size, not caller size */
    if (address == NULL)
        return -1;

    vm_lock();
    link = &vm_regions;
    while (*link != NULL && (*link)->address != address)
        link = &(*link)->next;

    region = *link;
    if (region == NULL) {
        vm_unlock();
        return -1;
    }
    *link = region->next;
    vm_unlock();

    rc = sceKernelMunmap(region->address, region->length);
    if (rc != 0) {
        /* Keep the bookkeeping consistent if the kernel refuses to unmap. */
        vm_lock();
        region->next = vm_regions;
        vm_regions = region;
        vm_unlock();
        return rc;
    }

    free(region);
    return 0;
}

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

static int pcps4_path_is_absolute_unix(const char *path)
{
    return path != NULL && path[0] == '/';
}

static int pcps4_path_has_drive(const char *path)
{
    return path != NULL &&
           ((path[0] >= 'A' && path[0] <= 'Z') ||
            (path[0] >= 'a' && path[0] <= 'z')) &&
           path[1] == ':';
}

/*
 * Translate ordinary Windows game paths into the PS4 application layout.
 * Reads resolve against /app0; writes resolve against /data/pcps4.
 */
static int pcps4_translate_path(
    const char *path,
    int writing,
    char *out,
    size_t out_size)
{
    char normalized[1024];
    char *segments[128];
    size_t segment_count = 0;
    size_t n = 0;
    size_t i = 0;
    const char *cursor;
    const char *root;
    int drive_path;

    if (path == NULL || out == NULL || out_size < 2)
        return -1;

    drive_path = pcps4_path_has_drive(path);

    if (drive_path) {
        cursor = path + 2;
        while (*cursor == '/' || *cursor == 92)
            ++cursor;
        root = writing ? "/data/pcps4" : "/app0";
    } else if (pcps4_path_is_absolute_unix(path)) {
        cursor = path;
        root = writing ? "/data/pcps4" : "";
    } else {
        cursor = path;
        root = writing ? "/data/pcps4" : "/app0";
    }

    while (*cursor != '\0' && n + 1 < sizeof(normalized)) {
        char ch = *cursor++;
        if ((unsigned char)ch == 92)
            ch = '/';
        normalized[n++] = ch;
    }
    normalized[n] = '\0';

    if (*cursor != '\0')
        return -1;

    cursor = normalized;
    while (*cursor != '\0') {
        const char *start;
        const char *end;
        size_t len;
        int has_more;

        while (*cursor == '/')
            ++cursor;
        if (*cursor == '\0')
            break;

        start = cursor;
        while (*cursor != '/' && *cursor != '\0')
            ++cursor;
        end = cursor;
        len = (size_t)(end - start);

        if (len == 1 && start[0] == '.')
            continue;

        if (len == 2 && start[0] == '.' && start[1] == '.') {
            if (segment_count > 0)
                --segment_count;
            continue;
        }

        if (segment_count >= sizeof(segments) / sizeof(segments[0]))
            return -1;

        has_more = (*end != '\0');
        ((char *)end)[0] = '\0';
        segments[segment_count++] = (char *)start;

        if (!has_more)
            break;
        cursor = end + 1;
    }

    n = 0;
    if (root[0] != '\0') {
        size_t root_len = strlen(root);
        if (root_len + 1 >= out_size)
            return -1;
        memcpy(out, root, root_len);
        n = root_len;
    }

    for (i = 0; i < segment_count; ++i) {
        size_t len = strlen(segments[i]);

        if (n + 1 + len >= out_size)
            return -1;

        if (n == 0 || out[n - 1] != '/')
            out[n++] = '/';

        memcpy(out + n, segments[i], len);
        n += len;
    }

    if (n == 0) {
        out[0] = '/';
        n = 1;
    }

    out[n] = '\0';
    return 0;
}

static int pcps4_mkdir_parents(const char *path)
{
    char tmp[1024];
    size_t i;

    if (path == NULL || strlen(path) >= sizeof(tmp))
        return -1;

    strcpy(tmp, path);

    for (i = 1; tmp[i] != '\0'; ++i) {
        if (tmp[i] != '/')
            continue;

        tmp[i] = '\0';
        if (mkdir(tmp, 0755) != 0 && errno != EEXIST) {
            tmp[i] = '/';
            return -1;
        }
        tmp[i] = '/';
    }

    if (mkdir(tmp, 0755) != 0 && errno != EEXIST)
        return -1;

    return 0;
}

pcps4_os_handle_t *pcps4_sys_file_open(const char *path,
                                        uint32_t desired_access,
                                        uint32_t share_mode,
                                        uint32_t creation_disposition,
                                        uint32_t attributes)
{
    int flags;
    int fd;
    int writing;
    char translated_path[1024];
    pcps4_os_handle_t *handle;

    (void)share_mode;
    (void)attributes;

    if (path == NULL) {
        return NULL;
    }

    writing = (desired_access & 0x40000000u) != 0 ||
              creation_disposition != 3u;

    if (pcps4_translate_path(
            path, writing, translated_path, sizeof(translated_path)) != 0)
        return NULL;

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

    if (writing)
        (void)pcps4_mkdir_parents(translated_path);

    fd = sceKernelOpen(translated_path, flags, 0644);
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

int64_t pcps4_sys_file_size(pcps4_os_handle_t *handle)
{
    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_FILE)
        return -1;
    off_t current = lseek(handle->value.fd, 0, SEEK_CUR);
    if (current < 0) return -1;
    off_t end = lseek(handle->value.fd, 0, SEEK_END);
    if (end < 0) {
        (void)lseek(handle->value.fd, current, SEEK_SET);
        return -1;
    }
    (void)lseek(handle->value.fd, current, SEEK_SET);
    return (int64_t)end;
}

int64_t pcps4_sys_file_seek(pcps4_os_handle_t *handle, int64_t offset, int origin)
{
    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_FILE)
        return -1;
    off_t result = lseek(
        handle->value.fd, (off_t)offset,
        origin == 0 ? SEEK_SET : origin == 1 ? SEEK_CUR : SEEK_END);
    return result < 0 ? -1 : (int64_t)result;
}

int pcps4_sys_file_delete(const char *path)
{
    char translated_path[1024];

    if (path == NULL ||
        pcps4_translate_path(
            path, 1, translated_path, sizeof(translated_path)) != 0)
        return -1;

    return unlink(translated_path);
}

int pcps4_sys_file_mkdir(const char *path)
{
    char translated_path[1024];

    if (path == NULL ||
        pcps4_translate_path(
            path, 1, translated_path, sizeof(translated_path)) != 0)
        return -1;

    return pcps4_mkdir_parents(translated_path);
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

void pcps4_console_tee(const void *buf, size_t n);

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

    if (handle->value.fd == 1 || handle->value.fd == 2)
        pcps4_console_tee(buffer, bytes_to_write);
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


pcps4_os_handle_t *pcps4_sys_event_create(
    int manual_reset,
    int initial_state,
    const char *name)
{
    pcps4_os_handle_t *handle =
        make_handle(PCPS4_HANDLE_EVENT);
    (void)name;
    if (handle == NULL)
        return NULL;
    handle->value.event.signaled = initial_state ? 1 : 0;
    handle->value.event.manual_reset = manual_reset ? 1 : 0;
    return handle;
}

int pcps4_sys_event_set(pcps4_os_handle_t *handle)
{
    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_EVENT)
        return -1;
    __atomic_store_n(&handle->value.event.signaled, 1, __ATOMIC_SEQ_CST);
    return 0;
}

int pcps4_sys_event_reset(pcps4_os_handle_t *handle)
{
    if (!valid_handle(handle) || handle->kind != PCPS4_HANDLE_EVENT)
        return -1;
    __atomic_store_n(&handle->value.event.signaled, 0, __ATOMIC_SEQ_CST);
    return 0;
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

        case PCPS4_HANDLE_EVENT:
            result = 0;
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

    if (handle->kind == PCPS4_HANDLE_EVENT) {
        uint64_t start = pcps4_sys_monotonic_ms();
        for (;;) {
            int signaled = __atomic_load_n(
                &handle->value.event.signaled, __ATOMIC_SEQ_CST);
            if (signaled) {
                if (!handle->value.event.manual_reset)
                    (void)__atomic_exchange_n(
                        &handle->value.event.signaled, 0, __ATOMIC_SEQ_CST);
                return 0;
            }

            if (milliseconds != 0xFFFFFFFFu) {
                uint64_t now = pcps4_sys_monotonic_ms();
                if (now - start >= milliseconds)
                    return 258;
            }

            (void)sceKernelUsleep(1000);
        }
    }

    if (milliseconds != 0xFFFFFFFFu) {
        return 258;
    }

    if (handle->kind == PCPS4_HANDLE_THREAD) {
        void *result = NULL;
        int rc = scePthreadJoin(handle->value.thread, &result);

        if (rc == 0) {
            handle->joined = 1;
            return 0;
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
    uint32_t thread_id;
} pcps4_thread_start_t;

static void *thread_bootstrap(void *opaque)
{
    pcps4_thread_start_t *start = (pcps4_thread_start_t *)opaque;
    pcps4_sys_thread_fn function = start->function;
    void *argument = start->argument;
    uint32_t thread_id = start->thread_id;
    uint32_t exit_code;

    current_thread_id = thread_id;
    free(start);
    pcps4_fault_thread_init();
    pcps4_fault_mark_worker(); /* SIGSYS on this thread ends only the thread */
    exit_code = function(argument);
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
    start->thread_id = __atomic_fetch_add(
        &next_thread_id, 1u, __ATOMIC_SEQ_CST);
    if (thread_id != NULL)
        *thread_id = start->thread_id;

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
