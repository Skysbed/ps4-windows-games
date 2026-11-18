/* ps4_preload.c - serve a game's many small resource files from RAM.
 *
 * The PS4 keeps almost no file data cached: reading 2000 different small files costs ~2.5 ms each on
 * every pass, while the same read from cache costs 0.02 ms. Horizon reads ~23,000 loose files at startup,
 * so it spends ~2 minutes waiting on storage. PS4 titles avoid this by packing assets; this does the same
 * underneath an unmodified Windows game.
 *
 * PCPS4_PRELOAD=<pack file>:<root dir> in /data/pcps4_env.txt. At startup a background thread reads the pack
 * in one sequential pass, then opens+fstats each listed file under <root> to learn its (dev, ino). read() and
 * pread() on a regular file whose (dev, ino, size) match are answered from memory; everything else - other
 * files, files not mapped yet, a size mismatch - goes to the real call, so the worst case is today's speed.
 *
 * Pack: "PCPKPK01", u32 count, then count x { u16 len, path[len], u64 offset, u64 size }, then data
 * (offsets are from the start of the file). Built by pack_res.py on the laptop. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/stat.h>

ssize_t __real_read( int fd, void *buf, size_t n );
ssize_t __real_pread( int fd, void *buf, size_t n, off_t off );

struct pk_entry { const char *path; uint64_t off, size; };
struct pk_slot  { uint64_t dev, ino; const struct pk_entry *e; };

#define SLOTS 65536            /* power of two, > 2x the files we pack */
static struct pk_slot slots[SLOTS];
static volatile int pk_live;   /* set once the pack data is in memory */
static unsigned char *pk_data;
static uint64_t pk_len;
static struct pk_entry *pk_ents;
static uint32_t pk_count;
static char pk_root[512];

static inline uint32_t hash( uint64_t dev, uint64_t ino ) { uint64_t h = ino * 0x9E3779B97F4A7C15ull ^ dev; return (uint32_t)(h >> 40) & (SLOTS - 1); }

static void slot_put( uint64_t dev, uint64_t ino, const struct pk_entry *e )
{
    uint32_t i = hash( dev, ino );
    while (slots[i].e) i = (i + 1) & (SLOTS - 1);
    slots[i].dev = dev; slots[i].ino = ino;
    __atomic_store_n( &slots[i].e, e, __ATOMIC_RELEASE );   /* readers see dev/ino before e */
}

static const struct pk_entry *slot_get( uint64_t dev, uint64_t ino )
{
    uint32_t i = hash( dev, ino );
    const struct pk_entry *e;
    while ((e = __atomic_load_n( &slots[i].e, __ATOMIC_ACQUIRE )))
    {
        if (slots[i].ino == ino && slots[i].dev == dev) return e;
        i = (i + 1) & (SLOTS - 1);
    }
    return NULL;
}

