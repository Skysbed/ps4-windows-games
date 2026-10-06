#define _POSIX_C_SOURCE 200809L

#include "pcps4_fault.h"

#include <fcntl.h>
#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

#define PCPS4_CRASH_PATH "/data/pcps4_crash.txt"

static volatile sig_atomic_t pcps4_fault_seen = 0;

/*
 * The last stage the app announced. Updated by a single pointer store, so it
 * is safe to set from anywhere - including inside a running Windows PE's call
 * chain - and safe to read from the signal handler.
 */
static const char *volatile pcps4_stage = "startup";

void pcps4_fault_stage(const char *str)
{
    if (str != NULL)
        pcps4_stage = str;
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

static void fd_write_hex(int fd, uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    char buf[19];
    size_t i = sizeof(buf);
    buf[--i] = '\0';
    do {
        buf[--i] = digits[value & 0xFu];
        value >>= 4;
    } while (value != 0 && i > 0);
    (void)write(fd, "0x", 2);
    fd_write_str(fd, &buf[i]);
}

static void emit_report(int fd, int signal_number, const void *fault_addr)
{
    fd_write_str(fd, "PcPs4: fatal fault signal=");
    fd_write_uint(fd, (unsigned long)signal_number);
    fd_write_str(fd, " addr=");
    fd_write_hex(fd, (uint64_t)(uintptr_t)fault_addr);
    fd_write_str(fd, " stage=");
    fd_write_str(fd, pcps4_stage);
    fd_write_str(fd, "\n");
}

static void pcps4_fault_handler(int signal_number, siginfo_t *info, void *ucontext)
{
    const void *fault_addr = info != NULL ? info->si_addr : NULL;
    int fd;

    (void)ucontext;

    if (pcps4_fault_seen)
        _exit(128 + signal_number);
    pcps4_fault_seen = 1;

    /* klog (captured over a PC) and a file on the console (captured over FTP). */
    emit_report(STDERR_FILENO, signal_number, fault_addr);

    fd = open(PCPS4_CRASH_PATH, O_WRONLY | O_CREAT | O_TRUNC, 0666);
    if (fd >= 0) {
        emit_report(fd, signal_number, fault_addr);
        (void)close(fd);
    }

    /*
     * Do not attempt to resume execution after SIGSEGV/SIGBUS/SIGILL etc.
     * The damaged state is not trustworthy.
     */
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

    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO;
    action.sa_sigaction = pcps4_fault_handler;

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
