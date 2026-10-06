#define _WIN32_WINNT 0x0601
#include <winsock2.h>
#include <windows.h>
#include <xinput.h>
#include <d3d11.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/*
 * Comprehensive native Windows x64 compatibility smoke binary.
 *
 * One PE exercises the supported import/ABI surface in a single PS4 install:
 * Win32/UCRT file + memory APIs, process/thread/timing APIs, synchronization,
 * Winsock, XInput, dynamic API lookup, and a wide D3D11/DXGI state/resource
 * path. Failure codes identify the first failing stage.
 *
 * The PE is linked with 16 KiB sections, so the loader/relocation/W^X path is
 * exercised at the same time as the imported API boundary.
 */

static volatile LONG g_thread_value = 0;

static const BYTE g_file_payload[32] = {
    0x50, 0x43, 0x50, 0x53, 0x34, 0x2D, 0x57, 0x49,
    0x4E, 0x33, 0x32, 0x2D, 0x41, 0x42, 0x49, 0x00,
    0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC, 0xDE, 0xF0,
    0x0F, 0xED, 0xCB, 0xA9, 0x87, 0x65, 0x43, 0x21
};

static DWORD WINAPI smoke_thread(LPVOID argument)
{
    volatile LONG *value = (volatile LONG *)argument;
    InterlockedIncrement(value);
    return 0x5A;
}

static void smoke_fail(UINT stage)
{
    ExitProcess(stage);
}

static int check_crt(void)
{
    char a[64];
    char b[64];
    char readback[32];
    char *heap_a;
    char *heap_b;
    FILE *file;
    long numeric;
    unsigned long numeric_u;
    long long numeric_ll;
    unsigned long long numeric_ull;

    file = fopen("pcps4_crt_smoke.bin", "wb+");
    if (file == NULL)
        return 1;

    if (fwrite(g_file_payload, 1, sizeof(g_file_payload), file) !=
        sizeof(g_file_payload)) {
        fclose(file);
        return 2;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 3;
    }

    memset(readback, 0, sizeof(readback));
    if (fread(readback, 1, sizeof(readback), file) != sizeof(readback)) {
        fclose(file);
        return 4;
    }

    if (memcmp(readback, g_file_payload, sizeof(readback)) != 0) {
        fclose(file);
        return 5;
    }

    if (ftell(file) != (long)sizeof(readback)) {
        fclose(file);
        return 6;
    }

    if (fclose(file) != 0)
        return 7;

    if (!DeleteFileA("pcps4_crt_smoke.bin"))
        return 8;

    memcpy(a, "alpha-beta", 12);
    memmove(b, a, 12);
    if (strcmp(a, b) != 0 ||
        strncmp(a, "alpha", 5) != 0 ||
        strlen(a) != 11)
        return 9;

    if (strstr(a, "beta") == NULL ||
        strchr(a, '-') == NULL ||
        strrchr(a, '-') == NULL)
        return 10;

    numeric = strtol("12345", NULL, 10);
    numeric_u = strtoul("67890", NULL, 10);
    numeric_ll = strtoll("123456789", NULL, 10);
    numeric_ull = strtoull("987654321", NULL, 10);
    if (numeric != 12345L ||
        numeric_u != 67890UL ||
        numeric_ll != 123456789LL ||
        numeric_ull != 987654321ULL ||
        atoi("42") != 42 ||
        atol("43") != 43L)
        return 11;

    heap_a = (char *)malloc(24);
    heap_b = (char *)calloc(8, 4);
    if (heap_a == NULL || heap_b == NULL) {
        free(heap_a);
        free(heap_b);
        return 12;
    }

    memset(heap_a, 0x5A, 24);
    memcpy(heap_b, heap_a, 24);
    if (memcmp(heap_a, heap_b, 24) != 0) {
        free(heap_a);
        free(heap_b);
        return 13;
    }

    heap_a = (char *)realloc(heap_a, 64);
    if (heap_a == NULL) {
        free(heap_b);
        return 14;
    }

    free(heap_a);
    free(heap_b);
    return 0;
}

