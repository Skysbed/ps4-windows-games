#ifndef PCPS4_DXVK_DIAGNOSTICS_H
#define PCPS4_DXVK_DIAGNOSTICS_H

#include <stdint.h>
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct pcps4_dxvk_scanout_report {
    uint32_t flip_count;
    uint32_t scanout_samples;
    uint32_t nonblack_samples;
    uint32_t color_changes;
    uint32_t first_rgb;
    uint32_t last_rgb;
} pcps4_dxvk_scanout_report;

/*
 * Parse the Vulkan driver's QueuePresent diagnostics from an already-open
 * breadcrumb log. RGB is taken from the low 24 bits of px[mid], deliberately
 * ignoring alpha so a black pixel with alpha=0xff is still black.
 *
 * Returns 0 when the stream was parsed, -1 for an invalid argument or I/O
 * error. A successful parse does not itself mean the scanout test passed;
 * use pcps4_dxvk_scanout_passes_visibility_check() for that decision.
 */
int pcps4_dxvk_scanout_parse(FILE *stream,
                             pcps4_dxvk_scanout_report *out_report);

/* The synthetic clear test must produce multiple non-black scanout samples. */
int pcps4_dxvk_scanout_passes_visibility_check(
    const pcps4_dxvk_scanout_report *report);

#ifdef __cplusplus
}
#endif

#endif
