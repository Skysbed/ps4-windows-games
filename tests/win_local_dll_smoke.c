#include <windows.h>

__declspec(dllexport) int WINAPI PcPs4LocalProbe(int seed)
{
    return seed * 17 + 5;
}

BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return TRUE;
}
