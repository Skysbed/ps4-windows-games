#include "pcps4_gpu.h"

#include <stdlib.h>
#include <string.h>

#if PCPS4_ENABLE_OPENGNM
#include "pcps4_gnm_renderer.h"
#endif

struct pcps4_gpu_device {
    uint32_t magic;
};

struct pcps4_gpu_context {
    uint32_t magic;
    pcps4_gpu_device_t *device;
    pcps4_gpu_render_target_t *render_target;
    pcps4_gpu_buffer_t *vertex_buffer;
    uint32_t vertex_stride;
    uint32_t vertex_offset;
    pcps4_gpu_buffer_t *vertex_buffers[16];
    uint32_t vertex_strides[16];
    uint32_t vertex_offsets[16];
    pcps4_gpu_vertex_element_t input_layout[16];
    uint32_t input_layout_count;
    pcps4_gpu_buffer_t *index_buffer;
    uint32_t index_format;
    uint32_t index_offset;
    pcps4_gpu_shader_t *vertex_shader;
    pcps4_gpu_shader_t *pixel_shader;
    pcps4_gfx_context_t *graphics;
    pcps4_gnm_renderer_t *gnm_renderer;
    uint64_t gnm_frame_id;
};

struct pcps4_gpu_buffer {
    uint32_t magic;
    pcps4_gpu_device_t *device;
    pcps4_gpu_buffer_desc_t desc;
    uint8_t *data;
    uint32_t mapped;
};

struct pcps4_gpu_texture {
    uint32_t magic;
    pcps4_gpu_device_t *device;
    pcps4_gpu_texture_desc_t desc;
    uint8_t *data;
    size_t row_pitch;
    pcps4_gfx_context_t *external_graphics;
    pcps4_gnm_renderer_t *external_gnm;
};

struct pcps4_gpu_shader {
    uint32_t magic;
    pcps4_gpu_device_t *device;
    uint32_t type;
    uint8_t *data;
    size_t size;
    uint32_t mapped;
};

struct pcps4_gpu_render_target {
    uint32_t magic;
    pcps4_gpu_device_t *device;
    pcps4_gpu_texture_t *texture;
};

enum {
    DEV_MAGIC = 0x50474456u,
    CTX_MAGIC = 0x50474358u,
    BUF_MAGIC = 0x50474246u,
    TEX_MAGIC = 0x50475458u,
    SHD_MAGIC = 0x50475348u,
    RTV_MAGIC = 0x50475254u
};

static int valid_device(const pcps4_gpu_device_t *d)
{
    return d != NULL && d->magic == DEV_MAGIC;
}

static int valid_context(const pcps4_gpu_context_t *c)
{
    return c != NULL && c->magic == CTX_MAGIC &&
           valid_device(c->device);
}

static int valid_buffer(const pcps4_gpu_buffer_t *b)
{
    return b != NULL && b->magic == BUF_MAGIC &&
           valid_device(b->device);
}

static int valid_texture(const pcps4_gpu_texture_t *t)
{
    return t != NULL && t->magic == TEX_MAGIC &&
           valid_device(t->device);
}

static int valid_shader(const pcps4_gpu_shader_t *s)
{
    return s != NULL && s->magic == SHD_MAGIC &&
           valid_device(s->device);
}

static int valid_rtv(const pcps4_gpu_render_target_t *r)
{
    return r != NULL && r->magic == RTV_MAGIC &&
           valid_device(r->device) && valid_texture(r->texture);
}

static int valid_format(pcps4_gpu_format_t format)
{
    return format == PCPS4_GPU_FORMAT_RGBA8_UNORM ||
           format == PCPS4_GPU_FORMAT_BGRA8_UNORM;
}

pcps4_gpu_device_t *pcps4_gpu_device_create(void)
{
    pcps4_gpu_device_t *device =
        (pcps4_gpu_device_t *)calloc(1, sizeof(*device));

    if (device == NULL) return NULL;
    device->magic = DEV_MAGIC;
    return device;
}

void pcps4_gpu_device_destroy(pcps4_gpu_device_t *device)
{
    if (!valid_device(device)) return;
    device->magic = 0;
    free(device);
}

