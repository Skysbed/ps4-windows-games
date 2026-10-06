#include "pcps4_gnm_renderer.h"

#if PCPS4_ENABLE_OPENGNM

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm_helpers.h>
#include <gnmdriver.h>

struct pcps4_gnm_renderer {
    uint32_t width;
    uint32_t height;

    GnmVideoOut video;

    GnmDirectMemory command_memory;
    size_t command_cursor;
    size_t command_memory_size;

    void *command_buffer_memory;
    size_t command_buffer_size;
    volatile uint64_t *eop_label;

    GnmRenderTarget render_targets[3];

    GnmVsShader *vertex_shader;
    GnmPsShader *pixel_shader;
};

static size_t align_up(size_t value, size_t alignment)
{
    size_t remainder;

    if (alignment == 0)
        return value;

    remainder = value % alignment;
    if (remainder == 0)
        return value;

    return value + alignment - remainder;
}

static void *gpu_alloc(pcps4_gnm_renderer_t *renderer,
                       size_t size,
                       size_t alignment)
{
    size_t start;

    if (renderer == NULL || size == 0 || alignment == 0)
        return NULL;

    start = align_up(renderer->command_cursor, alignment);

    if (start > renderer->command_memory_size ||
        size > renderer->command_memory_size - start)
        return NULL;

    renderer->command_cursor = start + size;

    return (uint8_t *)renderer->command_memory.mapped + start;
}

static int read_file(const char *path,
                     void **out_data,
                     size_t *out_size)
{
    int fd;
    struct stat st;
    uint8_t *data;
    size_t total = 0;

    if (path == NULL || out_data == NULL || out_size == NULL)
        return PCPS4_GNM_ERR_ARGUMENT;

    *out_data = NULL;
    *out_size = 0;

    fd = sceKernelOpen(path, O_RDONLY, 0);
    if (fd < 0)
        return PCPS4_GNM_ERR_IO;

    if (fstat(fd, &st) != 0 ||
        st.st_size <= 0 ||
        (uintmax_t)st.st_size > SIZE_MAX) {
        (void)sceKernelClose(fd);
        return PCPS4_GNM_ERR_IO;
    }

    data = (uint8_t *)malloc((size_t)st.st_size);
    if (data == NULL) {
        (void)sceKernelClose(fd);
        return PCPS4_GNM_ERR_MEMORY;
    }

    while (total < (size_t)st.st_size) {
        ssize_t n = (ssize_t)sceKernelRead(
            fd,
            data + total,
            (size_t)st.st_size - total);

        if (n <= 0) {
            free(data);
            (void)sceKernelClose(fd);
            return PCPS4_GNM_ERR_IO;
        }

        total += (size_t)n;
    }

    (void)sceKernelClose(fd);

    *out_data = data;
    *out_size = total;
    return PCPS4_GNM_OK;
}

static int load_vertex_shader(
    pcps4_gnm_renderer_t *renderer,
    const char *path)
{
    void *file_data = NULL;
    size_t file_size = 0;
    GnmShaderMetadata metadata;
    GnmError error;
    const GnmVsShader *source;
    uint32_t stage_size;
    uint32_t code_size;
    GnmVsShader *stage;
    void *code;

    if (read_file(path, &file_data, &file_size) != PCPS4_GNM_OK)
        return PCPS4_GNM_ERR_IO;

    memset(&metadata, 0, sizeof(metadata));

    error = sceGnmShaderBinaryGetMetadata(
        file_data,
        file_size,
        &metadata);

    if (error != GNM_ERROR_OK ||
        metadata.type != GNM_SHADER_VERTEX ||
        metadata.stage == NULL ||
        metadata.common == NULL ||
        metadata.shadercode == NULL) {
        free(file_data);
        return PCPS4_GNM_ERR_SHADER;
    }

    source = (const GnmVsShader *)metadata.stage;
    stage_size = sceGnmVsShaderCalcSize(source);
    code_size = sceGnmShaderCommonCodeSize(metadata.common);

    if (stage_size == 0 || code_size == 0) {
        free(file_data);
        return PCPS4_GNM_ERR_SHADER;
    }

    stage = (GnmVsShader *)malloc(stage_size);
    if (stage == NULL) {
        free(file_data);
        return PCPS4_GNM_ERR_MEMORY;
    }

    code = gpu_alloc(
        renderer,
        code_size,
        GNM_ALIGNMENT_SHADER_BYTES);

    if (code == NULL) {
        free(stage);
        free(file_data);
        return PCPS4_GNM_ERR_MEMORY;
    }

    memcpy(stage, source, stage_size);
    memcpy(code, metadata.shadercode, code_size);

    gnmVsRegsSetAddress(
        &stage->registers,
        code);

    renderer->vertex_shader = stage;

    free(file_data);
    return PCPS4_GNM_OK;
}