static int check_win32_file_api(void)
{
    static const char path_a[] = "pcps4_win32_smoke.bin";
    static const WCHAR path_w[] = L"pcps4_win32_w_smoke.bin";
    BYTE readback[32];
    DWORD written = 0;
    DWORD read = 0;
    DWORD high = 0;
    HANDLE handle;
    HANDLE handle_w;
    DWORD size;

    handle = CreateFileA(
        path_a,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (handle == INVALID_HANDLE_VALUE)
        return 1;

    if (!WriteFile(handle, g_file_payload, sizeof(g_file_payload),
                   &written, NULL) || written != sizeof(g_file_payload)) {
        CloseHandle(handle);
        DeleteFileA(path_a);
        return 2;
    }

    size = GetFileSize(handle, &high);
    if (size != sizeof(g_file_payload) || high != 0) {
        CloseHandle(handle);
        DeleteFileA(path_a);
        return 3;
    }

    if (SetFilePointer(handle, 0, NULL, FILE_BEGIN) == INVALID_SET_FILE_POINTER) {
        CloseHandle(handle);
        DeleteFileA(path_a);
        return 4;
    }

    ZeroMemory(readback, sizeof(readback));
    if (!ReadFile(handle, readback, sizeof(readback), &read, NULL) ||
        read != sizeof(readback) ||
        memcmp(readback, g_file_payload, sizeof(readback)) != 0) {
        CloseHandle(handle);
        DeleteFileA(path_a);
        return 5;
    }

    if (!CloseHandle(handle) || !DeleteFileA(path_a))
        return 6;

    handle_w = CreateFileW(
        path_w,
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        NULL,
        CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        NULL);
    if (handle_w == INVALID_HANDLE_VALUE)
        return 7;

    if (!WriteFile(handle_w, g_file_payload, 8, &written, NULL) ||
        written != 8 ||
        !CloseHandle(handle_w) ||
        !DeleteFileW(path_w))
        return 8;

    return 0;
}

static int check_sync_and_memory(void)
{
    HANDLE event;
    HANDLE mutex;
    HANDLE thread;
    HANDLE process;
    HANDLE current_thread;
    void *virtual_memory;
    void *heap_memory;
    BYTE temporary[32];
    DWORD thread_id = 0;

    process = GetCurrentProcess();
    current_thread = GetCurrentThread();
    if (process == NULL || current_thread == NULL ||
        GetCurrentProcessId() == 0 || GetCurrentThreadId() == 0)
        return 1;

    SetLastError(0x1234u);
    if (GetLastError() != 0x1234u)
        return 2;

    RtlMoveMemory(temporary, g_file_payload, sizeof(temporary));
    if (memcmp(temporary, g_file_payload, sizeof(temporary)) != 0)
        return 3;

    virtual_memory = VirtualAlloc(
        NULL, 0x4000, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (virtual_memory == NULL)
        return 4;

    memset(virtual_memory, 0xA5, 0x4000);
    if (((BYTE *)virtual_memory)[0] != 0xA5 ||
        ((BYTE *)virtual_memory)[0x3FFF] != 0xA5) {
        VirtualFree(virtual_memory, 0, MEM_RELEASE);
        return 5;
    }

    if (!VirtualFree(virtual_memory, 0, MEM_RELEASE))
        return 6;

    heap_memory = HeapAlloc(GetProcessHeap(), 0, 128);
    if (heap_memory == NULL)
        return 7;

    memset(heap_memory, 0x3C, 128);
    heap_memory = HeapReAlloc(GetProcessHeap(), 0, heap_memory, 256);
    if (heap_memory == NULL)
        return 8;

    ((BYTE *)heap_memory)[255] = 0x7E;
    if (!HeapFree(GetProcessHeap(), 0, heap_memory))
        return 9;

    g_thread_value = 0;
    thread = CreateThread(
        NULL, 0, smoke_thread, (LPVOID)&g_thread_value, 0, &thread_id);
    if (thread == NULL || thread_id == 0)
        return 10;

    if (WaitForSingleObject(thread, 5000) != WAIT_OBJECT_0 ||
        g_thread_value != 1) {
        CloseHandle(thread);
        return 11;
    }

    if (!CloseHandle(thread))
        return 12;

    mutex = CreateMutexA(NULL, FALSE, "pcps4-smoke-mutex");
    if (mutex == NULL)
        return 13;

    if (WaitForSingleObject(mutex, 1000) != WAIT_OBJECT_0 ||
        !ReleaseMutex(mutex) ||
        !CloseHandle(mutex))
        return 14;

    event = CreateEventA(NULL, TRUE, FALSE, "pcps4-smoke-event");
    if (event == NULL)
        return 15;

    if (!SetEvent(event) ||
        WaitForSingleObject(event, 1000) != WAIT_OBJECT_0 ||
        !ResetEvent(event) ||
        !CloseHandle(event))
        return 16;

    if (InterlockedDecrement(&g_thread_value) != 0)
        return 17;

    {
        CRITICAL_SECTION critical_section;
        int tls_marker = 0x13579BDF;
        DWORD tls_slot;

        InitializeCriticalSection(&critical_section);
        EnterCriticalSection(&critical_section);
        EnterCriticalSection(&critical_section);
        if (!TryEnterCriticalSection(&critical_section))
            return 18;
        LeaveCriticalSection(&critical_section);
        LeaveCriticalSection(&critical_section);
        LeaveCriticalSection(&critical_section);
        DeleteCriticalSection(&critical_section);

        tls_slot = TlsAlloc();
        if (tls_slot == TLS_OUT_OF_INDEXES)
            return 19;
        if (!TlsSetValue(tls_slot, &tls_marker) ||
            TlsGetValue(tls_slot) != &tls_marker ||
            !TlsFree(tls_slot))
            return 20;
    }

    return 0;
}

static int check_timing_environment(void)
{
    LARGE_INTEGER counter_a;
    LARGE_INTEGER counter_b;
    LARGE_INTEGER frequency;
    SYSTEM_INFO system_info;
    char env_a[128];
    WCHAR env_w[128];
    char module_name[256];
    DWORD env_result_a;
    DWORD env_result_w;

    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0)
        return 1;

    if (!QueryPerformanceCounter(&counter_a))
        return 2;

    Sleep(1);

    if (!QueryPerformanceCounter(&counter_b) ||
        counter_b.QuadPart < counter_a.QuadPart)
        return 3;

    (void)GetTickCount();
    (void)GetTickCount64();

    env_result_a = GetEnvironmentVariableA("PATH", env_a, sizeof(env_a));
    env_result_w = GetEnvironmentVariableW(
        L"PATH", env_w, sizeof(env_w) / sizeof(env_w[0]));

    ZeroMemory(&system_info, sizeof(system_info));
    GetSystemInfo(&system_info);
    if (system_info.wProcessorArchitecture != PROCESSOR_ARCHITECTURE_AMD64 ||
        system_info.dwPageSize != 4096u ||
        system_info.dwAllocationGranularity != 0x10000u ||
        system_info.dwNumberOfProcessors == 0)
        return 4;

    if (GetModuleFileNameA(NULL, module_name, sizeof(module_name)) == 0)
        return 5;

    if (GetCommandLineA() == NULL)
        return 6;

    (void)env_result_a;
    (void)env_result_w;
    return 0;
}

static int check_dynamic_resolution(void)
{
    typedef DWORD (WINAPI *get_tick_count_fn)(void);
    HMODULE kernel32;
    HMODULE kernel32_w;
    HMODULE d3d11;
    FARPROC tick_proc;
    FARPROC create_proc;
    get_tick_count_fn get_tick_count;

    kernel32 = GetModuleHandleA("kernel32.dll");
    if (kernel32 == NULL)
        return 1;

    kernel32_w = GetModuleHandleW(L"kernel32.dll");
    if (kernel32_w == NULL)
        return 2;

    tick_proc = GetProcAddress(kernel32, "GetTickCount");
    if (tick_proc == NULL)
        return 3;

    get_tick_count = (get_tick_count_fn)tick_proc;
    (void)get_tick_count();

    d3d11 = LoadLibraryW(L"d3d11.dll");
    if (d3d11 == NULL)
        return 4;

    create_proc = GetProcAddress(d3d11, "D3D11CreateDevice");
    if (create_proc == NULL) {
        FreeLibrary(d3d11);
        return 5;
    }

    if (!FreeLibrary(d3d11))
        return 6;

    if (!FreeLibrary(kernel32) || !FreeLibrary(kernel32_w))
        return 7;

    return 0;
}

static int check_local_dll(void)
{
    typedef int (WINAPI *local_probe_fn)(int);
    HMODULE module;
    FARPROC proc;
    local_probe_fn probe;

    module = LoadLibraryA("pcps4_local_smoke.dll");
    if (module == NULL)
        return 1;

    proc = GetProcAddress(module, "PcPs4LocalProbe");
    if (proc == NULL) {
        FreeLibrary(module);
        return 2;
    }

    probe = (local_probe_fn)proc;
    if (probe(37) != 634) {
        FreeLibrary(module);
        return 3;
    }

    if (!FreeLibrary(module))
        return 4;

    return 0;
}

static int check_winsock(void)
{
    WSADATA data;
    SOCKET socket_fd;
    int receive_buffer = 0;
    int receive_buffer_size = sizeof(receive_buffer);

    if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
        return 1;

    if (data.wVersion != MAKEWORD(2, 2) ||
        data.wHighVersion != MAKEWORD(2, 2))
        return 2;

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd == INVALID_SOCKET) {
        WSACleanup();
        return 3;
    }

    if (setsockopt(
            socket_fd, SOL_SOCKET, SO_RCVBUF,
            (const char *)&receive_buffer, sizeof(receive_buffer)) ==
        SOCKET_ERROR) {
        closesocket(socket_fd);
        WSACleanup();
        return 4;
    }

    if (getsockopt(
            socket_fd, SOL_SOCKET, SO_RCVBUF,
            (char *)&receive_buffer, &receive_buffer_size) ==
        SOCKET_ERROR) {
        closesocket(socket_fd);
        WSACleanup();
        return 5;
    }

    if (inet_addr("127.0.0.1") == INADDR_NONE ||
        htonl(1) != ntohl(1) ||
        htons(0x1234) != ntohs(0x3412)) {
        closesocket(socket_fd);
        WSACleanup();
        return 6;
    }

    /* shutdown on an unconnected UDP socket is not a required success path. */
    (void)shutdown(socket_fd, SD_BOTH);

    if (closesocket(socket_fd) != 0) {
        WSACleanup();
        return 7;
    }

    if (WSAGetLastError() < 0)
        return 8;

    if (WSACleanup() != 0)
        return 9;

    return 0;
}

