#define _POSIX_C_SOURCE 200112L
#include "pcps4_winapi.h"

#include "pcps4_compat.h"
#include "pcps4_xinput.h"
#include "pcps4_d3d11_com.h"
#include "pcps4_loader.h"
#include "pcps4_module.h"
#include "pcps4_msabi.h"
#include "pcps4_winabi->h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

static __thread DWORD g_last_error = 0;
typedef struct pcps4_module_handle {
    uint32_t magic;
    char name[128];
} pcps4_module_handle;

#define PCPS4_MODULE_MAGIC 0x504d4f44u

static pcps4_pe_resolver *g_resolver = NULL;
static pcps4_module_loader *g_module_loader = NULL;
static char g_main_module_name[128] = "game.exe";

static void PCPS4_MS_ABI win_SetLastError(DWORD value);
#define PCPS4_MAX_TLS_SLOTS 64u
#define TLS_OUT_OF_INDEXES 0xFFFFFFFFu
static uint64_t g_tls_bitmap;
static __thread void *g_tls_values[PCPS4_MAX_TLS_SLOTS];

typedef struct pcps4_win_critical_section {
    void *DebugInfo;
    int32_t LockCount;
    int32_t RecursionCount;
    HANDLE OwningThread;
    HANDLE LockSemaphore;
    uintptr_t SpinCount;
} pcps4_win_critical_section;

static BOOL PCPS4_MS_ABI win_InitializeCriticalSectionEx(
    void *critical_section, DWORD spin_count, DWORD flags)
{
    pcps4_win_critical_section *cs =
        (pcps4_win_critical_section *)critical_section;
    pcps4_os_handle_t *mutex;

    (void)spin_count;
    (void)flags;

    if (cs == NULL)
        return FALSE;

    mutex = pcps4_sys_mutex_create("pcps4-critical-section");
    if (mutex == NULL)
        return FALSE;

    memset(cs, 0, sizeof(*cs));
    cs->LockSemaphore = (HANDLE)mutex;
    return TRUE;
}

static void PCPS4_MS_ABI win_InitializeCriticalSection(void *critical_section)
{
    (void)win_InitializeCriticalSectionEx(critical_section, 0, 0);
}

static BOOL PCPS4_MS_ABI win_InitializeCriticalSectionAndSpinCount(
    void *critical_section, DWORD spin_count)
{
    return win_InitializeCriticalSectionEx(critical_section, spin_count, 0);
}

static void PCPS4_MS_ABI win_DeleteCriticalSection(void *critical_section)
{
    pcps4_win_critical_section *cs =
        (pcps4_win_critical_section *)critical_section;

    if (cs == NULL)
        return;

    if (cs->LockSemaphore != NULL) {
        (void)pcps4_sys_handle_close(
            (pcps4_os_handle_t *)cs->LockSemaphore);
        cs->LockSemaphore = NULL;
    }
    cs->OwningThread = NULL;
    cs->RecursionCount = 0;
}

static void PCPS4_MS_ABI win_EnterCriticalSection(void *critical_section)
{
    pcps4_win_critical_section *cs =
        (pcps4_win_critical_section *)critical_section;
    uint32_t thread_id;

    if (cs == NULL)
        return;

    if (cs->LockSemaphore == NULL &&
        !win_InitializeCriticalSectionEx(cs, 0, 0))
        return;

    thread_id = pcps4_sys_thread_id();
    if (cs->OwningThread == (HANDLE)(uintptr_t)thread_id) {
        ++cs->RecursionCount;
        cs->LockCount = cs->RecursionCount - 1;
        return;
    }

    (void)pcps4_sys_handle_wait(
        (pcps4_os_handle_t *)cs->LockSemaphore, INFINITE);
    cs->OwningThread = (HANDLE)(uintptr_t)thread_id;
    cs->RecursionCount = 1;
    cs->LockCount = 0;
}

