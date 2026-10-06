/*
 * Windows-style PC input source.
 * This file is intentionally written against a small Win32/MSVC surface.
 * PcPs4 translates it before OpenOrbis compiles it.
 */

#include <stdio.h>
#include <string.h>
#include <windows.h>

void pcps4_runtime_demo(char *buffer, size_t buffer_size);
int pcps4_gnm_smoke_run(void);
int pcps4_d3d11_smoke_test(void);
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

    /* Stay on the result screen; never return from main. */
    pcps4_status_finish_forever();
    return 0;
}
