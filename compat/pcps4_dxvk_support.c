/*
 * Windows API surface needed to host DXVK's real d3d9/d3d10/d3d11/dxgi DLLs.
 *
 * DXVK 1.10.3's DLLs import ~200 functions. The runtime already provided
 * about a third; this file supplies the rest (measured by diffing the DLLs'
 * import tables against the resolver). Everything here is called by Windows
 * code, so every function uses the Microsoft x64 calling convention.
 *
 * Real implementations: SRW locks, condition variables, semaphores, thread
 * helpers, text conversion, the C runtime pieces. Harmless fixed answers:
 * monitor / window / registry queries (the console has one 1920x1080 display
 * and no registry).
 *
 * Not implemented: C++/SEH exception unwinding (Rtl* are stubs). DXVK only
 * throws on error paths; a throw ends the run with a logged exit code.
 */

#include "pcps4_dxvk_support.h"

#include <ctype.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "pcps4_loader.h"
#include "pcps4_pe.h"
#include "pcps4_status.h"

#define MSA __attribute__((ms_abi))

extern int sceKernelUsleep(unsigned int microseconds);
extern uint64_t sceKernelGetProcessTime(void);
extern void scePthreadExit(void *value);

typedef uint32_t U32;
typedef int32_t I32;
typedef uint64_t U64;
typedef uint16_t U16;
typedef void *PV;

#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xFFFFFFFFu
#define INFINITE_MS 0xFFFFFFFFu

/* Forward to the runtime's existing (Microsoft-ABI) implementations. */
extern MSA U32 pcps4_abi_WaitForSingleObject(PV, U32);
extern MSA I32 pcps4_abi_CloseHandle(PV);
extern MSA PV pcps4_abi_CreateThread(void *, size_t, PV, PV, U32, U32 *);
extern MSA I32 pcps4_abi_CreateDirectoryA(const char *, void *);

static void sleep_ms(U32 ms) { sceKernelUsleep(ms * 1000u); }
static U64 now_us(void) { return sceKernelGetProcessTime(); }

/* ======================================================================== */
/* synchronisation: SRW locks, condition variables, semaphores               */
/* ======================================================================== */

/* SRWLOCK and CONDITION_VARIABLE are one pointer-sized word each. */
static MSA void k_AcquireSRWLockExclusive(volatile uintptr_t *lock) {
    while (__atomic_exchange_n(lock, (uintptr_t)1, __ATOMIC_ACQUIRE)) {
        sceKernelUsleep(50);
    }
}
static MSA void k_ReleaseSRWLockExclusive(volatile uintptr_t *lock) {
    __atomic_store_n(lock, (uintptr_t)0, __ATOMIC_RELEASE);
}
static MSA void k_InitializeConditionVariable(volatile uintptr_t *cv) {
    __atomic_store_n(cv, (uintptr_t)0, __ATOMIC_RELEASE);
}
static MSA void k_WakeConditionVariable(volatile uintptr_t *cv) {
    __atomic_add_fetch(cv, (uintptr_t)1, __ATOMIC_ACQ_REL);
}
static MSA void k_WakeAllConditionVariable(volatile uintptr_t *cv) {
    __atomic_add_fetch(cv, (uintptr_t)1, __ATOMIC_ACQ_REL);
}
/* A generation counter makes lost wake-ups impossible; extra (spurious)
 * wake-ups are allowed by the Windows contract. */
static MSA I32 k_SleepConditionVariableSRW(volatile uintptr_t *cv,
                                           volatile uintptr_t *lock,
                                           U32 timeout_ms, U32 flags) {
    (void)flags;
    uintptr_t gen = __atomic_load_n(cv, __ATOMIC_ACQUIRE);
    const U64 start = now_us();
    k_ReleaseSRWLockExclusive(lock);
    I32 signalled = 0;
    for (;;) {
        if (__atomic_load_n(cv, __ATOMIC_ACQUIRE) != gen) {
            signalled = 1;
            break;
        }
        if (timeout_ms != INFINITE_MS &&
            (now_us() - start) / 1000u >= timeout_ms) {
            break;
        }
        sceKernelUsleep(200);
    }
    k_AcquireSRWLockExclusive(lock);
    return signalled;
}

#define SEM_MAGIC 0x4D455350u /* "PSEM" */
typedef struct {
    U32 magic;
    volatile I32 count;
    I32 max;
} Sem;

static MSA PV k_CreateSemaphoreA(void *attrs, I32 initial, I32 maxcount, const char *name) {
    (void)attrs;
    (void)name;
    if (maxcount <= 0 || initial < 0 || initial > maxcount) return NULL;
    Sem *s = (Sem *)calloc(1, sizeof(*s));
    if (!s) return NULL;
    s->magic = SEM_MAGIC;
    s->count = initial;
    s->max = maxcount;
    return s;
}

