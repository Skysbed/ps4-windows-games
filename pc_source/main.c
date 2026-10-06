/*
 * Windows-style PC input source.
 * PcPs4 translates the supported Win32/MSVC surface before compilation.
 */

#include <stdio.h>
#include <windows.h>

static volatile LONG demo_value = 0;

int main(void)
{
    DWORD start = GetTickCount64();
    InterlockedIncrement(&demo_value);
    Sleep(10);

    printf("PcPs4 translated source is running.\n");
    printf("Elapsed: %u ms\n", (unsigned)(GetTickCount64() - start));
    printf("PID: %u\n", GetCurrentProcessId());
    printf("Value: %d\n", (int)demo_value);

    return 0;
}
