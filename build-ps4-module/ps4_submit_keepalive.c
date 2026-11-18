/* GPU submit keepalive for the PS4.
 *
 * The system refuses to suspend (and so to close) an app that has submitted
 * GPU work without calling sceGnmSubmitDone afterwards ("Can't suspend pid.
 * submitDone() is not called").  The driver calls it once per presented
 * frame, so a game that is still loading - GPU work done, no frame shown yet -
 * could not be closed cleanly, which is how the console got hard-crashed.
 *
 * Every submit and every submitDone pass through the wrappers below (linker
 * --wrap).  A background thread calls sceGnmSubmitDone itself when work has
 * been submitted, no submitDone has followed for KEEPALIVE_IDLE_MS, and the
 * GPU has finished everything, holding the lock so no new submit can start
 * in between.  Enabled together with the per-frame call by ORBIS_SUBMIT_DONE. */
typedef unsigned int uint32_t;
typedef int int32_t;
typedef unsigned long uint64_t;
typedef _Bool bool;

#define KEEPALIVE_IDLE_MS 400
#define KEEPALIVE_POLL_US 100000

extern char *getenv(const char *);
extern int pthread_create(void *thread, const void *attr, void *(*fn)(void *), void *arg);
extern int32_t sceKernelUsleep(uint32_t usec);
extern uint64_t os_time_get_nano(void);
extern bool ac_orbis_wait_gpu_idle(uint64_t timeout_ns);
extern void mesa_log(int level, const char *tag, const char *format, ...);
extern int32_t sceGnmAreSubmitsAllowed(void);
extern int32_t __real_sceGnmSubmitDone(void);
extern int32_t __real_sceGnmSubmitCommandBuffers(uint32_t count, void **dcb, uint32_t *dcb_sizes,
                                                 void **ccb, uint32_t *ccb_sizes);

static int ka_lock_word;
static volatile uint64_t ka_pending;      /* submits since the last submitDone */
static volatile uint64_t ka_last_done_ns;
static int ka_started;                    /* 0 not yet, 1 running, 2 disabled */

static void ka_lock(void)
{
    while (__atomic_exchange_n(&ka_lock_word, 1, __ATOMIC_ACQUIRE))
        sceKernelUsleep(20);
}

static void ka_unlock(void)
{
    __atomic_store_n(&ka_lock_word, 0, __ATOMIC_RELEASE);
}


/* WinePS4's lesson (port/include/ps4_prio.h there): the PS4 runs an app's threads by priority alone, with
 * no turns among threads of one priority, so a helper every game thread depends on must sit ABOVE them
 * (700) or it waits behind busy workers. FreeBSD's SCHED_RR is 3 (the toolchain's header says 2). */
struct sched_param { int sched_priority; };
extern int pthread_setschedparam(void *thread, int policy, const struct sched_param *param);
extern void *pthread_self(void);
static void pcps4_raise_self( int prio )
{
    struct sched_param p;
    p.sched_priority = prio;
    pthread_setschedparam( pthread_self(), 3, &p );
}

static void *ka_thread(void *arg)
{
    pcps4_raise_self( 620 );
    uint64_t calls = 0, skipped = 0;
    (void)arg;
    mesa_log(2, "MESA", "pcps4: submit keepalive running (idle %d ms)", KEEPALIVE_IDLE_MS);
    for (;;)
    {
        sceKernelUsleep(KEEPALIVE_POLL_US);
        if (!ka_pending) continue;
        if (os_time_get_nano() - ka_last_done_ns < (uint64_t)KEEPALIVE_IDLE_MS * 1000000ull) continue;

        ka_lock();
        if (ka_pending && os_time_get_nano() - ka_last_done_ns >= (uint64_t)KEEPALIVE_IDLE_MS * 1000000ull)
        {
            /* Only when the GPU has finished: submitDone with work in flight is what stalled before. */
            if (ac_orbis_wait_gpu_idle(50ull * 1000000ull) && sceGnmAreSubmitsAllowed())
            {
                int32_t rc = __real_sceGnmSubmitDone();
                ka_pending = 0;
                ka_last_done_ns = os_time_get_nano();
                if (++calls <= 4 || calls % 64 == 0)
                    mesa_log(2, "MESA", "pcps4: keepalive submitDone #%lu -> 0x%08x", calls, (unsigned)rc);
            }
            else if (++skipped <= 4 || skipped % 64 == 0)
                mesa_log(2, "MESA", "pcps4: keepalive waiting - GPU still busy (%lu)", skipped);
        }
        ka_unlock();
    }
    return 0;
}

static void ka_start(void)
{
    unsigned long thread;
    if (__atomic_load_n(&ka_started, __ATOMIC_ACQUIRE)) return;
    if (__atomic_exchange_n(&ka_started, 2, __ATOMIC_ACQ_REL)) return;
    if (!getenv("ORBIS_SUBMIT_DONE")) return;
    ka_last_done_ns = os_time_get_nano();
    if (pthread_create(&thread, 0, ka_thread, 0) == 0)
        __atomic_store_n(&ka_started, 1, __ATOMIC_RELEASE);
    else
        mesa_log(1, "MESA", "pcps4: could not start the submit keepalive");
}

int32_t __wrap_sceGnmSubmitCommandBuffers(uint32_t count, void **dcb, uint32_t *dcb_sizes,
                                          void **ccb, uint32_t *ccb_sizes)
{
    int32_t rc;
    ka_start();
    ka_lock();
    rc = __real_sceGnmSubmitCommandBuffers(count, dcb, dcb_sizes, ccb, ccb_sizes);
    if (rc == 0) ka_pending++;
    ka_unlock();
    return rc;
}

int32_t __wrap_sceGnmSubmitDone(void)
{
    int32_t rc;
    ka_lock();
    rc = __real_sceGnmSubmitDone();
    ka_pending = 0;
    ka_last_done_ns = os_time_get_nano();
    ka_unlock();
    return rc;
}
