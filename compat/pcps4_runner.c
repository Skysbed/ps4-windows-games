/*
 * Hands-free test runner.
 *
 * After its first run the app stays open and polls /data/pcps4/trigger. Dropping that file over
 * FTP starts another run without touching the console:
 *   - trigger containing a path to a core module (e.g. /data/pcps4/core_v47.prx): the module is
 *     loaded and its pcps4_core_entry() runs the whole test sequence with the new code;
 *   - empty trigger: the built-in sequence runs again.
 * Each run ends by writing /data/pcps4/done ("<rc> <path>"), and /data/pcps4_runner_log.txt
 * records every step. Modules are never unloaded (threads they started may still exist), so each
 * upload needs a new file name.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <orbis/libkernel.h>

#include "pcps4_status.h"
#include "pcps4_version.h"

int pcps4_main_sequence(void);

/* Exported entry point of a core module (built with the same sources as the app). */
int pcps4_core_entry(void)
{
    int rc = pcps4_main_sequence();
    pcps4_status_suspend_display(); /* give VideoOut back to the runner */
    return rc;
}

static void runner_log(const char *fmt, const char *a, long b)
{
    FILE *f = fopen("/data/pcps4_runner_log.txt", "a");
    if (!f) return;
    fprintf(f, fmt, a ? a : "", b);
    fputc('\n', f);
    fclose(f);
}

static void write_done(long rc, const char *path)
{
    FILE *f = fopen("/data/pcps4/done.tmp", "w");
    if (!f) return;
    fprintf(f, "%ld %s\n", rc, path ? path : "");
    fclose(f);
    rename("/data/pcps4/done.tmp", "/data/pcps4/done");
}

void pcps4_runner_loop(void)
{
    char path[512];
    unsigned runs = 0;

    sceKernelMkdir("/data/pcps4", 0777);
    runner_log("[runner] ready, app build %s", PCPS4_BUILD_VERSION, 0);
    pcps4_status_step("Runner: waiting for FTP trigger (/data/pcps4/trigger)");
    write_done(0, "ready");

    for (;;) {
        sceKernelUsleep(500 * 1000);
        FILE *t = fopen("/data/pcps4/trigger", "r");
        if (!t) continue;
        memset(path, 0, sizeof(path));
        if (!fgets(path, sizeof(path), t)) path[0] = 0;
        fclose(t);
        sceKernelUnlink("/data/pcps4/trigger");
        path[strcspn(path, "\r\n")] = 0;
        runs++;
        runner_log("[runner] run %s (#%ld)", path[0] ? path : "<built-in>", (long)runs);

        pcps4_status_suspend_display();
        long rc = -1;
        if (path[0]) {
            int res = 0;
            int32_t h = (int32_t)sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &res);
            runner_log("[runner] load module %s -> handle %ld", path, (long)h);
            if (h >= 0) {
                void *fn = NULL;
                int32_t r = sceKernelDlsym(h, "pcps4_core_entry", &fn);
                runner_log("[runner] dlsym pcps4_core_entry -> %s", r == 0 && fn ? "ok" : "missing", (long)r);
                if (r == 0 && fn) rc = ((int (*)(void))fn)();
            } else {
                rc = h;
            }
        } else {
            rc = pcps4_main_sequence();
            pcps4_status_suspend_display();
        }
        runner_log("[runner] run finished rc=%s%ld", "", rc);
        pcps4_status_resume_display();
        pcps4_status_step("Runner: waiting for FTP trigger (/data/pcps4/trigger)");
        write_done(rc, path[0] ? path : "<built-in>");
    }
}
