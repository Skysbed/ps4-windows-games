#define _POSIX_C_SOURCE 200809L

#include "pcps4_fault.h"

#include <signal.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <unistd.h>

static volatile sig_atomic_t pcps4_fault_seen = 0;

static char signal_digit(int signal_number)
{
    if (signal_number >= 0 && signal_number <= 9) {
        return (char)('0' + signal_number);
    }
    return '?';
}

static void pcps4_fault_handler(int signal_number)
{
    static const char prefix[] =
        "PcPs4: fatal user-space fault (signal ";
    static const char suffix[] =
        "); terminating the app safely.\n";
    char digit = signal_digit(signal_number);

    if (pcps4_fault_seen) {
        _exit(128 + signal_number);
    }

    pcps4_fault_seen = 1;

    (void)write(STDERR_FILENO, prefix, sizeof(prefix) - 1);
    (void)write(STDERR_FILENO, &digit, 1);
    (void)write(STDERR_FILENO, suffix, sizeof(suffix) - 1);

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

    action.sa_handler = pcps4_fault_handler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

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
