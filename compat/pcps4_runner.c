/*
 * Hands-free test runner.
 *
 * The app runs the test sequence once, then stays open and polls /data/pcps4/trigger. Dropping
 * that file over FTP starts another run without touching the console:
 *   - trigger containing /data/pcps4/core_<label>.prx: that module is loaded and its
 *     pcps4_core_entry() runs the whole sequence with the new code;
 *   - empty trigger: the built-in sequence runs again.
 * Every run happens on its own thread. A crash in any thread is reported to the crash file and
 * the faulting thread is parked instead of ending the process; the runner then abandons the run,
 * releases the display and keeps waiting. Each run ends by writing /data/pcps4/done
 * ("<rc> <path>"); /data/pcps4_runner_log.txt records every step. Modules are never unloaded
 * (abandoned threads may still reference them), so each upload needs a new file name.
 */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <orbis/libkernel.h>

#include "pcps4_fault.h"
#include "pcps4_status.h"
#include "pcps4_version.h"

int pcps4_main_sequence(void);

/* Exported entry point of a core module (built from the same sources as the app). */
int pcps4_core_entry(void)
{
    int rc = pcps4_main_sequence();
    pcps4_status_suspend_display(); /* give VideoOut back to the runner */
    return rc;
}

/* Exported: release whatever display an abandoned run still holds. */
void pcps4_core_abandon(void)
{
    extern void vk_ps4_force_close_video_out(void) __attribute__((weak));
    if (vk_ps4_force_close_video_out)
        vk_ps4_force_close_video_out();
    pcps4_status_suspend_display();
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

/* The functions of whichever code a run uses: the module's own copies or the built-in ones. */
typedef struct {
    int (*entry)(void);
    void (*abandon)(void);
    void (*set_park)(int);
    int (*fault_count)(void);
} CoreFns;

static volatile int g_run_done;
static volatile long g_run_rc;
static CoreFns g_run_fns;

static void *run_thread(void *arg)
{
    (void)arg;
    pcps4_fault_thread_init();
    g_run_rc = g_run_fns.entry();
    g_run_done = 1;
    return NULL;
}

/* Run fns.entry on a fresh thread; returns its rc, or -1000 - n on a crash, -999 on a timeout. */
static long run_protected(const CoreFns *fns)
{
    OrbisPthread th;
    OrbisPthreadAttr attr;
    g_run_fns = *fns;
    g_run_done = 0;
    g_run_rc = -1;
    int faults_before = fns->fault_count();
    fns->set_park(1);
    scePthreadAttrInit(&attr);
    scePthreadAttrSetstacksize(&attr, 16 * 1024 * 1024);
    if (scePthreadCreate(&th, &attr, run_thread, NULL, "pcps4_run") != 0) {
        scePthreadAttrDestroy(&attr);
        return -998;
    }
    scePthreadAttrDestroy(&attr);
    for (unsigned waited = 0; waited < 600 * 10; waited++) { /* 10 minutes */
        if (g_run_done) {
            scePthreadJoin(th, NULL);
            return g_run_rc;
        }
        int faults = fns->fault_count();
        if (faults != faults_before) {
            sceKernelUsleep(300 * 1000); /* let the crash file be written */
            return -1000 - faults;
        }
        sceKernelUsleep(100 * 1000);
    }
    return -999;
}

static int load_module(const char *path, CoreFns *fns)
{
    int res = 0;
    int32_t h = (int32_t)sceKernelLoadStartModule(path, 0, NULL, 0, NULL, &res);
    runner_log("[runner] load module %s -> handle %ld", path, (long)h);
    if (h < 0) return -1;
    void *e = NULL, *a = NULL, *p = NULL, *c = NULL;
    sceKernelDlsym(h, "pcps4_core_entry", &e);
    sceKernelDlsym(h, "pcps4_core_abandon", &a);
    sceKernelDlsym(h, "pcps4_fault_set_park", &p);
    sceKernelDlsym(h, "pcps4_fault_count", &c);
    runner_log("[runner] symbols %s (%ld of 4)", "", (long)(!!e + !!a + !!p + !!c));
    if (!e || !a || !p || !c) return -2;
    fns->entry = (int (*)(void))e;
    fns->abandon = (void (*)(void))a;
    fns->set_park = (void (*)(int))p;
    fns->fault_count = (int (*)(void))c;
    return 0;
}

static void do_run(const char *path)
{
    CoreFns fns = {pcps4_core_entry, pcps4_core_abandon, pcps4_fault_set_park, pcps4_fault_count};
    long rc;
    runner_log("[runner] run %s", path[0] ? path : "<built-in>", 0);
    pcps4_status_suspend_display();
    if (path[0] && load_module(path, &fns) != 0) {
        rc = -997;
    } else {
        rc = run_protected(&fns);
        if (rc <= -999) {
            runner_log("[runner] run abandoned (%s%ld): releasing the display", "", rc);
            fns.abandon();
        }
    }
    runner_log("[runner] run finished rc=%s%ld", "", rc);
    pcps4_status_resume_display();
    pcps4_status_step("Runner: waiting for FTP trigger (/data/pcps4/trigger)");
    write_done(rc, path[0] ? path : "<built-in>");
}

void pcps4_runner_loop(void)
{
    char path[512];

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
        /* Only load modules from the runner's own folder with the expected name. */
        if (path[0] && (strncmp(path, "/data/pcps4/core_", 17) != 0 || strstr(path, "..") ||
                        strlen(path) < 22 || strcmp(path + strlen(path) - 4, ".prx") != 0)) {
            runner_log("[runner] rejected module path %s", path, 0);
            write_done(-2, path);
            continue;
        }
        do_run(path);
    }
}
