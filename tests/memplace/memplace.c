/* memplace: where Vulkan memory lands (GARLIC or ONION) and how fast the CPU and GPU reach each kind.
 * The tile sets ORBIS_MEM_STATS=1 so the driver logs live placement to mesa.log.
 * Results: Z:\data\pcps4\games\memplace\result.txt, last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define RESULT "Z:\\data\\pcps4\\games\\memplace\\result.txt"
#define MIB (1024ull * 1024ull)
#define TEST_MIB 64ull
#define HOLD_BLOCKS 28   /* 28 x 32 MiB: a game-sized load that still leaves the system memory */

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

static double now_ns( void )
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return c.QuadPart * 1e9 / f.QuadPart;
}

#define FN(x) static PFN_##x p##x;
#define DEVICE_FUNCS(F) F(vkGetDeviceQueue) F(vkAllocateMemory) F(vkFreeMemory) F(vkCreateBuffer) \
    F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkMapMemory) F(vkCreateCommandPool) \
    F(vkAllocateCommandBuffers) F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkResetCommandBuffer) \
    F(vkCmdCopyBuffer) F(vkCreateFence) F(vkResetFences) F(vkWaitForFences) F(vkQueueSubmit)
DEVICE_FUNCS(FN)

static VkPhysicalDeviceMemoryProperties g_mem;
static VkDevice g_dev;
static VkQueue g_queue;
static VkCommandBuffer g_cmd;
static VkFence g_fence;

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

struct buf { VkBuffer b; VkDeviceMemory m; uint32_t *map; };

static struct buf make( VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid )
{
    VkBufferCreateInfo c = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements r;
    struct buf x;

    memset( &x, 0, sizeof(x) );
    c.size = TEST_MIB * MIB;
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

static double mbs( double ns, uint64_t bytes ) { return bytes / (1024.0 * 1024.0) / (ns / 1e9); }

static void cpu_speed( const char *name, struct buf *x )
{
    const size_t words = TEST_MIB * MIB / 4, read_words = 16 * MIB / 4;
    volatile uint32_t sink = 0;
    double best_w = 1e18, best_r = 1e18;
    int round;
    size_t i;

    for (round = 0; round < 3; round++)
    {
        uint32_t acc = 0;
        double t = now_ns();
        for (i = 0; i < words; i++) x->map[i] = (uint32_t)i;
        t = now_ns() - t;
        if (t < best_w) best_w = t;
        g_beat++;
        t = now_ns();
        for (i = 0; i < read_words; i++) acc += ((volatile uint32_t *)x->map)[i];
        t = now_ns() - t;
        sink += acc;
        if (t < best_r) best_r = t;
        g_beat++;
    }
    say( "INFO CPU on %-28s write %6.0f MiB/s   read %6.0f MiB/s\n", name, mbs( best_w, words * 4 ), mbs( best_r, read_words * 4 ) );
}

static void gpu_copy( const char *name, struct buf *src, struct buf *dst )
{
    double best = 1e18;
    int round;

    for (round = 0; round < 5; round++)
    {
        VkCommandBufferBeginInfo b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo s = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
        VkBufferCopy r = { 0, 0, TEST_MIB * MIB };
        double t;

        pvkResetCommandBuffer( g_cmd, 0 );
        must( pvkBeginCommandBuffer( g_cmd, &b ), "begin" );
        pvkCmdCopyBuffer( g_cmd, src->b, dst->b, 1, &r );
        must( pvkEndCommandBuffer( g_cmd ), "end" );
        s.commandBufferCount = 1; s.pCommandBuffers = &g_cmd;
        must( pvkResetFences( g_dev, 1, &g_fence ), "reset fence" );
        t = now_ns();
        must( pvkQueueSubmit( g_queue, 1, &s, g_fence ), "submit" );
        must( pvkWaitForFences( g_dev, 1, &g_fence, VK_TRUE, 10000000000ull ), "wait" );
        t = now_ns() - t;
        if (t < best) best = t;
    }
    say( "INFO GPU copy %-28s %6.0f MiB/s\n", name, mbs( best, TEST_MIB * MIB ) );
    g_pass++;
}

int main( void )
{
    static VkDeviceMemory hold[HOLD_BLOCKS];
    const VkMemoryPropertyFlags vis = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
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
    uint32_t count = 1, nqf = 8, family = ~0u, i, local_type;
    struct buf garlic_a, garlic_b, onion_a, onion_b, local;
    int held = 0;

    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO memplace 1\n" );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    CreateWindowExA( 0, "STATIC", "memplace", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );

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

    garlic_a = make( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | vis, 0 );
    garlic_b = make( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | vis, 0 );
    onion_a = make( vis | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    onion_b = make( vis | VK_MEMORY_PROPERTY_HOST_CACHED_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
    local = make( VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT );

    cpu_speed( "GPU-side memory (GARLIC)", &garlic_a );
    cpu_speed( "CPU-side memory (ONION)", &onion_a );
    memset( garlic_b.map, 0x5a, 4096 );
    memset( onion_b.map, 0x5a, 4096 );

    gpu_copy( "GARLIC to GARLIC", &garlic_a, &garlic_b );
    gpu_copy( "ONION to GARLIC", &onion_a, &garlic_b );
    gpu_copy( "GARLIC to ONION", &garlic_a, &onion_b );
    gpu_copy( "ONION to ONION", &onion_a, &onion_b );
    gpu_copy( "GARLIC to GPU-only", &garlic_a, &local );

    {
        VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        a.allocationSize = 32 * MIB;
        local_type = find_type( ~0u, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT );
        a.memoryTypeIndex = local_type;
        while (held < HOLD_BLOCKS && pvkAllocateMemory( g_dev, &a, NULL, &hold[held] ) == VK_SUCCESS) { held++; g_beat++; }
    }
    say( "INFO held %d MiB more GPU-only memory (driver report is in mesa.log)\n", held * 32 );
    Sleep( 300 );
    for (i = 0; i < (uint32_t)held; i++) pvkFreeMemory( g_dev, hold[i], NULL );
    g_pass++;
    finish( "ok" );
    return 0;
}
