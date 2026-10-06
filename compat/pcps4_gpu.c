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
    uint32_t refs;
    pcps4_gpu_device_t *device;
    pcps4_gpu_render_target_t *render_target;
    pcps4_gpu_texture_t *depth_target;
    uint32_t depth_enabled;
    uint32_t depth_write_enabled;
    uint32_t blend_enabled;
    uint32_t src_blend;
    uint32_t dst_blend;
    uint32_t cull_mode;
    pcps4_gpu_buffer_t *vs_constant_buffers[16];
    pcps4_gpu_buffer_t *ps_constant_buffers[16];
    pcps4_gpu_texture_t *vs_shader_resources[16];
    pcps4_gpu_texture_t *ps_shader_resources[16];
    uint32_t vs_samplers[16][5];
    uint32_t ps_samplers[16][5];
    uint32_t primitive_topology;
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
           format == PCPS4_GPU_FORMAT_BGRA8_UNORM ||
           format == PCPS4_GPU_FORMAT_R32_FLOAT;
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
    context->refs = 1;
    context->device = device;
    context->cull_mode = 0; /* no culling until an RS state is explicitly bound */
    context->primitive_topology = PCPS4_GPU_TOPOLOGY_TRIANGLELIST;
    return context;
}

void pcps4_gpu_context_add_ref(pcps4_gpu_context_t *context)
{
    if (!valid_context(context))
        return;
    if (context->refs != UINT32_MAX)
        ++context->refs;
}

