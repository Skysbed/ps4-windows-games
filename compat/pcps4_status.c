/*
 * PcPs4 on-console status reporting. See pcps4_status.h.
 */

#include "pcps4_status.h"
#include "pcps4_gfx.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

extern int sceKernelUsleep(unsigned int microseconds);

#define PCPS4_STATUS_MAX_TESTS 8
#define PCPS4_STATUS_LOG_PATH "/data/pcps4_log.txt"

static FILE *g_log = NULL;
static pcps4_gfx_context_t g_gfx;
static int g_gfx_ok = 0;
static uint64_t g_frame = 1;

static int g_results[PCPS4_STATUS_MAX_TESTS];
static int g_result_count = 0;
static int g_running = 0; /* a step is in progress (drawn yellow) */

static const pcps4_gfx_color_t COLOR_BG     = { 24, 24, 32, 255 };
static const pcps4_gfx_color_t COLOR_PASS   = { 0, 190, 70, 255 };
static const pcps4_gfx_color_t COLOR_FAIL   = { 220, 30, 30, 255 };
static const pcps4_gfx_color_t COLOR_RUN    = { 230, 190, 0, 255 };
static const pcps4_gfx_color_t COLOR_IDLE   = { 60, 60, 80, 255 };

void pcps4_status_log(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    printf("\n");

    if (g_log != NULL) {
        va_start(ap, fmt);
        vfprintf(g_log, fmt, ap);
        va_end(ap);
        fputc('\n', g_log);
        fflush(g_log);
    }
}

static void pcps4_status_redraw(int final_screen)
{
    int i;
    int all_ok = 1;
    int32_t w, h, bar_h, gap, top;

    if (!g_gfx_ok)
        return;

    w = (int32_t)g_gfx.width;
    h = (int32_t)g_gfx.height;

    (void)pcps4_gfx_clear(&g_gfx, COLOR_BG);

    for (i = 0; i < g_result_count; ++i)
        if (!g_results[i])
            all_ok = 0;

    /* Top banner: grey while running, then green/red overall result. */
    (void)pcps4_gfx_draw_rect(&g_gfx, 0, 0, w, h / 8,
                              final_screen
                                  ? (all_ok ? COLOR_PASS : COLOR_FAIL)
                                  : COLOR_IDLE);

    /* One bar per test. */
    top = h / 8 + h / 20;
    gap = h / 60;
    bar_h = (h - top - h / 20) / PCPS4_STATUS_MAX_TESTS - gap;
    for (i = 0; i < PCPS4_STATUS_MAX_TESTS; ++i) {
        pcps4_gfx_color_t c = COLOR_IDLE;
        if (i < g_result_count)
            c = g_results[i] ? COLOR_PASS : COLOR_FAIL;
        else if (i == g_result_count && g_running)
            c = COLOR_RUN;
        else
            continue;
        (void)pcps4_gfx_draw_rect(&g_gfx, w / 10, top + i * (bar_h + gap),
                                  (w * 8) / 10, bar_h, c);
    }

    (void)pcps4_gfx_present(&g_gfx, g_frame++);
}

void pcps4_status_begin(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);

    g_log = fopen(PCPS4_STATUS_LOG_PATH, "w");
    pcps4_status_log("[pcps4] status: begin (log file %s)",
                     g_log ? "open" : "unavailable");

    memset(&g_gfx, 0, sizeof(g_gfx));
    if (pcps4_gfx_init(&g_gfx, 1920, 1080, 2) == PCPS4_GFX_OK) {
        g_gfx_ok = 1;
        pcps4_status_log("[pcps4] status: screen ready %ux%u",
                         (unsigned)g_gfx.width, (unsigned)g_gfx.height);
    } else {
        pcps4_status_log("[pcps4] status: screen unavailable, log only");
    }
    pcps4_status_redraw(0);
}

void pcps4_status_step(const char *name)
{
    g_running = 1;
    pcps4_status_log("[pcps4] step: %s ...", name);
    pcps4_status_redraw(0);
}

void pcps4_status_report(const char *name, int ok)
{
    g_running = 0;
    if (g_result_count < PCPS4_STATUS_MAX_TESTS)
        g_results[g_result_count++] = ok ? 1 : 0;
    pcps4_status_log("[pcps4] result: %s = %s", name, ok ? "PASS" : "FAIL");
    pcps4_status_redraw(0);
}

void pcps4_status_suspend_display(void)
{
    if (!g_gfx_ok)
        return;

    pcps4_gfx_shutdown(&g_gfx);
    g_gfx_ok = 0;
}

void pcps4_status_resume_display(void)
{
    if (g_gfx_ok)
        return;

    if (pcps4_gfx_init(&g_gfx, 1920, 1080, 2) == PCPS4_GFX_OK) {
        g_gfx_ok = 1;
        pcps4_status_redraw(0);
    } else {
        pcps4_status_log("[pcps4] status: screen unavailable after game path");
    }
}

void pcps4_status_finish_forever(void)
{
    int i, passed = 0;
    for (i = 0; i < g_result_count; ++i)
        passed += g_results[i];
    pcps4_status_log("[pcps4] finished: %d/%d passed. Staying on result "
                     "screen (close the app from the PS button menu).",
                     passed, g_result_count);

    for (;;) {
        pcps4_status_redraw(1);
        sceKernelUsleep(500000);
    }
}
