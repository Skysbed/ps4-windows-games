#ifndef PCPS4_FAULT_H
#define PCPS4_FAULT_H

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Installs the PcPs4 user-space fault boundary.
 *
 * Fatal process faults are intentionally not recovered from. Continuing
 * after stack/memory corruption is less safe than terminating the app.
 */
int pcps4_fault_install(void);
void pcps4_fault_thread_init(void);

/*
 * Mark the calling thread as a (non-main) worker. A worker that trips SIGSYS
 * (a syscall the sandbox blocks, e.g. exit() on an error path) is ended on its
 * own via the permitted thread-exit so the process keeps running, instead of
 * crashing the whole app. The PE's main thread must NOT be marked.
 */
void pcps4_fault_mark_worker(void);

/*
 * Record the stage the app is currently executing. This is a single pointer
 * store (async-signal-safe and safe to call from inside a running PE's call
 * chain - no stdio, no allocation), and `str` must point to a string literal
 * with static lifetime. If the app then faults, the handler writes this label
 * - plus the signal and faulting address - to /data/pcps4_crash.txt, so a
 * crash can be pinpointed from the console without a PC attached.
 */
void pcps4_fault_stage(const char *str);

#ifdef __cplusplus
}
#endif

#endif
