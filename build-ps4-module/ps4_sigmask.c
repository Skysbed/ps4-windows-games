#include <errno.h>
#include <signal.h>

/* The ntdll objects use OpenOrbis headers. libkernel uses FreeBSD how values. */
_Static_assert(SIG_BLOCK == 0 && SIG_UNBLOCK == 1 && SIG_SETMASK == 2,
               "recheck the ntdll signal mask ABI");

extern int __real_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset);

__attribute__((visibility("hidden")))
int __wrap_pthread_sigmask(int how, const sigset_t *set, sigset_t *oldset)
{
    /* A query ignores how. Give libkernel a valid operation in that case. */
    if (!set) how = SIG_BLOCK;
    if ((unsigned int)how > SIG_SETMASK) return EINVAL;
    return __real_pthread_sigmask(how + 1, set, oldset);
}
