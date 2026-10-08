/*
 * Mac stand-ins for the 43 PlayStation system functions the runtime calls, so
 * the real PE loader, DLL loader, Win32 layer and Windows code can run on a
 * development machine (x86-64 under Rosetta) without a console.
 */
#define _DARWIN_C_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

static const char *g_root = NULL;

static const char *root(void) {
    if (!g_root) {
        g_root = getenv("PCPS4_HOST_ROOT");
        if (!g_root) g_root = "/tmp/pcps4_host";
    }
    return g_root;
}

/* /app0/... and /data/... live under $PCPS4_HOST_ROOT. */
static void map_path(const char *in, char *out, size_t n) {
    if (strncmp(in, "/app0/", 6) == 0 || strncmp(in, "/data/", 6) == 0 || strcmp(in, "/app0") == 0)
        snprintf(out, n, "%s%s", root(), in);
    else
        snprintf(out, n, "%s", in);
}

#undef fopen
FILE *host_fopen(const char *path, const char *mode) {
    char p[1024];
    map_path(path, p, sizeof(p));
    return fopen(p, mode);
}

/* ---- memory ------------------------------------------------------------- */
int32_t sceKernelMmap(void *addr, size_t len, int32_t prot, int32_t flags, int32_t fd, off_t off, void **res) {
    void *p = mmap(addr, len, prot, flags, fd, off);
    if (p == MAP_FAILED) return -1;
    *res = p;
    return 0;
}
int32_t sceKernelMprotect(const void *addr, size_t len, int prot) {
    return mprotect((void *)addr, len, prot) == 0 ? 0 : -1;
}
int32_t sceKernelMunmap(void *addr, size_t len) { return munmap(addr, len); }

int32_t sceKernelAllocateDirectMemory(off_t a, off_t b, size_t len, size_t align, int32_t type, off_t *out) {
    (void)a; (void)b; (void)len; (void)align; (void)type;
    static off_t next = 0x100000;
    *out = next;
    next += (off_t)len;
    return 0;
}
int32_t sceKernelMapDirectMemory(void **addr, size_t len, int32_t prot, int32_t flags, off_t phys, size_t align) {
    (void)flags; (void)phys; (void)align;
    void *p = mmap(NULL, len, prot & (PROT_READ | PROT_WRITE), MAP_PRIVATE | MAP_ANON, -1, 0);
    if (p == MAP_FAILED) return -1;
    *addr = p;
    return 0;
}
int32_t sceKernelReleaseDirectMemory(off_t start, size_t len) { (void)start; (void)len; return 0; }
int64_t sceKernelGetDirectMemorySize(void) { return 4608ll * 1024 * 1024; }

/* ---- time ---------------------------------------------------------------- */
int sceKernelUsleep(unsigned int us) { return usleep(us); }
uint64_t sceKernelGetProcessTime(void) {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000000ull + (uint64_t)t.tv_nsec / 1000ull;
}

/* ---- files --------------------------------------------------------------- */
int sceKernelOpen(const char *path, int flags, int mode) {
    char p[1024];
    map_path(path, p, sizeof(p));
    /* FreeBSD and macOS agree on O_RDONLY/O_WRONLY/O_RDWR; creation flags are
     * passed through as the runtime builds them from <fcntl.h>. */
    return open(p, flags, mode);
}
int sceKernelClose(int fd) { return close(fd); }
ssize_t sceKernelRead(int fd, void *b, size_t n) { return read(fd, b, n); }
ssize_t sceKernelWrite(int fd, const void *b, size_t n) { return write(fd, b, n); }

/* ---- event queues / video / pad / user service: not needed off-console ---- */
int32_t sceKernelCreateEqueue(void *q, const char *name) { (void)name; *(void **)q = (void *)1; return 0; }
int32_t sceKernelDeleteEqueue(void *q) { (void)q; return 0; }
int32_t sceKernelWaitEqueue(void *q, void *ev, int n, int *out, void *to) { (void)q; (void)ev; (void)n; (void)to; if (out) *out = 1; return 0; }
int sceVideoOutOpen(int user, int bus, int idx, void *p) { (void)user; (void)bus; (void)idx; (void)p; return -1; }
int sceVideoOutClose(int h) { (void)h; return 0; }
int sceVideoOutAddFlipEvent(void *q, int h, void *u) { (void)q; (void)h; (void)u; return 0; }
int sceVideoOutRegisterBuffers(int h, int s, void **a, int n, const void *attr) { (void)h; (void)s; (void)a; (void)n; (void)attr; return 0; }
int sceVideoOutUnregisterBuffers(int h, int s) { (void)h; (void)s; return 0; }
void sceVideoOutSetBufferAttribute(void *a, uint32_t f, uint32_t t, uint32_t asp, uint32_t w, uint32_t h, uint32_t p) { (void)a; (void)f; (void)t; (void)asp; (void)w; (void)h; (void)p; }
int sceVideoOutSetFlipRate(int h, int r) { (void)h; (void)r; return 0; }
int sceVideoOutSubmitFlip(int h, int i, uint32_t m, int64_t a) { (void)h; (void)i; (void)m; (void)a; return 0; }
int sceVideoOutGetFlipStatus(int h, void *s) { (void)h; (void)s; return 0; }
int scePadInit(void) { return 0; }
int scePadOpen(int u, int t, int i, void *p) { (void)u; (void)t; (void)i; (void)p; return -1; }
int scePadClose(int h) { (void)h; return 0; }
int scePadRead(int h, void *d, int n) { (void)h; (void)d; (void)n; return -1; }
int scePadSetVibration(int h, const void *p) { (void)h; (void)p; return 0; }
int sceUserServiceInitialize(void *p) { (void)p; return 0; }
int sceUserServiceGetInitialUser(int *u) { if (u) *u = 1; return 0; }

