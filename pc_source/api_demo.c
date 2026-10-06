/*
 * API bridge coverage sample.
 *
 * This intentionally uses Windows-shaped APIs so CI proves the translator
 * can route a real multi-subsystem source file through the PcPs4 OS adapter.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <windows.h>
#include <winsock2.h>

static DWORD WINAPI worker_thread(LPVOID argument)
{
    char *text = (char *)argument;
    strcat(text, " + worker");
    return 0;
}

int pcps4_api_demo(void)
{
    char buffer[64] = "hello";
    DWORD written = 0;
    DWORD thread_id = 0;

    HANDLE mutex = CreateMutexA(NULL, FALSE, "pcps4-demo");
    if (mutex != NULL) {
        WaitForSingleObject(mutex, INFINITE);
        ReleaseMutex(mutex);
        CloseHandle(mutex);
    }

    HANDLE thread = CreateThread(
        NULL, 0, worker_thread, buffer, 0, &thread_id);
    if (thread != NULL) {
        WaitForSingleObject(thread, INFINITE);
        CloseHandle(thread);
    }

    HANDLE file = CreateFileA(
        "pcps4_demo.txt",
        GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);

    if (file != INVALID_HANDLE_VALUE) {
        WriteFile(file, buffer, (DWORD)strlen(buffer), &written, NULL);
        CloseHandle(file);
    }

    void *memory = VirtualAlloc(
        NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (memory != NULL) {
        memcpy(memory, buffer, strlen(buffer) + 1);
        VirtualFree(memory, 4096, MEM_RELEASE);
    }

    WSADATA data;
    if (WSAStartup(0x0202, &data) == 0) {
        (void)WSAGetLastError();
        WSACleanup();
    }

    printf("API bridge: %s (%u bytes, thread %u)\n",
           buffer, (unsigned)written, (unsigned)thread_id);
    return 0;
}
