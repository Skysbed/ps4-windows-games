/*
 * pcps4_wine_host.h — Wine ntdll/unix host shim for OrbisOS (Stage 1)
 * ====================================================================
 *
 * OrbisOS is a FreeBSD 9 fork, so Wine's existing `#ifdef __FreeBSD__`
 * host paths in dlls/ntdll/unix compile almost unchanged. This header
 * supplies the handful of FreeBSD host primitives that Wine's FreeBSD path
 * references but the OpenOrbis toolchain headers do NOT declare:
 *
 *   - sysarch() + the AMD64_/X86_64_ GS/FS-base selectors   (TEB mechanism)
 *   - amd64_{set,get}_{gs,fs}base() libc convenience wrappers
 *   - thr_self() / pthread_getthreadid_np()                  (thread id)
 *   - _umtx_op() + struct _umtx_time                          (low-level sync)
 *   - kqueue()/kevent() + struct kevent + EV_/EVFILT_         (signal/wait loop)
 *
 * Every one of these resolves to a symbol the real 13.02 firmware libs
 * already export (verified by NID match against libkernel*.sprx /
 * libc.sprx — see docs/ORBISOS_HOST_SURFACE.md). We therefore call the
 * EXPORTED WRAPPER BY NAME and only declare the prototype the toolchain
 * omits — we never emit a raw `syscall(SYS_xxx)`. That sidesteps the one
 * real research risk (Sony renumbering a syscall): the wrapper inside
 * libkernel already calls whatever number OrbisOS uses.
 *
 * Constant values are the stock FreeBSD 9 values (x86/sysarch.h,
 * sys/event.h, sys/umtx.h); the OpenOrbis toolchain's bits/syscall.h
 * confirms the matching SYS_* numbers are unchanged (sysarch=165,
 * thr_self=432, _umtx_op=454, kqueue=362, kevent=363).
 *
 * This is the Unix side of ntdll — plain SysV/FreeBSD ABI, NOT MS-ABI.
 * The PE side (Win32 DLLs) keeps going through PcPs4's own MS-ABI bridge.
 */
#ifndef PCPS4_WINE_HOST_H
#define PCPS4_WINE_HOST_H

#include <stdint.h>
#include <stddef.h>
#include <sys/types.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------ *
 *  1. sysarch() + GS/FS-base selectors  (Windows TEB lives in GSBASE)
 * ------------------------------------------------------------------ *
 * Wine (signal_x86_64.c) sets the TEB with amd64_set_gsbase(teb), and
 * falls back to sysarch(X86_64_SET_GSBASE, &teb). OrbisOS exports
 * `sysarch`; the amd64_* convenience wrappers are libc-only and absent
 * from the firmware export set, so we define them here over sysarch,
 * exactly as FreeBSD libc does. */

/* FreeBSD <x86/sysarch.h> selector values (stable since FreeBSD 8). */
#ifndef I386_SET_LDT
#define I386_SET_LDT        1
#endif
#ifndef AMD64_GET_FSBASE
#define AMD64_GET_FSBASE    128   /* 0x80 */
#endif
#ifndef AMD64_SET_FSBASE
#define AMD64_SET_FSBASE    129   /* 0x81 */
#endif
#ifndef AMD64_GET_GSBASE
#define AMD64_GET_GSBASE    130   /* 0x82 */
#endif
#ifndef AMD64_SET_GSBASE
#define AMD64_SET_GSBASE    131   /* 0x83 */
#endif

/* Wine's spelling of the same selectors. */
#ifndef X86_64_GET_FSBASE
#define X86_64_GET_FSBASE   AMD64_GET_FSBASE
#endif
#ifndef X86_64_SET_FSBASE
#define X86_64_SET_FSBASE   AMD64_SET_FSBASE
#endif
#ifndef X86_64_GET_GSBASE
#define X86_64_GET_GSBASE   AMD64_GET_GSBASE
#endif
#ifndef X86_64_SET_GSBASE
#define X86_64_SET_GSBASE   AMD64_SET_GSBASE
#endif

/* Exported by libkernel; toolchain omits the prototype. */
int sysarch(int op, void *parms);

/* FreeBSD libc convenience wrappers, reimplemented over sysarch().
 * Semantics match FreeBSD libc: the AMD64_SET_* op takes a pointer to
 * the base value; AMD64_GET_* takes a pointer to a (void*) to fill. */
static inline int amd64_set_gsbase(void *base) { return sysarch(AMD64_SET_GSBASE, &base); }
static inline int amd64_set_fsbase(void *base) { return sysarch(AMD64_SET_FSBASE, &base); }
static inline int amd64_get_gsbase(void **base) { return sysarch(AMD64_GET_GSBASE, base); }
static inline int amd64_get_fsbase(void **base) { return sysarch(AMD64_GET_FSBASE, base); }

