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

static volatile LONG demo_value = 0;

int main(void)
{
    DWORD start;
    char buffer[32];
    int pe_rc;

    /* Screen + log first, so every later step is visible. */
    pcps4_status_begin();

    /* Test 1: translated Win32 runtime calls. */
    pcps4_status_step("Win32 runtime (translated source)");
    start = GetTickCount64();
    InterlockedIncrement(&demo_value);
    Sleep(10);
    memset(buffer, 0, sizeof(buffer));
    pcps4_runtime_demo(buffer, sizeof(buffer));
    printf("Elapsed: %u ms, PID: %u, Value: %d, Buffer: %s\n",
           (unsigned)(GetTickCount64() - start), GetCurrentProcessId(),
           (int)demo_value, buffer);
    pcps4_status_report("Win32 runtime (translated source)",
                        demo_value == 1 && buffer[0] != '\0');

    /* Test 2: load and execute a real (tiny) PE binary in executable memory. */
    pcps4_status_step("PE load + execute");
    pe_rc = pcps4_pe_exec_smoke();
    pcps4_status_report("PE load + execute", pe_rc == 0);

#if PCPS4_ENABLE_OPENGNM
    pcps4_status_step("GNM graphics smoke");
    pcps4_status_report("GNM graphics smoke", pcps4_gnm_smoke_run() == 0);

    pcps4_status_step("D3D11 -> GNM smoke");
    pcps4_status_report("D3D11 -> GNM smoke", pcps4_d3d11_smoke_test() == 0);
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
    pcps4_status_report("Bundled Windows game", game_rc == 0);
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