static MSA I32 k_ReleaseSemaphore(PV h, I32 n, I32 *previous) {
    Sem *s = (Sem *)h;
    if (!s || (uintptr_t)h < 0x10000 || s->magic != SEM_MAGIC || n <= 0) return 0;
    for (;;) {
        I32 cur = __atomic_load_n(&s->count, __ATOMIC_ACQUIRE);
        if (cur + n > s->max) return 0;
        if (__atomic_compare_exchange_n(&s->count, &cur, cur + n, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            if (previous) *previous = cur;
            return 1;
        }
    }
}

static int is_sem(PV h) {
    return (uintptr_t)h >= 0x10000 && ((Sem *)h)->magic == SEM_MAGIC;
}

static U32 sem_wait(Sem *s, U32 timeout_ms) {
    const U64 start = now_us();
    for (;;) {
        I32 cur = __atomic_load_n(&s->count, __ATOMIC_ACQUIRE);
        if (cur > 0 &&
            __atomic_compare_exchange_n(&s->count, &cur, cur - 1, 0,
                                        __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
            return WAIT_OBJECT_0;
        }
        if (timeout_ms == 0) return WAIT_TIMEOUT;
        if (timeout_ms != INFINITE_MS && (now_us() - start) / 1000u >= timeout_ms)
            return WAIT_TIMEOUT;
        sceKernelUsleep(200);
    }
}

/* Replacements for the runtime's wait/close that also understand semaphores. */
static MSA U32 k_WaitForSingleObject(PV h, U32 timeout_ms) {
    if (is_sem(h)) return sem_wait((Sem *)h, timeout_ms);
    return pcps4_abi_WaitForSingleObject(h, timeout_ms);
}
static MSA U32 k_WaitForSingleObjectEx(PV h, U32 timeout_ms, I32 alertable) {
    (void)alertable;
    return k_WaitForSingleObject(h, timeout_ms);
}
static MSA I32 k_CloseHandle(PV h) {
    if (is_sem(h)) {
        ((Sem *)h)->magic = 0;
        free(h);
        return 1;
    }
    return pcps4_abi_CloseHandle(h);
}
static MSA U32 k_WaitForMultipleObjects(U32 count, PV *handles, I32 wait_all, U32 timeout_ms) {
    if (!handles || count == 0 || count > 64) return WAIT_FAILED;
    const U64 start = now_us();
    for (;;) {
        if (wait_all) {
            U32 got = 0;
            for (U32 i = 0; i < count; i += 1) {
                /* Non-destructive for events; semaphores are taken only when all are ready. */
                if (k_WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0) got += 1;
            }
            if (got == count) return WAIT_OBJECT_0;
        } else {
            for (U32 i = 0; i < count; i += 1) {
                if (k_WaitForSingleObject(handles[i], 0) == WAIT_OBJECT_0)
                    return WAIT_OBJECT_0 + i;
            }
        }
        if (timeout_ms == 0) return WAIT_TIMEOUT;
        if (timeout_ms != INFINITE_MS && (now_us() - start) / 1000u >= timeout_ms)
            return WAIT_TIMEOUT;
        sceKernelUsleep(500);
    }
}

/* ======================================================================== */
/* kernel32: process, thread, text, exceptions                               */
/* ======================================================================== */

static MSA I32 k_DuplicateHandle(PV sp, PV src, PV dp, PV *dst, U32 acc, I32 inh, U32 opt) {
    (void)sp; (void)dp; (void)acc; (void)inh; (void)opt;
    if (dst) *dst = src;
    return 1;
}
static MSA I32 k_GetHandleInformation(PV h, U32 *flags) {
    (void)h;
    if (flags) *flags = 0;
    return 1;
}
static MSA PV k_OpenProcess(U32 acc, I32 inh, U32 pid) {
    (void)acc; (void)inh; (void)pid;
    return (PV)(uintptr_t)1;
}
static MSA I32 k_GetProcessAffinityMask(PV h, U64 *proc, U64 *sys) {
    (void)h;
    if (proc) *proc = 0x3F; /* six cores available to applications */
    if (sys) *sys = 0x3F;
    return 1;
}
static MSA I32 k_SetProcessAffinityMask(PV h, U64 m) { (void)h; (void)m; return 1; }
static MSA I32 k_SetThreadPriority(PV h, I32 p) { (void)h; (void)p; return 1; }
static MSA I32 k_GetThreadPriority(PV h) { (void)h; return 0; }
static MSA U32 k_SuspendThread(PV h) { (void)h; return 0; }
static MSA U32 k_ResumeThread(PV h) { (void)h; return 0; }
static MSA I32 k_GetThreadContext(PV h, PV ctx) { (void)h; (void)ctx; return 0; }
static MSA I32 k_SetThreadContext(PV h, PV ctx) { (void)h; (void)ctx; return 0; }
static MSA I32 k_SwitchToThread(void) { sceKernelUsleep(1); return 1; }
static MSA U32 k_FormatMessageA(U32 f, PV s, U32 id, U32 lang, char *buf, U32 n, PV args) {
    (void)f; (void)s; (void)id; (void)lang; (void)buf; (void)n; (void)args;
    return 0;
}
static MSA PV k_LocalFree(PV p) { (void)p; return NULL; }
static MSA I32 k_IsDBCSLeadByteEx(U32 cp, uint8_t b) { (void)cp; (void)b; return 0; }
static MSA void k_OutputDebugStringA(const char *s) { (void)s; }
static MSA I32 k_DeviceIoControl(PV h, U32 code, PV in, U32 insz, PV out, U32 outsz, U32 *ret, PV ov) {
    (void)h; (void)code; (void)in; (void)insz; (void)out; (void)outsz; (void)ov;
    if (ret) *ret = 0;
    return 0;
}

/* UTF-8 <-> UTF-16 (code pages other than UTF-8 are treated as UTF-8/ASCII). */
static MSA I32 k_MultiByteToWideChar(U32 cp, U32 flags, const char *src, I32 srclen, U16 *dst, I32 dstlen) {
    (void)cp; (void)flags;
    if (!src) return 0;
    if (srclen < 0) srclen = (I32)strlen(src) + 1;
    I32 out = 0;
    for (I32 i = 0; i < srclen;) {
        uint32_t c = (uint8_t)src[i];
        int extra = 0;
        if (c >= 0xF0) { c &= 0x07; extra = 3; }
        else if (c >= 0xE0) { c &= 0x0F; extra = 2; }
        else if (c >= 0xC0) { c &= 0x1F; extra = 1; }
        i += 1;
        while (extra-- > 0 && i < srclen) c = (c << 6) | ((uint8_t)src[i++] & 0x3F);
        int units = c >= 0x10000 ? 2 : 1;
        if (dst) {
            if (out + units > dstlen) return 0;
            if (units == 2) {
                c -= 0x10000;
                dst[out] = (U16)(0xD800 + (c >> 10));
                dst[out + 1] = (U16)(0xDC00 + (c & 0x3FF));
            } else {
                dst[out] = (U16)c;
            }
        }
        out += units;
    }
    return out;
}
static MSA I32 k_WideCharToMultiByte(U32 cp, U32 flags, const U16 *src, I32 srclen, char *dst,
                                     I32 dstlen, const char *def, I32 *used) {
    (void)cp; (void)flags; (void)def;
    if (used) *used = 0;
    if (!src) return 0;
    if (srclen < 0) {
        srclen = 0;
        while (src[srclen]) srclen += 1;
        srclen += 1;
    }
    I32 out = 0;
    for (I32 i = 0; i < srclen; i += 1) {
        uint32_t c = src[i];
        if (c >= 0xD800 && c < 0xDC00 && i + 1 < srclen) {
            c = 0x10000 + ((c - 0xD800) << 10) + (src[i + 1] - 0xDC00);
            i += 1;
        }
        uint8_t b[4];
        int n;
        if (c < 0x80) { b[0] = (uint8_t)c; n = 1; }
        else if (c < 0x800) { b[0] = (uint8_t)(0xC0 | (c >> 6)); b[1] = (uint8_t)(0x80 | (c & 0x3F)); n = 2; }
        else if (c < 0x10000) { b[0] = (uint8_t)(0xE0 | (c >> 12)); b[1] = (uint8_t)(0x80 | ((c >> 6) & 0x3F)); b[2] = (uint8_t)(0x80 | (c & 0x3F)); n = 3; }
        else { b[0] = (uint8_t)(0xF0 | (c >> 18)); b[1] = (uint8_t)(0x80 | ((c >> 12) & 0x3F)); b[2] = (uint8_t)(0x80 | ((c >> 6) & 0x3F)); b[3] = (uint8_t)(0x80 | (c & 0x3F)); n = 4; }
        if (dst) {
            if (out + n > dstlen) return 0;
            memcpy(dst + out, b, (size_t)n);
        }
        out += n;
    }
    return out;
}
static MSA I32 k_CreateDirectoryW(const U16 *path, PV attrs) {
    char buf[512];
    if (k_WideCharToMultiByte(65001, 0, path, -1, buf, (I32)sizeof(buf), NULL, NULL) <= 0) return 0;
    return pcps4_abi_CreateDirectoryA(buf, attrs);
}

/* Exceptions: no SEH/C++ unwinding. A raise ends the run with a logged code. */
static MSA void k_RaiseException(U32 code, U32 flags, U32 nargs, const U64 *args) {
    (void)flags; (void)nargs; (void)args;
    pcps4_status_log("[pcps4] RaiseException(0x%x): exception unwinding is not implemented", code);
    pcps4_pe_exit_process(code);
}
static MSA PV k_AddVectoredExceptionHandler(U32 first, PV handler) { (void)first; (void)handler; return (PV)(uintptr_t)1; }
static MSA U32 k_RemoveVectoredExceptionHandler(PV h) { (void)h; return 1; }
static MSA void k_RtlCaptureContext(PV ctx) { if (ctx) memset(ctx, 0, 0x4D0); }
static MSA PV k_RtlLookupFunctionEntry(U64 ip, U64 *base, PV hist) {
    (void)ip; (void)hist;
    if (base) *base = 0;
    return NULL;
}
static MSA PV k_RtlVirtualUnwind(U32 type, U64 base, U64 ip, PV fn, PV ctx, PV *hdata, U64 *frame, PV ctxptrs) {
    (void)type; (void)base; (void)ip; (void)fn; (void)ctx; (void)ctxptrs;
    if (hdata) *hdata = NULL;
    if (frame) *frame = 0;
    return NULL;
}
static MSA void k_RtlUnwindEx(PV target, PV ip, PV rec, PV value, PV ctx, PV hist) {
    (void)target; (void)ip; (void)rec; (void)value; (void)ctx; (void)hist;
}
static MSA I32 k___C_specific_handler(PV rec, PV frame, PV ctx, PV disp) {
    (void)rec; (void)frame; (void)ctx; (void)disp;
    return 1; /* ExceptionContinueSearch */
}

/* ======================================================================== */
/* msvcrt                                                                    */
/* ======================================================================== */

static char g_iob[3 * 0x58];
static MSA PV c___iob_func(void) { return g_iob; }
static MSA U32 c____lc_codepage_func(void) { return 0; }
static MSA I32 c____mb_cur_max_func(void) { return 1; }
static MSA void c___setusermatherr(PV f) { (void)f; }

static MSA PV c__aligned_malloc(size_t size, size_t align) {
    if (align < sizeof(void *)) align = sizeof(void *);
    void *raw = malloc(size + align + sizeof(void *));
    if (!raw) return NULL;
    uintptr_t a = ((uintptr_t)raw + sizeof(void *) + align - 1) & ~(uintptr_t)(align - 1);
    ((void **)a)[-1] = raw;
    return (void *)a;
}
static MSA void c__aligned_free(PV p) { if (p) free(((void **)p)[-1]); }
static MSA void c__amsg_exit(I32 code) { pcps4_pe_exit_process((U32)code); }

static MSA PV c__beginthreadex(PV sec, U32 stack, PV start, PV arg, U32 flags, U32 *id) {
    (void)sec;
    return pcps4_abi_CreateThread(NULL, stack, start, arg, flags, id);
}
static MSA void c__endthreadex(U32 code) { scePthreadExit((void *)(uintptr_t)code); }

static MSA I32 c__close(I32 fd) { (void)fd; return -1; }
static MSA I32 c__read(I32 fd, PV b, U32 n) { (void)fd; (void)b; (void)n; return -1; }
static MSA I32 c__write(I32 fd, const void *b, U32 n) { (void)fd; (void)b; (void)n; return -1; }
static MSA int64_t c__lseeki64(I32 fd, int64_t off, I32 whence) { (void)fd; (void)off; (void)whence; return -1; }
static MSA int64_t c__filelengthi64(I32 fd) { (void)fd; return -1; }
static MSA I32 c__fileno(PV f) { (void)f; return -1; }
static MSA I32 c__fstat64(I32 fd, PV st) { (void)fd; (void)st; return -1; }
static MSA PV c__fdopen(I32 fd, const char *m) { (void)fd; (void)m; return NULL; }
static MSA PV c__wfopen(const U16 *p, const U16 *m) { (void)p; (void)m; return NULL; }

static __thread I32 g_errno;
static MSA I32 *c__errno(void) { return &g_errno; }

typedef MSA void (*InitFn)(void);
static MSA void c__initterm(InitFn *b, InitFn *e) {
    for (InitFn *p = b; p && p < e; p += 1) {
        if (*p) (*p)();
    }
}
static MSA void c__lock(I32 n) { (void)n; }
static MSA void c__unlock(I32 n) { (void)n; }
static MSA I32 c__setjmp(PV buf, PV frame) { (void)buf; (void)frame; return 0; }
static MSA void c_longjmp(PV buf, I32 v) {
    (void)buf;
    pcps4_status_log("[pcps4] longjmp(%d) is not supported", v);
    pcps4_pe_exit_process(254);
}
static MSA char *c__strdup(const char *s) {
    if (!s) return NULL;
    size_t n = strlen(s) + 1;
    char *d = (char *)malloc(n);
    if (d) memcpy(d, s, n);
    return d;
}
static MSA char *c__ultoa(unsigned long v, char *buf, I32 radix) {
    char tmp[40];
    int n = 0;
    if (radix < 2 || radix > 36) radix = 10;
    do {
        int d = (int)(v % (unsigned long)radix);
        tmp[n++] = (char)(d < 10 ? '0' + d : 'a' + d - 10);
        v /= (unsigned long)radix;
    } while (v && n < 39);
    for (int i = 0; i < n; i += 1) buf[i] = tmp[n - 1 - i];
    buf[n] = 0;
    return buf;
}

static MSA I32 c_fflush(FILE *f) { return fflush(f); }
static MSA I32 c_fputc(I32 c, FILE *f) { return fputc(c, f); }
static MSA I32 c_fputs(const char *s, FILE *f) { return fputs(s, f); }
static MSA I32 c_putc(I32 c, FILE *f) { return fputc(c, f); }
static MSA I32 c_getc(FILE *f) { return fgetc(f); }
static MSA I32 c_ungetc(I32 c, FILE *f) { return ungetc(c, f); }
static MSA I32 c_setvbuf(FILE *f, char *b, I32 mode, size_t n) { return setvbuf(f, b, mode, n); }
static MSA I32 c_fgetpos(FILE *f, fpos_t *p) { return fgetpos(f, p); }
static MSA I32 c_fsetpos(FILE *f, const fpos_t *p) { return fsetpos(f, p); }
static MSA U32 c_getwc(PV f) { (void)f; return 0xFFFF; }
static MSA U32 c_putwc(U16 c, PV f) { (void)c; (void)f; return 0xFFFF; }
static MSA U32 c_ungetwc(U16 c, PV f) { (void)c; (void)f; return 0xFFFF; }
/* printf family: output is discarded (DXVK logs through its own file layer). */
static MSA I32 c_printf(const char *fmt, ...) { (void)fmt; return 0; }
static MSA I32 c_fprintf(PV f, const char *fmt, ...) { (void)f; (void)fmt; return 0; }
static MSA I32 c_vfprintf(PV f, const char *fmt, PV va) { (void)f; (void)fmt; (void)va; return 0; }

static MSA I32 c_iswctype(U32 c, U32 t) { (void)c; (void)t; return 0; }
static MSA I32 c_toupper(I32 c) { return toupper(c); }
static MSA U32 c_towlower(U32 c) { return (c >= 'A' && c <= 'Z') ? c + 32 : c; }
static MSA U32 c_towupper(U32 c) { return (c >= 'a' && c <= 'z') ? c - 32 : c; }
static char g_lconv[128];
static MSA PV c_localeconv(void) {
    static char dot[] = ".";
    if (!*(char **)g_lconv) *(char **)g_lconv = dot;
    return g_lconv;
}
static MSA char *c_setlocale(I32 cat, const char *loc) { (void)cat; (void)loc; static char c[] = "C"; return c; }
static MSA PV c_signal(I32 sig, PV h) { (void)sig; (void)h; return NULL; }
static MSA PV c_memchr(const void *p, I32 c, size_t n) { return memchr(p, c, n); }
static MSA char *c_strncpy(char *d, const char *s, size_t n) { return strncpy(d, s, n); }
static MSA char *c_strerror(I32 e) { (void)e; static char m[] = "error"; return m; }
static MSA I32 c_strcoll(const char *a, const char *b) { return strcmp(a, b); }
static MSA size_t c_strxfrm(char *d, const char *s, size_t n) {
    size_t l = strlen(s);
    if (d && n) { strncpy(d, s, n); if (l >= n) d[n - 1] = 0; }
    return l;
}
static MSA size_t c_strftime(char *b, size_t n, const char *f, PV t) { (void)f; (void)t; if (b && n) b[0] = 0; return 0; }
static MSA size_t c_wcslen(const U16 *s) { size_t n = 0; while (s[n]) n += 1; return n; }
static MSA I32 c_wcscoll(const U16 *a, const U16 *b) {
    while (*a && *a == *b) { a += 1; b += 1; }
    return (I32)*a - (I32)*b;
}
static MSA size_t c_wcsxfrm(U16 *d, const U16 *s, size_t n) {
    size_t l = c_wcslen(s);
    if (d && n) { size_t c = l < n ? l : n - 1; memcpy(d, s, c * 2); d[c] = 0; }
    return l;
}
static MSA size_t c_wcsftime(U16 *b, size_t n, const U16 *f, PV t) { (void)f; (void)t; if (b && n) b[0] = 0; return 0; }

/* ======================================================================== */
/* user32 / gdi32 / advapi32: one fixed 1920x1080 display, no registry        */
/* ======================================================================== */

typedef struct { I32 left, top, right, bottom; } Rect;

static MSA I32 u_AdjustWindowRectEx(Rect *r, U32 style, I32 menu, U32 ex) { (void)r; (void)style; (void)menu; (void)ex; return 1; }
static MSA PV u_CallWindowProcA(PV (MSA *proc)(PV, U32, U64, int64_t), PV h, U32 m, U64 w, int64_t l) { return proc ? proc(h, m, w, l) : NULL; }
static MSA I32 u_ChangeDisplaySettingsExW(const U16 *dev, PV mode, PV hwnd, U32 flags, PV p) { (void)dev; (void)mode; (void)hwnd; (void)flags; (void)p; return 0; }
static MSA PV u_CreateIconIndirect(PV info) { (void)info; return (PV)(uintptr_t)1; }
static MSA I32 u_DestroyCursor(PV c) { (void)c; return 1; }
static MSA I32 u_EnumDisplayDevicesA(const char *d, U32 n, PV dd, U32 f) { (void)d; (void)n; (void)dd; (void)f; return 0; }
typedef MSA I32 (*MonitorEnumProc)(PV, PV, Rect *, int64_t);
static MSA I32 u_EnumDisplayMonitors(PV hdc, const Rect *clip, MonitorEnumProc proc, int64_t data) {
    (void)clip;
    Rect r = {0, 0, 1920, 1080};
    if (proc) proc((PV)(uintptr_t)1, hdc, &r, data);
    return 1;
}
static MSA I32 u_EnumDisplaySettingsW(const U16 *dev, U32 mode, uint8_t *dm) {
    (void)dev;
    if (!dm || (mode != 0 && mode != 0xFFFFFFFFu)) return 0; /* ENUM_CURRENT_SETTINGS == -1 */
    *(U16 *)(dm + 68) = 220; /* dmSize */
    *(U32 *)(dm + 72) = 0x40000u | 0x80000u | 0x100000u | 0x400000u; /* bpp,w,h,freq */
    *(U32 *)(dm + 168) = 32;
    *(U32 *)(dm + 172) = 1920;
    *(U32 *)(dm + 176) = 1080;
    *(U32 *)(dm + 184) = 60;
    return 1;
}
static MSA I32 u_GetCursorPos(I32 *pt) { if (pt) { pt[0] = 0; pt[1] = 0; } return 1; }
static MSA I32 u_GetMonitorInfoW(PV mon, U32 *mi) {
    (void)mon;
    if (!mi) return 0;
    I32 *r = (I32 *)(mi + 1);
    r[0] = 0; r[1] = 0; r[2] = 1920; r[3] = 1080; /* rcMonitor */
    r[4] = 0; r[5] = 0; r[6] = 1920; r[7] = 1080; /* rcWork */
    mi[9] = 1;                                     /* MONITORINFOF_PRIMARY */
    if (mi[0] >= 104) {                            /* MONITORINFOEXW */
        static const char name[] = "\\\\.\\DISPLAY1";
        U16 *dev = (U16 *)(mi + 10);
        for (size_t i = 0; i < sizeof(name); i += 1) dev[i] = (U16)name[i];
    }
    return 1;
}
static MSA I32 u_GetWindowLongW(PV h, I32 idx) { (void)h; (void)idx; return 0; }
static MSA I32 u_SetWindowLongW(PV h, I32 idx, I32 v) { (void)h; (void)idx; (void)v; return 0; }
static MSA I32 u_IsIconic(PV h) { (void)h; return 0; }
static MSA I32 u_IsWindowUnicode(PV h) { (void)h; return 1; }
static MSA I32 u_IsWindowVisible(PV h) { (void)h; return 1; }
static MSA PV u_MonitorFromPoint(I32 x, I32 y, U32 flags) { (void)x; (void)y; (void)flags; return (PV)(uintptr_t)1; }
static MSA I32 u_MoveWindow(PV h, I32 x, I32 y, I32 w, I32 ht, I32 repaint) { (void)h; (void)x; (void)y; (void)w; (void)ht; (void)repaint; return 1; }
static MSA I32 u_OffsetRect(Rect *r, I32 dx, I32 dy) { if (!r) return 0; r->left += dx; r->right += dx; r->top += dy; r->bottom += dy; return 1; }
static MSA I32 u_PostMessageW(PV h, U32 m, U64 w, int64_t l) { (void)h; (void)m; (void)w; (void)l; return 1; }
static MSA PV u_SetCursor(PV c) { (void)c; return NULL; }
static MSA I32 u_SetCursorPos(I32 x, I32 y) { (void)x; (void)y; return 1; }
static MSA I32 u_SetProcessDPIAware(void) { return 1; }
static MSA I32 u_SetRect(Rect *r, I32 l, I32 t, I32 rt, I32 b) { if (!r) return 0; r->left = l; r->top = t; r->right = rt; r->bottom = b; return 1; }
static MSA I32 u_SetWindowPos(PV h, PV after, I32 x, I32 y, I32 cx, I32 cy, U32 f) { (void)h; (void)after; (void)x; (void)y; (void)cx; (void)cy; (void)f; return 1; }

static MSA PV g_CreateBitmap(I32 w, I32 h, U32 planes, U32 bpp, const void *bits) { (void)w; (void)h; (void)planes; (void)bpp; (void)bits; return (PV)(uintptr_t)1; }
static MSA PV g_CreateCompatibleDC(PV dc) { (void)dc; return (PV)(uintptr_t)1; }
static MSA I32 g_DeleteDC(PV dc) { (void)dc; return 1; }
static MSA I32 g_DeleteObject(PV o) { (void)o; return 1; }

static MSA I32 a_AllocateLocallyUniqueId(U32 *luid) {
    static volatile U32 next = 1000;
    if (!luid) return 0;
    luid[0] = __atomic_add_fetch(&next, 1, __ATOMIC_RELAXED);
    luid[1] = 0;
    return 1;
}
static MSA I32 a_RegCloseKey(PV k) { (void)k; return 0; }
static MSA I32 a_RegNotifyChangeKeyValue(PV k, I32 sub, U32 f, PV ev, I32 async) { (void)k; (void)sub; (void)f; (void)ev; (void)async; return 87; }
static MSA I32 a_RegOpenKeyExA(PV k, const char *s, U32 o, U32 sam, PV *out) { (void)k; (void)s; (void)o; (void)sam; if (out) *out = NULL; return 2; }
static MSA I32 a_RegQueryValueExA(PV k, const char *n, U32 *r, U32 *t, uint8_t *d, U32 *sz) { (void)k; (void)n; (void)r; (void)t; (void)d; (void)sz; return 2; }

/* ======================================================================== */
/* registration                                                              */
/* ======================================================================== */

#define R(dll, name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))

void pcps4_dxvk_register_win32(pcps4_pe_resolver *resolver) {
    static const char *const k32[] = {"KERNEL32.dll", "KERNELBASE.dll"};
    for (unsigned i = 0; i < 2; i += 1) {
        const char *d = k32[i];
        R(d, "AcquireSRWLockExclusive", k_AcquireSRWLockExclusive);
        R(d, "ReleaseSRWLockExclusive", k_ReleaseSRWLockExclusive);
        R(d, "InitializeConditionVariable", k_InitializeConditionVariable);
        R(d, "WakeConditionVariable", k_WakeConditionVariable);
        R(d, "WakeAllConditionVariable", k_WakeAllConditionVariable);
        R(d, "SleepConditionVariableSRW", k_SleepConditionVariableSRW);
        R(d, "CreateSemaphoreA", k_CreateSemaphoreA);
        R(d, "ReleaseSemaphore", k_ReleaseSemaphore);
        R(d, "WaitForSingleObject", k_WaitForSingleObject);
        R(d, "WaitForSingleObjectEx", k_WaitForSingleObjectEx);
        R(d, "WaitForMultipleObjects", k_WaitForMultipleObjects);
        R(d, "CloseHandle", k_CloseHandle);
        R(d, "DuplicateHandle", k_DuplicateHandle);
        R(d, "GetHandleInformation", k_GetHandleInformation);
        R(d, "OpenProcess", k_OpenProcess);
        R(d, "GetProcessAffinityMask", k_GetProcessAffinityMask);
        R(d, "SetProcessAffinityMask", k_SetProcessAffinityMask);
        R(d, "SetThreadPriority", k_SetThreadPriority);
        R(d, "GetThreadPriority", k_GetThreadPriority);
        R(d, "SuspendThread", k_SuspendThread);
        R(d, "ResumeThread", k_ResumeThread);
        R(d, "GetThreadContext", k_GetThreadContext);
        R(d, "SetThreadContext", k_SetThreadContext);
        R(d, "SwitchToThread", k_SwitchToThread);
        R(d, "FormatMessageA", k_FormatMessageA);
        R(d, "LocalFree", k_LocalFree);
        R(d, "IsDBCSLeadByteEx", k_IsDBCSLeadByteEx);
        R(d, "OutputDebugStringA", k_OutputDebugStringA);
        R(d, "DeviceIoControl", k_DeviceIoControl);
        R(d, "MultiByteToWideChar", k_MultiByteToWideChar);
        R(d, "WideCharToMultiByte", k_WideCharToMultiByte);
        R(d, "CreateDirectoryW", k_CreateDirectoryW);
        R(d, "RaiseException", k_RaiseException);
        R(d, "AddVectoredExceptionHandler", k_AddVectoredExceptionHandler);
        R(d, "RemoveVectoredExceptionHandler", k_RemoveVectoredExceptionHandler);
        R(d, "RtlCaptureContext", k_RtlCaptureContext);
        R(d, "RtlLookupFunctionEntry", k_RtlLookupFunctionEntry);
        R(d, "RtlVirtualUnwind", k_RtlVirtualUnwind);
        R(d, "RtlUnwindEx", k_RtlUnwindEx);
        R(d, "__C_specific_handler", k___C_specific_handler);
    }

    static const char *const crt[] = {"msvcrt.dll", "ucrtbase.dll", "MSVCRT.dll"};
    for (unsigned i = 0; i < 2; i += 1) {
        const char *d = crt[i];
        R(d, "__iob_func", c___iob_func);
        R(d, "___lc_codepage_func", c____lc_codepage_func);
        R(d, "___mb_cur_max_func", c____mb_cur_max_func);
        R(d, "__setusermatherr", c___setusermatherr);
        R(d, "_aligned_malloc", c__aligned_malloc);
        R(d, "_aligned_free", c__aligned_free);
        R(d, "_amsg_exit", c__amsg_exit);
        R(d, "_beginthreadex", c__beginthreadex);
        R(d, "_endthreadex", c__endthreadex);
        R(d, "_close", c__close);
        R(d, "_read", c__read);
        R(d, "_write", c__write);
        R(d, "_lseeki64", c__lseeki64);
        R(d, "_filelengthi64", c__filelengthi64);
        R(d, "_fileno", c__fileno);
        R(d, "_fstat64", c__fstat64);
        R(d, "_fdopen", c__fdopen);
        R(d, "_wfopen", c__wfopen);
        R(d, "_errno", c__errno);
        R(d, "_initterm", c__initterm);
        R(d, "_lock", c__lock);
        R(d, "_unlock", c__unlock);
        R(d, "_setjmp", c__setjmp);
        R(d, "longjmp", c_longjmp);
        R(d, "_strdup", c__strdup);
        R(d, "_ultoa", c__ultoa);
        R(d, "fflush", c_fflush);
        R(d, "fputc", c_fputc);
        R(d, "fputs", c_fputs);
        R(d, "putc", c_putc);
        R(d, "getc", c_getc);
        R(d, "ungetc", c_ungetc);
        R(d, "setvbuf", c_setvbuf);
        R(d, "fgetpos", c_fgetpos);
        R(d, "fsetpos", c_fsetpos);
        R(d, "getwc", c_getwc);
        R(d, "putwc", c_putwc);
        R(d, "ungetwc", c_ungetwc);
        R(d, "printf", c_printf);
        R(d, "fprintf", c_fprintf);
        R(d, "vfprintf", c_vfprintf);
        R(d, "iswctype", c_iswctype);
        R(d, "toupper", c_toupper);
        R(d, "towlower", c_towlower);
        R(d, "towupper", c_towupper);
        R(d, "localeconv", c_localeconv);
        R(d, "setlocale", c_setlocale);
        R(d, "signal", c_signal);
        R(d, "memchr", c_memchr);
        R(d, "strncpy", c_strncpy);
        R(d, "strerror", c_strerror);
        R(d, "strcoll", c_strcoll);
        R(d, "strxfrm", c_strxfrm);
        R(d, "strftime", c_strftime);
        R(d, "wcslen", c_wcslen);
        R(d, "wcscoll", c_wcscoll);
        R(d, "wcsxfrm", c_wcsxfrm);
        R(d, "wcsftime", c_wcsftime);
    }

    R("USER32.dll", "AdjustWindowRectEx", u_AdjustWindowRectEx);
    R("USER32.dll", "CallWindowProcA", u_CallWindowProcA);
    R("USER32.dll", "CallWindowProcW", u_CallWindowProcA);
    R("USER32.dll", "ChangeDisplaySettingsExW", u_ChangeDisplaySettingsExW);
    R("USER32.dll", "CreateIconIndirect", u_CreateIconIndirect);
    R("USER32.dll", "DestroyCursor", u_DestroyCursor);
    R("USER32.dll", "EnumDisplayDevicesA", u_EnumDisplayDevicesA);
    R("USER32.dll", "EnumDisplayMonitors", u_EnumDisplayMonitors);
    R("USER32.dll", "EnumDisplaySettingsW", u_EnumDisplaySettingsW);
    R("USER32.dll", "GetCursorPos", u_GetCursorPos);
    R("USER32.dll", "GetMonitorInfoW", u_GetMonitorInfoW);
    R("USER32.dll", "GetWindowLongW", u_GetWindowLongW);
    R("USER32.dll", "IsIconic", u_IsIconic);
    R("USER32.dll", "IsWindowUnicode", u_IsWindowUnicode);
    R("USER32.dll", "IsWindowVisible", u_IsWindowVisible);
    R("USER32.dll", "MonitorFromPoint", u_MonitorFromPoint);
    R("USER32.dll", "MoveWindow", u_MoveWindow);
    R("USER32.dll", "OffsetRect", u_OffsetRect);
    R("USER32.dll", "PostMessageW", u_PostMessageW);
    R("USER32.dll", "SetCursor", u_SetCursor);
    R("USER32.dll", "SetCursorPos", u_SetCursorPos);
    R("USER32.dll", "SetProcessDPIAware", u_SetProcessDPIAware);
    R("USER32.dll", "SetRect", u_SetRect);
    R("USER32.dll", "SetWindowLongW", u_SetWindowLongW);
    R("USER32.dll", "SetWindowPos", u_SetWindowPos);

    R("GDI32.dll", "CreateBitmap", g_CreateBitmap);
    R("GDI32.dll", "CreateCompatibleDC", g_CreateCompatibleDC);
    R("GDI32.dll", "DeleteDC", g_DeleteDC);
    R("GDI32.dll", "DeleteObject", g_DeleteObject);

    R("ADVAPI32.dll", "AllocateLocallyUniqueId", a_AllocateLocallyUniqueId);
    R("ADVAPI32.dll", "RegCloseKey", a_RegCloseKey);
    R("ADVAPI32.dll", "RegNotifyChangeKeyValue", a_RegNotifyChangeKeyValue);
    R("ADVAPI32.dll", "RegOpenKeyExA", a_RegOpenKeyExA);
    R("ADVAPI32.dll", "RegQueryValueExA", a_RegQueryValueExA);
}