static int load_pixel_shader(
    pcps4_gnm_renderer_t *renderer,
    const char *path)
{
    void *file_data = NULL;
    size_t file_size = 0;
    GnmShaderMetadata metadata;
    GnmError error;
    const GnmPsShader *source;
    uint32_t stage_size;
    uint32_t code_size;
    GnmPsShader *stage;
    void *code;

    if (read_file(path, &file_data, &file_size) != PCPS4_GNM_OK)
        return PCPS4_GNM_ERR_IO;

    memset(&metadata, 0, sizeof(metadata));

    error = sceGnmShaderBinaryGetMetadata(
        file_data,
        file_size,
        &metadata);

    if (error != GNM_ERROR_OK ||
        metadata.type != GNM_SHADER_PIXEL ||
        metadata.stage == NULL ||
        metadata.common == NULL ||
        metadata.shadercode == NULL) {
        free(file_data);
        return PCPS4_GNM_ERR_SHADER;
    }

    source = (const GnmPsShader *)metadata.stage;
    stage_size = sceGnmPsShaderCalcSize(source);
    code_size = sceGnmShaderCommonCodeSize(metadata.common);

    if (stage_size == 0 || code_size == 0) {
        free(file_data);
        return PCPS4_GNM_ERR_SHADER;
    }

    stage = (GnmPsShader *)malloc(stage_size);
    if (stage == NULL) {
        free(file_data);
        return PCPS4_GNM_ERR_MEMORY;
    }

    code = gpu_alloc(
        renderer,
        code_size,
        GNM_ALIGNMENT_SHADER_BYTES);

    if (code == NULL) {
        free(stage);
        free(file_data);
        return PCPS4_GNM_ERR_MEMORY;
    }

    memcpy(stage, source, stage_size);
    memcpy(code, metadata.shadercode, code_size);

    gnmPsRegsSetAddress(
        &stage->registers,
        code);

    stage->registers.spibaryccntl = 0;

    renderer->pixel_shader = stage;

    free(file_data);
    return PCPS4_GNM_OK;
}

static int create_display(
    pcps4_gnm_renderer_t *renderer)
{
    GnmVideoOutCreateInfo video_info;
    GnmError error;

    memset(&renderer->video, 0, sizeof(renderer->video));
    renderer->video.handle = -1;

    sceGnmVideoOutInitDefaultCreateInfo(
        &video_info,
        renderer->width,
        renderer->height);

    video_info.numbuffers = 2;

    error = sceGnmVideoOutOpen(
        &renderer->video,
        &video_info);

    if (error != GNM_ERROR_OK)
        return PCPS4_GNM_ERR_VIDEO;

    for (uint32_t i = 0; i < renderer->video.numbuffers; ++i) {
        error = sceGnmRtCreateColorTarget(
            &renderer->render_targets[i],
            sceGnmVideoOutGetBuffer(
                &renderer->video,
                i),
            GNM_FMT_R8G8B8A8_SRGB,
            renderer->width,
            renderer->height,
            1,
            1,
            1,
            GNM_TM_DISPLAY_LINEAR_ALIGNED,
            gnmGpuMode(),
            NULL,
            NULL);

        if (error != GNM_ERROR_OK) {
            sceGnmVideoOutClose(&renderer->video);
            renderer->video.handle = -1;
            return PCPS4_GNM_ERR_VIDEO;
        }
    }

    return PCPS4_GNM_OK;
}

