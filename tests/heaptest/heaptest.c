/* heaptest: does Vulkan report the GPU memory the driver can really hand out?
 * The tile sets ORBIS_ARENA_MIB=1024. Results: Z:\data\pcps4\games\heaptest\result.txt, last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define RESULT "Z:\\data\\pcps4\\games\\heaptest\\result.txt"
#define ARENA_MIB 1024ull
#define BLOCK_MIB 32ull
#define MAX_BLOCKS 256
#define MIB (1024ull * 1024ull)

static HANDLE g_log;
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

static void check( int ok, const char *what, unsigned long long got, unsigned long long want )
{
    if (ok) { g_pass++; say( "PASS %s  (%llu MiB, limit or target %llu MiB)\n", what, got, want ); }
    else { g_fail++; say( "FAIL %s  (%llu MiB, limit or target %llu MiB)\n", what, got, want ); }
}

static void finish( const char *how )
{
    say( "DONE %s pass=%d fail=%d\n", how, g_pass, g_fail );
    ExitProcess( g_fail ? 1 : 0 );
}

static PFN_vkGetPhysicalDeviceMemoryProperties2 pGetMem2;
static VkPhysicalDevice g_pd;

struct totals { uint64_t heaps, budget, usage, local_budget; };

static struct totals query( int print )
{
    VkPhysicalDeviceMemoryBudgetPropertiesEXT b = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT };
    VkPhysicalDeviceMemoryProperties2 m = { VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2 };
    struct totals t = { 0, 0, 0, 0 };
    uint32_t i;

    m.pNext = &b;
    pGetMem2( g_pd, &m );
    for (i = 0; i < m.memoryProperties.memoryHeapCount; i++)
    {
        t.heaps += m.memoryProperties.memoryHeaps[i].size;
        t.budget += b.heapBudget[i];
        t.usage += b.heapUsage[i];
        if (m.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) t.local_budget += b.heapBudget[i];
        if (print)
            say( "INFO heap %u: size %llu MiB, budget %llu MiB, used %llu MiB%s\n", i,
                 (unsigned long long)(m.memoryProperties.memoryHeaps[i].size / MIB),
                 (unsigned long long)(b.heapBudget[i] / MIB), (unsigned long long)(b.heapUsage[i] / MIB),
                 (m.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) ? " device-local" : "" );
    }
    return t;
}

/* A stuck test must end itself instead of holding the app. */
static DWORD WINAPI watchdog( void *arg )
{
    Sleep( 45000 );
    say( "FAIL HANG no result after 45 s\nDONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
    TerminateProcess( GetCurrentProcess(), 2 );
    return 0;
}

int main( void )
{
    static VkDeviceMemory blocks[MAX_BLOCKS];
    const char *ext = VK_EXT_MEMORY_BUDGET_EXTENSION_NAME;
    const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT |
                                       VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
    HMODULE lib;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create_instance;
    PFN_vkEnumeratePhysicalDevices enum_pd;
    PFN_vkGetPhysicalDeviceMemoryProperties get_mem;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties get_qf;
    PFN_vkCreateDevice create_device;
    PFN_vkGetDeviceProcAddr gdpa;
    PFN_vkAllocateMemory alloc;
    PFN_vkFreeMemory release;
    VkInstance inst;
    VkDevice dev;
    VkPhysicalDeviceMemoryProperties mem;
    VkQueueFamilyProperties qf[8];
    VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    uint32_t count = 1, nqf = 8, family = ~0u, type = ~0u, i;
    struct totals start, mid, full, after;
    int n = 0, again = 0, have_mid = 0;
    uint64_t allocated;

    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO heaptest 3, arena %llu MiB\n", ARENA_MIB );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    /* The working Vulkan tests create a window before loading winevulkan. */
    CreateWindowExA( 0, "STATIC", "heaptest", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );
    say( "INFO window ready\n" );

    lib = LoadLibraryA( "winevulkan.dll" );
    if (!lib) { g_fail++; say( "FAIL load winevulkan\n" ); finish( "error" ); }
    gipa = (PFN_vkGetInstanceProcAddr)GetProcAddress( lib, "vkGetInstanceProcAddr" );
    create_instance = (PFN_vkCreateInstance)gipa( NULL, "vkCreateInstance" );
    {
        VkApplicationInfo app = { VK_STRUCTURE_TYPE_APPLICATION_INFO };
        VkInstanceCreateInfo c = { VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO };
        app.apiVersion = VK_API_VERSION_1_1;
        c.pApplicationInfo = &app;
        if (create_instance( &c, NULL, &inst )) { g_fail++; say( "FAIL create instance\n" ); finish( "error" ); }
    }
    enum_pd = (PFN_vkEnumeratePhysicalDevices)gipa( inst, "vkEnumeratePhysicalDevices" );
    get_mem = (PFN_vkGetPhysicalDeviceMemoryProperties)gipa( inst, "vkGetPhysicalDeviceMemoryProperties" );
    get_qf = (PFN_vkGetPhysicalDeviceQueueFamilyProperties)gipa( inst, "vkGetPhysicalDeviceQueueFamilyProperties" );
    create_device = (PFN_vkCreateDevice)gipa( inst, "vkCreateDevice" );
    gdpa = (PFN_vkGetDeviceProcAddr)gipa( inst, "vkGetDeviceProcAddr" );
    pGetMem2 = (PFN_vkGetPhysicalDeviceMemoryProperties2)gipa( inst, "vkGetPhysicalDeviceMemoryProperties2" );
    enum_pd( inst, &count, &g_pd );
    get_mem( g_pd, &mem );
    get_qf( g_pd, &nqf, qf );
    for (i = 0; i < nqf; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    {
        float prio = 1;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo c = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &prio;
        c.queueCreateInfoCount = 1; c.pQueueCreateInfos = &q;
        c.enabledExtensionCount = 1; c.ppEnabledExtensionNames = &ext;
        if (create_device( g_pd, &c, NULL, &dev )) { g_fail++; say( "FAIL create device\n" ); finish( "error" ); }
    }
    alloc = (PFN_vkAllocateMemory)gdpa( dev, "vkAllocateMemory" );
    release = (PFN_vkFreeMemory)gdpa( dev, "vkFreeMemory" );
    /* CPU-cached memory is used so the test measures the address pool without taking extra GARLIC pages. */
    for (i = 0; i < mem.memoryTypeCount; i++)
        if ((mem.memoryTypes[i].propertyFlags & want) == want) { type = i; break; }
    if (type == ~0u) { g_fail++; say( "FAIL no CPU-cached memory type\n" ); finish( "error" ); }

    start = query( 1 );
    check( start.heaps / MIB <= ARENA_MIB, "reported memory fits the GPU pool", start.heaps / MIB, ARENA_MIB );
    check( start.budget / MIB <= ARENA_MIB, "reported budget fits the GPU pool", start.budget / MIB, ARENA_MIB );

    a.allocationSize = BLOCK_MIB * MIB;
    a.memoryTypeIndex = type;
    while (n < MAX_BLOCKS && alloc( dev, &a, NULL, &blocks[n] ) == VK_SUCCESS)
    {
        n++;
        if (!have_mid && n * BLOCK_MIB >= 256) { mid = query( 0 ); have_mid = 1; }
    }
    allocated = n * BLOCK_MIB * MIB;
    say( "INFO really allocated %llu MiB in %d blocks\n", (unsigned long long)(allocated / MIB), n );
    full = query( 1 );

    check( start.heaps <= allocated + allocated / 6, "reported memory can really be allocated", start.heaps / MIB,
           (allocated + allocated / 6) / MIB );
    check( allocated >= start.heaps - start.heaps / 5, "most reported memory is usable", allocated / MIB,
           (start.heaps - start.heaps / 5) / MIB );
    if (have_mid)
    {
        /* Each heap's budget is capped at its own size, and this test fills the small heap, so judge the other one:
         * it should be offered two thirds of what is really free. Counting usage twice makes it far smaller. */
        const uint64_t free_now = start.heaps > mid.usage ? start.heaps - mid.usage : 0;
        const uint64_t truth = free_now * 2 / 3;
        const uint64_t gap = mid.local_budget > truth ? mid.local_budget - truth : truth - mid.local_budget;
        check( gap <= 64 * MIB, "budget tracks free memory without double counting", mid.local_budget / MIB, truth / MIB );
    }
    check( full.usage + 96 * MIB >= allocated + start.usage, "usage counts what was allocated", full.usage / MIB,
           (allocated + start.usage) / MIB );

    for (i = 0; i < (uint32_t)n; i++) release( dev, blocks[i], NULL );
    after = query( 0 );
    check( after.usage <= start.usage + 32 * MIB, "usage returns after freeing", after.usage / MIB,
           (start.usage + 32 * MIB) / MIB );
    while (again < n && alloc( dev, &a, NULL, &blocks[again] ) == VK_SUCCESS) again++;
    check( again == n, "the same memory can be allocated again", again * BLOCK_MIB, n * BLOCK_MIB );
    for (i = 0; i < (uint32_t)again; i++) release( dev, blocks[i], NULL );
    finish( "ok" );
    return 0;
}
