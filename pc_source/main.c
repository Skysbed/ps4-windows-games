/*
 * Windows-style PC input source.
 * This file is intentionally written against a small Win32/MSVC surface.
 * PcPs4 translates it before OpenOrbis compiles it.
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>
#include "pcps4_status.h"
#include "pcps4_game.h"

void pcps4_runtime_demo(char *buffer, size_t buffer_size);
int pcps4_gnm_smoke_run(void);
int pcps4_d3d11_smoke_test(void);
int pcps4_d3d11_game_run(void);
int pcps4_pe_exec_smoke(void);

void pcps4_status_begin(void);
void pcps4_status_step(const char *name);
void pcps4_status_report(const char *name, int ok);
void pcps4_status_finish_forever(void);

/* Wine-port host shim (compat/wine_host). Non-destructive on-device check of
 * the FreeBSD host primitives (GS-base, thr_self, kqueue, _umtx_op): it saves
 * and restores GSBASE, so it is safe to run, but it is OFF unless the env var
 * PCPS4_WINE_SELFTEST=1 is set — PcPs4's own runtime deliberately avoids GSBASE,
 * so we never touch it on an ordinary run. */
int pcps4_wine_host_selftest(void);
char *getenv(const char *name);

static volatile LONG demo_value = 0;

int main(void)
{
    DWORD start;
    char buffer[32];
    int pe_rc;

    /* Screen + log first, so every later step is visible. */
    pcps4_status_begin();

    /* Optional: prove the Wine host shim's FreeBSD primitives on-device.
     * Single-threaded here, and the self-test restores GSBASE, so it is safe;
     * gated so it never runs on an ordinary boot. Runs when built with
     * -DPCPS4_WINE_SELFTEST_FORCE=1 (controller launches have no env), or when
     * PCPS4_WINE_SELFTEST=1 is set in the environment. */
    {
#if defined(PCPS4_WINE_SELFTEST_FORCE) && PCPS4_WINE_SELFTEST_FORCE
        int run_selftest = 1;
#else
        const char *e = getenv("PCPS4_WINE_SELFTEST");
        int run_selftest = (e && e[0] == '1');
#endif
        if (run_selftest) {
            pcps4_status_step("Wine host shim self-test");
            pcps4_status_report("Wine host shim self-test",
                                pcps4_wine_host_selftest() == 0);
        }
    }

    /* Test 1: translated Win32 runtime calls. */
    pcps4_status_step("Win32 runtime (translated source)");
    start = GetTickCount64();
    LONG increment_result = InterlockedIncrement(&demo_value);
    Sleep(10);
    memset(buffer, 0, sizeof(buffer));
    pcps4_runtime_demo(buffer, sizeof(buffer));

    int runtime_ok =
        increment_result == 1 &&
        demo_value == 1 &&
        strcmp(buffer, "hello from PC source") == 0;

    pcps4_status_log(
        "[pcps4] Win32 checks: increment=%d value=%d elapsed=%u buffer=\"%s\"",
        (int)increment_result,
        (int)demo_value,
        (unsigned)(GetTickCount64() - start),
        buffer);

    pcps4_status_report("Win32 runtime (translated source)", runtime_ok);

    /* Test 2: load and execute a real (tiny) PE binary in executable memory. */
    pcps4_status_step("PE load + execute");
    pe_rc = pcps4_pe_exec_smoke();
    pcps4_status_report("PE load + execute", pe_rc == 0);

#if PCPS4_ENABLE_OPENGNM
    pcps4_status_step("GNM graphics smoke");
    pcps4_status_report("GNM graphics smoke", pcps4_gnm_smoke_run() == 0);

    pcps4_status_step("D3D11 -> GNM smoke");
    int d3d11_rc = pcps4_d3d11_smoke_test();
    pcps4_status_log(
        "[pcps4] D3D11 -> GNM smoke return code=%d",
        d3d11_rc);
    pcps4_status_report("D3D11 -> GNM smoke", d3d11_rc == 0);
#endif

#if PCPS4_RUN_GAME
    /*
     * The status screen owns VideoOut while the compatibility tests run.
     * Release it before the D3D11 path creates its own graphics context.
     */
    pcps4_status_suspend_display();

#if PCPS4_BUNDLED_GAME
    /* Run a staged Windows x86-64 PE executable through the PcPs4 runtime. */
    pcps4_status_step("Bundled Windows game");
    int32_t game_entry_ret = 0;
    int game_rc = pcps4_game_run_bundled(&game_entry_ret);
    pcps4_status_log(
        "[pcps4] bundled Windows game returned rc=%d entry=%d",
        game_rc, (int)game_entry_ret);
    pcps4_status_report("Windows PE process exit", game_rc == 0);
#if PCPS4_DXVK
    /*
     * ExitProcess(0) proves that the PE returned; it does not prove pixels
     * reached the display. Verify this run's changing, non-black scanout data
     * as a separate result so a successful loader cannot mask a black screen.
     */
    pcps4_status_step("DXVK scanout RGB probe");
    int scanout_rc = pcps4_game_validate_dxvk_scanout();
    pcps4_status_report("DXVK scanout RGB probe", scanout_rc == 0);
#endif
#else
    /* Enter the interactive PC-style D3D11 + XInput path. */
    int game_rc = pcps4_d3d11_game_run();
    pcps4_status_log("[pcps4] interactive D3D11 demo returned %d", game_rc);
#endif

    /* Reacquire VideoOut if the user exited the interactive path. */
    pcps4_status_resume_display();
#endif

    /* Stay on the result screen if the game path is disabled or returns. */
    pcps4_status_finish_forever();
    return 0;
}