static BOOL PCPS4_MS_ABI win_TryEnterCriticalSection(void *critical_section)
{
    pcps4_win_critical_section *cs =
        (pcps4_win_critical_section *)critical_section;
    uint32_t thread_id;
    int wait_rc;

    if (cs == NULL)
        return FALSE;

    if (cs->LockSemaphore == NULL &&
        !win_InitializeCriticalSectionEx(cs, 0, 0))
        return FALSE;

    thread_id = pcps4_sys_thread_id();
    if (cs->OwningThread == (HANDLE)(uintptr_t)thread_id) {
        ++cs->RecursionCount;
        cs->LockCount = cs->RecursionCount - 1;
        return TRUE;
    }

    wait_rc = pcps4_sys_mutex_trylock(
        (pcps4_os_handle_t *)cs->LockSemaphore);
    if (wait_rc != 0)
        return FALSE;

    cs->OwningThread = (HANDLE)(uintptr_t)thread_id;
    cs->RecursionCount = 1;
    cs->LockCount = 0;
    return TRUE;
}

static void PCPS4_MS_ABI win_LeaveCriticalSection(void *critical_section)
{
    pcps4_win_critical_section *cs =
        (pcps4_win_critical_section *)critical_section;
    uint32_t thread_id;

    if (cs == NULL || cs->LockSemaphore == NULL)
        return;

    thread_id = pcps4_sys_thread_id();
    if (cs->OwningThread != (HANDLE)(uintptr_t)thread_id ||
        cs->RecursionCount <= 0)
        return;

    --cs->RecursionCount;
    cs->LockCount = cs->RecursionCount - 1;
    if (cs->RecursionCount == 0) {
        cs->OwningThread = NULL;
        (void)pcps4_sys_mutex_release(
            (pcps4_os_handle_t *)cs->LockSemaphore);
    }
}

static DWORD PCPS4_MS_ABI win_TlsAlloc(void)
{
    for (uint32_t slot = 0; slot < PCPS4_MAX_TLS_SLOTS; ++slot) {
        uint64_t bit = 1ull << slot;
        uint64_t old = __atomic_load_n(&g_tls_bitmap, __ATOMIC_ACQUIRE);
        while ((old & bit) == 0) {
            if (__atomic_compare_exchange_n(
                    &g_tls_bitmap, &old, old | bit, 0,
                    __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE)) {
                return slot;
            }
        }
    }

    return TLS_OUT_OF_INDEXES;
}

static BOOL PCPS4_MS_ABI win_TlsFree(DWORD slot)
{
    uint64_t bit;

    if (slot >= PCPS4_MAX_TLS_SLOTS)
        return FALSE;

    bit = 1ull << slot;
    if ((__atomic_load_n(&g_tls_bitmap, __ATOMIC_ACQUIRE) & bit) == 0)
        return FALSE;

    g_tls_values[slot] = NULL;
    __atomic_fetch_and(&g_tls_bitmap, ~bit, __ATOMIC_ACQ_REL);
    return TRUE;
}

static LPVOID PCPS4_MS_ABI win_TlsGetValue(DWORD slot)
{
    if (slot >= PCPS4_MAX_TLS_SLOTS ||
        (__atomic_load_n(&g_tls_bitmap, __ATOMIC_ACQUIRE) &
         (1ull << slot)) == 0) {
        win_SetLastError(87);
        return NULL;
    }

    return g_tls_values[slot];
}

static BOOL PCPS4_MS_ABI win_TlsSetValue(DWORD slot, LPVOID value)
{
    if (slot >= PCPS4_MAX_TLS_SLOTS ||
        (__atomic_load_n(&g_tls_bitmap, __ATOMIC_ACQUIRE) &
         (1ull << slot)) == 0) {
        win_SetLastError(87);
        return FALSE;
    }

    g_tls_values[slot] = value;
    return TRUE;
}

