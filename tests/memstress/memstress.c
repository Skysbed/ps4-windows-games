/* memstress: reuse the same GPU memory as ONION and GARLIC over and over and prove no data is lost.
 * Each round dirties CPU-cached memory, frees it so the next buffers can land on the same addresses, makes the GPU
 * write them, evicts the CPU's caches, and checks every word.
 * Results: Z:\data\pcps4\games\memstress\result.txt, last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define RESULT "Z:\\data\\pcps4\\games\\memstress\\result.txt"
#define MIB (1024ull * 1024ull)
#define ROUNDS 48
#define EVICT_MIB 8

static HANDLE g_log;
static volatile LONG g_beat;
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[300];
    DWORD done;
    int n;
    va_list a;
    va_start( a, fmt );
    n = _vsnprintf( b, sizeof(b) - 1, fmt, a );
    va_end( a );
    if (n < 0) n = sizeof(b) - 1;
    WriteFile( g_log, b, n, &done, NULL );
    FlushFileBuffers( g_log );
}

static void finish( const char *how )
{
    say( "DONE %s pass=%d fail=%d\n", how, g_pass, g_fail );
    ExitProcess( g_fail ? 1 : 0 );
}

static void must( VkResult r, const char *what )
{
    g_beat++;
    if (r == VK_SUCCESS) return;
    g_fail++;
    say( "FAIL %s  VkResult %d\n", what, r );
    finish( "error" );
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    int idle = 0;
    for (;;)
    {
        LONG now;
        Sleep( 1000 );
        now = g_beat;
        if (now != last) { last = now; idle = 0; continue; }
        if (++idle < 25) continue;
        say( "FAIL HANG no progress for 25 s\nDONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
        TerminateProcess( GetCurrentProcess(), 2 );
    }
    return 0;
}

#define FN(x) static PFN_##x p##x;
#define DEVICE_FUNCS(F) F(vkGetDeviceQueue) F(vkAllocateMemory) F(vkFreeMemory) F(vkCreateBuffer) F(vkDestroyBuffer) \
    F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkMapMemory) F(vkCreateCommandPool) \
    F(vkAllocateCommandBuffers) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkResetCommandBuffer) \
    F(vkCmdCopyBuffer) F(vkCmdFillBuffer) F(vkCmdPipelineBarrier) F(vkCreateFence) F(vkResetFences) \
    F(vkWaitForFences) F(vkQueueSubmit)
DEVICE_FUNCS(FN)

static VkPhysicalDeviceMemoryProperties g_mem;
static VkDevice g_dev;
static VkQueue g_queue;
static VkCommandBuffer g_cmd;
static VkFence g_fence;
static uint32_t g_rng = 0x51ed270b;

static uint32_t rnd( void ) { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return g_rng; }

static uint32_t find_type( uint32_t bits, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid )
{
    uint32_t i;
    for (i = 0; i < g_mem.memoryTypeCount; i++)
    {
        const VkMemoryPropertyFlags f = g_mem.memoryTypes[i].propertyFlags;
        if ((bits & (1u << i)) && (f & want) == want && !(f & avoid)) return i;
    }
    must( VK_ERROR_FEATURE_NOT_PRESENT, "memory type" );
    return 0;
}

struct buf { VkBuffer b; VkDeviceMemory m; uint32_t *map; VkDeviceSize size; };

static struct buf make( VkDeviceSize size, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid )
{
    VkBufferCreateInfo c = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements r;
    struct buf x;

    memset( &x, 0, sizeof(x) );
    x.size = size;
    c.size = size;
    c.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    must( pvkCreateBuffer( g_dev, &c, NULL, &x.b ), "create buffer" );
    pvkGetBufferMemoryRequirements( g_dev, x.b, &r );
    a.allocationSize = r.size;
    a.memoryTypeIndex = find_type( r.memoryTypeBits, want, avoid );
    must( pvkAllocateMemory( g_dev, &a, NULL, &x.m ), "allocate" );
    must( pvkBindBufferMemory( g_dev, x.b, x.m, 0 ), "bind" );
    if (want & VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)
        must( pvkMapMemory( g_dev, x.m, 0, VK_WHOLE_SIZE, 0, (void **)&x.map ), "map" );
    return x;
}

static void drop( struct buf *x )
{
    pvkDestroyBuffer( g_dev, x->b, NULL );
    pvkFreeMemory( g_dev, x->m, NULL );
    memset( x, 0, sizeof(*x) );
}

static void begin( void )
{
    VkCommandBufferBeginInfo b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    pvkResetCommandBuffer( g_cmd, 0 );
    must( pvkBeginCommandBuffer( g_cmd, &b ), "begin" );
}

static void barrier( VkAccessFlags dst, VkPipelineStageFlags stage )
{
    VkMemoryBarrier m = { VK_STRUCTURE_TYPE_MEMORY_BARRIER };
    m.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    m.dstAccessMask = dst;
    pvkCmdPipelineBarrier( g_cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, stage, 0, 1, &m, 0, NULL, 0, NULL );
}

static void submit( void )
{
    VkSubmitInfo s = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
    must( pvkEndCommandBuffer( g_cmd ), "end" );
    s.commandBufferCount = 1; s.pCommandBuffers = &g_cmd;
    must( pvkResetFences( g_dev, 1, &g_fence ), "reset fence" );
    must( pvkQueueSubmit( g_queue, 1, &s, g_fence ), "submit" );
    must( pvkWaitForFences( g_dev, 1, &g_fence, VK_TRUE, 15000000000ull ), "wait" );
}

static void copy( struct buf *src, struct buf *dst, VkDeviceSize size )
{
    VkBufferCopy r = { 0, 0, size };
    pvkCmdCopyBuffer( g_cmd, src->b, dst->b, 1, &r );
}

static void evict_cpu_caches( void )
{
    static volatile uint32_t *scratch;
    size_t i;
    if (!scratch) scratch = VirtualAlloc( NULL, EVICT_MIB * MIB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (!scratch) must( VK_ERROR_OUT_OF_HOST_MEMORY, "evict buffer" );
    for (i = 0; i < EVICT_MIB * MIB / 4; i += 8) scratch[i] = (uint32_t)i;
}

static uint32_t pattern( uint32_t seed, size_t i ) { return seed ^ (uint32_t)(i * 0x9E3779B1u); }

static int verify_pattern( const char *what, int round, const uint32_t *p, size_t words, uint32_t seed )
{
    size_t i, bad = 0, first = 0;
    for (i = 0; i < words; i++)
        if (p[i] != pattern( seed, i )) { if (!bad) first = i; bad++; }
    if (!bad) return 1;
    g_fail++;
    say( "FAIL round %d %s: %llu of %llu words wrong, first at byte %llu (got %08x want %08x)\n", round, what,
         (unsigned long long)bad, (unsigned long long)words, (unsigned long long)first * 4, p[first], pattern( seed, first ) );
    return 0;
}

static int verify_fill( const char *what, int round, const uint32_t *p, size_t words, uint32_t value )
{
    size_t i, bad = 0, first = 0;
    for (i = 0; i < words; i++)
        if (p[i] != value) { if (!bad) first = i; bad++; }
    if (!bad) return 1;
    g_fail++;
    say( "FAIL round %d %s: %llu of %llu words wrong, first at byte %llu (got %08x want %08x)\n", round, what,
         (unsigned long long)bad, (unsigned long long)words, (unsigned long long)first * 4, p[first], value );
    return 0;
}

int main( void )
{
    const VkMemoryPropertyFlags vis = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    const VkMemoryPropertyFlags cached = vis | VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    HMODULE lib;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create_instance;
    PFN_vkEnumeratePhysicalDevices enum_pd;
    PFN_vkGetPhysicalDeviceMemoryProperties get_mem;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_qf;
    PFN_vkCreateDevice create_device;
    PFN_vkGetDeviceProcAddr gdpa;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkQueueFamilyProperties qf[8];
    uint32_t count = 1, nqf = 8, family = ~0u, i;
    unsigned long long total_mib = 0;
    int round;

    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO memstress 1, %d rounds\n", ROUNDS );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    CreateWindowExA( 0, "STATIC", "memstress", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );

    lib = LoadLibraryA( "winevulkan.dll" );
    if (!lib) { g_fail++; say( "FAIL load winevulkan\n" ); finish( "error" ); }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress( lib, "vkGetInstanceProcAddr" );
    create_instance = (PFN_vkCreateInstance)gipa( NULL, "vkCreateInstance" );
    {
        VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        VkInstanceCreateInfo c = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        app.apiVersion = VK_API_VERSION_1_1;
        c.pApplicationInfo = &app;
        must( create_instance( &c, NULL, &inst ), "create instance" );
    }
    enum_pd = (PFN_vkEnumeratePhysicalDevices)gipa( inst, "vkEnumeratePhysicalDevices" );
    get_mem = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa( inst, "vkGetPhysicalDeviceMemoryProperties" );
    get_qf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa( inst, "vkGetPhysicalDeviceQueueFamilyProperties" );
    create_device = (PFN_vkCreateDevice)gipa( inst, "vkCreateDevice" );
    gdpa = (PFN_vkGetDeviceProcAddr)gipa( inst, "vkGetDeviceProcAddr" );
    enum_pd( inst, &count, &pd );
    get_mem( pd, &g_mem );
    get_qf( pd, &nqf, qf );
    for (i = 0; i < nqf; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    {
        float prio = 1;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo c = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &prio;
        c.queueCreateInfoCount = 1; c.pQueueCreateInfos = &q;
        must( create_device( pd, &c, NULL, &g_dev ), "create device" );
    }
#define LOADD(x) p##x = (PFN_##x)gdpa( g_dev, #x );
    DEVICE_FUNCS(LOADD)
    pvkGetDeviceQueue( g_dev, family, 0, &g_queue );
    {
        VkCommandPoolCreateInfo c = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo a = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo fc = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        VkCommandPool pool;
        c.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; c.queueFamilyIndex = family;
        must( pvkCreateCommandPool( g_dev, &c, NULL, &pool ), "command pool" );
        a.commandPool = pool; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1;
        must( pvkAllocateCommandBuffers( g_dev, &a, &g_cmd ), "command buffer" );
        must( pvkCreateFence( g_dev, &fc, NULL, &g_fence ), "fence" );
    }

    for (round = 0; round < ROUNDS; round++)
    {
        const VkDeviceSize size = (1ull << (rnd() % 6)) * MIB + (rnd() % 16) * 4096;
        const size_t words = size / 4;
        const uint32_t seed = rnd(), fill = rnd() | 1;
        struct buf old_cached, gpu_visible, gpu_only, readback;
        int ok = 1;
        size_t w;

        /* 1. Leave dirty CPU cache lines in ONION memory, then free it so the next buffers can inherit the address. */
        old_cached = make( size, cached, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
        for (w = 0; w < words; w++) old_cached.map[w] = 0xDEAD0000u ^ (uint32_t)w;
        drop( &old_cached );
        begin();
        submit();   /* lets the driver retire the freed range */

        gpu_visible = make( size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | vis, 0 );
        gpu_only = make( size, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT );
        readback = make( size, cached, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );

        /* 2. GPU writes GARLIC. Any stale dirty CPU line that lands afterward corrupts this. */
        begin();
        pvkCmdFillBuffer( g_cmd, gpu_visible.b, 0, VK_WHOLE_SIZE, fill );
        pvkCmdFillBuffer( g_cmd, gpu_only.b, 0, VK_WHOLE_SIZE, ~fill );
        barrier( VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
        submit();
        evict_cpu_caches();
        begin();
        copy( &gpu_visible, &readback, size );
        barrier( VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT );
        submit();
        ok &= verify_fill( "GPU fill of CPU-visible GARLIC", round, readback.map, words, fill );
        begin();
        copy( &gpu_only, &readback, size );
        barrier( VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT );
        submit();
        ok &= verify_fill( "GPU fill of GPU-only GARLIC", round, readback.map, words, ~fill );

        /* 3. CPU writes GARLIC through write-combining, GPU moves it through both kinds of memory. */
        for (w = 0; w < words; w++) gpu_visible.map[w] = pattern( seed, w );
        begin();
        copy( &gpu_visible, &gpu_only, size );
        barrier( VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT );
        copy( &gpu_only, &readback, size );
        barrier( VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT );
        submit();
        ok &= verify_pattern( "CPU data through GARLIC", round, readback.map, words, seed );

        /* 4. A sampled CPU read back from GARLIC, which is slow by design. */
        for (w = 0; w < words; w += 4096 / 4)
            if (((volatile uint32_t *)gpu_visible.map)[w] != pattern( seed, w ))
            {
                ok = 0; g_fail++;
                say( "FAIL round %d CPU read of GARLIC wrong at byte %llu\n", round, (unsigned long long)w * 4 );
                break;
            }

        /* Free in a different order each round so addresses and types mix. */
        switch (round % 3)
        {
        case 0: drop( &gpu_visible ); drop( &gpu_only ); drop( &readback ); break;
        case 1: drop( &readback ); drop( &gpu_visible ); drop( &gpu_only ); break;
        default: drop( &gpu_only ); drop( &readback ); drop( &gpu_visible ); break;
        }
        begin();
        submit();
        total_mib += 3 * (size >> 20);
        if (ok) g_pass++;
        if (g_fail >= 8) finish( "too many errors" );
        if (!((round + 1) % 12)) say( "INFO %d rounds done, %llu MiB allocated and verified\n", round + 1, total_mib );
    }
    finish( "ok" );
    return 0;
}
