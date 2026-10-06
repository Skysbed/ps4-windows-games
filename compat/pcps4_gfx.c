#include "pcps4_gfx.h"

#include <stdint.h>
#include <string.h>

#include <orbis/VideoOut.h>
#include <orbis/libkernel.h>

static int valid_context(const pcps4_gfx_context_t *context)
{
    return context != NULL &&
           context->video_handle >= 0 &&
           context->direct_memory != NULL &&
           context->direct_memory_size != 0 &&
           context->frame_count >= 2 &&
           context->frame_count <= 3 &&
           context->active_frame < context->frame_count;
}

static size_t frame_size(const pcps4_gfx_context_t *context)
{
    return (size_t)context->width *
           (size_t)context->height *
           (size_t)context->bytes_per_pixel;
}

static uint32_t encode_color(pcps4_gfx_color_t color)
{
    /*
     * Matches the pixel format used by OpenOrbis' reference CPU
     * framebuffer graphics sample.
     */
    return 0x80000000u |
           ((uint32_t)color.r << 16) |
           ((uint32_t)color.g << 8) |
           (uint32_t)color.b;
}

static void release_memory(pcps4_gfx_context_t *context)
{
    if (context->direct_memory != NULL &&
        context->direct_memory_size != 0) {
        (void)sceKernelMunmap(
            context->direct_memory,
            context->direct_memory_size);
    }

    if (context->direct_memory_offset != 0 &&
        context->direct_memory_size != 0) {
        (void)sceKernelReleaseDirectMemory(
            (off_t)context->direct_memory_offset,
            context->direct_memory_size);
    }

    context->direct_memory = NULL;
    context->direct_memory_offset = 0;
    context->direct_memory_size = 0;
}

