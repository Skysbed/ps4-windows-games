#include "pcps4_gnm_renderer.h"

#if PCPS4_ENABLE_OPENGNM

#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <gnm/drawcommandbuffer.h>
#include <gnm_helpers.h>
#include <gnmdriver.h>

struct pcps4_gnm_renderer {
    uint32_t width;
    uint32_t height;

    GnmVideoOut video;

    GnmDirectMemory memory;
    size_t memory_cursor;
    size_t memory_size;

    void *command_buffer;
    size_t command_buffer_size;
    volatile uint64_t *eop_label;

    GnmRenderTarget render_targets[3];

    GnmVsShader *vertex_shader;
    GnmPsShader *pixel_shader;

    const void *loaded_vs_blob;
    size_t loaded_vs_size;
    const void *loaded_ps_blob;
    size_t loaded_ps_size;

    uint64_t frame_serial;
    uint32_t frame_has_gpu_work;
};

static size_t align_up(size_t value, size_t alignment)
{
    if (alignment == 0)
        return value;

    size_t remainder = value % alignment;
    return remainder == 0
        ? value
        : value + alignment - remainder;
}

static void *gpu_alloc(
    pcps4_gnm_renderer_t *renderer,
    size_t size,
    size_t alignment)
{
    if (renderer == NULL || size == 0 || alignment == 0)
        return NULL;

    size_t start = align_up(renderer->memory_cursor, alignment);

    if (start > renderer->memory_size ||
        size > renderer->memory_size - start)
        return NULL;

    renderer->memory_cursor = start + size;
    return (uint8_t *)renderer->memory.mapped + start;
}

static int read_file(
    const char *path,
    void **out_data,
    size_t *out_size)
{
    int fd;
    struct stat st;
    uint8_t *data;
    size_t offset = 0;

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

    while (offset < (size_t)st.st_size) {
        ssize_t n = (ssize_t)sceKernelRead(
            fd,
            data + offset,
            (size_t)st.st_size - offset);

        if (n <= 0) {
            free(data);
            (void)sceKernelClose(fd);
            return PCPS4_GNM_ERR_IO;
        }

        offset += (size_t)n;
    }

    (void)sceKernelClose(fd);
    *out_data = data;
    *out_size = offset;
    return PCPS4_GNM_OK;
}

