#include <windows.h>

/*
 * Win32 surface smoke test (Wine-port stage 2 console path + stage 4 startup
 * APIs). No graphics, no DXVK, no window — it exercises the plain kernel32
 * surface a real game hits during CRT/process init, so a regression here is
 * caught before it reaches an actual title.
 *
 * Output goes to the PcPs4 runtime log via the console path
 * (GetStdHandle -> WriteFile, routed to the log). Each check logs its own
 * line; ExitProcess(0) on full success (green PASS in the harness), or a
 * small non-zero code naming the first failing check.
 */

static HANDLE g_out;

static void say(const char *s)
{
    DWORD w = 0, n = 0;
    while (s[n]) n++;
    WriteFile(g_out, s, n, &w, NULL);
}

void WINAPI WinMainCRTStartup(void)
{
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (g_out == INVALID_HANDLE_VALUE) ExitProcess(0x00E1);
    if (g_out == NULL)                 ExitProcess(0x00E2);
    say("PcPs4 Wine stage-2/4: console PE alive\n");

    /* GetVersion -> Windows 10 (major 10). */
    {
        DWORD v = GetVersion();
        if ((v & 0xFF) != 10) { say("FAIL GetVersion\n"); ExitProcess(0x41); }
        say("OK GetVersion=10\n");
    }

    /* GetSystemInfo -> sane page size + at least one CPU. */
    {
        SYSTEM_INFO si;
        GetSystemInfo(&si);
        if (si.dwPageSize == 0 || si.dwNumberOfProcessors == 0) { say("FAIL GetSystemInfo\n"); ExitProcess(0x42); }
        say("OK GetSystemInfo\n");
    }

    /* GetSystemTime -> a plausible year. */
    {
        SYSTEMTIME st;
        GetSystemTime(&st);
        if (st.wYear < 2024 || st.wMonth < 1 || st.wMonth > 12) { say("FAIL GetSystemTime\n"); ExitProcess(0x43); }
        say("OK GetSystemTime\n");
    }

    /* LocalAlloc(LPTR) -> zeroed, writable, freeable. */
    {
        unsigned char *p = (unsigned char *)LocalAlloc(0x0040 /*LPTR*/, 64);
        if (!p) { say("FAIL LocalAlloc\n"); ExitProcess(0x44); }
        if (p[0] != 0 || p[63] != 0) { say("FAIL LocalAlloc not zeroed\n"); ExitProcess(0x45); }
        p[0] = 0xAB; p[63] = 0xCD;
        if (p[0] != 0xAB || p[63] != 0xCD) { say("FAIL LocalAlloc rw\n"); ExitProcess(0x46); }
        LocalFree(p);
        say("OK LocalAlloc/Free\n");
    }

    /* GetACP -> an ANSI code page. */
    {
        UINT cp = GetACP();
        if (cp == 0) { say("FAIL GetACP\n"); ExitProcess(0x47); }
        say("OK GetACP\n");
    }

    /* GetModuleHandleExW(0, NULL, &h) -> this module's base. */
    {
        HMODULE h = NULL;
        if (!GetModuleHandleExW(0, NULL, &h) || h == NULL) { say("FAIL GetModuleHandleExW\n"); ExitProcess(0x48); }
        say("OK GetModuleHandleExW\n");
    }

    say("ALL Win32 surface checks passed, exiting 0\n");
    ExitProcess(0);
}
