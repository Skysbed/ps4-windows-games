/*
 * Freestanding Windows DLL used by the bundled PS4 compatibility smoke test.
 * It has no imports: the loader must map it, run DllMain, resolve GameProbe
 * from its PE export table, and let the Windows smoke executable call it.
 */

typedef void *HINSTANCE;
typedef unsigned long DWORD;
typedef void *LPVOID;

__declspec(dllexport) int GameProbe(void)
{
    return 0xBEEF;
}

int DllMain(HINSTANCE instance, DWORD reason, LPVOID reserved)
{
    (void)instance;
    (void)reason;
    (void)reserved;
    return 1;
}
