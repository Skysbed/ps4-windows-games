/*
 * PS4 stand-in for thread-local storage in a module with several TLS variables.
 *
 * The console refuses to load a module that carries a PT_TLS segment, so the
 * build strips it.  A module's TLS accesses still arrive here as
 * __tls_get_addr({module, offset}); with -Bsymbolic the offsets are fixed at
 * link time.  The single-slot shim used for ntdll returns one address for
 * everything, which is only safe for a module with a single TLS variable: in a
 * driver with several, they overwrite each other.
 *
 * This gives each thread its own zero-filled block (all of this module's TLS is
 * .tbss) and returns block + offset.  It deliberately avoids malloc and pthread
 * keys, either of which could re-enter TLS: blocks come from a static pool and
 * are found by thread id.
 */
#include <stddef.h>
#include <pthread.h>

#define PS4_TLS_BLOCK   0x2000      /* >= the module's PT_TLS memsz (0x1138 for RADV) */
#define PS4_TLS_THREADS 64

typedef struct { unsigned long ti_module, ti_offset; } ps4_tls_index;

static struct { pthread_t owner; volatile int used; } ps4_tls_slot[PS4_TLS_THREADS];
static char ps4_tls_pool[PS4_TLS_THREADS][PS4_TLS_BLOCK] __attribute__((aligned(64)));
static struct { void *cat[16]; } ps4_c_locale;      /* all-zero: the C locale */

extern __thread void *__musl_current_locale __attribute__((weak));

static char *ps4_tls_block(void)
{
    pthread_t self = pthread_self();
    int i;

    for (i = 0; i < PS4_TLS_THREADS; i++)
        if (ps4_tls_slot[i].used == 2 && pthread_equal( ps4_tls_slot[i].owner, self ))
            return ps4_tls_pool[i];
    for (i = 0; i < PS4_TLS_THREADS; i++)
        if (__sync_bool_compare_and_swap( &ps4_tls_slot[i].used, 0, 1 ))
        {
            ps4_tls_slot[i].owner = self;
            __sync_synchronize();
            ps4_tls_slot[i].used = 2;
            /* the block is live now, so this access resolves to it */
            if (&__musl_current_locale && !__musl_current_locale) __musl_current_locale = &ps4_c_locale;
            return ps4_tls_pool[i];
        }
    return ps4_tls_pool[0];     /* pool exhausted: degrade to sharing rather than crash */
}

void *__tls_get_addr( ps4_tls_index *index )
{
    unsigned long offset = index ? index->ti_offset : 0;
    if (offset >= PS4_TLS_BLOCK) offset = 0;
    return ps4_tls_block() + offset;
}