static void *loader( void *arg )
{
    const char *pack = arg;
    int fd = open( pack, O_RDONLY );
    struct stat st;
    uint64_t got = 0;
    char path[1024];
    uint32_t mapped = 0;

    if (fd < 0 || fstat( fd, &st )) { fprintf( stderr, "pcps4-preload: cannot open %s\n", pack ); return NULL; }
    pk_len = st.st_size;
    if (!(pk_data = malloc( pk_len ))) { fprintf( stderr, "pcps4-preload: no memory for %llu bytes\n", (unsigned long long)pk_len ); close( fd ); return NULL; }
    while (got < pk_len)
    {
        ssize_t r = __real_read( fd, pk_data + got, (pk_len - got) > (8u << 20) ? (8u << 20) : (pk_len - got) );
        if (r <= 0) break;
        got += r;
    }
    close( fd );
    if (got != pk_len || memcmp( pk_data, "PCPKPK01", 8 )) { fprintf( stderr, "pcps4-preload: bad pack %s\n", pack ); return NULL; }

    {
        const unsigned char *p = pk_data + 8;
        memcpy( &pk_count, p, 4 ); p += 4;
        if (!(pk_ents = calloc( pk_count, sizeof(*pk_ents) ))) return NULL;
        for (uint32_t i = 0; i < pk_count; i++)
        {
            uint16_t len; memcpy( &len, p, 2 ); p += 2;
            char *s = malloc( len + 1 ); memcpy( s, p, len ); s[len] = 0; p += len;
            pk_ents[i].path = s;
            memcpy( &pk_ents[i].off, p, 8 ); p += 8;
            memcpy( &pk_ents[i].size, p, 8 ); p += 8;
        }
    }
    __atomic_store_n( &pk_live, 1, __ATOMIC_RELEASE );
    fprintf( stderr, "pcps4-preload: %u files, %llu MB in memory\n", pk_count, (unsigned long long)(pk_len >> 20) );

    for (uint32_t i = 0; i < pk_count; i++)
    {
        int f;
        snprintf( path, sizeof(path), "%s/%s", pk_root, pk_ents[i].path );
        if ((f = open( path, O_RDONLY )) < 0) continue;
        if (!fstat( f, &st ) && S_ISREG( st.st_mode ) && (uint64_t)st.st_size == pk_ents[i].size)
        {
            slot_put( st.st_dev, st.st_ino, &pk_ents[i] );
            mapped++;
        }
        close( f );
    }
    fprintf( stderr, "pcps4-preload: %u of %u files mapped\n", mapped, pk_count );
    return NULL;
}

void pcps4_preload_init( void )
{
    static char pack[512];
    char line[1100];
    FILE *env = fopen( "/data/pcps4_env.txt", "r" );
    pthread_t t;

    if (!env) return;
    while (fgets( line, sizeof(line), env ))
    {
        char *colon;
        if (strncmp( line, "PCPS4_PRELOAD=", 14 )) continue;
        line[strcspn( line, "\r\n" )] = 0;
        if (!(colon = strchr( line + 14, ':' ))) continue;
        *colon = 0;
        snprintf( pack, sizeof(pack), "%s", line + 14 );
        snprintf( pk_root, sizeof(pk_root), "%s", colon + 1 );
    }
    fclose( env );
    if (!pack[0]) return;
    if (pthread_create( &t, NULL, loader, pack )) return;
    pthread_detach( t );
}

/* the hot path: one fstat for regular files once a pack is live, nothing otherwise */
static const struct pk_entry *lookup( int fd )
{
    struct stat st;
    const struct pk_entry *e;
    if (!__atomic_load_n( &pk_live, __ATOMIC_ACQUIRE )) return NULL;
    if (fstat( fd, &st ) || !S_ISREG( st.st_mode )) return NULL;
    if (!(e = slot_get( st.st_dev, st.st_ino ))) return NULL;
    return (uint64_t)st.st_size == e->size ? e : NULL;
}

static ssize_t copy_out( const struct pk_entry *e, void *buf, size_t n, uint64_t off )
{
    if (off >= e->size) return 0;
    if (n > e->size - off) n = e->size - off;
    memcpy( buf, pk_data + e->off + off, n );
    return n;
}

ssize_t __wrap_pread( int fd, void *buf, size_t n, off_t off )
{
    const struct pk_entry *e = lookup( fd );
    if (!e || off < 0) return __real_pread( fd, buf, n, off );
    return copy_out( e, buf, n, (uint64_t)off );
}

ssize_t __wrap_read( int fd, void *buf, size_t n )
{
    const struct pk_entry *e = lookup( fd );
    off_t pos;
    ssize_t r;
    if (!e || (pos = lseek( fd, 0, SEEK_CUR )) < 0) return __real_read( fd, buf, n );
    r = copy_out( e, buf, n, (uint64_t)pos );
    if (r > 0) lseek( fd, pos + r, SEEK_SET );   /* keep the kernel's file position honest */
    return r;
}
