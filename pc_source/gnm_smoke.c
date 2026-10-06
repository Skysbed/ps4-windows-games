#include <stdio.h>

#include "pcps4_gnm_renderer.h"

int pcps4_gnm_smoke_run(void)
{
#if PCPS4_ENABLE_OPENGNM
    pcps4_gnm_renderer_t *renderer =
        pcps4_gnm_renderer_create(1280, 720);

    if (renderer == NULL) {
        printf("PcPs4 GNM: renderer initialization failed\n");
        return PCPS4_GNM_ERR_VIDEO;
    }

    int result = pcps4_gnm_renderer_load_shaders(
        renderer,
        "/app0/assets/shaders/tri.vert.sb",
        "/app0/assets/shaders/tri.frag.sb");

    if (result == PCPS4_GNM_OK)
        result = pcps4_gnm_renderer_render_frames(renderer, 120);

    if (result != PCPS4_GNM_OK)
        printf("PcPs4 GNM: smoke failed: %d\n", result);

    pcps4_gnm_renderer_destroy(renderer);
    return result;
#else
    return PCPS4_GNM_ERR_UNAVAILABLE;
#endif
}