static int check_xinput(void)
{
    XINPUT_STATE state;
    XINPUT_CAPABILITIES capabilities;
    XINPUT_VIBRATION vibration;
    DWORD state_result;
    DWORD caps_result;
    DWORD set_result;

    ZeroMemory(&state, sizeof(state));
    ZeroMemory(&capabilities, sizeof(capabilities));
    ZeroMemory(&vibration, sizeof(vibration));

    XInputEnable(TRUE);
    state_result = XInputGetState(0, &state);
    caps_result = XInputGetCapabilities(0, XINPUT_FLAG_GAMEPAD, &capabilities);
    set_result = XInputSetState(0, &vibration);
    XInputEnable(FALSE);

    if (state_result != ERROR_SUCCESS &&
        state_result != ERROR_DEVICE_NOT_CONNECTED)
        return 1;

    if (caps_result != ERROR_SUCCESS &&
        caps_result != ERROR_DEVICE_NOT_CONNECTED)
        return 2;

    if (set_result != ERROR_SUCCESS &&
        set_result != ERROR_DEVICE_NOT_CONNECTED)
        return 3;

    return 0;
}

static int check_d3d11(void)
{
    DXGI_SWAP_CHAIN_DESC swap_desc;
    D3D11_BUFFER_DESC vertex_desc;
    D3D11_BUFFER_DESC constant_desc;
    D3D11_TEXTURE2D_DESC texture_desc;
    D3D11_TEXTURE2D_DESC depth_desc;
    D3D11_RENDER_TARGET_VIEW_DESC rtv_desc;
    D3D11_SHADER_RESOURCE_VIEW_DESC srv_desc;
    D3D11_DEPTH_STENCIL_VIEW_DESC dsv_desc;
    D3D11_SAMPLER_DESC sampler_desc;
    D3D11_RASTERIZER_DESC raster_desc;
    D3D11_BLEND_DESC blend_desc;
    D3D11_DEPTH_STENCIL_DESC depth_state_desc;
    D3D11_INPUT_ELEMENT_DESC input_desc;
    D3D11_SUBRESOURCE_DATA texture_data;
    D3D11_MAPPED_SUBRESOURCE mapped;
    D3D_FEATURE_LEVEL feature_level = 0;
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    ID3D11DeviceContext *context_again = NULL;
    IDXGISwapChain *swap_chain = NULL;
    ID3D11Texture2D *back_buffer = NULL;
    ID3D11Texture2D *texture = NULL;
    ID3D11Texture2D *depth_texture = NULL;
    ID3D11RenderTargetView *rtv = NULL;
    ID3D11ShaderResourceView *srv = NULL;
    ID3D11DepthStencilView *dsv = NULL;
    ID3D11SamplerState *sampler = NULL;
    ID3D11RasterizerState *rasterizer = NULL;
    ID3D11BlendState *blend = NULL;
    ID3D11DepthStencilState *depth_state = NULL;
    ID3D11InputLayout *input_layout = NULL;
    ID3D11Buffer *vertex_buffer = NULL;
    ID3D11Buffer *constant_buffer = NULL;
    HRESULT hr;
    const BYTE signature[4] = { 0, 0, 0, 0 };
    const BYTE pixels[4 * 4 * 4] = {
        0x20, 0x80, 0xE0, 0xFF
    };
    float vertices[9] = {
        -0.5f, -0.5f, 0.0f,
         0.0f,  0.5f, 0.0f,
         0.5f, -0.5f, 0.0f
    };

    ZeroMemory(&swap_desc, sizeof(swap_desc));
    swap_desc.BufferDesc.Width = 640;
    swap_desc.BufferDesc.Height = 360;
    swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    swap_desc.SampleDesc.Count = 1;
    swap_desc.BufferCount = 2;
    swap_desc.Windowed = TRUE;
    swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(
        NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0,
        NULL, 0, D3D11_SDK_VERSION,
        &swap_desc, &swap_chain, &device,
        &feature_level, &context);
    if (FAILED(hr) || !device || !context || !swap_chain)
        return 1;

    if (device->lpVtbl->GetFeatureLevel(device) != D3D_FEATURE_LEVEL_11_0)
        return 2;

    device->lpVtbl->GetImmediateContext(device, &context_again);
    if (!context_again)
        return 3;

    if (swap_chain->lpVtbl->GetBuffer(
            swap_chain, 0, 0, (void **)&back_buffer) != S_OK ||
        !back_buffer)
        return 4;

    ZeroMemory(&rtv_desc, sizeof(rtv_desc));
    rtv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (device->lpVtbl->CreateRenderTargetView(
            device, (ID3D11Resource *)back_buffer, &rtv_desc, &rtv) != S_OK || !rtv)
        return 5;

    ZeroMemory(&vertex_desc, sizeof(vertex_desc));
    vertex_desc.ByteWidth = sizeof(vertices);
    vertex_desc.Usage = D3D11_USAGE_DYNAMIC;
    vertex_desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    vertex_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    if (device->lpVtbl->CreateBuffer(
            device, &vertex_desc, NULL, &vertex_buffer) != S_OK ||
        !vertex_buffer)
        return 6;

    ZeroMemory(&mapped, sizeof(mapped));
    if (context->lpVtbl->Map(
            context, (ID3D11Resource *)vertex_buffer, 0, D3D11_MAP_WRITE_DISCARD, 0,
            &mapped) != S_OK || mapped.pData == NULL)
        return 7;

    memcpy(mapped.pData, vertices, sizeof(vertices));
    context->lpVtbl->Unmap(context, (ID3D11Resource *)vertex_buffer, 0);

    ZeroMemory(&constant_desc, sizeof(constant_desc));
    constant_desc.ByteWidth = 64;
    constant_desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    if (device->lpVtbl->CreateBuffer(
            device, &constant_desc, NULL, &constant_buffer) != S_OK ||
        !constant_buffer)
        return 8;

    ZeroMemory(&texture_desc, sizeof(texture_desc));
    texture_desc.Width = 4;
    texture_desc.Height = 4;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    ZeroMemory(&texture_data, sizeof(texture_data));
    texture_data.pSysMem = pixels;
    texture_data.SysMemPitch = 16;
    if (device->lpVtbl->CreateTexture2D(
            device, &texture_desc, &texture_data, &texture) != S_OK ||
        !texture)
        return 9;

    ZeroMemory(&srv_desc, sizeof(srv_desc));
    srv_desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    if (device->lpVtbl->CreateShaderResourceView(
            device, (ID3D11Resource *)texture, &srv_desc, &srv) != S_OK || !srv)
        return 10;

    ZeroMemory(&sampler_desc, sizeof(sampler_desc));
    sampler_desc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;
    sampler_desc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
    sampler_desc.MaxAnisotropy = 1;
    sampler_desc.ComparisonFunc = D3D11_COMPARISON_ALWAYS;
    sampler_desc.MaxLOD = D3D11_FLOAT32_MAX;
    if (device->lpVtbl->CreateSamplerState(
            device, &sampler_desc, &sampler) != S_OK || !sampler)
        return 11;

    ZeroMemory(&raster_desc, sizeof(raster_desc));
    raster_desc.FillMode = D3D11_FILL_SOLID;
    raster_desc.CullMode = D3D11_CULL_NONE;
    raster_desc.DepthClipEnable = TRUE;
    if (device->lpVtbl->CreateRasterizerState(
            device, &raster_desc, &rasterizer) != S_OK || !rasterizer)
        return 12;

    ZeroMemory(&blend_desc, sizeof(blend_desc));
    blend_desc.RenderTarget[0].BlendEnable = FALSE;
    blend_desc.RenderTarget[0].SrcBlend = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlend = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    blend_desc.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    blend_desc.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    blend_desc.RenderTarget[0].RenderTargetWriteMask =
        D3D11_COLOR_WRITE_ENABLE_ALL;
    if (device->lpVtbl->CreateBlendState(
            device, &blend_desc, &blend) != S_OK || !blend)
        return 13;

    ZeroMemory(&depth_state_desc, sizeof(depth_state_desc));
    depth_state_desc.DepthEnable = TRUE;
    depth_state_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    depth_state_desc.DepthFunc = D3D11_COMPARISON_LESS_EQUAL;
    if (device->lpVtbl->CreateDepthStencilState(
            device, &depth_state_desc, &depth_state) != S_OK ||
        !depth_state)
        return 14;

    ZeroMemory(&depth_desc, sizeof(depth_desc));
    depth_desc.Width = 640;
    depth_desc.Height = 360;
    depth_desc.MipLevels = 1;
    depth_desc.ArraySize = 1;
    depth_desc.Format = DXGI_FORMAT_R32_FLOAT;
    depth_desc.SampleDesc.Count = 1;
    depth_desc.Usage = D3D11_USAGE_DEFAULT;
    depth_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
    if (device->lpVtbl->CreateTexture2D(
            device, &depth_desc, NULL, &depth_texture) != S_OK ||
        !depth_texture)
        return 15;

    ZeroMemory(&dsv_desc, sizeof(dsv_desc));
    dsv_desc.Format = DXGI_FORMAT_R32_FLOAT;
    dsv_desc.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
    dsv_desc.Texture2D.MipSlice = 0;
    if (device->lpVtbl->CreateDepthStencilView(
            device, (ID3D11Resource *)depth_texture, &dsv_desc, &dsv) != S_OK || !dsv)
        return 16;

    ZeroMemory(&input_desc, sizeof(input_desc));
    input_desc.SemanticName = "POSITION";
    input_desc.Format = DXGI_FORMAT_R32G32B32_FLOAT;
    input_desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    {
        if (device->lpVtbl->CreateInputLayout(
                device, &input_desc, 1, signature, sizeof(signature),
                &input_layout) != S_OK || !input_layout)
            return 17;
    }

    {
        D3D11_VIEWPORT viewport;
        ID3D11Buffer *buffers[1] = { vertex_buffer };
        ID3D11Buffer *constant_buffers[1] = { constant_buffer };
        ID3D11ShaderResourceView *views[1] = { srv };
        ID3D11SamplerState *samplers[1] = { sampler };
        ID3D11RenderTargetView *render_targets[1] = { rtv };
        UINT strides[1] = { sizeof(float) * 3 };
        UINT offsets[1] = { 0 };

        ZeroMemory(&viewport, sizeof(viewport));
        viewport.Width = 640.0f;
        viewport.Height = 360.0f;
        viewport.MinDepth = 0.0f;
        viewport.MaxDepth = 1.0f;

        context->lpVtbl->IASetInputLayout(context, input_layout);
        context->lpVtbl->IASetVertexBuffers(
            context, 0, 1, buffers, strides, offsets);
        context->lpVtbl->IASetPrimitiveTopology(
            context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
        context->lpVtbl->VSSetConstantBuffers(
            context, 0, 1, constant_buffers);
        context->lpVtbl->PSSetConstantBuffers(
            context, 0, 1, constant_buffers);
        context->lpVtbl->VSSetShaderResources(
            context, 0, 1, views);
        context->lpVtbl->PSSetShaderResources(
            context, 0, 1, views);
        context->lpVtbl->VSSetSamplers(
            context, 0, 1, samplers);
        context->lpVtbl->PSSetSamplers(
            context, 0, 1, samplers);
        context->lpVtbl->RSSetState(context, rasterizer);
        context->lpVtbl->RSSetViewports(context, 1, &viewport);
        context->lpVtbl->OMSetBlendState(
            context, blend, NULL, 0xFFFFFFFFu);
        context->lpVtbl->OMSetDepthStencilState(context, depth_state, 0);
        context->lpVtbl->OMSetRenderTargets(
            context, 1, render_targets, dsv);
        context->lpVtbl->ClearRenderTargetView(
            context, rtv,
            (const float[4]){ 0.03f, 0.08f, 0.16f, 1.0f });
        context->lpVtbl->ClearDepthStencilView(
            context, dsv,
            D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);
        context->lpVtbl->Draw(context, 3, 0);
        context->lpVtbl->Flush(context);
    }

    if (swap_chain->lpVtbl->Present(swap_chain, 1, 0) != S_OK)
        return 18;

    input_layout->lpVtbl->Release(input_layout);
    depth_state->lpVtbl->Release(depth_state);
    blend->lpVtbl->Release(blend);
    rasterizer->lpVtbl->Release(rasterizer);
    sampler->lpVtbl->Release(sampler);
    dsv->lpVtbl->Release(dsv);
    depth_texture->lpVtbl->Release(depth_texture);
    srv->lpVtbl->Release(srv);
    texture->lpVtbl->Release(texture);
    constant_buffer->lpVtbl->Release(constant_buffer);
    vertex_buffer->lpVtbl->Release(vertex_buffer);
    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context_again->lpVtbl->Release(context_again);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);

    return 0;
}

void WINAPI WinMainCRTStartup(void)
{
    int rc;

    rc = check_crt();
    if (rc != 0)
        smoke_fail(10u + (UINT)rc);

    rc = check_win32_file_api();
    if (rc != 0)
        smoke_fail(30u + (UINT)rc);

    rc = check_sync_and_memory();
    if (rc != 0)
        smoke_fail(50u + (UINT)rc);

    rc = check_timing_environment();
    if (rc != 0)
        smoke_fail(70u + (UINT)rc);

    rc = check_dynamic_resolution();
    if (rc != 0)
        smoke_fail(90u + (UINT)rc);

    rc = check_local_dll();
    if (rc != 0)
        smoke_fail(105u + (UINT)rc);

    rc = check_winsock();
    if (rc != 0)
        smoke_fail(120u + (UINT)rc);

    rc = check_xinput();
    if (rc != 0)
        smoke_fail(140u + (UINT)rc);

    rc = check_d3d11();
    if (rc != 0)
        smoke_fail(160u + (UINT)rc);

    ExitProcess(0);
}
