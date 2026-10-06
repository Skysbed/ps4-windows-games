#ifndef PCPS4_GPU_H
#define PCPS4_GPU_H

#include <stddef.h>
#include <stdint.h>

#include "pcps4_gfx.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcps4_gpu_device pcps4_gpu_device_t;
typedef struct pcps4_gpu_context pcps4_gpu_context_t;
typedef struct pcps4_gpu_buffer pcps4_gpu_buffer_t;
typedef struct pcps4_gpu_texture pcps4_gpu_texture_t;
typedef struct pcps4_gpu_shader pcps4_gpu_shader_t;
typedef struct pcps4_gpu_render_target pcps4_gpu_render_target_t;
typedef struct pcps4_gnm_renderer pcps4_gnm_renderer_t;

typedef enum {
    PCPS4_GPU_FORMAT_RGBA8_UNORM = 1,
    PCPS4_GPU_FORMAT_BGRA8_UNORM = 2
} pcps4_gpu_format_t;

enum {
    PCPS4_GPU_BIND_VERTEX_BUFFER    = 1u << 0,
    PCPS4_GPU_BIND_INDEX_BUFFER     = 1u << 1,
    PCPS4_GPU_BIND_CONSTANT_BUFFER  = 1u << 2,
    PCPS4_GPU_BIND_SHADER_RESOURCE  = 1u << 3,
    PCPS4_GPU_BIND_RENDER_TARGET    = 1u << 4
};

enum {
    PCPS4_GPU_USAGE_DEFAULT = 0,
    PCPS4_GPU_USAGE_DYNAMIC = 1
};

enum {
    PCPS4_GPU_SHADER_VERTEX = 1,
    PCPS4_GPU_SHADER_PIXEL = 2
};

enum {
    PCPS4_GPU_OK = 0,
    PCPS4_GPU_ERR_ARGUMENT = -1,
    PCPS4_GPU_ERR_MEMORY = -2,
    PCPS4_GPU_ERR_STATE = -3,
    PCPS4_GPU_ERR_UNSUPPORTED = -4,
    PCPS4_GPU_ERR_BACKEND = -5
};

typedef struct {
    size_t size;
    uint32_t bind_flags;
    uint32_t usage;
    uint32_t cpu_write;
} pcps4_gpu_buffer_desc_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t mip_levels;
    pcps4_gpu_format_t format;
    uint32_t bind_flags;
} pcps4_gpu_texture_desc_t;

typedef struct {
    const void *data;
    size_t size;
    uint32_t type;
} pcps4_gpu_shader_desc_t;

typedef struct {
    pcps4_gpu_texture_t *texture;
} pcps4_gpu_render_target_desc_t;

typedef struct {
    uint32_t input_slot;
    uint32_t aligned_byte_offset;
    uint32_t format;
    uint32_t semantic_index;
} pcps4_gpu_vertex_element_t;

pcps4_gpu_device_t *pcps4_gpu_device_create(void);
void pcps4_gpu_device_destroy(pcps4_gpu_device_t *device);

pcps4_gpu_context_t *pcps4_gpu_context_create(pcps4_gpu_device_t *device);
void pcps4_gpu_context_destroy(pcps4_gpu_context_t *context);
int pcps4_gpu_context_attach_graphics(
    pcps4_gpu_context_t *context,
    pcps4_gfx_context_t *graphics);
int pcps4_gpu_context_attach_gnm(
    pcps4_gpu_context_t *context,
    pcps4_gnm_renderer_t *renderer);
int pcps4_gpu_context_set_input_layout(
    pcps4_gpu_context_t *context,
    const pcps4_gpu_vertex_element_t *elements,
    uint32_t count);
int pcps4_gpu_context_get_input_layout(
    const pcps4_gpu_context_t *context,
    const pcps4_gpu_vertex_element_t **elements,
    uint32_t *count);

