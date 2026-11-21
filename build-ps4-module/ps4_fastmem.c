/* SPDX-License-Identifier: MIT
 *
 * SSE2 memcmp, memcpy, memmove and memset for libvulkan.prx.
 *
 * The toolchain's libc.a has musl's generic C versions: memcmp compares one byte at a time and memcpy copies words.
 * On the PS4's Jaguar cores that made radv_bind_dynamic_state, which is nothing but comparisons, 45% of the cost of
 * a pipeline switch. These never read or write outside the ranges they are given.
 *
 * Build with -fno-builtin, or the compiler may turn these loops into calls to themselves.
 */
#include <emmintrin.h>
#include <stddef.h>
#include <stdint.h>

#ifndef FASTMEM_API
#define FASTMEM_API __attribute__((visibility("hidden")))
#endif

typedef uint64_t __attribute__((aligned(1), may_alias)) u64u;
typedef uint32_t __attribute__((aligned(1), may_alias)) u32u;
typedef uint16_t __attribute__((aligned(1), may_alias)) u16u;

static inline __m128i load16( const void *p ) { return _mm_loadu_si128( (const __m128i *)p ); }
static inline void store16( void *p, __m128i v ) { _mm_storeu_si128( (__m128i *)p, v ); }

/* the first differing byte of two 16-byte blocks known to differ */
static inline int diff16( const unsigned char *a, const unsigned char *b, unsigned equal_mask )
{
    const unsigned i = (unsigned)__builtin_ctz( ~equal_mask );
    return (int)a[i] - (int)b[i];
}

FASTMEM_API int memcmp( const void *a_, const void *b_, size_t n )
{
    const unsigned char *a = a_, *b = b_;

    if (n >= 16)
    {
        const size_t whole = n & ~(size_t)15;
        size_t i;
        unsigned m;

        for (i = 0; i < whole; i += 16)
        {
            m = (unsigned)_mm_movemask_epi8( _mm_cmpeq_epi8( load16( a + i ), load16( b + i ) ) );
            if (m != 0xffff) return diff16( a + i, b + i, m );
        }
        if (n == whole) return 0;
        /* the last 16 bytes, overlapping what was already compared */
        m = (unsigned)_mm_movemask_epi8( _mm_cmpeq_epi8( load16( a + n - 16 ), load16( b + n - 16 ) ) );
        return m == 0xffff ? 0 : diff16( a + n - 16, b + n - 16, m );
    }
    if (n >= 8)
    {
        if (*(const u64u *)a == *(const u64u *)b && *(const u64u *)(a + n - 8) == *(const u64u *)(b + n - 8)) return 0;
    }
    else if (n >= 4)
    {
        if (*(const u32u *)a == *(const u32u *)b && *(const u32u *)(a + n - 4) == *(const u32u *)(b + n - 4)) return 0;
    }
    /* short, or known to differ: at most 15 bytes */
    for (; n; n--, a++, b++) if (*a != *b) return (int)*a - (int)*b;
    return 0;
}

FASTMEM_API int bcmp( const void *a, const void *b, size_t n )
{
    return memcmp( a, b, n );
}

static inline void copy_small( unsigned char *d, const unsigned char *s, size_t n )  /* n <= 16; overlap safe */
{
    if (n >= 8)
    {
        const uint64_t head = *(const u64u *)s, tail = *(const u64u *)(s + n - 8);
        *(u64u *)d = head;
        *(u64u *)(d + n - 8) = tail;
    }
    else if (n >= 4)
    {
        const uint32_t head = *(const u32u *)s, tail = *(const u32u *)(s + n - 4);
        *(u32u *)d = head;
        *(u32u *)(d + n - 4) = tail;
    }
    else if (n >= 2)
    {
        const uint16_t head = *(const u16u *)s, tail = *(const u16u *)(s + n - 2);
        *(u16u *)d = head;
        *(u16u *)(d + n - 2) = tail;
    }
    else if (n)
        *d = *s;
}

FASTMEM_API void *memmove( void *d_, const void *s_, size_t n )
{
    unsigned char *d = d_;
    const unsigned char *s = s_;

    if (n <= 16)
    {
        copy_small( d, s, n );
        return d_;
    }
    if ((uintptr_t)d - (uintptr_t)s >= n)
    {
        /* forward: the destination does not start inside the source */
        const __m128i last = load16( s + n - 16 );
        size_t i;

        for (i = 0; i + 16 < n; i += 16) store16( d + i, load16( s + i ) );
        store16( d + n - 16, last );
    }
    else
    {
        /* backward */
        const __m128i first = load16( s );
        size_t i = n;

        while (i > 16)
        {
            i -= 16;
            store16( d + i, load16( s + i ) );
        }
        store16( d, first );
    }
    return d_;
}

FASTMEM_API void *memcpy( void *restrict d, const void *restrict s, size_t n )
{
    /* musl's forward copy tolerated a destination below the source, so keep overlap safe */
    return memmove( d, s, n );
}

FASTMEM_API void *memset( void *d_, int c, size_t n )
{
    unsigned char *d = d_;
    const unsigned char b = (unsigned char)c;

    if (n >= 16)
    {
        const __m128i v = _mm_set1_epi8( (char)b );
        size_t i;

        for (i = 0; i + 16 < n; i += 16) store16( d + i, v );
        store16( d + n - 16, v );
    }
    else if (n >= 8)
    {
        const uint64_t v = 0x0101010101010101ull * b;
        *(u64u *)d = v;
        *(u64u *)(d + n - 8) = v;
    }
    else if (n >= 4)
    {
        const uint32_t v = 0x01010101u * b;
        *(u32u *)d = v;
        *(u32u *)(d + n - 4) = v;
    }
    else if (n)
    {
        d[0] = b;
        d[n - 1] = b;
        d[n / 2] = b;
    }
    return d_;
}
