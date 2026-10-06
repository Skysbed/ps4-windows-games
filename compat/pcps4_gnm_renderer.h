#ifndef PCPS4_GNM_RENDERER_H
#define PCPS4_GNM_RENDERER_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PCPS4_GNM_OK = 0,
    PCPS4_GNM_ERR_ARGUMENT = -1,
    PCPS4_GNM_ERR_MEMORY = -2,
    PCPS4_GNM_ERR_SHADER = -3,
    PCPS4_GNM_ERR_VIDEO = -4,
    PCPS4_GNM_ERR_COMMAND = -5,
    PCPS4_GNM_ERR_DRIVER = -6,
    PCPS4_GNM_ERR_UNAVAILABLE = -7,
    PCPS4_GNM_ERR_IO = -8
};

typedef struct pcps4_gnm_renderer pcps4_gnm_renderer_t;

pcps4_gnm_renderer_t *pcps4_gnm_renderer_create(
    uint32_t width,
    uint32_t height);

void pcps4_gnm_renderer_destroy(
    pcps4_gnm_renderer_t *renderer);

int pcps4_gnm_renderer_load_shaders(
    pcps4_gnm_renderer_t *renderer,
    const char *vertex_path,
    const char *pixel_path);

int pcps4_gnm_renderer_draw_triangle(
    pcps4_gnm_renderer_t *renderer,
    uint64_t frame_id);

int pcps4_gnm_renderer_render_frames(
    pcps4_gnm_renderer_t *renderer,
    uint32_t frame_count);

#ifdef __cplusplus
}
#endif

#endif
