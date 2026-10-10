#include "pcps4_dxvk_diagnostics.h"

#include <string.h>
#include <stdlib.h>

int pcps4_dxvk_scanout_parse(FILE *stream,
                             pcps4_dxvk_scanout_report *out_report)
{
    char line[768];
    uint32_t previous_nonblack_rgb = 0;
    int have_previous_nonblack = 0;

    if (stream == NULL || out_report == NULL)
        return -1;

    memset(out_report, 0, sizeof(*out_report));

    while (fgets(line, sizeof(line), stream) != NULL) {
        const char *sample;
        char *end;
        unsigned long pixel;
        uint32_t rgb;

        if (strstr(line, "QueuePresent: flip done") != NULL)
            out_report->flip_count += 1;

        if (strstr(line, "QueuePresent: scanout") == NULL)
            continue;

        sample = strstr(line, "px[mid]=");
        if (sample == NULL)
            continue;
        sample += sizeof("px[mid]=") - 1;

        pixel = strtoul(sample, &end, 16);
        if (end == sample)
            continue;

        out_report->scanout_samples += 1;

        /* The diagnostic stores a packed 32-bit pixel. Ignore alpha. */
        rgb = (uint32_t)pixel & 0x00FFFFFFu;
        out_report->last_rgb = rgb;

        if (rgb == 0)
            continue;

        out_report->nonblack_samples += 1;
        if (!have_previous_nonblack) {
            out_report->first_rgb = rgb;
            have_previous_nonblack = 1;
        } else if (rgb != previous_nonblack_rgb) {
            out_report->color_changes += 1;
        }
        previous_nonblack_rgb = rgb;
    }

    if (ferror(stream))
        return -1;

    return 0;
}

int pcps4_dxvk_scanout_passes_visibility_check(
    const pcps4_dxvk_scanout_report *report)
{
    return report != NULL &&
           report->flip_count >= 2 &&
           report->scanout_samples >= 2 &&
           report->nonblack_samples >= 2 &&
           report->color_changes >= 1;
}
