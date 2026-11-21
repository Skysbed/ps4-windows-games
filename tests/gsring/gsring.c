/* gsring: reproduces a geometry-shader ring growing while GPU memory is busy.
 * Draws with pipeline A (smaller GS), then B (larger GS), then A and B again, and checks every picture.
 * Results: Z:\data\pcps4\games\gsring\result.txt, last line starts with DONE. */
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define RESULT "Z:\\data\\pcps4\\games\\gsring\\result.txt"
#define FILLER_MIB 900
#define FILLER_CHUNK_MIB 100
#define SIZE 64

static const uint32_t vs_code[] =
#include "vs.inc"
;
static const uint32_t gs_a_code[] =
#include "gs_a.inc"
;
static const uint32_t gs_b_code[] =
#include "gs_b.inc"
;
static const uint32_t fs_code[] =
#include "fs.inc"
;
static const uint32_t vs2_code[] =
#include "vs2.inc"
;
static const uint32_t gs_s_code[] =
#include "gs_s.inc"
;
static const uint32_t gs_v1_code[] =
#include "gs_v1.inc"
;
static const uint32_t gs_v2_code[] =
#include "gs_v2.inc"
;
static const uint32_t vs3_code[] =
#include "vs3.inc"
;
static const uint32_t gs_grid_code[] =
#include "gs_grid.inc"
;

static HANDLE g_log;
static CRITICAL_SECTION g_cs;
static volatile LONG g_beat, g_watchdog_up;
static const char *volatile g_phase = "start";
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[500];
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

static void check( const char *name, int ok, const char *detail )
{
    if (ok) g_pass++; else g_fail++;
    say( "%s %s  %s\n", ok ? "PASS" : "FAIL", name, detail ? detail : "" );
}

static void must( VkResult r, const char *what )
{
    char d[64];
    g_beat++;
    if (r == VK_SUCCESS) return;
    _snprintf( d, sizeof(d), "VkResult %d", r );
    check( what, 0, d );
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
        if (++idle < 45) continue;
        say( "FAIL HANG no progress for 45 s in: %s\n", g_phase );
        say( "DONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
        TerminateProcess( GetCurrentProcess(), 2 );
    }
    return 0;
}

#define IFN(x) static PFN_##x p##x;
#define DFN(x) static PFN_##x p##x;
#define INSTANCE_FUNCS(F) F(vkEnumeratePhysicalDevices) F(vkGetPhysicalDeviceProperties) F(vkGetPhysicalDeviceFeatures) \
    F(vkGetPhysicalDeviceQueueFamilyProperties) F(vkGetPhysicalDeviceMemoryProperties) F(vkCreateDevice) F(vkGetDeviceProcAddr)
#define DEVICE_FUNCS(F) F(vkGetDeviceQueue) F(vkAllocateMemory) F(vkCreateImage) F(vkGetImageMemoryRequirements) \
    F(vkBindImageMemory) F(vkCreateImageView) F(vkCreateRenderPass) F(vkCreateFramebuffer) F(vkCreateShaderModule) \
    F(vkCreatePipelineLayout) F(vkCreateGraphicsPipelines) F(vkCreateCommandPool) F(vkAllocateCommandBuffers) \
    F(vkBeginCommandBuffer) F(vkEndCommandBuffer) F(vkResetCommandBuffer) F(vkCmdBeginRenderPass) F(vkCmdEndRenderPass) \
    F(vkCmdBindPipeline) F(vkCmdDraw) F(vkCmdCopyImageToBuffer) F(vkCmdPipelineBarrier) F(vkCreateQueryPool) F(vkCmdResetQueryPool) F(vkCmdBeginQuery) F(vkCmdEndQuery) F(vkGetQueryPoolResults) F(vkCreateFence) F(vkResetFences) F(vkWaitForFences) \
    F(vkQueueSubmit) F(vkQueueWaitIdle) F(vkCreateBuffer) F(vkGetBufferMemoryRequirements) F(vkBindBufferMemory) F(vkMapMemory)
