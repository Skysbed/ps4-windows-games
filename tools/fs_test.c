/* Filesystem smoke test: what Minecraft does when it creates a world. */
#include <windows.h>
#include <stdio.h>

static FILE *out;
static void step(const char *what, BOOL ok)
{
    fprintf(out, "%-48s %s (GetLastError %lu)\n", what, ok ? "OK  " : "FAIL", ok ? 0 : GetLastError());
    fflush(out);
}

int main(void)
{
    const char *base = "Z:\\data\\stkuser\\fstest";
    char dir[MAX_PATH], f1[MAX_PATH], f2[MAX_PATH];
    HANDLE h;
    DWORD n;

    out = fopen("Z:\\data\\stkuser\\fstest.log", "w");
    if (!out) return 1;
    snprintf(dir, sizeof dir, "%s\\dir", base);
    snprintf(f1, sizeof f1, "%s\\a.txt", base);
    snprintf(f2, sizeof f2, "%s\\b.txt", base);

    step("CreateDirectory base", CreateDirectoryA(base, NULL) || GetLastError() == ERROR_ALREADY_EXISTS);
    step("CreateDirectory dir", CreateDirectoryA(dir, NULL));
    step("RemoveDirectory dir (empty)", RemoveDirectoryA(dir));
    step("dir gone", GetFileAttributesA(dir) == INVALID_FILE_ATTRIBUTES);

    h = CreateFileA(f1, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    step("CreateFile a.txt", h != INVALID_HANDLE_VALUE);
    step("WriteFile a.txt", WriteFile(h, "hello", 5, &n, NULL) && n == 5);
    CloseHandle(h);
    h = CreateFileA(f2, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, 0, NULL);
    step("CreateFile b.txt", h != INVALID_HANDLE_VALUE);
    step("WriteFile b.txt", WriteFile(h, "old", 3, &n, NULL) && n == 3);
    CloseHandle(h);
    step("MoveFileEx a->b REPLACE_EXISTING", MoveFileExA(f1, f2, MOVEFILE_REPLACE_EXISTING));
    {
        char buf[16] = {0};
        h = CreateFileA(f2, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING, 0, NULL);
        ReadFile(h, buf, sizeof buf - 1, &n, NULL);
        CloseHandle(h);
        fprintf(out, "b.txt now holds \"%s\" (want \"hello\")\n", buf);
    }
    step("a.txt gone after move", GetFileAttributesA(f1) == INVALID_FILE_ATTRIBUTES);
    step("DeleteFile b.txt", DeleteFileA(f2));
    step("b.txt gone", GetFileAttributesA(f2) == INVALID_FILE_ATTRIBUTES);
    step("RemoveDirectory base", RemoveDirectoryA(base));
    step("base gone", GetFileAttributesA(base) == INVALID_FILE_ATTRIBUTES);
    fprintf(out, "done\n");
    fclose(out);
    return 0;
}
