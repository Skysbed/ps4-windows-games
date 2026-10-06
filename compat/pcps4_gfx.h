#ifndef PCPS4_GFX_H
#define PCPS4_GFX_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    uint8_t a;
} pcps4_gfx_color_t;

typedef struct {
    uint32_t width;
    uint32_t height;
    uint32_t bytes_per_pixel;
    uint32_t frame_count;
    uint32_t active_frame;
    int32_t video_handle;
    size_t direct_memory_size;
    int64_t direct_memory_offset;
    void *direct_memory;
    void *frame_buffers[3];
    void *flip_queue_storage;
} pcps4_gfx_context_t;

enum {
    PCPS4_GFX_OK = 0,
    PCPS4_GFX_ERR_ARGUMENT = -1,
    PCPS4_GFX_ERR_STATE = -2,
    PCPS4_GFX_ERR_PLATFORM = -3,
    PCPS4_GFX_ERR_MEMORY = -4,
    PCPS4_GFX_ERR_NOT_READY = -5,
    PCPS4_GFX_ERR_BACKEND = -6
};

int pcps4_gfx_init(pcps4_gfx_context_t *context,
                   uint32_t width,
                   uint32_t height,
                   uint32_t frame_count);

void pcps4_gfx_shutdown(pcps4_gfx_context_t *context);

int pcps4_gfx_clear(pcps4_gfx_context_t *context,
                    pcps4_gfx_color_t color);

int pcps4_gfx_draw_pixel(pcps4_gfx_context_t *context,
                         int32_t x,
                         int32_t y,
                         pcps4_gfx_color_t color);

int pcps4_gfx_draw_rect(pcps4_gfx_context_t *context,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height,
                        pcps4_gfx_color_t color);

int pcps4_gfx_present(pcps4_gfx_context_t *context,
                      uint64_t frame_id);

int pcps4_gfx_copy_rgba8(pcps4_gfx_context_t *context,
                         const uint8_t *rgba,
                         uint32_t src_width,
                         uint32_t src_height,
                         uint32_t src_stride);

/*
 * Copy an RGBA8 image (bytes r,g,b,a) to the current framebuffer, scaled to
 * fit the screen with its aspect ratio preserved (nearest-neighbour) and
 * black bars on the unused edges. Equivalent to DXGI_SCALING_ASPECT_RATIO_
 * STRETCH. Writes the framebuffer directly; this is the fast path used to
 * present a CPU-rendered back buffer.
 */
int pcps4_gfx_blit_rgba8_scaled(pcps4_gfx_context_t *context,
                                const uint8_t *rgba,
                                uint32_t src_width,
                                uint32_t src_height,
                                uint32_t src_stride);

#ifdef __cplusplus
}
#endif

#endif
