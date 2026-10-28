#ifndef _PCPS4_SYS_EVENT_H
#define _PCPS4_SYS_EVENT_H
#include <orbis/_types/kernel.h>
#ifndef EV_SET
#define EV_SET(kevp,a,b,c,d,e,f) do { struct kevent *__k=(kevp); \
  __k->ident=(a);__k->filter=(b);__k->flags=(c);__k->fflags=(d);__k->data=(e);__k->udata=(f);} while(0)
#endif
#ifndef NOTE_TRIGGER
#define NOTE_TRIGGER 0x01000000
#endif
struct timespec;
#ifdef __cplusplus
extern "C" {
#endif
int kqueue(void);
int kevent(int, const struct kevent *, int, struct kevent *, int, const struct timespec *);
#ifdef __cplusplus
}
#endif
#endif