void pcps4_gpu_context_destroy(pcps4_gpu_context_t *context)
{
    if (!valid_context(context)) return;
    if (context->refs > 1) {
        --context->refs;
        return;
    }
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

int pcps4_gpu_buffer_get_size(
    const pcps4_gpu_buffer_t *buffer,
    size_t *size)
{
    if (!valid_buffer(buffer) || size == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;
    *size = buffer->desc.size;
    return PCPS4_GPU_OK;
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

    /* Uploading into the VideoOut back buffer: blit straight to the
     * framebuffer (scaled to fit if the source is smaller). */
    if (valid_texture(texture) && texture->external_graphics != NULL &&
        data != NULL && width != 0 && height != 0) {
        if (row_pitch < (size_t)width * 4u)
            return PCPS4_GPU_ERR_ARGUMENT;
        return pcps4_gfx_blit_rgba8_scaled(texture->external_graphics,
                                           (const uint8_t *)data,
                                           width, height,
                                           (uint32_t)row_pitch) ==
               PCPS4_GFX_OK
                   ? PCPS4_GPU_OK
                   : PCPS4_GPU_ERR_BACKEND;
    }

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

int pcps4_gpu_context_set_primitive_topology(
    pcps4_gpu_context_t *context,
    uint32_t topology)
{
    if (!valid_context(context) ||
        topology < PCPS4_GPU_TOPOLOGY_POINTLIST ||
        topology > PCPS4_GPU_TOPOLOGY_TRIANGLESTRIP)
        return PCPS4_GPU_ERR_ARGUMENT;

    context->primitive_topology = topology;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_depth_target(
    pcps4_gpu_context_t *context,
    pcps4_gpu_texture_t *texture)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    if (texture != NULL && !valid_texture(texture))
        return PCPS4_GPU_ERR_ARGUMENT;
    if (texture != NULL &&
        (texture->desc.format != PCPS4_GPU_FORMAT_R32_FLOAT ||
         (texture->desc.bind_flags & PCPS4_GPU_BIND_DEPTH_STENCIL) == 0))
        return PCPS4_GPU_ERR_UNSUPPORTED;
    context->depth_target = texture;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_clear_depth(
    pcps4_gpu_context_t *context,
    float depth)
{
    if (!valid_context(context) || context->depth_target == NULL ||
        context->depth_target->data == NULL)
        return PCPS4_GPU_ERR_STATE;

    if (depth < 0.0f) depth = 0.0f;
    if (depth > 1.0f) depth = 1.0f;

    size_t count =
        (size_t)context->depth_target->desc.width *
        context->depth_target->desc.height;
    float *values = (float *)context->depth_target->data;
    for (size_t i = 0; i < count; ++i)
        values[i] = depth;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_depth_state(
    pcps4_gpu_context_t *context,
    uint32_t enabled,
    uint32_t write_enabled)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    context->depth_enabled = enabled ? 1u : 0u;
    context->depth_write_enabled = write_enabled ? 1u : 0u;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_blend_state(
    pcps4_gpu_context_t *context,
    uint32_t enabled,
    uint32_t src_blend,
    uint32_t dst_blend)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    context->blend_enabled = enabled ? 1u : 0u;
    context->src_blend = src_blend;
    context->dst_blend = dst_blend;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_cull_mode(
    pcps4_gpu_context_t *context,
    uint32_t cull_mode)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    context->cull_mode = cull_mode;
    return PCPS4_GPU_OK;
}

static int pcps4_set_slot4(
    pcps4_gpu_buffer_t **slots,
    uint32_t slot,
    pcps4_gpu_buffer_t *buffer)
{
    if (slot >= 16 || (buffer != NULL && !valid_buffer(buffer)))
        return PCPS4_GPU_ERR_ARGUMENT;
    slots[slot] = buffer;
    return PCPS4_GPU_OK;
}

static int pcps4_set_tex_slot(
    pcps4_gpu_texture_t **slots,
    uint32_t slot,
    pcps4_gpu_texture_t *texture)
{
    if (slot >= 16 || (texture != NULL && !valid_texture(texture)))
        return PCPS4_GPU_ERR_ARGUMENT;
    slots[slot] = texture;
    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_set_constant_buffer(
    pcps4_gpu_context_t *context,
    uint32_t stage,
    uint32_t slot,
    pcps4_gpu_buffer_t *buffer)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    return stage == PCPS4_GPU_SHADER_VERTEX
        ? pcps4_set_slot4(context->vs_constant_buffers, slot, buffer)
        : stage == PCPS4_GPU_SHADER_PIXEL
            ? pcps4_set_slot4(context->ps_constant_buffers, slot, buffer)
            : PCPS4_GPU_ERR_ARGUMENT;
}

int pcps4_gpu_context_set_shader_resource(
    pcps4_gpu_context_t *context,
    uint32_t stage,
    uint32_t slot,
    pcps4_gpu_texture_t *texture)
{
    if (!valid_context(context))
        return PCPS4_GPU_ERR_ARGUMENT;
    return stage == PCPS4_GPU_SHADER_VERTEX
        ? pcps4_set_tex_slot(context->vs_shader_resources, slot, texture)
        : stage == PCPS4_GPU_SHADER_PIXEL
            ? pcps4_set_tex_slot(context->ps_shader_resources, slot, texture)
            : PCPS4_GPU_ERR_ARGUMENT;
}

int pcps4_gpu_context_set_sampler(
    pcps4_gpu_context_t *context,
    uint32_t stage,
    uint32_t slot,
    uint32_t filter,
    uint32_t address_u,
    uint32_t address_v,
    uint32_t address_w)
{
    uint32_t (*slots)[5];
    if (!valid_context(context) || slot >= 16 || stage < PCPS4_GPU_SHADER_VERTEX ||
        stage > PCPS4_GPU_SHADER_PIXEL)
        return PCPS4_GPU_ERR_ARGUMENT;
    slots = stage == PCPS4_GPU_SHADER_VERTEX
        ? context->vs_samplers
        : context->ps_samplers;
    slots[slot][0] = filter;
    slots[slot][1] = address_u;
    slots[slot][2] = address_v;
    slots[slot][3] = address_w;
    slots[slot][4] = 1u;
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

    if (context->depth_target != NULL)
        (void)pcps4_gpu_context_clear_depth(context, 1.0f);

    /* RGBA8 textures store bytes r,g,b,a (D3D DXGI_FORMAT_R8G8B8A8). */
    for (uint32_t y = 0; y < texture->desc.height; ++y) {
        uint8_t *row = texture->data + (size_t)y * texture->row_pitch;

        for (uint32_t x = 0; x < texture->desc.width; ++x) {
            row[x * 4u + 0] = color.r;
            row[x * 4u + 1] = color.g;
            row[x * 4u + 2] = color.b;
            row[x * 4u + 3] = color.a;
        }
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

static float pcps4_edge(
    float ax,
    float ay,
    float bx,
    float by,
    float px,
    float py)
{
    return (px - ax) * (by - ay) -
           (py - ay) * (bx - ax);
}

static void pcps4_write_pixel(
    pcps4_gpu_texture_t *texture,
    int32_t x,
    int32_t y,
    float r,
    float g,
    float b,
    float a)
{
    uint8_t *row;

    if (texture == NULL ||
        texture->data == NULL ||
        x < 0 || y < 0 ||
        x >= (int32_t)texture->desc.width ||
        y >= (int32_t)texture->desc.height)
        return;

    if (r < 0.0f) r = 0.0f;
    if (r > 1.0f) r = 1.0f;
    if (g < 0.0f) g = 0.0f;
    if (g > 1.0f) g = 1.0f;
    if (b < 0.0f) b = 0.0f;
    if (b > 1.0f) b = 1.0f;
    if (a < 0.0f) a = 0.0f;
    if (a > 1.0f) a = 1.0f;

    row = texture->data + (size_t)y * texture->row_pitch;
    row[(size_t)x * 4u + 0] = (uint8_t)(r * 255.0f + 0.5f);
    row[(size_t)x * 4u + 1] = (uint8_t)(g * 255.0f + 0.5f);
    row[(size_t)x * 4u + 2] = (uint8_t)(b * 255.0f + 0.5f);
    row[(size_t)x * 4u + 3] = (uint8_t)(a * 255.0f + 0.5f);
}

static int pcps4_decode_float3(
    const uint8_t *vertex,
    uint32_t format,
    float out[3])
{
    if (vertex == NULL || out == NULL)
        return 0;

    if (format != 6u)
        return 0;

    memcpy(out, vertex, sizeof(float) * 3u);
    return 1;
}

static int pcps4_decode_float4(
    const uint8_t *vertex,
    uint32_t format,
    float out[4])
{
    if (vertex == NULL || out == NULL)
        return 0;

    if (format != 2u)
        return 0;

    memcpy(out, vertex, sizeof(float) * 4u);
    return 1;
}

static float pcps4_blend_factor(
    uint32_t factor,
    float src_alpha)
{
    switch (factor) {
        case 1u: return 0.0f; /* ZERO */
        case 2u: return 1.0f; /* ONE */
        case 5u: return src_alpha; /* SRC_ALPHA */
        case 6u: return 1.0f - src_alpha; /* INV_SRC_ALPHA */
        default: return 1.0f;
    }
}

static int pcps4_raster_triangle(
    pcps4_gpu_context_t *context,
    const float p0[3],
    const float c0[4],
    const float p1[3],
    const float c1[4],
    const float p2[3],
    const float c2[4])
{
    pcps4_gpu_texture_t *texture;
    float area;
    float min_x, max_x, min_y, max_y;

    if (context == NULL ||
        !valid_context(context) ||
        context->render_target == NULL ||
        !valid_rtv(context->render_target))
        return PCPS4_GPU_ERR_STATE;

    texture = context->render_target->texture;
    if (texture == NULL || texture->data == NULL ||
        p0 == NULL || p1 == NULL || p2 == NULL ||
        c0 == NULL || c1 == NULL || c2 == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    area = pcps4_edge(p0[0], p0[1], p1[0], p1[1], p2[0], p2[1]);
    if (area == 0.0f)
        return PCPS4_GPU_OK;

    /* D3D11 default front face is counter-clockwise. */
    if ((context->cull_mode == 3u && area > 0.0f) ||
        (context->cull_mode == 2u && area < 0.0f))
        return PCPS4_GPU_OK;

    min_x = p0[0]; if (p1[0] < min_x) min_x = p1[0]; if (p2[0] < min_x) min_x = p2[0];
    max_x = p0[0]; if (p1[0] > max_x) max_x = p1[0]; if (p2[0] > max_x) max_x = p2[0];
    min_y = p0[1]; if (p1[1] < min_y) min_y = p1[1]; if (p2[1] < min_y) min_y = p2[1];
    max_y = p0[1]; if (p1[1] > max_y) max_y = p1[1]; if (p2[1] > max_y) max_y = p2[1];

    int32_t x0 = (int32_t)((min_x * 0.5f + 0.5f) * (float)texture->desc.width);
    int32_t x1 = (int32_t)((max_x * 0.5f + 0.5f) * (float)texture->desc.width);
    int32_t y0 = (int32_t)((-max_y * 0.5f + 0.5f) * (float)texture->desc.height);
    int32_t y1 = (int32_t)((-min_y * 0.5f + 0.5f) * (float)texture->desc.height);

    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 >= (int32_t)texture->desc.width) x1 = (int32_t)texture->desc.width - 1;
    if (y1 >= (int32_t)texture->desc.height) y1 = (int32_t)texture->desc.height - 1;
    if (x0 > x1 || y0 > y1)
        return PCPS4_GPU_OK;

    for (int32_t y = y0; y <= y1; ++y) {
        for (int32_t x = x0; x <= x1; ++x) {
            float px = ((float)x + 0.5f) / (float)texture->desc.width * 2.0f - 1.0f;
            float py = 1.0f - ((float)y + 0.5f) / (float)texture->desc.height * 2.0f;
            float w0 = pcps4_edge(p1[0], p1[1], p2[0], p2[1], px, py);
            float w1 = pcps4_edge(p2[0], p2[1], p0[0], p0[1], px, py);
            float w2 = pcps4_edge(p0[0], p0[1], p1[0], p1[1], px, py);

            if (!((area > 0.0f && w0 >= 0.0f && w1 >= 0.0f && w2 >= 0.0f) ||
                  (area < 0.0f && w0 <= 0.0f && w1 <= 0.0f && w2 <= 0.0f)))
                continue;

            float inv_area = 1.0f / area;
            float a0 = w0 * inv_area;
            float a1 = w1 * inv_area;
            float a2 = w2 * inv_area;
            float depth = p0[2] * a0 + p1[2] * a1 + p2[2] * a2;
            depth = depth * 0.5f + 0.5f;

            if (context->depth_enabled && context->depth_target != NULL) {
                float *depth_values = (float *)context->depth_target->data;
                size_t depth_index = (size_t)y * context->depth_target->desc.width + (size_t)x;
                float current = depth_values[depth_index];
                if (depth >= current)
                    continue;
                if (context->depth_write_enabled)
                    depth_values[depth_index] = depth;
            }

            float sr = c0[0] * a0 + c1[0] * a1 + c2[0] * a2;
            float sg = c0[1] * a0 + c1[1] * a1 + c2[1] * a2;
            float sb = c0[2] * a0 + c1[2] * a1 + c2[2] * a2;
            float sa = c0[3] * a0 + c1[3] * a1 + c2[3] * a2;

            if (context->blend_enabled) {
                uint8_t *row = texture->data + (size_t)y * texture->row_pitch;
                float dr = row[x * 4u + 0] / 255.0f;
                float dg = row[x * 4u + 1] / 255.0f;
                float db = row[x * 4u + 2] / 255.0f;
                float da = row[x * 4u + 3] / 255.0f;
                float sf = pcps4_blend_factor(context->src_blend, sa);
                float df = pcps4_blend_factor(context->dst_blend, sa);
                sr = sr * sf + dr * df;
                sg = sg * sf + dg * df;
                sb = sb * sf + db * df;
                sa = sa * sf + da * df;
            }

            pcps4_write_pixel(texture, x, y, sr, sg, sb, sa);
        }
    }

    return PCPS4_GPU_OK;
}

static int pcps4_cpu_draw_trianglelist(
    pcps4_gpu_context_t *context,
    uint32_t vertex_count,
    uint32_t start_vertex)
{
    const pcps4_gpu_vertex_element_t *layout;
    uint32_t layout_count;
    pcps4_gpu_texture_t *texture;
    pcps4_gpu_buffer_t *buffer;
    const uint32_t vertex_stride = context->vertex_stride;
    const uint32_t vertex_offset = context->vertex_offset;
    int position_index = -1;
    int color_index = -1;

    if (context == NULL ||
        vertex_count < 3 ||
        context->render_target == NULL ||
        !valid_rtv(context->render_target) ||
        context->vertex_buffer == NULL ||
        !valid_buffer(context->vertex_buffer) ||
        vertex_stride < 12)
        return PCPS4_GPU_ERR_STATE;

    texture = context->render_target->texture;
    if (!valid_texture(texture) ||
        texture->data == NULL)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    if (pcps4_gpu_context_get_input_layout(
            context, &layout, &layout_count) != PCPS4_GPU_OK)
        return PCPS4_GPU_ERR_STATE;

    for (uint32_t i = 0; i < layout_count; ++i) {
        if (layout[i].input_slot != 0)
            continue;
        if (layout[i].semantic_kind == 1u && position_index < 0)
            position_index = (int)i;
        if (layout[i].semantic_kind == 2u && color_index < 0)
            color_index = (int)i;
    }

    if (position_index < 0 || color_index < 0)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    for (uint32_t base = 0; base + 2 < vertex_count; base += 3) {
        const uint8_t *v[3];
        float p[3][3];
        float c[3][4];

        for (uint32_t i = 0; i < 3; ++i) {
            size_t index = (size_t)start_vertex + base + i;
            size_t byte_offset = index * vertex_stride +
                                 (size_t)vertex_offset;
            if (byte_offset > context->vertex_buffer->desc.size ||
                vertex_stride > context->vertex_buffer->desc.size - byte_offset)
                return PCPS4_GPU_ERR_ARGUMENT;
            v[i] = context->vertex_buffer->data + byte_offset;

            if (!pcps4_decode_float3(
                    v[i] + layout[position_index].aligned_byte_offset,
                    layout[position_index].format,
                    p[i]) ||
                !pcps4_decode_float4(
                    v[i] + layout[color_index].aligned_byte_offset,
                    layout[color_index].format,
                    c[i]))
                return PCPS4_GPU_ERR_UNSUPPORTED;
        }

        int rc = pcps4_raster_triangle(
            context, p[0], c[0], p[1], c[1], p[2], c[2]);
        if (rc != PCPS4_GPU_OK)
            return rc;
    }

    return PCPS4_GPU_OK;
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
    if (context->primitive_topology != PCPS4_GPU_TOPOLOGY_TRIANGLELIST)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    return pcps4_cpu_draw_trianglelist(
        context, vertex_count, start_vertex);
#endif
}

static int pcps4_cpu_fetch_vertex(
    pcps4_gpu_context_t *context,
    uint32_t vertex_index,
    float position[3],
    float color[4])
{
    const pcps4_gpu_vertex_element_t *layout;
    uint32_t layout_count;
    int position_index = -1;
    int color_index = -1;

    if (context == NULL ||
        context->vertex_buffer == NULL ||
        !valid_buffer(context->vertex_buffer) ||
        position == NULL ||
        color == NULL)
        return PCPS4_GPU_ERR_STATE;

    if (pcps4_gpu_context_get_input_layout(
            context, &layout, &layout_count) != PCPS4_GPU_OK)
        return PCPS4_GPU_ERR_STATE;

    for (uint32_t i = 0; i < layout_count; ++i) {
        if (layout[i].input_slot != 0)
            continue;
        if (layout[i].semantic_kind == 1u && position_index < 0)
            position_index = (int)i;
        if (layout[i].semantic_kind == 2u && color_index < 0)
            color_index = (int)i;
    }

    if (position_index < 0 || color_index < 0)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    size_t byte_offset =
        (size_t)vertex_index * context->vertex_stride +
        (size_t)context->vertex_offset;

    if (byte_offset > context->vertex_buffer->desc.size ||
        context->vertex_stride >
            context->vertex_buffer->desc.size - byte_offset)
        return PCPS4_GPU_ERR_ARGUMENT;

    const uint8_t *v = context->vertex_buffer->data + byte_offset;

    if (!pcps4_decode_float3(
            v + layout[position_index].aligned_byte_offset,
            layout[position_index].format,
            position) ||
        !pcps4_decode_float4(
            v + layout[color_index].aligned_byte_offset,
            layout[color_index].format,
            color))
        return PCPS4_GPU_ERR_UNSUPPORTED;

    return PCPS4_GPU_OK;
}

static int pcps4_cpu_draw_indexed(
    pcps4_gpu_context_t *context,
    uint32_t index_count,
    uint32_t start_index,
    int32_t base_vertex)
{
    pcps4_gpu_texture_t *texture;

    if (context == NULL ||
        context->render_target == NULL ||
        !valid_rtv(context->render_target) ||
        context->index_buffer == NULL ||
        !valid_buffer(context->index_buffer))
        return PCPS4_GPU_ERR_STATE;

    texture = context->render_target->texture;
    if (!valid_texture(texture) || texture->data == NULL)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    if (context->index_format != 57u && context->index_format != 42u)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    if (context->primitive_topology != PCPS4_GPU_TOPOLOGY_TRIANGLELIST)
        return PCPS4_GPU_ERR_UNSUPPORTED;

    size_t index_size = context->index_format == 57u ? 2u : 4u;
    if ((size_t)start_index > context->index_buffer->desc.size / index_size ||
        (size_t)index_count >
            (context->index_buffer->desc.size / index_size) - start_index)
        return PCPS4_GPU_ERR_ARGUMENT;

    for (uint32_t base = 0; base + 2 < index_count; base += 3) {
        uint32_t raw[3];
        float p[3][3];
        float c[3][4];

        for (uint32_t i = 0; i < 3; ++i) {
            size_t byte = ((size_t)start_index + base + i) * index_size +
                          context->index_offset;

            if (byte > context->index_buffer->desc.size ||
                index_size >
                    context->index_buffer->desc.size - byte)
                return PCPS4_GPU_ERR_ARGUMENT;

            if (index_size == 2u) {
                uint16_t value;
                memcpy(&value, context->index_buffer->data + byte, sizeof(value));
                raw[i] = value;
            } else {
                uint32_t value;
                memcpy(&value, context->index_buffer->data + byte, sizeof(value));
                raw[i] = value;
            }

            if (base_vertex < 0) {
                if (raw[i] > (uint32_t)(-base_vertex))
                    raw[i] -= (uint32_t)(-base_vertex);
                else
                    return PCPS4_GPU_ERR_ARGUMENT;
            } else {
                if ((uint32_t)base_vertex > UINT32_MAX - raw[i])
                    return PCPS4_GPU_ERR_ARGUMENT;
                raw[i] += (uint32_t)base_vertex;
            }

            int rc = pcps4_cpu_fetch_vertex(context, raw[i], p[i], c[i]);
            if (rc != PCPS4_GPU_OK)
                return rc;
        }

        int rc = pcps4_raster_triangle(
            context, p[0], c[0], p[1], c[1], p[2], c[2]);
        if (rc != PCPS4_GPU_OK)
            return rc;
    }

    return PCPS4_GPU_OK;
}

int pcps4_gpu_context_draw_indexed(
    pcps4_gpu_context_t *context,
    uint32_t index_count,
    uint32_t start_index,
    int32_t base_vertex)
{
    const void *vs_data = NULL;
    const void *ps_data = NULL;
    size_t vs_size = 0;
    size_t ps_size = 0;
    uint32_t vs_type = 0;
    uint32_t ps_type = 0;
    int rc;

    if (!valid_context(context) || index_count == 0)
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

    /*
     * The optional GNM backend currently exposes DrawAuto only. Keep indexed
     * drawing explicit rather than silently changing the primitive semantics.
     */
    return PCPS4_GPU_ERR_UNSUPPORTED;
#else
    return pcps4_cpu_draw_indexed(
        context, index_count, start_index, base_vertex);
#endif
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

int pcps4_gpu_texture_cpu_data(const pcps4_gpu_texture_t *texture,
                               const uint8_t **data,
                               size_t *row_pitch,
                               uint32_t *width,
                               uint32_t *height)
{
    if (!valid_texture(texture) || texture->data == NULL ||
        data == NULL || row_pitch == NULL ||
        width == NULL || height == NULL)
        return PCPS4_GPU_ERR_ARGUMENT;

    *data = texture->data;
    *row_pitch = texture->row_pitch;
    *width = texture->desc.width;
    *height = texture->desc.height;
    return PCPS4_GPU_OK;
}