int pcps4_gfx_init(pcps4_gfx_context_t *context,
                   uint32_t width,
                   uint32_t height,
                   uint32_t frame_count)
{
    size_t bytes;
    size_t allocation_size;
    uintptr_t framebuffer_start;
    int rc;
    uint32_t i;
    OrbisKernelEqueue *queue;
    OrbisVideoOutBufferAttribute attribute;

    if (context == NULL ||
        width < 64 || width > 3840 ||
        height < 64 || height > 2160 ||
        frame_count < 2 || frame_count > 3) {
        return PCPS4_GFX_ERR_ARGUMENT;
    }

    memset(context, 0, sizeof(*context));
    context->video_handle = -1;
    context->bytes_per_pixel = 4;
    context->width = width;
    context->height = height;
    context->frame_count = frame_count;

    bytes = frame_size(context);
    if (bytes == 0 || bytes > (SIZE_MAX / frame_count)) {
        return PCPS4_GFX_ERR_MEMORY;
    }

    allocation_size = bytes * frame_count;
    if (allocation_size > SIZE_MAX - 0x1FFFFFu) {
        return PCPS4_GFX_ERR_MEMORY;
    }

    allocation_size =
        (allocation_size + 0x1FFFFFu) & ~(size_t)0x1FFFFFu;

    context->direct_memory_size = allocation_size;

    rc = sceVideoOutOpen(
        ORBIS_VIDEO_USER_MAIN,
        ORBIS_VIDEO_OUT_BUS_MAIN,
        0,
        0);

    if (rc < 0) {
        context->video_handle = -1;
        context->direct_memory_size = 0;
        return PCPS4_GFX_ERR_PLATFORM;
    }

    context->video_handle = rc;

    rc = sceKernelAllocateDirectMemory(
        0,
        sceKernelGetDirectMemorySize(),
        allocation_size,
        0x200000,
        3,
        (off_t *)&context->direct_memory_offset);

    if (rc < 0) {
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        context->direct_memory_size = 0;
        context->direct_memory_offset = 0;
        return PCPS4_GFX_ERR_MEMORY;
    }

    rc = sceKernelMapDirectMemory(
        &context->direct_memory,
        allocation_size,
        0x33,
        0,
        (off_t)context->direct_memory_offset,
        0x200000);

    if (rc < 0) {
        release_memory(context);
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        return PCPS4_GFX_ERR_MEMORY;
    }

    framebuffer_start = (uintptr_t)context->direct_memory;
    for (i = 0; i < frame_count; ++i) {
        context->frame_buffers[i] =
            (void *)(framebuffer_start + bytes * i);
    }

    memset(&attribute, 0, sizeof(attribute));

    /*
     * SRGB framebuffer attributes used by the OpenOrbis graphics sample.
     */
    sceVideoOutSetBufferAttribute(
        &attribute,
        0x80000000u,
        1,
        0,
        width,
        height,
        width);

    rc = sceVideoOutRegisterBuffers(
        context->video_handle,
        0,
        context->frame_buffers,
        (int32_t)frame_count,
        &attribute);

    if (rc < 0) {
        release_memory(context);
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        return PCPS4_GFX_ERR_PLATFORM;
    }

    context->flip_queue_storage =
        __builtin_calloc(1, sizeof(OrbisKernelEqueue));

    if (context->flip_queue_storage == NULL) {
        (void)sceVideoOutUnregisterBuffers(
            context->video_handle,
            0);
        release_memory(context);
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        return PCPS4_GFX_ERR_MEMORY;
    }

    queue = (OrbisKernelEqueue *)context->flip_queue_storage;

    rc = sceKernelCreateEqueue(queue, "pcps4-gfx-flip");
    if (rc < 0) {
        __builtin_free(context->flip_queue_storage);
        context->flip_queue_storage = NULL;
        (void)sceVideoOutUnregisterBuffers(
            context->video_handle,
            0);
        release_memory(context);
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        return PCPS4_GFX_ERR_PLATFORM;
    }

    rc = sceVideoOutAddFlipEvent(
        *queue,
        context->video_handle,
        NULL);

    if (rc < 0) {
        (void)sceKernelDeleteEqueue(*queue);
        __builtin_free(context->flip_queue_storage);
        context->flip_queue_storage = NULL;
        (void)sceVideoOutUnregisterBuffers(
            context->video_handle,
            0);
        release_memory(context);
        sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
        return PCPS4_GFX_ERR_PLATFORM;
    }

    (void)sceVideoOutSetFlipRate(context->video_handle, 0);
    context->active_frame = 0;

    return PCPS4_GFX_OK;
}

void pcps4_gfx_shutdown(pcps4_gfx_context_t *context)
{
    if (context == NULL) {
        return;
    }

    if (context->video_handle >= 0) {
        (void)sceVideoOutUnregisterBuffers(
            context->video_handle,
            0);

        (void)sceVideoOutClose(context->video_handle);
        context->video_handle = -1;
    }

    if (context->flip_queue_storage != NULL) {
        OrbisKernelEqueue *queue =
            (OrbisKernelEqueue *)context->flip_queue_storage;

        (void)sceKernelDeleteEqueue(*queue);
        __builtin_free(context->flip_queue_storage);
        context->flip_queue_storage = NULL;
    }

    release_memory(context);
    memset(context->frame_buffers, 0, sizeof(context->frame_buffers));
    context->width = 0;
    context->height = 0;
    context->bytes_per_pixel = 0;
    context->frame_count = 0;
    context->active_frame = 0;
}

int pcps4_gfx_clear(pcps4_gfx_context_t *context,
                    pcps4_gfx_color_t color)
{
    size_t count;
    size_t i;
    uint32_t encoded;
    uint32_t *pixels;

    if (!valid_context(context)) {
        return PCPS4_GFX_ERR_STATE;
    }

    count = (size_t)context->width * context->height;
    encoded = encode_color(color);
    pixels =
        (uint32_t *)context->frame_buffers[context->active_frame];

    for (i = 0; i < count; ++i) {
        pixels[i] = encoded;
    }

    return PCPS4_GFX_OK;
}

