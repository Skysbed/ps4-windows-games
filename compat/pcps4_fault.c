#define _POSIX_C_SOURCE 200809L

#include "pcps4_fault.h"
#include "pcps4_version.h"

#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>



static volatile sig_atomic_t pcps4_fault_seen = 0;

/*
 * The last stage the app announced. Updated by a single pointer store, so it
 * is safe to set from anywhere - including inside a running Windows PE's call
 * chain - and safe to read from the signal handler.
 */
static const char *volatile pcps4_stage = "startup";

static int pcps4_trace_fd = -2; /* -2 = not opened yet, -1 = unavailable */

static void fd_write_str(int fd, const char *s);

void pcps4_fault_stage(const char *str)
{
    if (str == NULL)
        return;
    pcps4_stage = str;

    /*
     * Also append each stage to a trace file with a direct write(): it hits
     * the filesystem immediately, so even a hard kill (no signal delivered)
     * leaves the last stage reached.
     */
    if (pcps4_trace_fd == -2)
        pcps4_trace_fd = open(PCPS4_TRACE_PATH,
                              O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (pcps4_trace_fd >= 0) {
        fd_write_str(pcps4_trace_fd, str);
        fd_write_str(pcps4_trace_fd, "\n");
    }
}

/* Async-signal-safe helpers: no stdio, no allocation. */
static void fd_write_str(int fd, const char *s)
{
    size_t n = 0;
    if (s == NULL)
        s = "(null)";
    while (s[n] != '\0')
        ++n;
    (void)write(fd, s, n);
}

static void fd_write_uint(int fd, unsigned long value)
{
    char buf[20];
    size_t i = sizeof(buf);
    buf[--i] = '\0';
    do {
        buf[--i] = (char)('0' + (value % 10u));
        value /= 10u;
    } while (value != 0 && i > 0);
    fd_write_str(fd, &buf[i]);
}

static void fd_write_hex(int fd, uintptr_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[2 + sizeof(uintptr_t) * 2 + 1];
    size_t pos = sizeof(buf);

    buf[--pos] = '\0';
    do {
        buf[--pos] = digits[value & 0xfu];
        value >>= 4;
    } while (value != 0 && pos > 2);

    buf[--pos] = 'x';
    buf[--pos] = '0';
    fd_write_str(fd, &buf[pos]);
}

static void emit_report(
    int fd,
    int signal_number,
    uintptr_t fault_address)
{
    fd_write_str(fd, "PcPs4: fatal fault signal=");
    fd_write_uint(fd, (unsigned long)signal_number);
    fd_write_str(fd, " address=");
    fd_write_hex(fd, fault_address);
    fd_write_str(fd, " stage=");
    fd_write_str(fd, pcps4_stage);
    fd_write_str(fd, "\n");
}

/* FreeBSD amd64 ucontext: 16-byte sigmask, then mcontext as 8-byte slots. */
static void emit_context(int fd, void *uc)
{
    const uint64_t *mc;
    const uint64_t *sp;
    int i;
    static const struct { const char *name; int slot; } regs[] = {
        {"rip", 20}, {"rsp", 23}, {"rbp", 9}, {"rax", 7}, {"rbx", 8}, {"rcx", 4}, {"rdx", 3},
        {"rsi", 2}, {"rdi", 1}, {"r8", 5}, {"r9", 6}, {"r10", 10}, {"r11", 11}, {"r12", 12},
        {"r13", 13}, {"r14", 14}, {"r15", 15}, {"addr", 17}
    };

    if (uc == NULL)
        return;
    mc = (const uint64_t *)((const char *)uc + 16);
    sp = (const uint64_t *)(uintptr_t)mc[23];
    /* Stack first: [rsp] is the return address of a call through a bad pointer. */
    for (i = 0; i < 24; ++i) {
        fd_write_str(fd, "stack+");
        fd_write_uint(fd, (unsigned long)(i * 8));
        fd_write_str(fd, "=");
        fd_write_hex(fd, (uintptr_t)sp[i]);
        fd_write_str(fd, "\n");
        (void)fsync(fd);
    }
    for (i = 0; i < (int)(sizeof(regs) / sizeof(regs[0])); ++i) {
        fd_write_str(fd, regs[i].name);
        fd_write_str(fd, "=");
        fd_write_hex(fd, (uintptr_t)mc[regs[i].slot]);
        fd_write_str(fd, "\n");
    }
}

static void pcps4_fault_handler(int signal_number, void *info, void *uc)
{
    int fd;
    (void)info;

    if (pcps4_fault_seen)
        _exit(128 + signal_number);
    pcps4_fault_seen = 1;

    /* Toolchain signal handlers use the legacy one-argument ABI. */
    emit_report(STDERR_FILENO, signal_number, 0);

    fd = open(PCPS4_CRASH_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        emit_report(fd, signal_number, 0);
        emit_context(fd, uc);
        (void)close(fd);
    }

    _exit(128 + signal_number);
}

int pcps4_fault_install(void)
{
    static const int signals[] = {
        SIGSEGV,
        SIGBUS,
        SIGILL,
        SIGFPE,
        SIGABRT
    };
    struct sigaction action;
    size_t i;

#if defined(__FreeBSD__)
    /* The toolchain's <signal.h> names the union member __sa_sigaction and omits SA_SIGINFO. */
    action.__sa_handler.__sa_sigaction = (void (*)(int, struct __siginfo *, void *))pcps4_fault_handler;
#else
    action.sa_handler = (void (*)(int))pcps4_fault_handler;
#endif
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0x40; /* SA_SIGINFO */

    for (i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &action, NULL) != 0) {
            return -1;
        }
    }

    return 0;
}

/*
 * Run before main so an application does not need to remember to call the
 * safety layer.
 */
__attribute__((constructor))
static void pcps4_fault_constructor(void)
{
    (void)pcps4_fault_install();
}