pcps4_gnm_renderer_t *pcps4_gnm_renderer_create(
    uint32_t width,
    uint32_t height)
{
    pcps4_gnm_renderer_t *renderer;
    const size_t memory_size = 16u * 1024u * 1024u;

    if (width < 320 || width > 3840 ||
        height < 240 || height > 2160)
        return NULL;

    renderer =
        (pcps4_gnm_renderer_t *)calloc(1, sizeof(*renderer));
    if (renderer == NULL)
        return NULL;

    renderer->width = width;
    renderer->height = height;

    if (sceGnmDirectMemoryAllocate(
            &renderer->command_memory,
            memory_size,
            GNM_VIDEO_OUT_MEMORY_ALIGNMENT,
            GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
            GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) {
        free(renderer);
        return NULL;
    }

    renderer->command_memory_size = memory_size;

    if (create_display(renderer) != PCPS4_GNM_OK) {
        sceGnmDirectMemoryRelease(&renderer->command_memory);
        free(renderer);
        return NULL;
    }

    renderer->command_buffer_size = 64u * 1024u;

    renderer->command_buffer_memory = gpu_alloc(
        renderer,
        renderer->command_buffer_size,
        GNM_ALIGNMENT_BUFFER_BYTES);

    renderer->eop_label = (volatile uint64_t *)gpu_alloc(
        renderer,
        sizeof(uint64_t),
        sizeof(uint64_t));

    if (renderer->command_buffer_memory == NULL ||
        renderer->eop_label == NULL) {
        sceGnmVideoOutClose(&renderer->video);
        renderer->video.handle = -1;
        sceGnmDirectMemoryRelease(&renderer->command_memory);
        free(renderer);
        return NULL;
    }

    *renderer->eop_label = 0;

    return renderer;
}

void pcps4_gnm_renderer_destroy(
    pcps4_gnm_renderer_t *renderer)
{
    if (renderer == NULL)
        return;

    if (renderer->video.handle >= 0) {
        sceGnmVideoOutClose(&renderer->video);
        renderer->video.handle = -1;
    }

    free(renderer->vertex_shader);
    free(renderer->pixel_shader);

    sceGnmDirectMemoryRelease(&renderer->command_memory);
    free(renderer);
}

int pcps4_gnm_renderer_load_shaders(
    pcps4_gnm_renderer_t *renderer,
    const char *vertex_path,
    const char *pixel_path)
{
    int result;

    if (renderer == NULL ||
        vertex_path == NULL ||
        pixel_path == NULL)
        return PCPS4_GNM_ERR_ARGUMENT;

    result = load_vertex_shader(renderer, vertex_path);
    if (result != PCPS4_GNM_OK)
        return result;

    result = load_pixel_shader(renderer, pixel_path);
    if (result != PCPS4_GNM_OK)
        return result;

    return PCPS4_GNM_OK;
}

static int build_triangle_commands(
    pcps4_gnm_renderer_t *renderer,
    uint32_t buffer_index,
    uint64_t frame_id,
    GnmCommandBuffer *out_command)
{
    GnmCommandBuffer command;

    const GnmPrimitiveSetup primitive_setup = {
        .cullmode = GNM_CULL_NONE,
        .frontface = GNM_FACE_CCW,
        .frontmode = GNM_FILL_SOLID,
        .backmode = GNM_FILL_SOLID,
        .frontoffsetmode = false,
        .backoffsetmode = false,
        .vertexwindowoffsetenable = false,
        .provokemode = GNM_PROVOKINGVTX_FIRST,
        .perspectivecorrectiondisable = false
    };

    if (renderer == NULL ||
        out_command == NULL ||
        buffer_index >= renderer->video.numbuffers ||
        renderer->vertex_shader == NULL ||
        renderer->pixel_shader == NULL)
        return PCPS4_GNM_ERR_COMMAND;

    memset(
        renderer->command_buffer_memory,
        0,
        renderer->command_buffer_size);

    command = sceGnmCmdInit(
        renderer->command_buffer_memory,
        renderer->command_buffer_size,
        NULL,
        NULL);

    sceGnmDrawCmdInitDefaultHardwareState(&command);

    sceGnmDrawCmdSetPrimitiveSetup(
        &command,
        &primitive_setup);

    sceGnmDrawCmdSetRenderTarget(
        &command,
        0,
        &renderer->render_targets[buffer_index]);

    sceGnmDrawCmdSetRenderTargetMask(
        &command,
        0x1);

    {
        const GnmSetViewportInfo viewport = {
            .dmin = 0.0f,
            .dmax = 1.0f,
            .scale = {
                renderer->width * 0.5f,
                -(renderer->height * 0.5f),
                0.5f
            },
            .offset = {
                renderer->width * 0.5f,
                renderer->height * 0.5f,
                0.5f
            }
        };

        sceGnmDrawCmdSetViewport(
            &command,
            0,
            &viewport);
    }

    sceGnmDrawCmdSetScreenScissor(
        &command,
        0,
        0,
        renderer->width,
        renderer->height);

    sceGnmDrawCmdSetVsShader(
        &command,
        &renderer->vertex_shader->registers,
        0);

    sceGnmDrawCmdSetPsShader(
        &command,
        &renderer->pixel_shader->registers);

    sceGnmDrawCmdSetPsInputUsage(
        &command,
        sceGnmVsShaderExportSemanticTable(
            renderer->vertex_shader),
        renderer->vertex_shader->numexportsemantics,
        sceGnmPsShaderInputSemanticTable(
            renderer->pixel_shader),
        renderer->pixel_shader->numinputsemantics);

    sceGnmDrawCmdSetPrimitiveType(
        &command,
        GNM_PT_TRILIST);

    sceGnmDrawCmdDrawIndexAuto(
        &command,
        3);

    *renderer->eop_label = 0;

    sceGnmDrawCmdEventWriteEop(
        &command,
        GNM_CACHE_FLUSH_TS,
        (uint64_t)(uintptr_t)renderer->eop_label,
        GNM_DATA_SEL_SEND_DATA64,
        frame_id);

    size_t used =
        (size_t)(
            (uintptr_t)command.cmdptr -
            (uintptr_t)command.beginptr);

    if (used == 0 ||
        used > renderer->command_buffer_size)
        return PCPS4_GNM_ERR_COMMAND;

    *out_command = command;
    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_draw_triangle(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id)
{
    GnmCommandBuffer command;
    uint32_t buffer_index;
    void *dcb_addr;
    uint32_t dcb_size;

    if (renderer == NULL ||
        renderer->video.numbuffers < 2)
        return PCPS4_GNM_ERR_ARGUMENT;

    buffer_index =
        (uint32_t)(frame_id % renderer->video.numbuffers);

    if (build_triangle_commands(
            renderer,
            buffer_index,
            frame_id,
            &command) != PCPS4_GNM_OK)
        return PCPS4_GNM_ERR_COMMAND;

    dcb_addr = command.beginptr;
    dcb_size =
        (uint32_t)(
            (uintptr_t)command.cmdptr -
            (uintptr_t)command.beginptr);

    if (sceGnmSubmitCommandBuffers(
            1,
            &dcb_addr,
            &dcb_size,
            NULL,
            NULL) < 0)
        return PCPS4_GNM_ERR_DRIVER;

    if (sceGnmSubmitDone() < 0)
        return PCPS4_GNM_ERR_DRIVER;

    if (sceGnmVideoOutSubmitFlipAndWait(
            &renderer->video,
            buffer_index,
            (int64_t)frame_id,
            GNM_VIDEO_OUT_FLIP_VSYNC) != GNM_ERROR_OK)
        return PCPS4_GNM_ERR_VIDEO;

    if (*renderer->eop_label != frame_id)
        return PCPS4_GNM_ERR_DRIVER;

    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_render_frames(
    pcps4_gnm_renderer_t *renderer,
    uint32_t frame_count)
{
    if (renderer == NULL || frame_count == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    for (uint32_t frame = 1; frame <= frame_count; ++frame) {
        int result =
            pcps4_gnm_renderer_draw_triangle(
                renderer,
                frame);

        if (result != PCPS4_GNM_OK)
            return result;
    }

    return PCPS4_GNM_OK;
}

#else

struct pcps4_gnm_renderer {
    int unused;
};

pcps4_gnm_renderer_t *pcps4_gnm_renderer_create(
    uint32_t width,
    uint32_t height)
{
    (void)width;
    (void)height;
    return NULL;
}

void pcps4_gnm_renderer_destroy(
    pcps4_gnm_renderer_t *renderer)
{
    (void)renderer;
}

int pcps4_gnm_renderer_load_shaders(
    pcps4_gnm_renderer_t *renderer,
    const char *vertex_path,
    const char *pixel_path)
{
    (void)renderer;
    (void)vertex_path;
    (void)pixel_path;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

int pcps4_gnm_renderer_draw_triangle(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id)
{
    (void)renderer;
    (void)frame_id;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

int pcps4_gnm_renderer_render_frames(
    pcps4_gnm_renderer_t *renderer,
    uint32_t frame_count)
{
    (void)renderer;
    (void)frame_count;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

#endif