static HMODULE PCPS4_MS_ABI win_LoadLibraryA(LPCSTR name);
static HMODULE PCPS4_MS_ABI win_LoadLibraryW(LPCWSTR name);
static HMODULE PCPS4_MS_ABI win_GetModuleHandleA(LPCSTR name);
static HMODULE PCPS4_MS_ABI win_GetModuleHandleW(LPCWSTR name);
typedef struct pcps4_win_system_info {
    WORD wProcessorArchitecture;
    WORD wReserved;
    DWORD dwPageSize;
    LPVOID lpMinimumApplicationAddress;
    LPVOID lpMaximumApplicationAddress;
    ULONG_PTR dwActiveProcessorMask;
    DWORD dwNumberOfProcessors;
    DWORD dwProcessorType;
    DWORD dwAllocationGranularity;
    WORD wProcessorLevel;
    WORD wProcessorRevision;
} pcps4_win_system_info;

static void PCPS4_MS_ABI win_GetSystemInfo(LPVOID output)
{
    pcps4_win_system_info *info =
        (pcps4_win_system_info *)output;
    long processors;

    if (info == NULL)
        return;

    memset(info, 0, sizeof(*info));
    processors = sysconf(_SC_NPROCESSORS_ONLN);

    info->wProcessorArchitecture = 9; /* PROCESSOR_ARCHITECTURE_AMD64 */
    info->dwPageSize = 4096u;
    info->lpMinimumApplicationAddress = (LPVOID)(uintptr_t)0x10000u;
    info->lpMaximumApplicationAddress =
        (LPVOID)(uintptr_t)0x00007ffffffff000ull;
    info->dwActiveProcessorMask = (ULONG_PTR)~(ULONG_PTR)0;
    info->dwNumberOfProcessors =
        processors > 0 ? (DWORD)processors : 1u;
    if (info->dwNumberOfProcessors > sizeof(ULONG_PTR) * 8u)
        info->dwNumberOfProcessors = (DWORD)(sizeof(ULONG_PTR) * 8u);
    info->dwProcessorType = 8664u;
    info->dwAllocationGranularity = 0x10000u;
    info->wProcessorLevel = 6;
}

static void PCPS4_MS_ABI win_GetNativeSystemInfo(LPVOID output)
{
    win_GetSystemInfo(output);
}

static DWORD PCPS4_MS_ABI win_GetEnvironmentVariableA(
    LPCSTR name,
    LPSTR buffer,
    DWORD size);
static DWORD PCPS4_MS_ABI win_GetEnvironmentVariableW(
    LPCWSTR name,
    LPWSTR buffer,
    DWORD size);


static int module_name_normalize(
    const char *input,
    char *output,
    size_t output_size)
{
    const char *base;
    size_t i;
    size_t length;

    if (input == NULL || output == NULL || output_size < 2)
        return -1;

    base = strrchr(input, '/');
    {
        const char *slash = strrchr(input, 92);
        if (slash != NULL && (base == NULL || slash > base))
            base = slash;
    }
    base = base != NULL ? base + 1 : input;

    length = strlen(base);
    if (length + 1 > output_size)
        return -1;

    for (i = 0; i < length; ++i) {
        char ch = base[i];
        if (ch >= 'A' && ch <= 'Z')
            ch = (char)(ch - 'A' + 'a');
        output[i] = ch;
    }
    output[length] = '\0';

    if (strchr(output, '.') == NULL) {
        static const char suffix[] = ".dll";
        size_t have = strlen(output);
        if (have + sizeof(suffix) > output_size)
            return -1;
        memcpy(output + have, suffix, sizeof(suffix));
    }

    return 0;
}

static HMODULE PCPS4_MS_ABI win_LoadLibraryW(LPCWSTR name)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(name, utf8, sizeof(utf8)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    return win_LoadLibraryA(utf8);
}

