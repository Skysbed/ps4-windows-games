/*
 * Windows-style PC input source.
 * This file is intentionally written against a small Win32/MSVC surface.
 * PcPs4 translates it before OpenOrbis compiles it.
 */

#include <stdio.h>
#include <windows.h>

void pcps4_runtime_demo(char *buffer, size_t buffer_size);
int pcps4_gnm_smoke_run(void);

static volatile LONG demo_value = 0;

int main(void)
{
    DWORD start = GetTickCount64();
    char buffer[32];

    InterlockedIncrement(&demo_value);
    Sleep(10);
    pcps4_runtime_demo(buffer, sizeof(buffer));

    printf("PcPs4 translated source is running.\n");
    printf("Elapsed: %u ms\n", (unsigned)(GetTickCount64() - start));
    printf("PID: %u\n", GetCurrentProcessId());
    printf("Value: %d\n", (int)demo_value);
    printf("Buffer: %s\n", buffer);

#if PCPS4_ENABLE_OPENGNM
    printf("Starting real GNM graphics smoke...\n");
    return pcps4_gnm_smoke_run();
#else
    return 0;
#endif
}
