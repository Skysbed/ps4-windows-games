#ifndef _PCPS4_PTHREAD_NP_H
#define _PCPS4_PTHREAD_NP_H
#include <pthread.h>
#ifdef __cplusplus
extern "C" {
#endif
int pthread_getthreadid_np(void);
int pthread_set_name_np(pthread_t, const char *);
#ifdef __cplusplus
}
#endif
#endif