static HMODULE PCPS4_MS_ABI win_GetModuleHandleW(LPCWSTR name)
{
    if (name == NULL) {
        pcps4_module_handle *module =
            (pcps4_module_handle *)calloc(1, sizeof(*module));
        if (module == NULL)
            return NULL;
        module->magic = PCPS4_MODULE_MAGIC;
        memcpy(module->name, g_main_module_name,
               strlen(g_main_module_name) + 1);
        return (HMODULE)module;
    }
    return win_LoadLibraryW(name);
}

static HMODULE PCPS4_MS_ABI win_LoadLibraryA(LPCSTR name)
{
    pcps4_module_handle *module;
    char normalized[128];

    if (module_name_normalize(name, normalized, sizeof(normalized)) != 0) {
        win_SetLastError(87);
        return NULL;
    }

    if (g_module_loader != NULL) {
        pcps4_module_status module_status =
            pcps4_module_loader_load(g_module_loader, normalized);
        if (module_status != PCPS4_MODULE_OK &&
            module_status != PCPS4_MODULE_NOT_FOUND) {
            win_SetLastError(126); /* ERROR_MOD_NOT_FOUND */
            return NULL;
        }
    }

    module = (pcps4_module_handle *)calloc(1, sizeof(*module));
    if (module == NULL) {
        win_SetLastError(8);
        return NULL;
    }

    module->magic = PCPS4_MODULE_MAGIC;
    memcpy(module->name, normalized, strlen(normalized) + 1);
    return (HMODULE)module;
}

static HMODULE PCPS4_MS_ABI win_GetModuleHandleA(LPCSTR name)
{
    if (name == NULL) {
        pcps4_module_handle *module =
            (pcps4_module_handle *)calloc(1, sizeof(*module));
        if (module == NULL)
            return NULL;
        module->magic = PCPS4_MODULE_MAGIC;
        memcpy(module->name, g_main_module_name,
               strlen(g_main_module_name) + 1);
        return (HMODULE)module;
    }
    return win_LoadLibraryA(name);
}

static FARPROC PCPS4_MS_ABI win_GetProcAddress(
    HMODULE module,
    LPCSTR name)
{
    pcps4_module_handle *handle = (pcps4_module_handle *)module;
    void *fn = NULL;

    if (name == NULL || g_resolver == NULL)
        return NULL;

    if (handle == NULL || handle->magic != PCPS4_MODULE_MAGIC) {
        win_SetLastError(6);
        return NULL;
    }

    if (name[0] >= '0' && name[0] <= '9') {
        uint16_t ordinal = (uint16_t)strtoul(name, NULL, 10);
        fn = pcps4_pe_resolver_lookup_ordinal(
            g_resolver, handle->name, ordinal);
    } else {
        fn = pcps4_pe_resolver_lookup(
            g_resolver, handle->name, name);
    }

    if (fn == NULL)
        win_SetLastError(127);

    return (FARPROC)fn;
}

static DWORD PCPS4_MS_ABI win_GetEnvironmentVariableW(
    LPCWSTR name,
    LPWSTR buffer,
    DWORD size)
{
    char utf8_name[512];
    char utf8_value[1024];
    DWORD result;
    size_t i;

    if (pcps4_wide_to_utf8(
            name, utf8_name, sizeof(utf8_name)) != 0)
        return 0;

    result = win_GetEnvironmentVariableA(
        utf8_name, utf8_value, sizeof(utf8_value));

    if (result == 0 || buffer == NULL || size == 0)
        return result;

    if (result >= size) {
        size_t limit = size > 0 ? (size_t)size - 1 : 0;
        for (i = 0; i < limit && utf8_value[i] != '\0'; ++i)
            buffer[i] = (WCHAR)(unsigned char)utf8_value[i];
        buffer[i] = 0;
        return result;
    }

    for (i = 0; i < (size_t)result; ++i)
        buffer[i] = (WCHAR)(unsigned char)utf8_value[i];
    buffer[result] = 0;
    return result;
}