INSTANCE_FUNCS(IFN)
DEVICE_FUNCS(DFN)

static VkPhysicalDeviceMemoryProperties g_mem;
static VkDevice g_dev;

static uint32_t mem_type( uint32_t bits, VkMemoryPropertyFlags want )
{
    uint32_t i;
    for (i = 0; i < g_mem.memoryTypeCount; i++)
        if ((bits & (1u << i)) && (g_mem.memoryTypes[i].propertyFlags & want) == want) return i;
    check( "memory type", 0, "none suitable" );
    finish( "error" );
    return 0;
}

static VkShaderModule shader( const uint32_t *code, size_t size )
{
    VkShaderModuleCreateInfo c = { VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO };
    VkShaderModule m;
    c.codeSize = size;
    c.pCode = code;
    must( pvkCreateShaderModule( g_dev, &c, NULL, &m ), "create shader" );
    return m;
}

static VkPipeline pipeline( VkRenderPass rp, VkPipelineLayout layout, VkShaderModule vs, VkShaderModule gs, VkShaderModule fs )
{
    VkPipelineShaderStageCreateInfo st[3] = {
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_VERTEX_BIT, vs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_GEOMETRY_BIT, gs, "main" },
        { VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO, NULL, 0, VK_SHADER_STAGE_FRAGMENT_BIT, fs, "main" } };
    VkPipelineVertexInputStateCreateInfo vi = { VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO };
    VkPipelineInputAssemblyStateCreateInfo ia = { VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO };
    VkViewport vp = { 0, 0, SIZE, SIZE, 0, 1 };
    VkRect2D sc = { { 0, 0 }, { SIZE, SIZE } };
    VkPipelineViewportStateCreateInfo v = { VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO };
    VkPipelineRasterizationStateCreateInfo r = { VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO };
    VkPipelineMultisampleStateCreateInfo m = { VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO };
    VkPipelineColorBlendAttachmentState a = { 0 };
    VkPipelineColorBlendStateCreateInfo b = { VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO };
    VkGraphicsPipelineCreateInfo c = { VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO };
    VkPipeline p;

    ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    v.viewportCount = 1; v.pViewports = &vp; v.scissorCount = 1; v.pScissors = &sc;
    r.polygonMode = VK_POLYGON_MODE_FILL; r.cullMode = VK_CULL_MODE_NONE; r.lineWidth = 1;
    m.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    a.colorWriteMask = 15;
    b.attachmentCount = 1; b.pAttachments = &a;
    if (!gs) st[1] = st[2];
    c.stageCount = gs ? 3 : 2; c.pStages = st; c.pVertexInputState = &vi; c.pInputAssemblyState = &ia; c.pViewportState = &v;
    c.pRasterizationState = &r; c.pMultisampleState = &m; c.pColorBlendState = &b; c.layout = layout; c.renderPass = rp;
    must( pvkCreateGraphicsPipelines( g_dev, VK_NULL_HANDLE, 1, &c, NULL, &p ), "create pipeline" );
    return p;
}