/* ------------------------------------------------------------------ *
 *  2. Thread id  (Wine thread.c:  thr_self(&lwpid) / getthreadid_np)
 * ------------------------------------------------------------------ */
/* pthread_getthreadid_np IS exported by OrbisOS (NID match); declare it. */
int pthread_getthreadid_np(void);

/* thr_self fills *id with the kernel LWP id. The raw thr_self export was
 * not found by NID scan, so route through the confirmed pthread wrapper —
 * same value (the LWP tid), no syscall-number dependency. */
static inline int thr_self(long *id)
{
    if (id) *id = (long)pthread_getthreadid_np();
    return 0;
}

/* ------------------------------------------------------------------ *
 *  3. _umtx_op  (low-level mutex/condvar primitive; exported)
 * ------------------------------------------------------------------ */
/* FreeBSD 9 <sys/umtx.h> op codes Wine's fast-sync path may use. */
#ifndef UMTX_OP_WAIT
#define UMTX_OP_WAIT            2
#define UMTX_OP_WAKE            3
#define UMTX_OP_WAIT_UINT       11
#define UMTX_OP_WAIT_UINT_PRIVATE 15
#define UMTX_OP_WAKE_PRIVATE    16
#endif

struct _umtx_time {          /* FreeBSD 9 layout */
    struct { int64_t tv_sec; long tv_nsec; } _timeout;
    uint32_t _flags;
    uint32_t _clockid;
};

/* Exported by libkernel; toolchain omits the prototype. */
int _umtx_op(void *obj, int op, unsigned long val, void *uaddr, void *uaddr2);

/* ------------------------------------------------------------------ *
 *  4. kqueue / kevent  (Wine's FreeBSD signal + wait loop; exported)
 * ------------------------------------------------------------------ */
#ifndef EVFILT_READ
#define EVFILT_READ     (-1)
#define EVFILT_WRITE    (-2)
#define EVFILT_SIGNAL   (-6)
#define EVFILT_TIMER    (-7)
#define EVFILT_USER     (-11)
#endif

#ifndef EV_ADD
#define EV_ADD      0x0001
#define EV_DELETE   0x0002
#define EV_ENABLE   0x0004
#define EV_DISABLE  0x0008
#define EV_ONESHOT  0x0010
#define EV_CLEAR    0x0020
#define EV_RECEIPT  0x0040
#define EV_DISPATCH 0x0080
#endif

/* FreeBSD 9 struct kevent (amd64). ident/udata are pointer-width. */
struct kevent {
    uintptr_t ident;
    short     filter;
    unsigned short flags;
    unsigned int   fflags;
    intptr_t  data;
    void     *udata;
};

struct timespec; /* from <time.h> at the call site */

/* All three exported by libkernel; toolchain omits the prototypes. */
int kqueue(void);
int kevent(int kq, const struct kevent *changelist, int nchanges,
           struct kevent *eventlist, int nevents,
           const struct timespec *timeout);

/* FreeBSD EV_SET macro (same as libc). */
#ifndef EV_SET
#define EV_SET(kevp, a, b, c, d, e, f) do {   \
    struct kevent *__kevp = (kevp);           \
    __kevp->ident  = (a);                     \
    __kevp->filter = (b);                     \
    __kevp->flags  = (c);                     \
    __kevp->fflags = (d);                     \
    __kevp->data   = (e);                     \
    __kevp->udata  = (f);                     \
} while (0)
#endif

/* ------------------------------------------------------------------ *
 *  5. Stage-1 self-test / init hook
 * ------------------------------------------------------------------ */
/* Round-trips GS-base set/get and reads the thread id, to prove the host
 * primitives actually work on-device before any Wine code depends on them.
 * Returns 0 on success; logs via PcPs4's runtime log. Safe to call once
 * at startup from the host (Unix) side. */
int pcps4_wine_host_selftest(void);

/*
 * Install `teb` as the current thread's Windows TEB using Wine's FreeBSD
 * mechanism (dlls/ntdll/unix/signal_x86_64.c, __FreeBSD__ branch):
 *     amd64_set_gsbase( teb );
 *     amd64_get_fsbase( &pthread_teb );
 * so that native `%gs:`-relative TEB reads resolve on-device. This is
 * ADDITIVE to PcPs4's existing instruction-patching TEB scheme (which
 * rewrites gs: loads to RIP-relative): patched images keep working, and
 * any unpatched Wine-origin code that expects a real GSBASE now also works.
 *
 * `teb` is PcPs4's existing fake-TEB page (pcps4_teb_init). On success the
 * host pthread's own fsbase is saved to *out_pthread_teb (may be NULL) so a
 * later Unix->Win->Unix transition can restore it, exactly as Wine does.
 * Runs the self-test once. Returns 0 on success, -1 on failure. */
int pcps4_wine_host_init(void *teb, void **out_pthread_teb);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_WINE_HOST_H */