pcps4_gpu_context_t *pcps4_gpu_context_create(pcps4_gpu_device_t *device)
{
    pcps4_gpu_context_t *context;

    if (!valid_device(device)) return NULL;

    context =
        (pcps4_gpu_context_t *)calloc(1, sizeof(*context));
    if (context == NULL) return NULL;

    context->magic = CTX_MAGIC;
    context->device = device;
    return context;
}

void pcps4_gpu_context_destroy(pcps4_gpu_context_t *context)
{
    if (!valid_context(context)) return;
    context->magic = 0;
    context->graphics = NULL;
    context->gnm_renderer = NULL;
    free(context);
}

int pcps4_gpu_context_attach_graphics(
    pcps4_gpu_context_t *context,
    pcps4_gfx_context_t *graphics)
{
    if (!valid_context(context) || graphics == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->graphics = graphics;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_attach_gnm(
    pcps4_gpu_context_t *context,
    pcps4_gnm_renderer_t *renderer)
{
    if (!valid_context(context) || renderer == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->gnm_renderer = renderer;
    context->gnm_frame_id = 1;
    return PCPS4_GPU_OK;
}

pcps4_gpu_buffer_t *pcps4_gpu_buffer_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_buffer_desc_t *desc,
    const void *initial_data)
{
    pcps4_gpu_buffer_t *buffer;

    if (!valid_device(device) || desc == NULL ||
        desc->size == 0)
        return NULL;

    buffer =
        (pcps4_gpu_buffer_t *)calloc(1, sizeof(*buffer));
    if (buffer == NULL) return NULL;

    buffer->data = (uint8_t *)malloc(desc->size);
    if (buffer->data == NULL) {
        free(buffer);
        return NULL;
    }

    buffer->magic = BUF_MAGIC;
    buffer->device = device;
    buffer->desc = *desc;

    if (initial_data != NULL)
        memcpy(buffer->data, initial_data, desc->size);
    else
        memset(buffer->data, 0, desc->size);

    return buffer;
}

void pcps4_gpu_buffer_destroy(pcps4_gpu_buffer_t *buffer)
{
    if (!valid_buffer(buffer)) return;
    buffer->magic = 0;
    free(buffer->data);
    buffer->data = NULL;
    free(buffer);
}

int pcps4_gpu_buffer_update(pcps4_gpu_buffer_t *buffer,
                            size_t offset,
                            const void *data,
                            size_t size)
{
    if (!valid_buffer(buffer) ||
        data == NULL ||
        offset > buffer->desc.size ||
        size > buffer->desc.size - offset)
        return PCPS4_GPU_ERR_ARGUMENT;

    memcpy(buffer->data + offset, data, size);
    return PCPS4_GPU_OK;
}

int pcps4_gpu_buffer_map(pcps4_gpu_buffer_t *buffer,
                         uint32_t map_mode,
                         void **data)
{
    if (!valid_buffer(buffer) || data == NULL ||
        buffer->mapped != 0)
        return PCPS4_GPU_ERR_ARGUMENT;

    /*
     * Dynamic/CPU-write buffers map directly to their bounded heap storage.
     * Readback is intentionally not exposed until a GPU readback backend
     * exists.
     */
    if (map_mode == 1u)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    if (buffer->desc.usage != PCPS4_GPU_USAGE_DYNAMIC &&
        buffer->desc.cpu_write == 0)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    buffer->mapped = 1;
    *data = buffer->data;

    if (map_mode == 4u) {
        memset(buffer->data, 0, buffer->desc.size);
    }

    return PCPS4_GPU_OK;
}

int pcps4_gpu_buffer_unmap(pcps4_gpu_buffer_t *buffer)
{
    if (!valid_buffer(buffer) || buffer->mapped == 0)
        return PCPS4_GPU_ERR_ARGUMENT;

    buffer->mapped = 0;
    return PCPS4_GPU_OK;
}

pcps4_gpu_texture_t *pcps4_gpu_texture_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_texture_desc_t *desc,
    const void *initial_data,
    size_t row_pitch)
{
    pcps4_gpu_texture_t *texture;
    size_t tight_pitch;
    size_t total;

    if (!valid_device(device) || desc == NULL ||
        desc->width == 0 || desc->height == 0 ||
        desc->width > 16384 || desc->height > 16384 ||
        desc->mip_levels == 0 || desc->mip_levels > 16 ||
        !valid_format(desc->format))
        return NULL;

    tight_pitch = (size_t)desc->width * 4u;
    if (row_pitch == 0) row_pitch = tight_pitch;

    if (row_pitch < tight_pitch ||
        row_pitch > SIZE_MAX / desc->height)
        return NULL;

    total = row_pitch * desc->height;

    texture =
        (pcps4_gpu_texture_t *)calloc(1, sizeof(*texture));
    if (texture == NULL) return NULL;

    texture->data = (uint8_t *)calloc(1, total);
    if (texture->data == NULL) {
        free(texture);
        return NULL;
    }

    texture->magic = TEX_MAGIC;
    texture->device = device;
    texture->desc = *desc;
    texture->row_pitch = row_pitch;

    if (initial_data != NULL) {
        const uint8_t *src = (const uint8_t *)initial_data;
        for (uint32_t y = 0; y < desc->height; ++y) {
            memcpy(texture->data + (size_t)y * row_pitch,
                   src + (size_t)y * row_pitch,
                   tight_pitch);
        }
    }

    return texture;
}

pcps4_gpu_texture_t *pcps4_gpu_texture_create_backbuffer(
    pcps4_gpu_device_t *device,
    pcps4_gfx_context_t *graphics)
{
    pcps4_gpu_texture_t *texture;

    if (!valid_device(device) || graphics == NULL ||
        graphics->width == 0 || graphics->height == 0)
        return NULL;

    texture =
        (pcps4_gpu_texture_t *)calloc(1, sizeof(*texture));
    if (texture == NULL) return NULL;

    texture->magic = TEX_MAGIC;
    texture->device = device;
    texture->desc.width = graphics->width;
    texture->desc.height = graphics->height;
    texture->desc.mip_levels = 1;
    texture->desc.format = PCPS4_GPU_FORMAT_RGBA8_UNORM;
    texture->desc.bind_flags = PCPS4_GPU_BIND_RENDER_TARGET;
    texture->row_pitch =
        (size_t)graphics->width * 4u;
    texture->external_graphics = graphics;
    texture->external_gnm = NULL;
    return texture;
}

pcps4_gpu_texture_t *pcps4_gpu_texture_create_gnm_backbuffer(
    pcps4_gpu_device_t *device,
    pcps4_gnm_renderer_t *renderer,
    uint32_t width,
    uint32_t height)
{
#if PCPS4_ENABLE_OPENGNM
    pcps4_gpu_texture_t *texture;

    if (!valid_device(device) || renderer == NULL ||
        width == 0 || height == 0)
        return NULL;

    texture =
        (pcps4_gpu_texture_t *)calloc(1, sizeof(*texture));
    if (texture == NULL)
        return NULL;

    texture->magic = TEX_MAGIC;
    texture->device = device;
    texture->desc.width = width;
    texture->desc.height = height;
    texture->desc.mip_levels = 1;
    texture->desc.format = PCPS4_GPU_FORMAT_RGBA8_UNORM;
    texture->desc.bind_flags = PCPS4_GPU_BIND_RENDER_TARGET;
    texture->row_pitch = (size_t)width * 4u;
    texture->external_gnm = renderer;
    return texture;
#else
    (void)device;
    (void)renderer;
    (void)width;
    (void)height;
    return NULL;
#endif
}

void pcps4_gpu_texture_destroy(pcps4_gpu_texture_t *texture)
{
    if (!valid_texture(texture)) return;

    texture->magic = 0;
    if (texture->external_graphics == NULL &&
        texture->external_gnm == NULL)
        free(texture->data);

    texture->data = NULL;
    texture->external_graphics = NULL;
    texture->external_gnm = NULL;
    free(texture);
}

int pcps4_gpu_texture_get_desc(
    const pcps4_gpu_texture_t *texture,
    pcps4_gpu_texture_desc_t *desc)
{
    if (!valid_texture(texture) || desc == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    *desc = texture->desc;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_texture_update_rgba8(pcps4_gpu_texture_t *texture,
                                   const void *data,
                                   size_t row_pitch,
                                   uint32_t width,
                                   uint32_t height)
{
    const uint8_t *src;
    size_t tight_pitch;

    if (!valid_texture(texture) ||
        texture->external_graphics != NULL ||
        texture->data == NULL ||
        data == NULL ||
        width == 0 || height == 0 ||
        width > texture->desc.width ||
        height > texture->desc.height)
        return PCPS4_GPU_ERR_ARGUMENT;

    tight_pitch = (size_t)width * 4u;
    if (row_pitch < tight_pitch)
        return PCPS4_GPU_ERR_ARGUMENT;

    src = (const uint8_t *)data;

    for (uint32_t y = 0; y < height; ++y) {
        memcpy(texture->data + (size_t)y * texture->row_pitch,
               src + (size_t)y * row_pitch,
               tight_pitch);
    }

    return PCPS4_GPU_OK;
}

pcps4_gpu_shader_t *pcps4_gpu_shader_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_shader_desc_t *desc)
{
    pcps4_gpu_shader_t *shader;

    if (!valid_device(device) || desc == NULL ||
        desc->data == NULL || desc->size == 0 ||
        (desc->type != PCPS4_GPU_SHADER_VERTEX &&
         desc->type != PCPS4_GPU_SHADER_PIXEL))
        return NULL;

    shader =
        (pcps4_gpu_shader_t *)calloc(1, sizeof(*shader));
    if (shader == NULL) return NULL;

    shader->data = (uint8_t *)malloc(desc->size);
    if (shader->data == NULL) {
        free(shader);
        return NULL;
    }

    memcpy(shader->data, desc->data, desc->size);
    shader->magic = SHD_MAGIC;
    shader->device = device;
    shader->type = desc->type;
    shader->size = desc->size;
    return shader;
}

int pcps4_gpu_shader_get_blob(
    const pcps4_gpu_shader_t *shader,
    const void **data,
    size_t *size,
    uint32_t *type)
{
    if (!valid_shader(shader) || data == NULL || size == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    *data = shader->data;
    *size = shader->size;
    if (type != NULL)
        *type = shader->type;
    return PCPS4_GPU_OK;
}

void pcps4_gpu_shader_destroy(pcps4_gpu_shader_t *shader)
{
    if (!valid_shader(shader)) return;
    shader->magic = 0;
    free(shader->data);
    shader->data = NULL;
    free(shader);
}

pcps4_gpu_render_target_t *pcps4_gpu_render_target_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_render_target_desc_t *desc)
{
    pcps4_gpu_render_target_t *render_target;

    if (!valid_device(device) || desc == NULL ||
        !valid_texture(desc->texture) ||
        (desc->texture->desc.bind_flags &
         PCPS4_GPU_BIND_RENDER_TARGET) == 0)
        return NULL;

    render_target =
        (pcps4_gpu_render_target_t *)calloc(1, sizeof(*render_target));
    if (render_target == NULL) return NULL;

    render_target->magic = RTV_MAGIC;
    render_target->device = device;
    render_target->texture = desc->texture;
    return render_target;
}

void pcps4_gpu_render_target_destroy(
    pcps4_gpu_render_target_t *render_target)
{
    if (!valid_rtv(render_target)) return;
    render_target->magic = 0;
    render_target->texture = NULL;
    free(render_target);
}

int pcps4_gpu_context_set_input_layout(
    pcps4_gpu_context_t *context,
    const pcps4_gpu_vertex_element_t *elements,
    uint32_t count)
{
    if (!valid_context(context) ||
        (count != 0 && elements == NULL) ||
        count > 16)
        return PCPS4_GPU_ERR_ARGUMENT;

    if (count != 0)
        memcpy(context->input_layout, elements,
               (size_t)count * sizeof(elements[0]));

    if (count < 16)
        memset(context->input_layout + count,
               0,
               (size_t)(16 - count) * sizeof(elements[0]));

    context->input_layout_count = count;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_get_input_layout(
    const pcps4_gpu_context_t *context,
    const pcps4_gpu_vertex_element_t **elements,
    uint32_t *count)
{
    if (!valid_context(context) || elements == NULL || count == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    *elements = context->input_layout;
    *count = context->input_layout_count;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_render_target(
    pcps4_gpu_context_t *context,
    pcps4_gpu_render_target_t *render_target)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;

    if (render_target != NULL &&
        (!valid_rtv(render_target) ||
         render_target->device != context->device))
        return PCPS4_GPU_ERR_ARGUMENT;

    context->render_target = render_target;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_clear(
    pcps4_gpu_context_t *context,
    pcps4_gfx_color_t color)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_STATE;

    if (context->render_target == NULL) {
#if PCPS4_ENABLE_OPENGNM
        if (context->gnm_renderer != NULL) {
            return pcps4_gnm_renderer_clear(
                       context->gnm_renderer,
                       context->gnm_frame_id,
                       color.r,
                       color.g,
                       color.b,
                       color.a) == PCPS4_GNM_OK
                ? PCPS4_GPU_OK
                : PCPS4_GPU_ERR_BACKEND;
        }
#endif
        if (context->graphics == NULL)
            return PCPS4_GPU_ERR_STATE;

        return pcps4_gfx_clear(context->graphics, color) ==
               PCPS4_GFX_OK
                   ? PCPS4_GPU_OK
                   : PCPS4_GPU_ERR_BACKEND;
    }

    pcps4_gpu_texture_t *texture =
        context->render_target->texture;

    if (texture->external_graphics != NULL) {
        return pcps4_gfx_clear(
                   texture->external_graphics,
                   color) == PCPS4_GFX_OK
                   ? PCPS4_GPU_OK
                   : PCPS4_GPU_ERR_BACKEND;
    }

    if (texture->data == NULL)
        return PCPS4_GPU_ERR_STATE;

    uint32_t packed =
        ((uint32_t)color.r << 24) |
        ((uint32_t)color.g << 16) |
        ((uint32_t)color.b << 8) |
        (uint32_t)color.a;

    for (uint32_t y = 0; y < texture->desc.height; ++y) {
        uint32_t *row =
            (uint32_t *)(texture->data +
                         (size_t)y * texture->row_pitch);

        for (uint32_t x = 0; x < texture->desc.width; ++x)
            row[x] = packed;
    }

    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_present(
    pcps4_gpu_context_t *context,
    uint64_t frame_id)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_STATE;

#if PCPS4_ENABLE_OPENGNM
    if (context->gnm_renderer != NULL) {
        if (pcps4_gnm_renderer_present(
                context->gnm_renderer,
                frame_id) != PCPS4_GNM_OK)
            return PCPS4_GPU_ERR_BACKEND;

        context->gnm_frame_id = frame_id + 1;
        return PCPS4_GPU_OK;
    }
#endif

    if (context->graphics == NULL)
        return PCPS4_GPU_ERR_STATE;

    return pcps4_gfx_present(
               context->graphics,
               frame_id) == PCPS4_GFX_OK
        ? PCPS4_GPU_OK
        : PCPS4_GPU_ERR_BACKEND;
}

int pcps4_gpu_context_draw(
    pcps4_gpu_context_t *context,
    uint32_t vertex_count,
    uint32_t start_vertex)
{
    const void *vs_data = NULL;
    const void *ps_data = NULL;
    size_t vs_size = 0;
    size_t ps_size = 0;
    uint32_t vs_type = 0;
    uint32_t ps_type = 0;
    int rc;

    (void)start_vertex;

    if (!valid_context(context) || vertex_count == 0)
        return PCPS4_GPU_ERR_ARGUMENT;

#if PCPS4_ENABLE_OPENGNM
    if (context->gnm_renderer == NULL ||
        context->vertex_shader == NULL ||
        context->pixel_shader == NULL)
        return PCPS4_GPU_ERR_STATE;

    if (pcps4_gpu_shader_get_blob(
            context->vertex_shader, &vs_data, &vs_size, &vs_type) != PCPS4_GPU_OK ||
        pcps4_gpu_shader_get_blob(
            context->pixel_shader, &ps_data, &ps_size, &ps_type) != PCPS4_GPU_OK)
        return PCPS4_GPU_ERR_STATE;

    if (vs_type != PCPS4_GPU_SHADER_VERTEX ||
        ps_type != PCPS4_GPU_SHADER_PIXEL ||
        vs_size < 16 ||
        ps_size < 16)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    /*
     * The real execution path currently accepts converted PS4 .sb shader
     * binaries. A DXBC blob must first pass through the offline
     * DXBC -> SPIR-V -> PS4 shader pipeline.
     */
    if (((const uint8_t *)vs_data)[0] != 'S' ||
        ((const uint8_t *)vs_data)[1] != 'h' ||
        ((const uint8_t *)vs_data)[2] != 'd' ||
        ((const uint8_t *)vs_data)[3] != 'r' ||
        ((const uint8_t *)ps_data)[0] != 'S' ||
        ((const uint8_t *)ps_data)[1] != 'h' ||
        ((const uint8_t *)ps_data)[2] != 'd' ||
        ((const uint8_t *)ps_data)[3] != 'r')
        return PCPS4_GPU_ERR_UNSUPPORTED;

    rc = pcps4_gnm_renderer_load_shader_blobs(
        context->gnm_renderer,
        vs_data, vs_size,
        ps_data, ps_size);

    if (rc != PCPS4_GNM_OK)
        return PCPS4_GPU_ERR_BACKEND;

    rc = pcps4_gnm_renderer_draw_auto(
        context->gnm_renderer,
        context->gnm_frame_id,
        vertex_count);

    return rc == PCPS4_GNM_OK
        ? PCPS4_GPU_OK
        : PCPS4_GPU_ERR_BACKEND;
#else
    return PCPS4_GPU_ERR_UNSUPPORTED;
#endif
}

int pcps4_gpu_context_draw_indexed(
    pcps4_gpu_context_t *context,
    uint32_t index_count,
    uint32_t start_index,
    int32_t base_vertex)
{
    (void)start_index;
    (void)base_vertex;

    if (!valid_context(context) || index_count == 0)
        return PCPS4_GPU_ERR_ARGUMENT;

    return PCPS4_GPU_ERR_UNSUPPORTED;
}

int pcps4_gpu_set_vertex_buffer_slot(
    pcps4_gpu_context_t *context,
    uint32_t slot,
    pcps4_gpu_buffer_t *buffer,
    uint32_t stride,
    uint32_t offset)
{
    if (!valid_context(context) ||
        slot >= 16 ||
        !valid_buffer(buffer) ||
        buffer->device != context->device ||
        stride == 0 ||
        offset >= buffer->desc.size)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->vertex_buffers[slot] = buffer;
    context->vertex_strides[slot] = stride;
    context->vertex_offsets[slot] = offset;

    if (slot == 0) {
        context->vertex_buffer = buffer;
        context->vertex_stride = stride;
        context->vertex_offset = offset;
    }

    return PCPS4_GPU_OK;
}

int pcps4_gpu_set_vertex_buffer(
    pcps4_gpu_context_t *context,
    pcps4_gpu_buffer_t *buffer,
    uint32_t stride,
    uint32_t offset)
{
    return pcps4_gpu_set_vertex_buffer_slot(
        context, 0, buffer, stride, offset);
}

int pcps4_gpu_set_index_buffer(
    pcps4_gpu_context_t *context,
    pcps4_gpu_buffer_t *buffer,
    uint32_t format,
    uint32_t offset)
{
    if (!valid_context(context) ||
        !valid_buffer(buffer) ||
        buffer->device != context->device ||
        (format != 57u && format != 42u) ||
        offset >= buffer->desc.size)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->index_buffer = buffer;
    context->index_format = format;
    context->index_offset = offset;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_set_vertex_shader(
    pcps4_gpu_context_t *context,
    pcps4_gpu_shader_t *shader)
{
    if (!valid_context(context) || !valid_shader(shader) ||
        shader->device != context->device ||
        shader->type != PCPS4_GPU_SHADER_VERTEX)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->vertex_shader = shader;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_set_pixel_shader(
    pcps4_gpu_context_t *context,
    pcps4_gpu_shader_t *shader)
{
    if (!valid_context(context) || !valid_shader(shader) ||
        shader->device != context->device ||
        shader->type != PCPS4_GPU_SHADER_PIXEL)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->pixel_shader = shader;
    return PCPS4_GPU_OK;
}