int main( void )
{
    HMODULE lib;
    PFN_vkGetInstanceProcAddr gipa;
    PFN_vkCreateInstance create_instance;
    VkInstance inst;
    VkPhysicalDevice pd;
    VkPhysicalDeviceProperties props;
    VkPhysicalDeviceFeatures have, features = { 0 };
    VkQueueFamilyProperties qf[8];
    uint32_t count = 1, nqf = 8, family = ~0u, i;
    VkQueue queue;
    char d[200];

    InitializeCriticalSection( &g_cs );
    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    /* the watchdog must be running before anything takes the loader lock */
    for (i = 0; i < 2000 && !g_watchdog_up; i++) Sleep( 1 );
    say( "INFO gsring 2, watchdog %s\n", g_watchdog_up ? "up" : "NOT up" );

    /* a real game has a window before it touches the GPU */
    g_phase = "create window";
    {
        HWND w = CreateWindowExA( 0, "STATIC", "gsring", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );
        g_beat++;
        say( "INFO window %s\n", w ? "created" : "failed" );
    }

    g_phase = "load winevulkan";
    lib = LoadLibraryA( "winevulkan.dll" );
    g_beat++;
    say( "INFO winevulkan %s\n", lib ? "loaded" : "failed" );
    if (!lib) { check( "load winevulkan", 0, NULL ); finish( "error" ); }
    g_phase = "create instance";
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
    pvkGetPhysicalDeviceFeatures( pd, &have );
    pvkGetPhysicalDeviceMemoryProperties( pd, &g_mem );
    pvkGetPhysicalDeviceQueueFamilyProperties( pd, &nqf, qf );
    for (i = 0; i < nqf; i++) if (qf[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { family = i; break; }
    say( "INFO device %s, GS %u, max GS vertices %u, max GS total components %u\n", props.deviceName, have.geometryShader,
         props.limits.maxGeometryOutputVertices, props.limits.maxGeometryTotalOutputComponents );
    if (!have.geometryShader || family == ~0u) { check( "geometry shaders available", 0, NULL ); finish( "error" ); }

    g_phase = "create device";
    {
        float prio = 1;
        VkDeviceQueueCreateInfo q = { VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO };
        VkDeviceCreateInfo c = { VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO };
        q.queueFamilyIndex = family; q.queueCount = 1; q.pQueuePriorities = &prio;
        features.geometryShader = VK_TRUE; features.pipelineStatisticsQuery = have.pipelineStatisticsQuery;
        c.queueCreateInfoCount = 1; c.pQueueCreateInfos = &q; c.pEnabledFeatures = &features;
        must( pvkCreateDevice( pd, &c, NULL, &g_dev ), "create device" );
    }
#define LOADD(x) p##x = (PFN_##x)pvkGetDeviceProcAddr( g_dev, #x );
    DEVICE_FUNCS(LOADD)
    pvkGetDeviceQueue( g_dev, family, 0, &queue );

    /* the picture and a buffer to read it back */
    g_phase = "create targets";
    VkImage image; VkImageView view; VkBuffer buffer; VkDeviceMemory mem; VkMemoryRequirements req; volatile uint8_t *pixels;
    {
        VkImageCreateInfo c = { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
        VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
        VkImageViewCreateInfo vc = { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
        VkBufferCreateInfo bc = { VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO };
        void *map;

        c.imageType = VK_IMAGE_TYPE_2D; c.format = VK_FORMAT_R8G8B8A8_UNORM; c.extent.width = SIZE; c.extent.height = SIZE;
        c.extent.depth = 1; c.mipLevels = 1; c.arrayLayers = 1; c.samples = VK_SAMPLE_COUNT_1_BIT;
        c.tiling = VK_IMAGE_TILING_OPTIMAL; c.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        must( pvkCreateImage( g_dev, &c, NULL, &image ), "create image" );
        pvkGetImageMemoryRequirements( g_dev, image, &req );
        a.allocationSize = req.size;
        a.memoryTypeIndex = mem_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT );
        must( pvkAllocateMemory( g_dev, &a, NULL, &mem ), "image memory" );
        must( pvkBindImageMemory( g_dev, image, mem, 0 ), "bind image" );
        vc.image = image; vc.viewType = VK_IMAGE_VIEW_TYPE_2D; vc.format = c.format;
        vc.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; vc.subresourceRange.levelCount = 1;
        vc.subresourceRange.layerCount = 1;
        must( pvkCreateImageView( g_dev, &vc, NULL, &view ), "image view" );
        bc.size = SIZE * SIZE * 4; bc.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        must( pvkCreateBuffer( g_dev, &bc, NULL, &buffer ), "create buffer" );
        pvkGetBufferMemoryRequirements( g_dev, buffer, &req );
        a.allocationSize = req.size;
        a.memoryTypeIndex = mem_type( req.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT );
        must( pvkAllocateMemory( g_dev, &a, NULL, &mem ), "buffer memory" );
        must( pvkBindBufferMemory( g_dev, buffer, mem, 0 ), "bind buffer" );
        must( pvkMapMemory( g_dev, mem, 0, VK_WHOLE_SIZE, 0, &map ), "map buffer" );
        pixels = map;
    }

    g_phase = "create pipelines";
    VkRenderPass rp; VkFramebuffer fb; VkPipelineLayout layout; VkPipeline pipes[7];
    {
        VkAttachmentDescription at = { 0 };
        VkAttachmentReference ref = { 0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL };
        VkSubpassDescription sp = { 0 };
        VkRenderPassCreateInfo c = { VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO };
        VkFramebufferCreateInfo f = { VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO };
        VkPipelineLayoutCreateInfo l = { VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO };
        VkShaderModule vs, vs2, vs3, gsa, gsb, gss, gv1, gv2, ggrid, fs;

        at.format = VK_FORMAT_R8G8B8A8_UNORM; at.samples = VK_SAMPLE_COUNT_1_BIT; at.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        at.storeOp = VK_ATTACHMENT_STORE_OP_STORE; at.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        at.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; at.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        at.finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sp.colorAttachmentCount = 1; sp.pColorAttachments = &ref;
        c.attachmentCount = 1; c.pAttachments = &at; c.subpassCount = 1; c.pSubpasses = &sp;
        {
            /* the copy that follows must see what the render pass wrote */
            static const VkSubpassDependency dep = { 0, VK_SUBPASS_EXTERNAL,
                VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT, VK_ACCESS_TRANSFER_READ_BIT, 0 };
            c.dependencyCount = 1; c.pDependencies = &dep;
        }
        must( pvkCreateRenderPass( g_dev, &c, NULL, &rp ), "render pass" );
        f.renderPass = rp; f.attachmentCount = 1; f.pAttachments = &view; f.width = SIZE; f.height = SIZE; f.layers = 1;
        must( pvkCreateFramebuffer( g_dev, &f, NULL, &fb ), "framebuffer" );
        must( pvkCreatePipelineLayout( g_dev, &l, NULL, &layout ), "pipeline layout" );
        vs = shader( vs_code, sizeof(vs_code) ); gsa = shader( gs_a_code, sizeof(gs_a_code) );
        gsb = shader( gs_b_code, sizeof(gs_b_code) ); fs = shader( fs_code, sizeof(fs_code) );
        vs2 = shader( vs2_code, sizeof(vs2_code) ); gss = shader( gs_s_code, sizeof(gs_s_code) );
        gv1 = shader( gs_v1_code, sizeof(gs_v1_code) ); gv2 = shader( gs_v2_code, sizeof(gs_v2_code) );
        vs3 = shader( vs3_code, sizeof(vs3_code) ); ggrid = shader( gs_grid_code, sizeof(gs_grid_code) );
        pipes[0] = pipeline( rp, layout, vs, gsa, fs );
        pipes[1] = pipeline( rp, layout, vs, gsb, fs );
        pipes[2] = pipeline( rp, layout, vs2, VK_NULL_HANDLE, fs );
        pipes[3] = pipeline( rp, layout, vs, gss, fs );
        pipes[4] = pipeline( rp, layout, vs, gv1, fs );
        pipes[5] = pipeline( rp, layout, vs, gv2, fs );
        pipes[6] = pipeline( rp, layout, vs3, ggrid, fs );
    }

    /* keep GPU memory busy the way a real game does */
    g_phase = "filling GPU memory";
    {
        VkPhysicalDeviceMemoryProperties *m = &g_mem;
        uint32_t type = 0, got = 0;
        for (i = 0; i < m->memoryTypeCount; i++)
            if (m->memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT) { type = i; break; }
        char fb[16];
        int filler_mib = GetEnvironmentVariableA( "GSRING_FILLER", fb, sizeof(fb) ) ? atoi( fb ) : FILLER_MIB;
        for (i = 0; i < (uint32_t)filler_mib / FILLER_CHUNK_MIB; i++)
        {
            VkMemoryAllocateInfo a = { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
            VkDeviceMemory filler;
            a.allocationSize = (VkDeviceSize)FILLER_CHUNK_MIB << 20;
            a.memoryTypeIndex = type;
            if (pvkAllocateMemory( g_dev, &a, NULL, &filler ) != VK_SUCCESS) break;
            got += FILLER_CHUNK_MIB;
            g_beat++;
        }
        say( "INFO holding %u MiB of GPU memory\n", got );
    }

    VkCommandPool pool; VkCommandBuffer cmd; VkFence fence; VkQueryPool qp = VK_NULL_HANDLE;
    if (have.pipelineStatisticsQuery)
    {
        VkQueryPoolCreateInfo q = { VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO };
        q.queryType = VK_QUERY_TYPE_PIPELINE_STATISTICS; q.queryCount = 1;
        q.pipelineStatistics = VK_QUERY_PIPELINE_STATISTIC_INPUT_ASSEMBLY_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_VERTEX_SHADER_INVOCATIONS_BIT |
            VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_GEOMETRY_SHADER_PRIMITIVES_BIT |
            VK_QUERY_PIPELINE_STATISTIC_CLIPPING_INVOCATIONS_BIT | VK_QUERY_PIPELINE_STATISTIC_CLIPPING_PRIMITIVES_BIT | VK_QUERY_PIPELINE_STATISTIC_FRAGMENT_SHADER_INVOCATIONS_BIT;
        must( pvkCreateQueryPool( g_dev, &q, NULL, &qp ), "query pool" );
    }
    {
        VkCommandPoolCreateInfo c = { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
        VkCommandBufferAllocateInfo a = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
        VkFenceCreateInfo f = { VK_STRUCTURE_TYPE_FENCE_CREATE_INFO };
        c.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; c.queueFamilyIndex = family;
        must( pvkCreateCommandPool( g_dev, &c, NULL, &pool ), "command pool" );
        a.commandPool = pool; a.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; a.commandBufferCount = 1;
        must( pvkAllocateCommandBuffers( g_dev, &a, &cmd ), "command buffer" );
        must( pvkCreateFence( g_dev, &f, NULL, &fence ), "fence" );
    }

    {
        static const int order[10] = { 2, 4, 5, 3, 0, 1, 0, 1, 6, 6 };
        static const char *const label[7] = { "GS A (51 MB ring)", "GS B (63 MB ring)", "control, no GS", "small GS", "GS ignoring its input", "GS reading positions", "every pixel unique" };
        static const int want_blue[7] = { 64, 128, 64, 64, 64, 64, 64 };
        int step;
        char ib[16];
        uint32_t instances = GetEnvironmentVariableA( "GSRING_STRESS", ib, sizeof(ib) ) ? (uint32_t)atoi( ib ) : 1;

        for (step = 0; step < 10; step++)
        {
            VkCommandBufferBeginInfo b = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
            VkClearValue clear = { .color = { .float32 = { 0.2f, 0.4f, 0.6f, 1.0f } } };
            VkRenderPassBeginInfo rb = { VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO };
            VkBufferImageCopy copy = { 0 };
            VkSubmitInfo s = { VK_STRUCTURE_TYPE_SUBMIT_INFO };
            VkResult r;
            int p = order[step], ok;
            const volatile uint8_t *px;
            char name[80];

            _snprintf( name, sizeof(name), "draw %d: %s", step + 1, label[p] );
            g_phase = name;
            must( pvkResetCommandBuffer( cmd, 0 ), "reset command buffer" );
            must( pvkBeginCommandBuffer( cmd, &b ), "begin command buffer" );
            rb.renderPass = rp; rb.framebuffer = fb; rb.renderArea.extent.width = SIZE; rb.renderArea.extent.height = SIZE;
            rb.clearValueCount = 1; rb.pClearValues = &clear;
            if (qp) pvkCmdResetQueryPool( cmd, qp, 0, 1 );
            pvkCmdBeginRenderPass( cmd, &rb, VK_SUBPASS_CONTENTS_INLINE );
            pvkCmdBindPipeline( cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipes[p] );
            if (qp) pvkCmdBeginQuery( cmd, qp, 0, 0 );
            pvkCmdDraw( cmd, 3, p == 6 ? (step == 8 ? 4096u : 102400u) : instances, 0, 0 );
            if (qp) pvkCmdEndQuery( cmd, qp, 0 );
            pvkCmdEndRenderPass( cmd );
            copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT; copy.imageSubresource.layerCount = 1;
            copy.imageExtent.width = SIZE; copy.imageExtent.height = SIZE; copy.imageExtent.depth = 1;
            pvkCmdCopyImageToBuffer( cmd, image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, buffer, 1, &copy );
            {
                VkMemoryBarrier host = { VK_STRUCTURE_TYPE_MEMORY_BARRIER, NULL, VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_HOST_READ_BIT };
                pvkCmdPipelineBarrier( cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &host, 0, NULL, 0, NULL );
            }
            must( pvkEndCommandBuffer( cmd ), "end command buffer" );
            must( pvkResetFences( g_dev, 1, &fence ), "reset fence" );
            s.commandBufferCount = 1; s.pCommandBuffers = &cmd;
            r = pvkQueueSubmit( queue, 1, &s, fence );
            g_beat++;
            if (r != VK_SUCCESS)
            {
                _snprintf( d, sizeof(d), "vkQueueSubmit returned %d%s", r, r == VK_ERROR_DEVICE_LOST ? " (DEVICE_LOST)" : "" );
                check( name, 0, d );
                finish( "error" );
            }
            r = pvkWaitForFences( g_dev, 1, &fence, VK_TRUE, 8000000000ull );
            g_beat++;
            if (r != VK_SUCCESS)
            {
                _snprintf( d, sizeof(d), "vkWaitForFences returned %d", r );
                check( name, 0, d );
                finish( "error" );
            }
            pvkQueueWaitIdle( queue );
            g_beat++;
            if (qp)
            {
                uint64_t v[7] = { 0 };
                VkResult qr = pvkGetQueryPoolResults( g_dev, qp, 0, 1, sizeof(v), v, sizeof(v), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT );
                g_beat++;
                say( "INFO   stages (%d): IA prims %I64u, VS %I64u, GS runs %I64u, GS prims %I64u, clip in %I64u, clip out %I64u, FS %I64u\n",
                     qr, v[0], v[1], v[2], v[3], v[4], v[5], v[6] );
            }
            if (p == 6)
            {
                int x, y, bad = 0, first_x = -1, first_y = -1;
                for (y = 0; y < SIZE; y++)
                    for (x = 0; x < SIZE; x++)
                    {
                        const volatile uint8_t *q = pixels + (y * SIZE + x) * 4;
                        int wr = (int)((x + 0.5) * 255.0 / SIZE + 0.5), wg = (int)((y + 0.5) * 255.0 / SIZE + 0.5);
                        if (abs( q[0] - wr ) > 2 || abs( q[1] - wg ) > 2 || abs( q[2] - 64 ) > 2 || q[3] < 253)
                        {
                            if (!bad) { first_x = x; first_y = y; }
                            bad++;
                        }
                    }
                _snprintf( d, sizeof(d), "%d of %d pixels wrong (%s triangles)%s", bad, SIZE * SIZE, step == 8 ? "8192" : "204800", bad ? "" : ", all correct" );
                if (bad) say( "INFO   first wrong pixel at %d,%d\n", first_x, first_y );
                check( name, !bad, d );
                continue;
            }
            px = pixels + (SIZE / 2 * SIZE + SIZE / 2) * 4;
            ok = abs( px[0] - 109 ) <= 3 && abs( px[2] - want_blue[p] ) <= 3 && px[3] >= 252;
            _snprintf( d, sizeof(d), "center pixel %u,%u,%u,%u want r~109 b~%d a~255 (clear would be 51,102,153,255)", px[0], px[1], px[2], px[3], want_blue[p] );
            check( name, ok, d );
        }
    }
    finish( "ok" );
    return 0;
}
