/* Standalone getentropy-compatible adapter. Link explicitly; do not use the
 * orbis-compat sysctl stub, which cannot provide kernel randomness. */
#include <sys/types.h>
#include <errno.h>
#include <stddef.h>

/* orbis-compat's sys/sysctl.h defines an inline failure stub.  The PS4
 * libkernel stub exports the actual function; declare it without that header. */
extern int sysctlbyname(const char *, void *, size_t *, const void *, size_t);

int ps4_getentropy(void *buffer, size_t length)
{
    unsigned char *out = buffer;
    size_t remaining = length;

    if (length > 256) {
        errno = EIO;
        return -1;
    }
    while (remaining != 0) {
        size_t received = remaining;
        if (sysctlbyname("kern.arandom", out, &received, NULL, 0) != 0)
            return -1;
        if (received == 0 || received > remaining) {
            errno = EIO;
            return -1;
        }
        out += received;
        remaining -= received;
    }
    return 0;
}

/* Link GnuTLS with --wrap=getentropy to prevent the musl syscall stub from
 * supplying entropy.  This adapter can also be tested independently. */
int __wrap_getentropy(void *buffer, size_t length)
{
    return ps4_getentropy(buffer, length);
}
