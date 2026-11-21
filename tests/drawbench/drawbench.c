/* drawbench: what one draw call costs the CPU in RADV, under the state changes real games make.
 * Each scenario records DRAWS draws, ends with one full-screen draw, and checks the picture.
 * Results: Z:\data\pcps4\games\drawbench\result.txt, last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define RESULT "Z:\\data\\pcps4\\games\\drawbench\\result.txt"
#define DRAWS 10000
#define ROUNDS 5
#define SIZE 64

static const uint32_t vs_code[] =
#include "vs.inc"
;
static const uint32_t fs_a_code[] =
#include "fs_a.inc"
;
static const uint32_t fs_b_code[] =
#include "fs_b.inc"
;

static HANDLE g_log;
static CRITICAL_SECTION g_cs;
static volatile LONG g_beat, g_watchdog_up;
static const char *volatile g_phase = "start";
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[400];
    DWORD done;
    int n;
    va_list a;

    va_start( a, fmt );
    n = _vsnprintf( b, sizeof(b) - 1, fmt, a );
    va_end( a );
    if (n < 0) n = sizeof(b) - 1;
    EnterCriticalSection( &g_cs );
    WriteFile( g_log, b, n, &done, NULL );
    FlushFileBuffers( g_log );
    LeaveCriticalSection( &g_cs );
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

    InterlockedExchange( &g_watchdog_up, 1 );
    for (;;)
    {
        LONG now;
        Sleep( 1000 );
        now = g_beat;
        if (now != last) { last = now; idle = 0; continue; }
        if (++idle < 20) continue;
        say( "FAIL HANG no progress for 20 s in: %s\n", g_phase );
        say( "DONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
        TerminateProcess( GetCurrentProcess(), 2 );
    }
    return 0;
}

static double now_ns( void )
{
#ifdef NATIVE
    struct timespec t;
    clock_gettime( CLOCK_MONOTONIC, &t );
    return t.tv_sec * 1e9 + t.tv_nsec;
#else
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return c.QuadPart * 1e9 / f.QuadPart;
#endif
}

#define FN(x) static PFN_##x p##x;
#define INSTANCE_FUNCS(F) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) \
    F(vkGetPhysicalDeviceQueueFamilyProperties) F(vkGetPhysicalDeviceMemoryProperties) F(vkCreateDevice) F(vkGetDeviceProcAddr)
#define DEVICE_FUNCS(F) F(vkGetDeviceQueue) F(vkAllocateMemory) F(vkCreateImage) F(vkGetImageMemoryRequirements) \
    F(vkBindImageMemory) F(vkCreateImageView) F(vkCreateRenderPass) F(vkCreateFramebuffer) F(vkCreateShaderModule) \
    F(vkCreatePipelineLayout) F(vkCreateGraphicsPipelines) F(vkCreateCommandPool) F(vkAllocateCommandBuffers) \
    F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkResetCommandBuffer) F(vkCmdBeginRenderPass) F(vkCmdEndRenderPass) \
    F(vkCmdBindPipeline) F(vkCmdDraw) F(vkCmdDrawIndexed) F(vkCmdCopyImageToBuffer) F(vkCmdPipelineBarrier) \
    F(vkCmdPushConstants) F(vkCmdBindDescriptorSets) F(vkCmdBindVertexBuffers) F(vkCmdBindIndexBuffer) \
    F(vkCmdSetViewport) F(vkCmdSetScissor) F(vkCmdSetLineWidth) F(vkCreateFence) F(vkResetFences) F(vkWaitForFences) F(vkQueueSubmit) \
    F(vkCreateBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkMapMemory) \
    F(vkCreateDescriptorSetLayout) F(vkCreateDescriptorPool) F(vkAllocateDescriptorSets) F(vkUpdateDescriptorSets)
INSTANCE_FUNCS(FN)
DEVICE_FUNCS(FN)

static VkPhysicalDeviceMemoryProperties g_mem;
static VkDevice g_dev;

static uint32_t mem_type( uint32_t bits, VkMemoryPropertyFlags want )
{
    uint32_t i;
    for (i = 0; i < g_mem.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
    must( VK_ERROR_FEATURE_NOT_PRESENT, "memory type" );
    return 0;
}

static VkBuffer host_buffer( VkDeviceSize size, VkBufferUsageFlags usage, void **map )
{
    VkBufferCreateInfo c = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
    VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    VkMemoryRequirements r;
    VkDeviceMemory m;
    VkBuffer b;

    c.size = size; c.usage = usage;
    must( pvkCreateBuffer( g_dev, &c, NULL, &b ), "create buffer" );
    pvkGetBufferMemoryRequirements( g_dev, b, &r );
    a.allocationSize = r.size;
    a.memoryTypeIndex = mem_type( r.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
    must( pvkAllocateMemory( g_dev, &a, NULL, &m ), "buffer memory" );
    must( pvkBindBufferMemory( g_dev, b, m, 0 ), "bind buffer" );
    must( pvkMapMemory( g_dev, m, 0, VK_WHOLE_SIZE, 0, map ), "map buffer" );
    return b;
}

static VkShaderModule shader( const uint32_t *code, size_t size )
{
    VkShaderModuleCreateInfo c = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule m;
    c.codeSize = size; c.pCode = code;
    must( pvkCreateShaderModule( g_dev, &c, NULL, &m ), "create shader" );
    return m;
}

static VkPipeline pipeline( VkRenderPass rp, VkPipelineLayout layout, VkShaderModule vs, VkShaderModule fs )
{
    static const VkDynamicState dyn[3] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_LINE_WIDTH };
    VkPipelineShaderStageCreateInfo st[2] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main" } };
    VkVertexInputBindingDescription vb = { 0, 8, VK_VERTEX_INPUT_RATE_VERTEX };
    VkVertexInputAttributeDescription va = { 0, 0, VK_FORMAT_R32G32_SFLOAT, 0 };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkPipelineViewportStateCreateInfo v = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo r = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo m = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState a = { 0 };
    VkPipelineColorBlendStateCreateInfo b = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkPipelineDynamicStateCreateInfo d = { VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO };
    VkGraphicsPipelineCreateInfo c = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipeline p;

    vi.vertexBindingDescriptionCount = 1; vi.pVertexBindingDescriptions = &vb;
    vi.vertexAttributeDescriptionCount = 1; vi.pVertexAttributeDescriptions = &va;
    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    v.viewportCount = 1; v.scissorCount = 1;
    r.polygonMode = VK_POLYGON_MODE_FILL; r.cullMode = VK_CULL_MODE_NONE; r.lineWidth = 1;
    m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    a.colorWriteMask = 15;
    b.attachmentCount = 1; b.pAttachments = &a;
    d.dynamicStateCount = 3; d.pDynamicStates = dyn;
    c.stageCount = 2; c.pStages = st; c.pVertexInputState = &vi; c.pInputAssemblyState = &ia; c.pViewportState = &v;
    c.pRasterizationState = &r; c.pMultisampleState = &m; c.pColorBlendState = &b; c.pDynamicState = &d;
    c.layout = layout; c.renderPass = rp;
    must( pvkCreateGraphicsPipelines( g_dev, VK_NULL_HANDLE, 1, &c, NULL, &p ), "create pipeline" );
    return p;
}

enum { S_NOOP, S_SAME, S_PUSH, S_VB, S_DESC, S_DYN, S_PIPE, S_GAME, S_COUNT };
static const char *const names[S_COUNT] = {
    "near-empty call (no draw)", "same state", "push constants", "vertex buffer + indexed", "uniform offset", "viewport + scissor",
    "pipeline switch", "game-like mix" };

struct bench
{
    VkCommandBuffer cmd;
    VkPipeline pipes[2];
    VkPipelineLayout layout;
    VkDescriptorSet set;
    VkBuffer vb, ib;
    uint32_t ubo_step;
};

static void push( struct bench *b, float scale, float x )
{
    float pc[4] = { scale, x, 0, 0 };
    pvkCmdPushConstants( b->cmd, b->layout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pc), pc );
}

static void record( struct bench *b, int sc )
{
    const VkViewport vp = { 0, 0, SIZE, SIZE, 0, 1 };
    const VkRect2D sr = { { 0, 0 }, { SIZE, SIZE } };
    VkDeviceSize off = 0;
    uint32_t dyn = 0;
    int i;

    pvkCmdSetViewport( b->cmd, 0, 1, &vp );
    pvkCmdSetScissor( b->cmd, 0, 1, &sr );
    pvkCmdSetLineWidth( b->cmd, 1.0f );
    pvkCmdBindPipeline( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipes[0] );
    pvkCmdBindDescriptorSets( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->layout, 0, 1, &b->set, 1, &dyn );
    pvkCmdBindVertexBuffers( b->cmd, 0, 1, &b->vb, &off );
    pvkCmdBindIndexBuffer( b->cmd, b->ib, 0, VK_INDEX_TYPE_UINT16 );
    push( b, 0.01f, 0 );

    for (i = 0; i < DRAWS; i++)
    {
        switch (sc)
        {
        case S_NOOP:
            pvkCmdSetLineWidth( b->cmd, 1.0f );
            break;
        case S_SAME:
            pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
            break;
        case S_PUSH:
            push( b, 0.01f, (i & 255) / 256.0f );
            pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
            break;
        case S_VB:
            off = (i & 255) * 24;
            pvkCmdBindVertexBuffers( b->cmd, 0, 1, &b->vb, &off );
            pvkCmdDrawIndexed( b->cmd, 3, 1, 0, 0, 0 );
            break;
        case S_DESC:
            dyn = (i & 255) * b->ubo_step;
            pvkCmdBindDescriptorSets( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->layout, 0, 1, &b->set, 1, &dyn );
            pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
            break;
        case S_DYN:
        {
            VkRect2D s = { { i & 7, 0 }, { SIZE - 8, SIZE } };
            pvkCmdSetViewport( b->cmd, 0, 1, &vp );
            pvkCmdSetScissor( b->cmd, 0, 1, &s );
            pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
            break;
        }
        case S_PIPE:
            pvkCmdBindPipeline( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipes[i & 1] );
            pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
            break;
        case S_GAME:
            if (!(i & 7)) pvkCmdBindPipeline( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipes[(i >> 3) & 1] );
            dyn = (i & 255) * b->ubo_step;
            pvkCmdBindDescriptorSets( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->layout, 0, 1, &b->set, 1, &dyn );
            off = (i & 255) * 24;
            pvkCmdBindVertexBuffers( b->cmd, 0, 1, &b->vb, &off );
            push( b, 0.01f, (i & 255) / 256.0f );
            pvkCmdDrawIndexed( b->cmd, 3, 1, 0, 0, 0 );
            break;
        }
        if (!(i & 1023)) g_beat++;
    }

    /* the picture the check reads: pipeline A, color entry 0, full screen */
    off = 0; dyn = 0;
    pvkCmdSetScissor( b->cmd, 0, 1, &sr );
    pvkCmdBindPipeline( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->pipes[0] );
    pvkCmdBindDescriptorSets( b->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, b->layout, 0, 1, &b->set, 1, &dyn );
    pvkCmdBindVertexBuffers( b->cmd, 0, 1, &b->vb, &off );
    push( b, 1.0f, 0 );
    pvkCmdDraw( b->cmd, 3, 1, 0, 0 );
}