int pcps4_gfx_draw_pixel(pcps4_gfx_context_t *context,
                         int32_t x,
                         int32_t y,
                         pcps4_gfx_color_t color)
{
    uint32_t *pixels;
    size_t index;

    if (!valid_context(context)) {
        return PCPS4_GFX_ERR_STATE;
    }

    if (x < 0 || y < 0 ||
        (uint32_t)x >= context->width ||
        (uint32_t)y >= context->height) {
        return PCPS4_GFX_ERR_ARGUMENT;
    }

    index = (size_t)(uint32_t)y * context->width +
            (size_t)(uint32_t)x;

    pixels =
        (uint32_t *)context->frame_buffers[context->active_frame];
    pixels[index] = encode_color(color);

    return PCPS4_GFX_OK;
}

int pcps4_gfx_draw_rect(pcps4_gfx_context_t *context,
                        int32_t x,
                        int32_t y,
                        int32_t width,
                        int32_t height,
                        pcps4_gfx_color_t color)
{
    int64_t x1;
    int64_t y1;
    int64_t x2;
    int64_t y2;
    int64_t py;

    if (!valid_context(context) || width <= 0 || height <= 0) {
        return PCPS4_GFX_ERR_ARGUMENT;
    }

    x1 = x;
    y1 = y;
    x2 = x1 + (int64_t)width;
    y2 = y1 + (int64_t)height;

    if (x2 <= 0 || y2 <= 0 ||
        x1 >= (int64_t)context->width ||
        y1 >= (int64_t)context->height) {
        return PCPS4_GFX_OK;
    }

    if (x1 < 0) x1 = 0;
    if (y1 < 0) y1 = 0;
    if (x2 > (int64_t)context->width) x2 = context->width;
    if (y2 > (int64_t)context->height) y2 = context->height;

    for (py = y1; py < y2; ++py) {
        int64_t px;
        for (px = x1; px < x2; ++px) {
            uint32_t *pixels =
                (uint32_t *)context->frame_buffers[context->active_frame];
            size_t index =
                (size_t)py * context->width + (size_t)px;

            pixels[index] = encode_color(color);
        }
    }

    return PCPS4_GFX_OK;
}

int pcps4_gfx_present(pcps4_gfx_context_t *context,
                      uint64_t frame_id)
{
    OrbisKernelEqueue *queue;
    OrbisKernelEvent event;
    OrbisVideoOutFlipStatus status;
    int count = 0;
    int rc;
    uint32_t spins;

    if (!valid_context(context)) {
        return PCPS4_GFX_ERR_STATE;
    }

    queue = (OrbisKernelEqueue *)context->flip_queue_storage;
    if (queue == NULL) {
        return PCPS4_GFX_ERR_STATE;
    }

    rc = sceVideoOutSubmitFlip(
        context->video_handle,
        (int32_t)context->active_frame,
        ORBIS_VIDEO_OUT_FLIP_VSYNC,
        (int64_t)frame_id);

    if (rc < 0) {
        return PCPS4_GFX_ERR_PLATFORM;
    }

    /*
     * Wait for the submitted frame, but fail closed instead of spinning
     * forever if the display subsystem stops returning flip events.
     */
    for (spins = 0; spins < 120; ++spins) {
        memset(&status, 0, sizeof(status));

        rc = sceVideoOutGetFlipStatus(
            context->video_handle,
            &status);

        if (rc == 0 &&
            status.flipArg == (int64_t)frame_id) {
            context->active_frame =
                (context->active_frame + 1) % context->frame_count;
            return PCPS4_GFX_OK;
        }

        OrbisKernelUseconds timeout = 100000;

        rc = sceKernelWaitEqueue(
            *queue,
            &event,
            1,
            &count,
            &timeout);

        if (rc != 0) {
            break;
        }
    }

    return PCPS4_GFX_ERR_NOT_READY;
}

