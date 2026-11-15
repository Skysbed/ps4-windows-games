/* PS4: real kernel pipes once the socket budget is spent.
 *
 * The port's compat layer makes every pipe() a socketpair (the PS5 needs
 * that).  A PS4 process may hold only about 256 sockets, so a program with
 * many threads (each Wine thread needs pipes) gets EMFILE from pipe() while
 * hundreds of ordinary descriptors are still free, and the new threads die.
 * The PS4 kernel has real pipes; ntdll defines its own pipe() over the compat
 * layer, so the kernel's is looked up in libkernel by name.  Socket pairs stay
 * the first choice, so nothing changes until the budget runs out.
 * Linked with -Wl,--wrap=pw_compat_pipe. */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>

int __real_pw_compat_pipe(int fds[2]);
int sceKernelGetModuleList(int32_t *handles, size_t count, size_t *actual);
int sceKernelGetModuleInfo(int32_t handle, void *info);
int sceKernelDlsym(int32_t handle, const char *name, void **address);

typedef int (*pipe_fn)(int fds[2]);

static pipe_fn find_kernel_pipe(void)
{
    static pipe_fn found;
    static int looked;
    int32_t handles[256];
    size_t count = 0;

    if (looked) return found;
    looked = 1;
    if (sceKernelGetModuleList(handles, 256, &count) != 0) count = 0;
    for (size_t i = 0; i < count && i < 256; i++)
    {
        unsigned char info[0x200];
        uint64_t size = 0x160;
        const char *name = (const char *)info + 8;
        void *address = NULL;

        memset(info, 0, sizeof(info));
        memcpy(info, &size, sizeof(size));
        if (sceKernelGetModuleInfo(handles[i], info) != 0) continue;
        if (strncmp(name, "libkernel", 9)) continue;
        if (sceKernelDlsym(handles[i], "pipe", &address) == 0 && address)
        {
            found = (pipe_fn)address;
            fprintf(stderr, "wine-ps4: kernel pipe() found in %s (handle %d)\n", name, (int)handles[i]);
            break;
        }
    }
    if (!found) fprintf(stderr, "wine-ps4: kernel pipe() not found (%zu modules)\n", count);
    return found;
}

int __wrap_pw_compat_pipe(int fds[2])
{
    static unsigned used;
    pipe_fn kernel_pipe;
    int err;

    if (!__real_pw_compat_pipe(fds)) return 0;
    err = errno;
    if (err != EMFILE && err != ENFILE) return -1;
    if (!(kernel_pipe = find_kernel_pipe())) { errno = err; return -1; }
    if (kernel_pipe(fds)) return -1;
    if (++used <= 3 || used % 64 == 0)
        fprintf(stderr, "wine-ps4: socket budget spent - kernel pipe #%u (fds %d,%d)\n", used, fds[0], fds[1]);
    return 0;
}