static BOOL PCPS4_MS_ABI win_FreeLibrary(HMODULE module)
{
    pcps4_module_handle *handle = (pcps4_module_handle *)module;

    if (handle == NULL || handle->magic != PCPS4_MODULE_MAGIC)
        return FALSE;

    handle->magic = 0;
    free(handle);
    return TRUE;
}


static DWORD PCPS4_MS_ABI win_GetLastError(void)
{
    return g_last_error;
}

static void PCPS4_MS_ABI win_SetLastError(DWORD value)
{
    g_last_error = value;
}

static DWORD PCPS4_MS_ABI win_GetFileAttributesA(LPCSTR path)
{
    struct stat st;
    char translated[1024];

    if (path == NULL ||
        pcps4_sys_translate_path(path, 0, translated, sizeof(translated)) != 0 ||
        stat(translated, &st) != 0) {
        win_SetLastError(2);
        return 0xFFFFFFFFu;
    }

    if (S_ISDIR(st.st_mode))
        return 0x00000010u; /* FILE_ATTRIBUTE_DIRECTORY */

    return 0x00000080u; /* FILE_ATTRIBUTE_NORMAL */
}

static DWORD PCPS4_MS_ABI win_GetFileAttributesW(LPCWSTR path)
{
    char utf8[1024];

    if (pcps4_wide_to_utf8(path, utf8, sizeof(utf8)) != 0) {
        win_SetLastError(87);
        return 0xFFFFFFFFu;
    }

    return win_GetFileAttributesA(utf8);
}

static HANDLE PCPS4_MS_ABI win_GetCurrentProcess(void)
{
    return (HANDLE)(intptr_t)1;
}

static HANDLE PCPS4_MS_ABI win_GetCurrentThread(void)
{
    return (HANDLE)(intptr_t)1;
}

static void PCPS4_MS_ABI win_ExitProcess(UINT code)
{
    /*
     * Hand control back to the loader that launched this PE instead of
     * calling the host exit(), which would tear down the whole PcPs4 app
     * (status screen included) before the bundled-game result can be shown.
     */
    pcps4_loader_request_exit((int)code);
}

static DWORD PCPS4_MS_ABI win_GetEnvironmentVariableA(
    LPCSTR name,
    LPSTR buffer,
    DWORD size)
{
    const char *value;
    size_t length;

    if (name == NULL) {
        win_SetLastError(87);
        return 0;
    }

    value = getenv(name);
    if (value == NULL) {
        if (buffer != NULL && size != 0)
            buffer[0] = '\0';
        return 0;
    }

    length = strlen(value);

    if (buffer == NULL || size == 0) {
        return (DWORD)(length + 1);
    }

    if (length >= size) {
        size_t copy = size > 0 ? (size_t)size - 1 : 0;
        if (copy != 0)
            memcpy(buffer, value, copy);
        buffer[copy] = '\0';
        return (DWORD)(length + 1);
    }

    memcpy(buffer, value, length + 1);
    return (DWORD)length;
}

static LPSTR PCPS4_MS_ABI win_GetCommandLineA(void)
{
    static char command_line[] = "game.exe";
    return command_line;
}

static DWORD PCPS4_MS_ABI win_GetModuleFileNameA(
    HANDLE module,
    LPSTR filename,
    DWORD size)
{
    const char *path = "/app0/game/game.exe";
    size_t length;

    (void)module;

    if (filename == NULL || size == 0) {
        win_SetLastError(87);
        return 0;
    }

    length = strlen(path);
    if (length + 1 > size) {
        memcpy(filename, path, size - 1);
        filename[size - 1] = '\0';
        win_SetLastError(122);
        return size;
    }

    memcpy(filename, path, length + 1);
    return (DWORD)length;
}

static BOOL PCPS4_MS_ABI win_QueryPerformanceCounter(int64_t *counter)
{
    struct timespec now;

    if (counter == NULL)
        return FALSE;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return FALSE;

    *counter = (int64_t)now.tv_sec * 1000000000LL +
               (int64_t)now.tv_nsec;
    return TRUE;
}

