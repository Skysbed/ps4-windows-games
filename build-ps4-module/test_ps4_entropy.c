#include <assert.h>
#include <errno.h>
#include <string.h>
#include "ps4_entropy.c"

static int calls, fail, zero;
int sysctlbyname(const char *name, void *buf, size_t *len, const void *in, size_t inlen)
{
    (void)in; (void)inlen;
    assert(!strcmp(name, "kern.arandom"));
    calls++;
    if (fail) { errno = EIO; return -1; }
    if (zero) { *len = 0; return 0; }
    if (*len > 3) *len = 3;
    memset(buf, 0x5a, *len);
    return 0;
}
int main(void)
{
    unsigned char b[257];
    assert(ps4_getentropy(b, 0) == 0 && calls == 0);
    assert(ps4_getentropy(b, 7) == 0 && calls == 3);
    for (int i = 0; i < 7; i++) assert(b[i] == 0x5a);
    assert(ps4_getentropy(b, 257) == -1 && errno == EIO);
    zero = 1; assert(ps4_getentropy(b, 1) == -1 && errno == EIO);
    zero = 0; fail = 1; assert(ps4_getentropy(b, 1) == -1);
    return 0;
}
