#ifndef PCPS4_COMPAT_H
#define PCPS4_COMPAT_H

/*
 * Win32/MSVC compatibility surface used by the PcPs4 source translator.
 * These wrappers intentionally use portable C/POSIX primitives.
 */

#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>
#include <time.h>
#include <unistd.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline void pcps4_sleep_ms(uint32_t milliseconds)
{
    struct timespec request;
    request.tv_sec = (time_t)(milliseconds / 1000u);
    request.tv_nsec = (long)((milliseconds % 1000u) * 1000000u);
    (void)nanosleep(&request, 0);
}

static inline uint64_t pcps4_monotonic_ms(void)
{
    struct timespec now;
    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0) {
        return 0;
    }
    return (uint64_t)now.tv_sec * 1000u + (uint64_t)now.tv_nsec / 1000000u;
}

static inline uint32_t pcps4_get_current_process_id(void)
{
    return (uint32_t)getpid();
}

static inline int32_t pcps4_interlocked_increment(volatile int32_t *value)
{
    return __atomic_add_fetch(value, 1, __ATOMIC_SEQ_CST);
}

static inline int32_t pcps4_interlocked_decrement(volatile int32_t *value)
{
    return __atomic_sub_fetch(value, 1, __ATOMIC_SEQ_CST);
}

#define ZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define RtlZeroMemory(Destination, Length) memset((Destination), 0, (Length))
#define CopyMemory(Destination, Source, Length) memcpy((Destination), (Source), (Length))
#define MoveMemory(Destination, Source, Length) memmove((Destination), (Source), (Length))

#ifdef __cplusplus
}
#endif

#endif