static BOOL PCPS4_MS_ABI win_QueryPerformanceFrequency(int64_t *frequency)
{
    if (frequency == NULL)
        return FALSE;

    *frequency = 1000000000LL;
    return TRUE;
}

static HANDLE PCPS4_MS_ABI win_GetProcessHeap(void)
{
    return (HANDLE)(intptr_t)1;
}

static LPVOID PCPS4_MS_ABI win_HeapAlloc(HANDLE heap, DWORD flags, SIZE_T size)
{
    (void)heap;
    (void)flags;
    return malloc(size);
}

static LPVOID PCPS4_MS_ABI win_HeapReAlloc(
    HANDLE heap,
    DWORD flags,
    LPVOID memory,
    SIZE_T size)
{
    (void)heap;
    (void)flags;
    return realloc(memory, size);
}

static BOOL PCPS4_MS_ABI win_HeapFree(HANDLE heap, DWORD flags, LPVOID memory)
{
    (void)heap;
    (void)flags;
    free(memory);
    return TRUE;
}

static void PCPS4_MS_ABI win_RtlMoveMemory(PVOID dst, const void *src, SIZE_T length)
{
    memmove(dst, src, length);
}

static void register_kernel32(
    pcps4_pe_resolver *resolver,
    const char *dll,
    const pcps4_winabi_exports *abi)
{
#define KREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    KREG("GetLastError", win_GetLastError);
    KREG("SetLastError", win_SetLastError);
    KREG("GetCurrentProcess", win_GetCurrentProcess);
    KREG("GetCurrentThread", win_GetCurrentThread);
    KREG("GetCurrentProcessId", abi->get_current_process_id);
    KREG("GetCurrentThreadId", abi->get_current_thread_id);
    KREG("GetTickCount", abi->get_tick_count);
    KREG("GetTickCount64", abi->get_tick_count64);
    KREG("Sleep", abi->sleep_ms);
    KREG("InterlockedIncrement", abi->interlocked_increment);
    KREG("InterlockedDecrement", abi->interlocked_decrement);
    KREG("CreateFileA", abi->create_file_a);
    KREG("CreateFileW", abi->create_file_w);
    KREG("ReadFile", abi->read_file);
    KREG("WriteFile", abi->write_file);
    KREG("GetFileSize", abi->get_file_size);
    KREG("GetFileAttributesA", win_GetFileAttributesA);
    KREG("GetFileAttributesW", win_GetFileAttributesW);
    KREG("SetFilePointer", abi->set_file_pointer);
    KREG("DeleteFileA", abi->delete_file_a);
    KREG("DeleteFileW", abi->delete_file_w);
    KREG("CreateDirectoryA", abi->create_directory_a);
    KREG("CreateDirectoryW", abi->create_directory_w);
    KREG("CloseHandle", abi->close_handle);
    KREG("CreateThread", abi->create_thread);
    KREG("CreateMutexA", abi->create_mutex_a);
    KREG("CreateEventA", abi->create_event_a);
    KREG("SetEvent", abi->set_event);
    KREG("ResetEvent", abi->reset_event);
    KREG("WaitForSingleObject", abi->wait_for_single_object);
    KREG("ReleaseMutex", abi->release_mutex);
    KREG("VirtualAlloc", abi->virtual_alloc);
    KREG("VirtualFree", abi->virtual_free);
    KREG("GetEnvironmentVariableA", win_GetEnvironmentVariableA);
    KREG("GetCommandLineA", win_GetCommandLineA);
    KREG("GetModuleFileNameA", win_GetModuleFileNameA);
    KREG("QueryPerformanceCounter", win_QueryPerformanceCounter);
    KREG("QueryPerformanceFrequency", win_QueryPerformanceFrequency);
    KREG("GetSystemInfo", win_GetSystemInfo);
    KREG("GetNativeSystemInfo", win_GetNativeSystemInfo);
    KREG("GetProcessHeap", win_GetProcessHeap);
    KREG("HeapAlloc", win_HeapAlloc);
    KREG("HeapReAlloc", win_HeapReAlloc);
    KREG("HeapFree", win_HeapFree);
    KREG("RtlMoveMemory", win_RtlMoveMemory);
    KREG("InitializeCriticalSection", win_InitializeCriticalSection);
    KREG("InitializeCriticalSectionAndSpinCount", win_InitializeCriticalSectionAndSpinCount);
    KREG("InitializeCriticalSectionEx", win_InitializeCriticalSectionEx);
    KREG("DeleteCriticalSection", win_DeleteCriticalSection);
    KREG("EnterCriticalSection", win_EnterCriticalSection);
    KREG("TryEnterCriticalSection", win_TryEnterCriticalSection);
    KREG("LeaveCriticalSection", win_LeaveCriticalSection);
    KREG("TlsAlloc", win_TlsAlloc);
    KREG("TlsFree", win_TlsFree);
    KREG("TlsGetValue", win_TlsGetValue);
    KREG("TlsSetValue", win_TlsSetValue);
    KREG("ExitProcess", win_ExitProcess);
    KREG("LoadLibraryA", win_LoadLibraryA);
    KREG("LoadLibraryW", win_LoadLibraryW);
    KREG("GetModuleHandleA", win_GetModuleHandleA);
    KREG("GetModuleHandleW", win_GetModuleHandleW);
    KREG("GetProcAddress", win_GetProcAddress);
    KREG("FreeLibrary", win_FreeLibrary);
    KREG("GetEnvironmentVariableW", win_GetEnvironmentVariableW);
#undef KREG
}

