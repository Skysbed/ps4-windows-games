#ifndef _PCPS4_SYS_THR_H
#define _PCPS4_SYS_THR_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
int thr_self(long *);
int thr_kill(long, int);
int thr_set_name(long, const char *);
#ifdef __cplusplus
}
#endif
#endif
