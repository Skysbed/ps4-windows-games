#ifndef PCPS4_COMPAT_H
#define PCPS4_COMPAT_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

static inline int pcps4_clamp_int(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    if (value > maximum) return maximum;
    return value;
}

#ifdef __cplusplus
}
#endif

#endif
