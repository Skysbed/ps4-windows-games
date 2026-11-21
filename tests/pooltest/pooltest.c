/* pooltest: fill a larger GPU pool with real GPU-written data, check every word, and prove the CPU still has memory.
 * The important part is the top of the pool, which sits above the first 4 GiB address window. */
#define main memstress_main
#include "../memstress/memstress.c"
#undef main
#undef RESULT
#define RESULT "Z:\\data\\pcps4\\games\\pooltest\\result.txt"
#define BLOCK_MIB 64ull
#define MAX_BLOCKS 64
#define CPU_MIB 512ull

static int try_make( struct buf *x, VkMemoryPropertyFlags want, VkMemoryPropertyFlags avoid )
{
    VkBufferCreateInfo c = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements r;

    memset( x, 0, sizeof(*x) );
    x->size = BLOCK_MIB * MIB;
    c.size = x->size;
    c.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    must( pvkCreateBuffer( g_dev, &c, NULL, &x->b ), "create buffer" );
    pvkGetBufferMemoryRequirements( g_dev, x->b, &r );
    a.allocationSize = r.size;
    a.memoryTypeIndex = find_type( r.memoryTypeBits, want, avoid );
    if (pvkAllocateMemory( g_dev, &a, NULL, &x->m ) != VK_SUCCESS)
    {
        pvkDestroyBuffer( g_dev, x->b, NULL );
        memset( x, 0, sizeof(*x) );
        return 0;
    }
    must( pvkBindBufferMemory( g_dev, x->b, x->m, 0 ), "bind" );
    return 1;
}

int main( void )
{
    static struct buf blocks[MAX_BLOCKS];
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
    unsigned long long heaps = 0, allocated;
    struct buf readback;
    int n = 0, k, good = 0;
    volatile uint32_t *cpu;
    size_t w;

    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO pooltest 1\n" );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    CreateWindowExA( 0, "STATIC", "pooltest", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );

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
    for (i = 0; i < g_mem.memoryHeapCount; i++) heaps += g_mem.memoryHeaps[i].size;
    for (i = 0; i < nqf; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    {
        float prio = 1;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo c = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &prio;
        c.queueCreateInfoCount = 1; c.pQueueCreateInfos = &q;
        must( create_device( pd, &c, NULL, &g_dev ), "create device" );
    }
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

    say( "INFO Vulkan reports %llu MiB\n", heaps / MIB );
    if (!try_make( &readback, cached, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT )) must( VK_ERROR_OUT_OF_DEVICE_MEMORY, "readback" );
    must( pvkMapMemory( g_dev, readback.m, 0, VK_WHOLE_SIZE, 0, (void **)&readback.map ), "map readback" );

    /* Leave 128 MiB for the driver's own command buffers and rings. */
    while (n < MAX_BLOCKS && (n + 2) * BLOCK_MIB + 128 <= heaps / MIB &&
           try_make( &blocks[n], VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT )) { n++; g_beat++; }
    allocated = n * BLOCK_MIB;
    say( "INFO GPU buffers allocated: %llu MiB in %d blocks\n", allocated, n );
    if (allocated + BLOCK_MIB < (heaps / MIB) * 8 / 10)
    {
        g_fail++;
        say( "FAIL only %llu of %llu reported MiB could be allocated\n", allocated, heaps / MIB );
    }
    else g_pass++;

    for (k = 0; k < n; k++)
    {
        begin();
        pvkCmdFillBuffer( g_cmd, blocks[k].b, 0, VK_WHOLE_SIZE, 0x5A000000u ^ (uint32_t)(k * 0x01010101u) ^ 0x77u );
        submit();
    }
    evict_cpu_caches();
    for (k = 0; k < n; k++)
    {
        begin();
        copy( &blocks[k], &readback, BLOCK_MIB * MIB );
        barrier( VK_ACCESS_HOST_READ_BIT, VK_PIPELINE_STAGE_HOST_BIT );
        submit();
        good += verify_fill( "GPU data in the pool", k, readback.map, BLOCK_MIB * MIB / 4, 0x5A000000u ^ (uint32_t)(k * 0x01010101u) ^ 0x77u );
        if (g_fail >= 8) finish( "too many errors" );
    }
    say( "INFO verified %d of %d blocks, every word\n", good, n );
    if (good == n) g_pass++;

    cpu = VirtualAlloc( NULL, CPU_MIB * MIB, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE );
    if (!cpu) { g_fail++; say( "FAIL the CPU could not get %llu MiB with the pool full\n", CPU_MIB ); }
    else
    {
        for (w = 0; w < CPU_MIB * MIB / 4; w += 1024) cpu[w] = (uint32_t)w;
        for (w = 0; w < CPU_MIB * MIB / 4; w += 1024)
            if (cpu[w] != (uint32_t)w) { g_fail++; say( "FAIL CPU memory wrong at %llu\n", (unsigned long long)w ); break; }
        g_beat++;
        if (!g_fail) { g_pass++; say( "INFO CPU still got and used %llu MiB with the pool full\n", CPU_MIB ); }
        VirtualFree( (void *)cpu, 0, MEM_RELEASE );
    }

    for (k = 0; k < n; k++) drop( &blocks[k] );
    begin();
    submit();
    finish( "ok" );
    return 0;
}