/* ---- threads: OrbisPthread* are pointer-sized handles ---------------------- */
typedef struct { size_t stack; } Attr;
int scePthreadAttrInit(void **a) { *a = calloc(1, sizeof(Attr)); return 0; }
int scePthreadAttrDestroy(void **a) { free(*a); *a = NULL; return 0; }
int scePthreadAttrSetstacksize(void **a, size_t n) { ((Attr *)*a)->stack = n; return 0; }
int scePthreadCreate(void **t, void **attr, void *(*fn)(void *), void *arg, const char *name) {
    (void)name;
    pthread_attr_t pa;
    pthread_attr_init(&pa);
    if (attr && *attr && ((Attr *)*attr)->stack) pthread_attr_setstacksize(&pa, ((Attr *)*attr)->stack);
    pthread_t th;
    int rc = pthread_create(&th, &pa, fn, arg);
    pthread_attr_destroy(&pa);
    if (rc != 0) return rc;
    *t = (void *)th;
    return 0;
}
int scePthreadJoin(void *t, void **ret) { return pthread_join((pthread_t)t, ret); }
int scePthreadDetach(void *t) { return pthread_detach((pthread_t)t); }
void scePthreadExit(void *v) { pthread_exit(v); }
int scePthreadMutexInit(void **m, const void *attr, const char *name) {
    (void)attr; (void)name;
    pthread_mutex_t *p = malloc(sizeof(*p));
    pthread_mutex_init(p, NULL);
    *m = p;
    return 0;
}
int scePthreadMutexLock(void **m) {
    if ((uintptr_t)m < 0x10000 || !*m || (uintptr_t)*m < 0x10000) {
        fprintf(stderr, "[host] BAD mutex lock m=%p *m=%p caller=%p\n", (void *)m, m ? *m : NULL, __builtin_return_address(0));
        abort();
    }
    return pthread_mutex_lock((pthread_mutex_t *)*m); }
int scePthreadMutexUnlock(void **m) { return pthread_mutex_unlock((pthread_mutex_t *)*m); }
int scePthreadMutexDestroy(void **m) { pthread_mutex_destroy((pthread_mutex_t *)*m); free(*m); *m = NULL; return 0; }

/* Fault reporting for the harness: print RIP and a backtrace instead of the on-console one-liner. */
#include <execinfo.h>
#include <signal.h>
#include <sys/ucontext.h>
static void host_segv(int sig, siginfo_t *si, void *uc_) {
    ucontext_t *uc = uc_;
    fprintf(stderr, "[host] signal %d addr=%p rip=%p rsp=%p rdi=%p rax=%p\n", sig, si->si_addr,
            (void *)uc->uc_mcontext->__ss.__rip, (void *)uc->uc_mcontext->__ss.__rsp,
            (void *)uc->uc_mcontext->__ss.__rdi, (void *)uc->uc_mcontext->__ss.__rax);
    uint64_t *sp = (uint64_t *)uc->uc_mcontext->__ss.__rsp;
    for (int i = 0; i < 24; i++) fprintf(stderr, "[host]  stack[%d]=%p\n", i, (void *)sp[i]);
    void *bt[32]; int n = backtrace(bt, 32); backtrace_symbols_fd(bt, n, 2);
    _exit(99);
}
int host_sigaction(int sig, const struct sigaction *a, struct sigaction *o) {
    (void)a;
    struct sigaction s; memset(&s, 0, sizeof(s));
    s.sa_sigaction = host_segv; s.sa_flags = SA_SIGINFO | SA_ONSTACK;
    return sigaction(sig, &s, o);
}
