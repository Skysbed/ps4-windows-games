/* Exhaustive check of ps4_fastmem.c against simple reference loops, with guard pages on both sides of every buffer.
 * Any read or write outside the given range faults. */
#define _GNU_SOURCE
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <unistd.h>

#define FASTMEM_API
#define memcmp f_memcmp
#define bcmp f_bcmp
#define memcpy f_memcpy
#define memmove f_memmove
#define memset f_memset
#include "ps4_fastmem.c"
#undef memcmp
#undef bcmp
#undef memcpy
#undef memmove
#undef memset

#define MAXN 300
#define MAXOFF 33

static unsigned long long checks;
static uint32_t rng_state = 0x12345678;
static uint8_t rnd( void ) { rng_state ^= rng_state << 13; rng_state ^= rng_state >> 17; rng_state ^= rng_state << 5; return (uint8_t)rng_state; }

static void fail( const char *what, size_t n, size_t x, size_t y ) { printf( "FAIL %s n=%zu a=%zu b=%zu\n", what, n, x, y ); exit( 1 ); }

/* a buffer of `size` bytes ending exactly at an inaccessible page, with one before it too */
static uint8_t *guarded( size_t size, size_t *map_size )
{
    const size_t page = (size_t)sysconf( _SC_PAGESIZE );
    const size_t body = (size + page - 1) & ~(page - 1);
    uint8_t *m = mmap( NULL, body + 2 * page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0 );
    if (m == MAP_FAILED || mprotect( m + page, body, PROT_READ | PROT_WRITE )) { puts( "mmap failed" ); exit( 2 ); }
    *map_size = body;
    return m + page + body;   /* one past the end of the writable area */
}

static int sign( int v ) { return (v > 0) - (v < 0); }
static int ref_cmp( const uint8_t *a, const uint8_t *b, size_t n ) { for (size_t i = 0; i < n; i++) if (a[i] != b[i]) return (int)a[i] - (int)b[i]; return 0; }

int main( void )
{
    size_t ms, ms_o;   /* (the bug was here: one size variable shared by buffers of different sizes) */
    uint8_t *end_a = guarded( 4096, &ms ), *end_b = guarded( 4096, &ms ), *end_o = guarded( 8192, &ms_o );
    static uint8_t ref[8192];

    /* memcpy and memset, touching the guard page on each side */
    for (size_t n = 0; n <= MAXN; n++)
        for (size_t x = 0; x < MAXOFF; x++)
            for (size_t y = 0; y < MAXOFF; y++)
            {
                uint8_t *src = end_a - n - x, *dst = end_b - n - y;     /* the ranges end x and y before the guard */
                uint8_t *low = end_b - MAXN - 2 * MAXOFF;
                size_t span = (size_t)(end_b - low);
                for (size_t i = 0; i < n; i++) src[i] = rnd();
                for (size_t i = 0; i < span; i++) low[i] = 0xA5;
                if (f_memcpy( dst, src, n ) != dst) fail( "memcpy return", n, x, y );
                for (uint8_t *p = low; p < end_b; p++)
                {
                    const int in = p >= dst && p < dst + n;
                    if (*p != (in ? src[p - dst] : 0xA5)) fail( "memcpy", n, x, y );
                }
                checks++;
            }
    /* exactly at both guard pages */
    for (size_t n = 1; n <= MAXN; n++)
    {
        uint8_t *src = end_a - n, *dst = end_b - n;
        for (size_t i = 0; i < n; i++) src[i] = rnd();
        f_memcpy( dst, src, n );
        if (ref_cmp( dst, src, n )) fail( "memcpy at guard", n, 0, 0 );
        if (f_memcmp( dst, src, n ) || f_bcmp( dst, src, n )) fail( "memcmp at guard", n, 0, 0 );
        f_memset( dst, 0x5C, n );
        for (size_t i = 0; i < n; i++) if (dst[i] != 0x5C) fail( "memset at guard", n, i, 0 );
        checks += 3;
    }
    /* buffers that START right after a guard page */
    {
        uint8_t *la = end_a - ms, *lb = end_b - ms;
        for (size_t n = 1; n <= MAXN; n++)
        {
            for (size_t i = 0; i < n; i++) la[i] = rnd();
            f_memcpy( lb, la, n );
            if (f_memcmp( la, lb, n )) fail( "memcmp at low guard", n, 0, 0 );
            f_memset( lb, 7, n );
            for (size_t i = 0; i < n; i++) if (lb[i] != 7) fail( "memset at low guard", n, i, 0 );
            f_memmove( lb, la, n );
            if (ref_cmp( la, lb, n )) fail( "memmove at low guard", n, 0, 0 );
            checks += 3;
        }
    }

    /* memset */
    for (size_t n = 0; n <= MAXN; n++)
        for (size_t x = 0; x < MAXOFF; x++)
        {
            uint8_t *low = end_b - MAXN - 2 * MAXOFF, *dst = end_b - n - x;
            const int v = rnd();
            for (uint8_t *p = low; p < end_b; p++) *p = (uint8_t)~v;
            if (f_memset( dst, v | 0x100, n ) != dst) fail( "memset return", n, x, 0 );
            for (uint8_t *p = low; p < end_b; p++)
                if (*p != ((p >= dst && p < dst + n) ? (uint8_t)v : (uint8_t)~v)) fail( "memset", n, x, 0 );
            checks++;
        }

    /* memcmp: equal, and one byte different at every position, both directions */
    for (size_t n = 0; n <= 130; n++)
        for (size_t x = 0; x < 18; x++)
            for (size_t y = 0; y < 18; y++)
            {
                uint8_t *a = end_a - n - x, *b = end_b - n - y;
                for (size_t i = 0; i < n; i++) a[i] = b[i] = rnd();
                if (f_memcmp( a, b, n ) || f_bcmp( a, b, n )) fail( "memcmp equal", n, x, y );
                for (size_t k = 0; k < n; k++)
                {
                    const uint8_t keep = b[k];
                    b[k] = (uint8_t)(keep + 1 + (rnd() % 255));
                    if (sign( f_memcmp( a, b, n ) ) != sign( ref_cmp( a, b, n ) )) fail( "memcmp sign", n, k, y );
                    if (f_memcmp( a, b, n ) != ref_cmp( a, b, n )) fail( "memcmp value", n, k, y );
                    if (!f_bcmp( a, b, n )) fail( "bcmp", n, k, y );
                    if (sign( f_memcmp( b, a, n ) ) != -sign( ref_cmp( a, b, n ) )) fail( "memcmp reversed", n, k, y );
                    b[k] = keep;
                    checks += 4;
                }
            }

    /* memmove with every overlap, in both directions */
    for (size_t n = 0; n <= 200; n++)
        for (size_t x = 0; x <= 64; x++)
            for (int dir = 0; dir < 2; dir++)
            {
                uint8_t *base = end_o - 600;
                uint8_t *src = base + 128 + (dir ? x : 0), *dst = base + 128 + (dir ? 0 : x);
                for (size_t i = 0; i < 600; i++) ref[i] = base[i] = rnd();
                for (size_t i = 0; i < n; i++) ref[(dst - base) + i] = base[(src - base) + i];
                if (f_memmove( dst, src, n ) != dst) fail( "memmove return", n, x, (size_t)dir );
                if (ref_cmp( base, ref, 600 )) fail( "memmove", n, x, (size_t)dir );
                checks++;
            }
    printf( "PASS %llu checks\n", checks );
    return 0;
}
