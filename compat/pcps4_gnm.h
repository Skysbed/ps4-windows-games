#ifndef PCPS4_GNM_H
#define PCPS4_GNM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

enum {
    PCPS4_GNM_OK = 0,
    PCPS4_GNM_ERR_ARGUMENT = -1,
    PCPS4_GNM_ERR_UNAVAILABLE = -2,
    PCPS4_GNM_ERR_NOT_READY = -3,
    PCPS4_GNM_ERR_DRIVER = -4
};

/*
 * Safe command-buffer submission boundary.
 *
 * This does not manufacture a GNM command stream. It validates the host
 * pointers/counts first, checks the GNM driver's submit gate, and only then
 * submits. Actual PS4 shader/resource translation will sit above this API.
 */
int pcps4_gnm_submit(void *const *draw_buffers,
                     uint32_t *draw_sizes,
                     uint32_t draw_count,
                     void *const *compute_buffers,
                     uint32_t *compute_sizes,
                     uint32_t compute_count);

#ifdef __cplusplus
}
#endif

#endif