static int load_vertex_blob(
    pcps4_gnm_renderer_t *renderer,
    const void *data,
    size_t size)
{
    GnmShaderMetadata metadata;
    GnmError error;
    const GnmVsShader *source;
    GnmVsShader *stage;
    uint32_t stage_size;
    uint32_t code_size;
    void *code;

    if (renderer == NULL || data == NULL || size == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    memset(&metadata, 0, sizeof(metadata));
    error = sceGnmShaderBinaryGetMetadata(
        data, size, &metadata);

    if (error != GNM_ERROR_OK ||
        metadata.type != GNM_SHADER_VERTEX ||
        metadata.stage == NULL ||
        metadata.common == NULL ||
        metadata.shadercode == NULL)
        return PCPS4_GNM_ERR_SHADER;

    source = (const GnmVsShader *)metadata.stage;
    stage_size = sceGnmVsShaderCalcSize(source);
    code_size = sceGnmShaderCommonCodeSize(metadata.common);

    if (stage_size == 0 || code_size == 0)
        return PCPS4_GNM_ERR_SHADER;

    stage = (GnmVsShader *)malloc(stage_size);
    if (stage == NULL)
        return PCPS4_GNM_ERR_MEMORY;

    code = gpu_alloc(
        renderer,
        code_size,
        GNM_ALIGNMENT_SHADER_BYTES);

    if (code == NULL) {
        free(stage);
        return PCPS4_GNM_ERR_MEMORY;
    }

    memcpy(stage, source, stage_size);
    memcpy(code, metadata.shadercode, code_size);
    gnmVsRegsSetAddress(&stage->registers, code);

    free(renderer->vertex_shader);
    renderer->vertex_shader = stage;
    renderer->loaded_vs_blob = data;
    renderer->loaded_vs_size = size;

    return PCPS4_GNM_OK;
}

static int load_pixel_blob(
    pcps4_gnm_renderer_t *renderer,
    const void *data,
    size_t size)
{
    GnmShaderMetadata metadata;
    GnmError error;
    const GnmPsShader *source;
    GnmPsShader *stage;
    uint32_t stage_size;
    uint32_t code_size;
    void *code;

    if (renderer == NULL || data == NULL || size == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    memset(&metadata, 0, sizeof(metadata));
    error = sceGnmShaderBinaryGetMetadata(
        data, size, &metadata);

    if (error != GNM_ERROR_OK ||
        metadata.type != GNM_SHADER_PIXEL ||
        metadata.stage == NULL ||
        metadata.common == NULL ||
        metadata.shadercode == NULL)
        return PCPS4_GNM_ERR_SHADER;

    source = (const GnmPsShader *)metadata.stage;
    stage_size = sceGnmPsShaderCalcSize(source);
    code_size = sceGnmShaderCommonCodeSize(metadata.common);

    if (stage_size == 0 || code_size == 0)
        return PCPS4_GNM_ERR_SHADER;

    stage = (GnmPsShader *)malloc(stage_size);
    if (stage == NULL)
        return PCPS4_GNM_ERR_MEMORY;

    code = gpu_alloc(
        renderer,
        code_size,
        GNM_ALIGNMENT_SHADER_BYTES);

    if (code == NULL) {
        free(stage);
        return PCPS4_GNM_ERR_MEMORY;
    }

    memcpy(stage, source, stage_size);
    memcpy(code, metadata.shadercode, code_size);
    gnmPsRegsSetAddress(&stage->registers, code);
    stage->registers.spibaryccntl = 0;

    free(renderer->pixel_shader);
    renderer->pixel_shader = stage;
    renderer->loaded_ps_blob = data;
    renderer->loaded_ps_size = size;

    return PCPS4_GNM_OK;
}

static int create_display(
    pcps4_gnm_renderer_t *renderer)
{
    GnmVideoOutCreateInfo info;
    GnmError error;

    memset(&renderer->video, 0, sizeof(renderer->video));
    renderer->video.handle = -1;

    sceGnmVideoOutInitDefaultCreateInfo(
        &info,
        renderer->width,
        renderer->height);

    info.numbuffers = 2;

    error = sceGnmVideoOutOpen(
        &renderer->video,
        &info);

    if (error != GNM_ERROR_OK)
        return PCPS4_GNM_ERR_VIDEO;

    for (uint32_t i = 0; i < renderer->video.numbuffers; ++i) {
        error = sceGnmRtCreateColorTarget(
            &renderer->render_targets[i],
            sceGnmVideoOutGetBuffer(&renderer->video, i),
            GNM_FMT_R8G8B8A8_SRGB,
            renderer->width,
            renderer->height,
            1, 1, 1,
            GNM_TM_DISPLAY_LINEAR_ALIGNED,
            gnmGpuMode(),
            NULL, NULL);

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
    const size_t memory_size = 16u * 1024u * 1024u;
    pcps4_gnm_renderer_t *renderer;

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
            &renderer->memory,
            memory_size,
            GNM_VIDEO_OUT_MEMORY_ALIGNMENT,
            GNM_DIRECT_MEMORY_TYPE_WC_GARLIC,
            GNM_PROT_CPU_GPU_RW) != GNM_ERROR_OK) {
        free(renderer);
        return NULL;
    }

    renderer->memory_size = memory_size;

    if (create_display(renderer) != PCPS4_GNM_OK) {
        sceGnmDirectMemoryRelease(&renderer->memory);
        free(renderer);
        return NULL;
    }

    renderer->command_buffer_size = 64u * 1024u;
    renderer->command_buffer = gpu_alloc(
        renderer,
        renderer->command_buffer_size,
        GNM_ALIGNMENT_BUFFER_BYTES);

    renderer->eop_label =
        (volatile uint64_t *)gpu_alloc(
            renderer,
            sizeof(uint64_t),
            sizeof(uint64_t));

    if (renderer->command_buffer == NULL ||
        renderer->eop_label == NULL) {
        sceGnmVideoOutClose(&renderer->video);
        renderer->video.handle = -1;
        sceGnmDirectMemoryRelease(&renderer->memory);
        free(renderer);
        return NULL;
    }

    *renderer->eop_label = 0;
    renderer->frame_serial = 0;
    renderer->frame_has_gpu_work = 0;
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

    sceGnmDirectMemoryRelease(&renderer->memory);
    free(renderer);
}

int pcps4_gnm_renderer_load_shader_blobs(
    pcps4_gnm_renderer_t *renderer,
    const void *vertex_data,
    size_t vertex_size,
    const void *pixel_data,
    size_t pixel_size)
{
    int rc;

    if (renderer == NULL ||
        vertex_data == NULL ||
        vertex_size == 0 ||
        pixel_data == NULL ||
        pixel_size == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    if (renderer->loaded_vs_blob != vertex_data ||
        renderer->loaded_vs_size != vertex_size) {
        rc = load_vertex_blob(
            renderer,
            vertex_data,
            vertex_size);

        if (rc != PCPS4_GNM_OK)
            return rc;
    }

    if (renderer->loaded_ps_blob != pixel_data ||
        renderer->loaded_ps_size != pixel_size) {
        rc = load_pixel_blob(
            renderer,
            pixel_data,
            pixel_size);

        if (rc != PCPS4_GNM_OK)
            return rc;
    }

    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_load_shaders(
    pcps4_gnm_renderer_t *renderer,
    const char *vertex_path,
    const char *pixel_path)
{
    void *vertex = NULL;
    void *pixel = NULL;
    size_t vertex_size = 0;
    size_t pixel_size = 0;
    int rc;

    if (renderer == NULL ||
        vertex_path == NULL ||
        pixel_path == NULL)
        return PCPS4_GNM_ERR_ARGUMENT;

    rc = read_file(vertex_path, &vertex, &vertex_size);
    if (rc != PCPS4_GNM_OK)
        return rc;

    rc = read_file(pixel_path, &pixel, &pixel_size);
    if (rc != PCPS4_GNM_OK) {
        free(vertex);
        return rc;
    }

    rc = pcps4_gnm_renderer_load_shader_blobs(
        renderer,
        vertex, vertex_size,
        pixel, pixel_size);

    free(vertex);
    free(pixel);
    return rc;
}

int pcps4_gnm_renderer_clear(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    (void)a;

    if (renderer == NULL ||
        renderer->video.numbuffers < 2)
        return PCPS4_GNM_ERR_ARGUMENT;

    uint32_t index =
        (uint32_t)(frame_id % renderer->video.numbuffers);

    uint8_t *buffer =
        (uint8_t *)sceGnmVideoOutGetBuffer(
            &renderer->video,
            index);

    if (buffer == NULL)
        return PCPS4_GNM_ERR_VIDEO;

    uint32_t color =
        0x80000000u |
        ((uint32_t)r << 16) |
        ((uint32_t)g << 8) |
        (uint32_t)b;

    uint32_t *pixels = (uint32_t *)buffer;
    size_t count =
        (size_t)renderer->width *
        (size_t)renderer->height;

    for (size_t i = 0; i < count; ++i)
        pixels[i] = color;

    /*
     * A clear is a valid frame-producing operation for the D3D11 facade.
     * Mark the frame ready even though no GNM draw packet was submitted.
     */
    renderer->frame_serial = frame_id;
    renderer->frame_has_gpu_work = 0;
    return PCPS4_GNM_OK;
}

static int build_draw_commands(
    pcps4_gnm_renderer_t *renderer,
    uint32_t buffer_index,
    uint64_t frame_id,
    uint32_t vertex_count,
    GnmCommandBuffer *out)
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
        out == NULL ||
        renderer->vertex_shader == NULL ||
        renderer->pixel_shader == NULL ||
        buffer_index >= renderer->video.numbuffers ||
        vertex_count == 0)
        return PCPS4_GNM_ERR_COMMAND;

    memset(
        renderer->command_buffer,
        0,
        renderer->command_buffer_size);

    command = sceGnmCmdInit(
        renderer->command_buffer,
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
        0, 0,
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
        vertex_count);

    *renderer->eop_label = 0;

    sceGnmDrawCmdEventWriteEop(
        &command,
        GNM_CACHE_FLUSH_TS,
        (uint64_t)(uintptr_t)renderer->eop_label,
        GNM_DATA_SEL_SEND_DATA64,
        frame_id);

    size_t used =
        (size_t)((uintptr_t)command.cmdptr -
                 (uintptr_t)command.beginptr);

    if (used == 0 ||
        used > renderer->command_buffer_size)
        return PCPS4_GNM_ERR_COMMAND;

    *out = command;
    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_draw_auto(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id,
    uint32_t vertex_count)
{
    GnmCommandBuffer command;
    uint32_t buffer_index;
    void *address;
    uint32_t size;

    if (renderer == NULL ||
        vertex_count == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    buffer_index =
        (uint32_t)(frame_id % renderer->video.numbuffers);

    if (build_draw_commands(
            renderer,
            buffer_index,
            frame_id,
            vertex_count,
            &command) != PCPS4_GNM_OK)
        return PCPS4_GNM_ERR_COMMAND;

    address = command.beginptr;
    size =
        (uint32_t)(
            (uintptr_t)command.cmdptr -
            (uintptr_t)command.beginptr);

    if (sceGnmSubmitCommandBuffers(
            1,
            &address,
            &size,
            NULL,
            NULL) < 0)
        return PCPS4_GNM_ERR_DRIVER;

    if (sceGnmSubmitDone() < 0)
        return PCPS4_GNM_ERR_DRIVER;

    renderer->frame_serial = frame_id;
    renderer->frame_has_gpu_work = 1;
    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_present(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id)
{
    if (renderer == NULL ||
        renderer->frame_serial != frame_id)
        return PCPS4_GNM_ERR_ARGUMENT;

    if (sceGnmVideoOutSubmitFlipAndWait(
            &renderer->video,
            (uint32_t)(
                frame_id % renderer->video.numbuffers),
            (int64_t)frame_id,
            GNM_VIDEO_OUT_FLIP_VSYNC) != GNM_ERROR_OK)
        return PCPS4_GNM_ERR_VIDEO;

    /*
     * GNM draw frames get an EOP timestamp. Clear-only D3D11 frames do not,
     * so only require the timestamp when command-buffer work was submitted.
     */
    if (renderer->frame_has_gpu_work != 0 &&
        *renderer->eop_label != frame_id)
        return PCPS4_GNM_ERR_DRIVER;

    renderer->frame_has_gpu_work = 0;
    return PCPS4_GNM_OK;
}

int pcps4_gnm_renderer_draw_triangle(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id)
{
    int rc = pcps4_gnm_renderer_draw_auto(
        renderer, frame_id, 3);

    if (rc != PCPS4_GNM_OK)
        return rc;

    return pcps4_gnm_renderer_present(
        renderer, frame_id);
}

int pcps4_gnm_renderer_render_frames(
    pcps4_gnm_renderer_t *renderer,
    uint32_t frame_count)
{
    if (renderer == NULL || frame_count == 0)
        return PCPS4_GNM_ERR_ARGUMENT;

    for (uint32_t frame = 1; frame <= frame_count; ++frame) {
        int rc =
            pcps4_gnm_renderer_draw_triangle(
                renderer, frame);

        if (rc != PCPS4_GNM_OK)
            return rc;
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

int pcps4_gnm_renderer_load_shader_blobs(
    pcps4_gnm_renderer_t *renderer,
    const void *vertex_data,
    size_t vertex_size,
    const void *pixel_data,
    size_t pixel_size)
{
    (void)renderer;
    (void)vertex_data;
    (void)vertex_size;
    (void)pixel_data;
    (void)pixel_size;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

int pcps4_gnm_renderer_clear(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id,
    uint8_t r,
    uint8_t g,
    uint8_t b,
    uint8_t a)
{
    (void)renderer;
    (void)frame_id;
    (void)r;
    (void)g;
    (void)b;
    (void)a;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

int pcps4_gnm_renderer_draw_auto(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id,
    uint32_t vertex_count)
{
    (void)renderer;
    (void)frame_id;
    (void)vertex_count;
    return PCPS4_GNM_ERR_UNAVAILABLE;
}

int pcps4_gnm_renderer_present(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id)
{
    (void)renderer;
    (void)frame_id;
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
