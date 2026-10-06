#include "pcps4_gnm.h"

#if PCPS4_ENABLE_GNM
#include <orbis/GnmDriver.h>
#endif

int pcps4_gnm_submit(void *const *draw_buffers,
                     uint32_t *draw_sizes,
                     uint32_t draw_count,
                     void *const *compute_buffers,
                     uint32_t *compute_sizes,
                     uint32_t compute_count)
{
    uint32_t i;
    void *draw_nonconst[8];
    void *compute_nonconst[8];

    if (draw_count > 8 || compute_count > 8) {
        return PCPS4_GNM_ERR_ARGUMENT;
    }

    if (draw_count == 0 && compute_count == 0) {
        return PCPS4_GNM_ERR_ARGUMENT;
    }

    if (draw_count != 0 &&
        (draw_buffers == 0 || draw_sizes == 0)) {
        return PCPS4_GNM_ERR_ARGUMENT;
    }

    if (compute_count != 0 &&
        (compute_buffers == 0 || compute_sizes == 0)) {
        return PCPS4_GNM_ERR_ARGUMENT;
    }

    for (i = 0; i < draw_count; ++i) {
        if (draw_buffers[i] == 0 ||
            draw_sizes[i] == 0 ||
            (draw_sizes[i] & 3u) != 0) {
            return PCPS4_GNM_ERR_ARGUMENT;
        }

        draw_nonconst[i] = draw_buffers[i];
    }

    for (i = 0; i < compute_count; ++i) {
        if (compute_buffers[i] == 0 ||
            compute_sizes[i] == 0 ||
            (compute_sizes[i] & 3u) != 0) {
            return PCPS4_GNM_ERR_ARGUMENT;
        }

        compute_nonconst[i] = compute_buffers[i];
    }

#if PCPS4_ENABLE_GNM
    /*
     * OpenOrbis documents sceGnmAreSubmitsAllowed() as returning 1 when
     * submission will not stall and 0 when it would stall.
     */
    if (sceGnmAreSubmitsAllowed() == 0) {
        return PCPS4_GNM_ERR_NOT_READY;
    }

    {
        int32_t rc = sceGnmSubmitCommandBuffers(
            draw_count,
            draw_count ? draw_nonconst : 0,
            draw_count ? draw_sizes : 0,
            compute_count ? compute_nonconst : 0,
            compute_count ? compute_sizes : 0);

        if (rc != 0) {
            return PCPS4_GNM_ERR_DRIVER;
        }

        rc = sceGnmSubmitDone();
        if (rc != 0) {
            return PCPS4_GNM_ERR_DRIVER;
        }
    }

    return PCPS4_GNM_OK;
#else
    (void)draw_nonconst;
    (void)compute_nonconst;
    return PCPS4_GNM_ERR_UNAVAILABLE;
#endif
}