static void register_d3d11(
    pcps4_pe_resolver *resolver,
    const char *dll)
{
    /*
     * The PE calls these the Microsoft-x64 way, so register the MS-ABI entry
     * points (…_ms), not the System V implementations.
     */
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDeviceAndSwapChain",
        (void *)pcps4_d3d11_com_create_device_and_swap_chain_ms);
    (void)pcps4_pe_resolver_register(
        resolver, dll, "D3D11CreateDevice",
        (void *)pcps4_d3d11_com_create_device_ms);
}

static void register_winsock(
    pcps4_pe_resolver *resolver,
    const char *dll,
    const pcps4_winabi_exports *abi)
{
#define WREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    WREG("WSAStartup", abi->wsa_startup);
    WREG("WSACleanup", abi->wsa_cleanup);
    WREG("WSAGetLastError", abi->wsa_get_last_error);
    WREG("socket", abi->socket_fn);
    WREG("connect", abi->connect_fn);
    WREG("send", abi->send_fn);
    WREG("recv", abi->recv_fn);
    WREG("shutdown", abi->shutdown_fn);
    WREG("setsockopt", abi->setsockopt_fn);
    WREG("getsockopt", abi->getsockopt_fn);
    WREG("getaddrinfo", abi->getaddrinfo_fn);
    WREG("freeaddrinfo", abi->freeaddrinfo_fn);
    WREG("htons", abi->htons_fn);
    WREG("ntohs", abi->ntohs_fn);
    WREG("htonl", abi->htonl_fn);
    WREG("ntohl", abi->ntohl_fn);
    WREG("inet_addr", abi->inet_addr_fn);
    WREG("closesocket", abi->closesocket_fn);
#undef WREG
}

static void register_xinput(
    pcps4_pe_resolver *resolver,
    const char *dll,
    const pcps4_winabi_exports *abi)
{
#define XREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    XREG("XInputGetState", abi->xinput_get_state);
    XREG("XInputSetState", abi->xinput_set_state);
    XREG("XInputGetCapabilities", abi->xinput_get_capabilities);
    XREG("XInputEnable", abi->xinput_enable);
#undef XREG
}