int pcps4_gfx_copy_rgba8(pcps4_gfx_context_t *context,
                         const uint8_t *rgba,
                         uint32_t src_width,
                         uint32_t src_height,
                         uint32_t src_stride)
{
    uint32_t y;

    if (!valid_context(context) ||
        rgba == NULL ||
        src_width == 0 ||
        src_height == 0 ||
        src_width > context->width ||
        src_height > context->height ||
        src_stride < src_width * 4u) {
        return PCPS4_GFX_ERR_ARGUMENT;
    }

    for (y = 0; y < src_height; ++y) {
        uint32_t x;
        const uint8_t *row =
            rgba + ((size_t)y * src_stride);

        for (x = 0; x < src_width; ++x) {
            pcps4_gfx_color_t color = {
                row[x * 4u + 0],
                row[x * 4u + 1],
                row[x * 4u + 2],
                row[x * 4u + 3]
            };

            (void)pcps4_gfx_draw_pixel(
                context,
                (int32_t)x,
                (int32_t)y,
                color);
        }
    }

    return PCPS4_GFX_OK;
}

int pcps4_gfx_blit_rgba8_scaled(pcps4_gfx_context_t *context,
                                const uint8_t *rgba,
                                uint32_t src_width,
                                uint32_t src_height,
                                uint32_t src_stride)
{
    /* Column lookup table: destination x -> source byte offset. */
    static uint32_t column_offset[4096];
    uint32_t *pixels;
    uint32_t dst_w, dst_h, fit_w, fit_h, off_x, off_y;
    uint32_t x, y;

    if (!valid_context(context) || rgba == NULL ||
        src_width == 0 || src_height == 0 ||
        src_stride < src_width * 4u)
        return PCPS4_GFX_ERR_ARGUMENT;

    dst_w = context->width;
    dst_h = context->height;
    if (dst_w == 0 || dst_h == 0 || dst_w > 4096)
        return PCPS4_GFX_ERR_STATE;

    /* Fit inside the screen, preserving aspect ratio. */
    if ((uint64_t)src_width * dst_h >= (uint64_t)src_height * dst_w) {
        fit_w = dst_w;
        fit_h = (uint32_t)(((uint64_t)src_height * dst_w) / src_width);
    } else {
        fit_h = dst_h;
        fit_w = (uint32_t)(((uint64_t)src_width * dst_h) / src_height);
    }
    if (fit_w == 0) fit_w = 1;
    if (fit_h == 0) fit_h = 1;
    off_x = (dst_w - fit_w) / 2;
    off_y = (dst_h - fit_h) / 2;

    for (x = 0; x < fit_w; ++x)
        column_offset[x] =
            (uint32_t)(((uint64_t)x * src_width) / fit_w) * 4u;

    pixels = (uint32_t *)context->frame_buffers[context->active_frame];

    for (y = 0; y < dst_h; ++y) {
        uint32_t *row = pixels + (size_t)y * dst_w;

        if (y < off_y || y >= off_y + fit_h) {
            for (x = 0; x < dst_w; ++x)
                row[x] = 0x80000000u;
            continue;
        }

        {
            uint32_t sy = (uint32_t)(((uint64_t)(y - off_y) * src_height) /
                                     fit_h);
            /*
             * Defensive clamp: never index a source row at or past
             * src_height. Rounding at the bottom edge (or a dimension/pitch
             * mismatch upstream) would otherwise read into the heap just past
             * the back buffer, which shows up as a band of garbage (reddish
             * noise) along the bottom of the frame.
             */
            const uint8_t *src_row;
            if (sy >= src_height)
                sy = src_height - 1u;
            src_row = rgba + (size_t)sy * src_stride;

            for (x = 0; x < off_x; ++x)
                row[x] = 0x80000000u;
            for (x = 0; x < fit_w; ++x) {
                const uint8_t *p = src_row + column_offset[x];
                row[off_x + x] = 0x80000000u |
                                 ((uint32_t)p[0] << 16) |
                                 ((uint32_t)p[1] << 8) |
                                 (uint32_t)p[2];
            }
            for (x = off_x + fit_w; x < dst_w; ++x)
                row[x] = 0x80000000u;
        }
    }

    return PCPS4_GFX_OK;
}