int main( void )
{
    HMODULE lib;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create_instance;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkPhysicalDeviceProperties props;
    VkQueueFamilyProperties qf[8];
    uint32_t count = 1, nqf = 8, family = ~0u, i;
    VkQueue queue;
    struct bench b;
    VkImage image; VkImageView view; VkRenderPass rp; VkFramebuffer fb; VkBuffer readback;
    VkCommandPool pool; VkFence fence;
    void *vb_map, *ib_map, *ubo_map, *rb_map;
    VkBuffer ubo;

    InitializeCriticalSection( &g_cs );
    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    for (i = 0; i < 2000 && !g_watchdog_up; i++) Sleep( 1 );
    say( "INFO drawbench 1, %d draws per scenario, best of %d\n", DRAWS, ROUNDS );
    CreateWindowExA( 0, "STATIC", "drawbench", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );

    g_phase = "create device";
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
#define LOADI(x) p##x = (PFN_##x)gipa( inst, #x );
    INSTANCE_FUNCS(LOADI)
    pvkEnumeratePhysicalDevices( inst, &count, &pd );
    pvkGetPhysicalDeviceProperties( pd, &props );
    pvkGetPhysicalDeviceMemoryProperties( pd, &g_mem );
    pvkGetPhysicalDeviceQueueFamilyProperties( pd, &nqf, qf );
    for (i = 0; i < nqf; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    say( "INFO device %s\n", props.deviceName );
    {
        float prio = 1;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo c = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &prio;
        c.queueCreateInfoCount = 1; c.pQueueCreateInfos = &q;
        must( pvkCreateDevice( pd, &c, NULL, &g_dev ), "create device" );
    }
#define LOADD(x) p##x = (PFN_##x)pvkGetDeviceProcAddr( g_dev, #x );
    DEVICE_FUNCS(LOADD)
    pvkGetDeviceQueue( g_dev, family, 0, &queue );

    g_phase = "create resources";
    {
        VkImageCreateInfo c = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        VkImageViewCreateInfo vc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        VkMemoryRequirements r;
        VkDeviceMemory m;

        c.imageType = VK_IMAGE_TYPE_2D; c.format = VK_FORMAT_R8G8B8A8_UNORM; c.extent.width = SIZE; c.extent.height = SIZE;
        c.extent.depth = 1; c.mipLevels = 1; c.arrayLayers = 1; c.samples = VK_SAMPLE_COUNT_1_BIT;
        c.tiling = VK_IMAGE_TILING_OPTIMAL; c.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        must( pvkCreateImage( g_dev, &c, NULL, &image ), "create image" );
        pvkGetImageMemoryRequirements( g_dev, image, &r );
        a.allocationSize = r.size;
        a.memoryTypeIndex = mem_type( r.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
        must( pvkAllocateMemory( g_dev, &a, NULL, &m ), "image memory" );
        must( pvkBindImageMemory( g_dev, image, m, 0 ), "bind image" );
        vc.image = image; vc.viewType = VK_IMAGE_VIEW_TYPE_2D; vc.format = c.format;
        vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vc.subresourceRange.levelCount = 1;
        vc.subresourceRange.layerCount = 1;
        must( pvkCreateImageView( g_dev, &vc, NULL, &view ), "image view" );
    }
    readback = host_buffer( SIZE * SIZE * 4, VK_BUFFER_USAGE_TRANSFER_DST_BIT, &rb_map );
    b.vb = host_buffer( 256 * 24, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, &vb_map );
    b.ib = host_buffer( 64, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &ib_map );
    b.ubo_step = (uint32_t)props.limits.minUniformBufferOffsetAlignment;
    if (b.ubo_step < 16) b.ubo_step = 16;
    ubo = host_buffer( 256 * b.ubo_step, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, &ubo_map );
    {
        static const float tri[6] = { -1, -1, 3, -1, -1, 3 };
        static const uint16_t idx[3] = { 0, 1, 2 };
        for (i = 0; i < 256; i++)
        {
            float *c = (float *)((char *)ubo_map + i * b.ubo_step);
            memcpy( (char *)vb_map + i * 24, tri, 24 );
            c[0] = 0.25f; c[1] = 0.5f; c[2] = 0.75f; c[3] = 1.0f;
        }
        memcpy( ib_map, idx, sizeof(idx) );
    }

    {
        VkAttachmentDescription at = { 0 };
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = { 0 };
        static const VkSubpassDependency dep = { 0, VK_SUBPASS_EXTERNAL, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0 };
        VkRenderPassCreateInfo c = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        VkFramebufferCreateInfo f = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        VkDescriptorSetLayoutBinding bind = { 0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_FRAGMENT_BIT };
        VkDescriptorSetLayoutCreateInfo dl = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
        VkDescriptorPoolSize ps = { VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1 };
        VkDescriptorPoolCreateInfo dp = { VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO };
        VkDescriptorSetAllocateInfo da = { VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO };
        VkDescriptorBufferInfo bi = { ubo, 0, 16 };
        VkWriteDescriptorSet w = { VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET };
        VkPushConstantRange pr = { VK_SHADER_STAGE_VERTEX_BIT, 0, 16 };
        VkPipelineLayoutCreateInfo l = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkDescriptorSetLayout dsl;
        VkDescriptorPool dpool;
        VkShaderModule vs, fa, fb2;

        at.format = VK_FORMAT_R8G8B8A8_UNORM; at.samples = VK_SAMPLE_COUNT_1_BIT; at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE; at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; at.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        c.attachmentCount = 1; c.pAttachments = &at; c.subpassCount = 1; c.pSubpasses = &sp;
        c.dependencyCount = 1; c.pDependencies = &dep;
        must( pvkCreateRenderPass( g_dev, &c, NULL, &rp ), "render pass" );
        f.renderPass = rp; f.attachmentCount = 1; f.pAttachments = &view; f.width = SIZE; f.height = SIZE; f.layers = 1;
        must( pvkCreateFramebuffer( g_dev, &f, NULL, &fb ), "framebuffer" );
        dl.bindingCount = 1; dl.pBindings = &bind;
        must( pvkCreateDescriptorSetLayout( g_dev, &dl, NULL, &dsl ), "set layout" );
        dp.maxSets = 1; dp.poolSizeCount = 1; dp.pPoolSizes = &ps;
        must( pvkCreateDescriptorPool( g_dev, &dp, NULL, &dpool ), "descriptor pool" );
        da.descriptorPool = dpool; da.descriptorSetCount = 1; da.pSetLayouts = &dsl;
        must( pvkAllocateDescriptorSets( g_dev, &da, &b.set ), "descriptor set" );
        w.dstSet = b.set; w.descriptorCount = 1; w.descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w.pBufferInfo = &bi;
        pvkUpdateDescriptorSets( g_dev, 1, &w, 0, NULL );
        l.setLayoutCount = 1; l.pSetLayouts = &dsl; l.pushConstantRangeCount = 1; l.pPushConstantRanges = &pr;
        must( pvkCreatePipelineLayout( g_dev, &l, NULL, &b.layout ), "pipeline layout" );
        vs = shader( vs_code, sizeof(vs_code) ); fa = shader( fs_a_code, sizeof(fs_a_code) ); fb2 = shader( fs_b_code, sizeof(fs_b_code) );
        b.pipes[0] = pipeline( rp, b.layout, vs, fa );
        b.pipes[1] = pipeline( rp, b.layout, vs, fb2 );
    }
    {
        VkCommandPoolCreateInfo c = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo a = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo f = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        c.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; c.queueFamilyIndex = family;
        must( pvkCreateCommandPool( g_dev, &c, NULL, &pool ), "command pool" );
        a.commandPool = pool; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1;
        must( pvkAllocateCommandBuffers( g_dev, &a, &b.cmd ), "command buffer" );
        must( pvkCreateFence( g_dev, &f, NULL, &fence ), "fence" );
    }

    say( "INFO %-26s %10s %10s %10s\n", "scenario", "ns each", "end us", "submit+GPU ms" );
    {
        int sc, round;
        for (sc = 0; sc < S_COUNT; sc++)
        {
            double best_rec = 1e18, best_end = 1e18, best_sub = 1e18;
            const volatile uint8_t *px = (const uint8_t *)rb_map + (SIZE / 2 * SIZE + SIZE / 2) * 4;
            int ok = 1;

            g_phase = names[sc];
            for (round = 0; round < ROUNDS; round++)
            {
                VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
                VkClearValue clear = { .color = { .float32 = { 0, 0, 0, 0 } } };
                VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
                VkBufferImageCopy copy = { 0 };
                VkMemoryBarrier host = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
                VkSubmitInfo s = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
                double t0, t1, t2, t3;

                must( pvkResetCommandBuffer( b.cmd, 0 ), "reset command buffer" );
                must( pvkBeginCommandBuffer( b.cmd, &bi ), "begin command buffer" );
                rb.renderPass = rp; rb.framebuffer = fb; rb.renderArea.extent.width = SIZE; rb.renderArea.extent.height = SIZE;
                rb.clearValueCount = 1; rb.pClearValues = &clear;
                pvkCmdBeginRenderPass( b.cmd, &rb, VK_SUBPASS_CONTENTS_INLINE );
                t0 = now_ns();
                record( &b, sc );
                t1 = now_ns();
                pvkCmdEndRenderPass( b.cmd );
                copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount = 1;
                copy.imageExtent.width = SIZE; copy.imageExtent.height = SIZE; copy.imageExtent.depth = 1;
                pvkCmdCopyImageToBuffer( b.cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, readback, 1, &copy );
                pvkCmdPipelineBarrier( b.cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL );
                must( pvkEndCommandBuffer( b.cmd ), "end command buffer" );
                t2 = now_ns();
                must( pvkResetFences( g_dev, 1, &fence ), "reset fence" );
                s.commandBufferCount = 1; s.pCommandBuffers = &b.cmd;
                must( pvkQueueSubmit( queue, 1, &s, fence ), "queue submit" );
                must( pvkWaitForFences( g_dev, 1, &fence, VK_TRUE, 10000000000ull ), "wait for GPU" );
                t3 = now_ns();
                if (t1 - t0 < best_rec) best_rec = t1 - t0;
                if (t2 - t1 < best_end) best_end = t2 - t1;
                if (t3 - t2 < best_sub) best_sub = t3 - t2;
                if (abs( px[0] - 64 ) > 2 || abs( px[1] - 128 ) > 2 || abs( px[2] - 191 ) > 2 || px[3] < 253) ok = 0;
            }
            say( "INFO %-26s %10.0f %10.0f %10.2f\n", names[sc], best_rec / DRAWS, best_end / 1000, best_sub / 1e6 );
            if (ok) g_pass++; else { g_fail++; say( "FAIL %s: wrong picture, center %u,%u,%u,%u\n", names[sc], px[0], px[1], px[2], px[3] ); }
            g_beat++;
        }
    }
    finish( "ok" );
    return 0;
}
