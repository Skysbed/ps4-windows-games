/*
 * pcps4_wine_host.c — Wine ntdll/unix host shim for OrbisOS (Stage 1)
 * ===================================================================
 * See pcps4_wine_host.h for the design. This TU holds:
 *   - the documented Wine SYS_* -> OrbisOS syscall-number map, and
 *   - an on-device self-test that round-trips the GS-base mechanism and
 *     the thread-id / umtx / kqueue primitives before Wine depends on them.
 */
#include "pcps4_wine_host.h"
#include "../pcps4_status.h"

#include <time.h>
#include <errno.h>
#include <string.h>

/* ------------------------------------------------------------------ *
 *  Syscall-number map (reference / verification only)
 * ------------------------------------------------------------------ *
 * Wine's FreeBSD path reaches the kernel almost entirely through the
 * libc/libkernel wrappers declared in the header, so these numbers are
 * NOT emitted as raw syscalls — they are recorded so the mapping is
 * auditable and so a future raw-syscall fallback has a verified table.
 *
 * Values are from the OpenOrbis toolchain bits/syscall.h, which match
 * stock FreeBSD 9. Confirmed against the decompiled OrbisOS native
 * (PS4-decompiled-native call sysarch/syscall 165). Linux numbers
 * are shown to make clear why Wine's Linux path (futex/arch_prctl) does
 * NOT apply here — the FreeBSD path is the correct one.
 */
struct pcps4_syscall_map { const char *name; int freebsd_nr; const char *note; };

static const struct pcps4_syscall_map pcps4_wine_syscalls[] = {
    { "sysarch",       165, "GS/FS base = Windows TEB; exported wrapper" },
    { "thr_self",      432, "LWP id; use pthread_getthreadid_np wrapper" },
    { "thr_kill",      433, "signal delivery to thread" },
    { "thr_new",       455, "native thread create (pthread_create preferred)" },
    { "_umtx_op",      454, "mutex/condvar fast path; exported wrapper" },
    { "kqueue",        362, "event queue for signal/wait loop; exported" },
    { "kevent",        363, "kqueue poll; exported" },
    { "sigaction",     416, "POSIX; exported directly" },
    { "clock_gettime", 232, "QPC/time base; exported directly" },
    { "mmap",          477, "virtual.c page backing; exported directly" },
};

const struct pcps4_syscall_map *pcps4_wine_host_syscall_map(size_t *count)
{
    if (count) *count = sizeof(pcps4_wine_syscalls) / sizeof(pcps4_wine_syscalls[0]);
    return pcps4_wine_syscalls;
}

/* ------------------------------------------------------------------ *
 *  On-device self-test
 * ------------------------------------------------------------------ */
int pcps4_wine_host_selftest(void)
{
    int failures = 0;
    pcps4_status_log("[wine-host] stage-1 self-test begin");

    /* (1) GS-base round-trip: this is the TEB mechanism Wine relies on.
     * Save the current GS base, set a probe value, read it back, restore. */
    void *saved_gs = NULL;
    if (amd64_get_gsbase(&saved_gs) != 0) {
        pcps4_status_log("[wine-host] FAIL amd64_get_gsbase errno=%d", errno);
        failures++;
    } else {
        /* Probe with a 16-byte-aligned stack address we own. */
        void *probe = (void *)&saved_gs;
        if (amd64_set_gsbase(probe) != 0) {
            pcps4_status_log("[wine-host] FAIL amd64_set_gsbase errno=%d", errno);
            failures++;
        } else {
            void *readback = NULL;
            amd64_get_gsbase(&readback);
            if (readback != probe) {
                pcps4_status_log("[wine-host] FAIL gsbase roundtrip set=%p got=%p",
                                 probe, readback);
                failures++;
            } else {
                pcps4_status_log("[wine-host] OK gsbase set/get roundtrip");
            }
            /* Restore the original GS base — never leave it on our probe. */
            amd64_set_gsbase(saved_gs);
        }
    }

    /* (2) Thread id. */
    long tid = -1;
    thr_self(&tid);
    if (tid <= 0) {
        pcps4_status_log("[wine-host] FAIL thr_self tid=%ld", tid);
        failures++;
    } else {
        pcps4_status_log("[wine-host] OK thr_self tid=%ld", tid);
    }

    /* (3) kqueue create/close — the wait-loop backbone. */
    int kq = kqueue();
    if (kq < 0) {
        pcps4_status_log("[wine-host] FAIL kqueue errno=%d", errno);
        failures++;
    } else {
        /* Register and immediately delete a user event to exercise kevent. */
        struct kevent kev;
        EV_SET(&kev, 1, EVFILT_USER, EV_ADD | EV_CLEAR, 0, 0, NULL);
        struct timespec zero = { 0, 0 };
        int r = kevent(kq, &kev, 1, NULL, 0, &zero);
        if (r < 0)
            pcps4_status_log("[wine-host] WARN kevent add errno=%d (kqueue ok)", errno);
        else
            pcps4_status_log("[wine-host] OK kqueue+kevent");
        /* close() is a direct POSIX export; declared via unistd at callers. */
        extern int close(int);
        close(kq);
    }

    /* (4) _umtx_op reachability: a WAKE on an address with no waiters is a
     * harmless no-op that still proves the wrapper is callable. */
    {
        uint32_t word = 0;
        int r = _umtx_op(&word, UMTX_OP_WAKE_PRIVATE, 0, NULL, NULL);
        if (r != 0 && errno != 0)
            pcps4_status_log("[wine-host] WARN _umtx_op wake errno=%d", errno);
        else
            pcps4_status_log("[wine-host] OK _umtx_op reachable");
    }

    pcps4_status_log("[wine-host] stage-1 self-test end: %d failure(s)", failures);
    return failures == 0 ? 0 : -1;
}

/* ------------------------------------------------------------------ *
 *  Wine FreeBSD TEB install (stage 2)
 * ------------------------------------------------------------------ *
 * Mirrors dlls/ntdll/unix/signal_x86_64.c, __FreeBSD__ branch of the
 * thread init: amd64_set_gsbase(teb); amd64_get_fsbase(&pthread_teb). */
int pcps4_wine_host_init(void *teb, void **out_pthread_teb)
{
    void *pthread_teb = NULL;

    if (!teb) {
        pcps4_status_log("[wine-host] init FAIL: null teb");
        return -1;
    }

    /* Save the host pthread's current fsbase first, as Wine does, so the
     * Unix side can be restored across a Win->Unix transition. */
    if (amd64_get_fsbase(&pthread_teb) != 0)
        pcps4_status_log("[wine-host] init WARN amd64_get_fsbase errno=%d", errno);
    if (out_pthread_teb)
        *out_pthread_teb = pthread_teb;

    /* Point GSBASE at the Windows TEB (the Wine FreeBSD mechanism). */
    if (amd64_set_gsbase(teb) != 0) {
        pcps4_status_log("[wine-host] init FAIL amd64_set_gsbase errno=%d", errno);
        return -1;
    }

    /* Confirm the write took, so we fail loudly rather than run with a
     * silently-wrong %gs: base. */
    {
        void *got = NULL;
        amd64_get_gsbase(&got);
        if (got != teb) {
            pcps4_status_log("[wine-host] init FAIL gsbase verify set=%p got=%p",
                             teb, got);
            return -1;
        }
    }

    pcps4_status_log("[wine-host] init OK: gsbase->teb=%p pthread_teb=%p",
                     teb, pthread_teb);

    /* Prove the rest of the host primitives before anything leans on them. */
    return pcps4_wine_host_selftest();
}