pcps4_gpu_buffer_t *pcps4_gpu_buffer_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_buffer_desc_t *desc,
    const void *initial_data);
void pcps4_gpu_buffer_destroy(pcps4_gpu_buffer_t *buffer);
int pcps4_gpu_buffer_update(pcps4_gpu_buffer_t *buffer,
                            size_t offset,
                            const void *data,
                            size_t size);
int pcps4_gpu_buffer_map(pcps4_gpu_buffer_t *buffer,
                         uint32_t map_mode,
                         void **data);
int pcps4_gpu_buffer_unmap(pcps4_gpu_buffer_t *buffer);

pcps4_gpu_texture_t *pcps4_gpu_texture_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_texture_desc_t *desc,
    const void *initial_data,
    size_t row_pitch);
pcps4_gpu_texture_t *pcps4_gpu_texture_create_backbuffer(
    pcps4_gpu_device_t *device,
    pcps4_gfx_context_t *graphics);
pcps4_gpu_texture_t *pcps4_gpu_texture_create_gnm_backbuffer(
    pcps4_gpu_device_t *device,
    pcps4_gnm_renderer_t *renderer,
    uint32_t width,
    uint32_t height);
void pcps4_gpu_texture_destroy(pcps4_gpu_texture_t *texture);
int pcps4_gpu_texture_update_rgba8(pcps4_gpu_texture_t *texture,
                                   const void *data,
                                   size_t row_pitch,
                                   uint32_t width,
                                   uint32_t height);
int pcps4_gpu_texture_get_desc(
    const pcps4_gpu_texture_t *texture,
    pcps4_gpu_texture_desc_t *desc);

pcps4_gpu_shader_t *pcps4_gpu_shader_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_shader_desc_t *desc);
void pcps4_gpu_shader_destroy(pcps4_gpu_shader_t *shader);
int pcps4_gpu_shader_get_blob(
    const pcps4_gpu_shader_t *shader,
    const void **data,
    size_t *size,
    uint32_t *type);

pcps4_gpu_render_target_t *pcps4_gpu_render_target_create(
    pcps4_gpu_device_t *device,
    const pcps4_gpu_render_target_desc_t *desc);
void pcps4_gpu_render_target_destroy(
    pcps4_gpu_render_target_t *render_target);

int pcps4_gpu_context_set_render_target(
    pcps4_gpu_context_t *context,
    pcps4_gpu_render_target_t *render_target);
int pcps4_gpu_context_clear(
    pcps4_gpu_context_t *context,
    pcps4_gfx_color_t color);
int pcps4_gpu_context_present(
    pcps4_gpu_context_t *context,
    uint64_t frame_id);
int pcps4_gpu_context_draw(
    pcps4_gpu_context_t *context,
    uint32_t vertex_count,
    uint32_t start_vertex);
int pcps4_gpu_context_draw_indexed(
    pcps4_gpu_context_t *context,
    uint32_t index_count,
    uint32_t start_index,
    int32_t base_vertex);

int pcps4_gpu_set_vertex_buffer(
    pcps4_gpu_context_t *context,
    pcps4_gpu_buffer_t *buffer,
    uint32_t stride,
    uint32_t offset);
int pcps4_gpu_set_vertex_buffer_slot(
    pcps4_gpu_context_t *context,
    uint32_t slot,
    pcps4_gpu_buffer_t *buffer,
    uint32_t stride,
    uint32_t offset);
int pcps4_gpu_set_index_buffer(
    pcps4_gpu_context_t *context,
    pcps4_gpu_buffer_t *buffer,
    uint32_t format,
    uint32_t offset);
int pcps4_gpu_set_vertex_shader(
    pcps4_gpu_context_t *context,
    pcps4_gpu_shader_t *shader);
int pcps4_gpu_set_pixel_shader(
    pcps4_gpu_context_t *context,
    pcps4_gpu_shader_t *shader);

#ifdef __cplusplus
}
#endif

#endif