static void register_ucrt(
    pcps4_pe_resolver *resolver,
    const char *dll,
    const pcps4_winabi_exports *abi)
{
#define UREG(name, fn) (void)pcps4_pe_resolver_register(resolver, dll, name, (void *)(fn))
    UREG("malloc", abi->malloc_fn);
    UREG("calloc", abi->calloc_fn);
    UREG("realloc", abi->realloc_fn);
    UREG("free", abi->free_fn);
    UREG("memcpy", abi->memcpy_fn);
    UREG("memmove", abi->memmove_fn);
    UREG("memset", abi->memset_fn);
    UREG("memcmp", abi->memcmp_fn);
    UREG("strlen", abi->strlen_fn);
    UREG("strcmp", abi->strcmp_fn);
    UREG("strncmp", abi->strncmp_fn);
    UREG("strstr", abi->strstr_fn);
    UREG("strchr", abi->strchr_fn);
    UREG("strrchr", abi->strrchr_fn);
    UREG("atoi", abi->atoi_fn);
    UREG("atol", abi->atol_fn);
    UREG("strtol", abi->strtol_fn);
    UREG("strtoul", abi->strtoul_fn);
    UREG("strtoll", abi->strtoll_fn);
    UREG("strtoull", abi->strtoull_fn);
    UREG("abort", abi->abort_fn);
    UREG("exit", abi->exit_fn);
    UREG("fopen", abi->fopen_fn);
    UREG("_wfopen", abi->wfopen_fn);
    UREG("fclose", abi->fclose_fn);
    UREG("fread", abi->fread_fn);
    UREG("fwrite", abi->fwrite_fn);
    UREG("fseek", abi->fseek_fn);
    UREG("ftell", abi->ftell_fn);
#undef UREG
}

void pcps4_winapi_set_module_loader(pcps4_module_loader *loader)
{
    g_module_loader = loader;
}

void pcps4_winapi_set_main_module(const char *name)
{
    char normalized[128];

    if (name == NULL ||
        module_name_normalize(name, normalized, sizeof(normalized)) != 0)
        return;

    memcpy(g_main_module_name, normalized, strlen(normalized) + 1);
}

void pcps4_winapi_build_resolver(pcps4_pe_resolver *resolver)
{
    pcps4_winabi_exports abi;

    if (resolver == NULL)
        return;

    pcps4_winabi_fill(&abi);
    pcps4_pe_resolver_init(resolver);
    g_resolver = resolver;

    /*
     * ABI NOTE: every function pointer placed in a native Windows PE import
     * table must use the Microsoft x64 convention. The compatibility layer's
     * real implementations remain System V; pcps4_winabi_fill() supplies
     * compiler-generated MS-ABI forwarding thunks for those functions.
     *
     * The special win_* helpers in this file are themselves declared
     * PCPS4_MS_ABI because they are import entry points directly.
     */
    register_kernel32(resolver, "KERNEL32.dll", &abi);
    register_kernel32(resolver, "KERNELBASE.dll", &abi);
    register_winsock(resolver, "WS2_32.dll", &abi);
    register_d3d11(resolver, "d3d11.dll");
    register_xinput(resolver, "XINPUT1_4.dll", &abi);
    register_xinput(resolver, "XINPUT1_3.dll", &abi);
    register_ucrt(resolver, "ucrtbase.dll", &abi);
    register_ucrt(resolver, "VCRUNTIME140.dll", &abi);
    register_ucrt(resolver, "MSVCRT.dll", &abi);


    /*
     * Some compilers import the memory functions from the API-set CRT DLL
     * names. They are intentionally added explicitly rather than making the
     * PE resolver guess based on prefixes.
     */
    register_ucrt(resolver, "api-ms-win-crt-runtime-l1-1-0.dll", &abi);
    register_ucrt(resolver, "api-ms-win-crt-string-l1-1-0.dll", &abi);
    register_ucrt(resolver, "api-ms-win-crt-heap-l1-1-0.dll", &abi);
}
