#include <windows.h>

/*
 * Trivial Win32 console PE — the Wine-port stage-2 bring-up target.
 *
 * It exercises the plain kernel32 console path (GetStdHandle -> WriteFile ->
 * ExitProcess) with NO graphics, NO DXVK, NO window. If this runs, the PE
 * loader, import resolver, MS-ABI bridge and the host TEB/GSBASE bring-up are
 * all working for an ordinary Win32 program — the floor every real game stands
 * on, independent of the GPU path.
 *
 * There is no console device on the PS4, so the WriteFile may legitimately
 * write 0 bytes or fail; what we actually verify is that each call RETURNS
 * correctly (a valid handle, a sane BOOL) rather than faulting. The outcome is
 * encoded in the ExitProcess code, which PcPs4 logs as
 * "binary called ExitProcess(...)":
 *
 *   0x5A00  success: valid stdout handle + WriteFile returned without faulting
 *   0x00E1  GetStdHandle returned INVALID_HANDLE_VALUE
 *   0x00E2  GetStdHandle returned NULL
 *   (low byte on success = bytes WriteFile reported written, for information)
 */
void WINAPI WinMainCRTStartup(void)
{
    static const char msg[] = "PcPs4 Wine stage-2: console PE alive\n";
    DWORD written = 0;
    HANDLE out;
    BOOL ok;

    out = GetStdHandle(STD_OUTPUT_HANDLE);
    if (out == INVALID_HANDLE_VALUE)
        ExitProcess(0x00E1);
    if (out == NULL)
        ExitProcess(0x00E2);

    ok = WriteFile(out, msg, (DWORD)(sizeof(msg) - 1), &written, NULL);
    (void)ok; /* a no-op console is fine; we only require it not to fault */

    ExitProcess(0x5A00u | (written & 0xFFu));
}
