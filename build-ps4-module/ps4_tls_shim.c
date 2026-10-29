/* PS4 PRX modules cannot carry TLS, so the loader rejects a PT_TLS segment and
 * we strip it. ntdll's only thread-local is musl's __musl_current_locale, read
 * through __tls_get_addr (general-dynamic model). Provide a process-global
 * replacement: a single C locale (a zeroed __locale_struct; musl treats a NULL
 * category map as the built-in C locale). Single-locale is correct for a game. */
#include <stddef.h>
static struct { void *cat[16]; } ps4_c_locale;      /* all-zero => C locale */
static void *ps4_locale_slot = &ps4_c_locale;        /* the locale_t value */
void *__tls_get_addr(void *index)
{
    (void)index;                                     /* ntdll has exactly one TLS datum */
    return &ps4_locale_slot;
}
