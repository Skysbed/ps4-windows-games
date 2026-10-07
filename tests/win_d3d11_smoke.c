#include <windows.h>
#include <d3d11.h>

/*
 * Freestanding x64 Windows compatibility diagnostic, run on PS4 as
 * /app0/game/game.exe. It probes process-heap APIs, DLL loading, DXGI adapter discovery, D3D11
 * device/swap-chain setup, vertex-buffer/input-layout binding, triangle Draw,
 * repeated Present calls, cleanup, and return to the PcPs4 status screen.
 *
 * The current CPU renderer consumes POSITION/COLOR from the input layout
 * directly; it does not execute a DXBC shader program. This is a deliberate
 * test of the implemented CPU geometry path, not a claim of full shader
 * compatibility for arbitrary PC games.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_clear_color[4];

typedef struct PcPs4SmokeVertex {
    float position[3];
    float color[4];
} PcPs4SmokeVertex;

static const PcPs4SmokeVertex g_triangle_vertices[3] = {
    {{ 0.00f,  0.72f, 0.0f}, {1.00f, 0.20f, 0.18f, 1.0f}},
    {{-0.68f, -0.55f, 0.0f}, {0.15f, 1.00f, 0.35f, 1.0f}},
    {{ 0.68f, -0.55f, 0.0f}, {0.20f, 0.45f, 1.00f, 1.0f}}
};

static void cleanup_graphics(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    IDXGISwapChain *swap_chain,
    ID3D11Texture2D *back_buffer,
    ID3D11RenderTargetView *rtv,
    ID3D11Buffer *vertex_buffer,
    ID3D11InputLayout *input_layout)
{
    if (rtv != 0)
        rtv->lpVtbl->Release(rtv);
    if (back_buffer != 0)
        back_buffer->lpVtbl->Release(back_buffer);
    if (input_layout != 0)
        input_layout->lpVtbl->Release(input_layout);
    if (vertex_buffer != 0)
        vertex_buffer->lpVtbl->Release(vertex_buffer);
    if (context != 0)
        context->lpVtbl->Release(context);
    if (swap_chain != 0)
        swap_chain->lpVtbl->Release(swap_chain);
    if (device != 0)
        device->lpVtbl->Release(device);
}

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *context = 0;
    IDXGISwapChain *swap_chain = 0;
    ID3D11Texture2D *back_buffer = 0;
    ID3D11RenderTargetView *rtv = 0;
    ID3D11Buffer *vertex_buffer = 0;
    ID3D11InputLayout *input_layout = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr;
    unsigned i;

    g_swap_desc.BufferDesc.Width = 640;
    g_swap_desc.BufferDesc.Height = 360;
    g_swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_swap_desc.SampleDesc.Count = 1;
    g_swap_desc.BufferCount = 2;
    g_swap_desc.Windowed = TRUE;
    g_swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    /*
     * Private-heap lifecycle smoke. Destroying a heap with a live allocation
     * must invalidate the handle and reclaim its tracked allocations.
     */
    {
        HANDLE private_heap = HeapCreate(0, 0, 0);
        void *block;

        if (private_heap == 0)
            ExitProcess(18);
        block = HeapAlloc(private_heap, HEAP_ZERO_MEMORY, 24);
        if (block == 0 || !HeapValidate(private_heap, 0, block)) {
            if (block != 0)
                (void)HeapFree(private_heap, 0, block);
            (void)HeapDestroy(private_heap);
            ExitProcess(19);
        }
        if (!HeapDestroy(private_heap))
            ExitProcess(20);
        if (HeapAlloc(private_heap, 0, 8) != 0 ||
            HeapDestroy(private_heap))
            ExitProcess(21);
    }

    /*
     * Process-heap ABI smoke: allocate zeroed memory, query its size, grow it
     * with HEAP_ZERO_MEMORY, verify preserved bytes and the cleared extension,
     * then validate and free the block.
     */
    {
        HANDLE heap = GetProcessHeap();
        unsigned char *memory;
        unsigned char *grown;

        if (heap == 0)
            ExitProcess(12);
        memory = (unsigned char *)HeapAlloc(heap, HEAP_ZERO_MEMORY, 16);
        if (memory == 0)
            ExitProcess(13);
        if (HeapSize(heap, 0, memory) != 16 ||
            memory[0] != 0 ||
            !HeapValidate(heap, 0, memory)) {
            (void)HeapFree(heap, 0, memory);
            ExitProcess(14);
        }

        memory[0] = 0xA5;
        grown = (unsigned char *)HeapReAlloc(
            heap, HEAP_ZERO_MEMORY, memory, 32);
        if (grown == 0) {
            (void)HeapFree(heap, 0, memory);
            ExitProcess(15);
        }
        memory = grown;

        if (HeapSize(heap, 0, memory) != 32 ||
            memory[0] != 0xA5 ||
            memory[16] != 0 ||
            !HeapValidate(heap, 0, memory)) {
            (void)HeapFree(heap, 0, memory);
            ExitProcess(16);
        }
        if (!HeapFree(heap, 0, memory))
            ExitProcess(17);
    }

    /* Probe a real adjacent PE DLL and its exported function. */
    {
        HMODULE probe_module = LoadLibraryA("winprobe.dll");
        int (*probe_fn)(void);

        if (!probe_module)
            ExitProcess(5);
        probe_fn = (int (*)(void))GetProcAddress(probe_module, "GameProbe");
        if (!probe_fn || probe_fn() != 0xBEEF) {
            FreeLibrary(probe_module);
            ExitProcess(6);
        }
        FreeLibrary(probe_module);
    }

    /* Exercise DXGI factory creation and adapter enumeration. */
    {
        IDXGIFactory1 *factory = 0;
        static const GUID iid_idxgifactory1 = {
            0x770aae78, 0xf26f, 0x4dba,
            {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}
        };
        IDXGIAdapter1 *adapter = 0;
        DXGI_ADAPTER_DESC1 adapter_desc;

        hr = CreateDXGIFactory1(&iid_idxgifactory1, (void **)&factory);
        if (FAILED(hr) || !factory)
            ExitProcess(7);
        hr = factory->lpVtbl->EnumAdapters1(factory, 0, &adapter);
        if (FAILED(hr) || !adapter) {
            factory->lpVtbl->Release(factory);
            ExitProcess(8);
        }
        hr = adapter->lpVtbl->GetDesc1(adapter, &adapter_desc);
        if (FAILED(hr)) {
            adapter->lpVtbl->Release(adapter);
            factory->lpVtbl->Release(factory);
            ExitProcess(9);
        }
        adapter->lpVtbl->Release(adapter);
        factory->lpVtbl->Release(factory);
    }

    hr = D3D11CreateDeviceAndSwapChain(
        0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, 0, 0, 7, &g_swap_desc,
        &swap_chain, &device, &feature_level, &context);
    if (FAILED(hr) || !device || !context || !swap_chain) {
        cleanup_graphics(device, context, swap_chain, 0, 0, 0, 0);
        ExitProcess(1);
    }

    hr = swap_chain->lpVtbl->GetBuffer(
        swap_chain, 0, 0, (void **)&back_buffer);
    if (FAILED(hr) || !back_buffer) {
        cleanup_graphics(device, context, swap_chain, back_buffer, 0, 0, 0);
        ExitProcess(2);
    }

    hr = device->lpVtbl->CreateRenderTargetView(
        device, (ID3D11Resource *)back_buffer, 0, &rtv);
    if (FAILED(hr) || !rtv) {
        cleanup_graphics(device, context, swap_chain, back_buffer, rtv, 0, 0);
        ExitProcess(3);
    }

    /* Upload triangle vertices through the Windows D3D11 buffer interface. */
    {
        D3D11_BUFFER_DESC desc = {0};
        D3D11_SUBRESOURCE_DATA initial = {0};
        desc.ByteWidth = (UINT)sizeof(g_triangle_vertices);
        desc.Usage = D3D11_USAGE_DEFAULT;
        desc.BindFlags = D3D11_BIND_VERTEX_BUFFER;
        initial.pSysMem = g_triangle_vertices;
        hr = device->lpVtbl->CreateBuffer(
            device, &desc, &initial, &vertex_buffer);
    }
    if (FAILED(hr) || !vertex_buffer) {
        cleanup_graphics(device, context, swap_chain, back_buffer, rtv,
                         vertex_buffer, input_layout);
        ExitProcess(10);
    }

    /*
     * PcPs4's CPU renderer currently derives vertex semantics from this
     * descriptor and does not execute shader bytecode. Keep a non-empty
     * signature marker because the implemented CreateInputLayout contract
     * requires a non-empty bytecode/signature argument.
     */
    {
        static const unsigned char cpu_input_signature_marker = 0;
        D3D11_INPUT_ELEMENT_DESC elements[2] = {
            {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0,
             D3D11_INPUT_PER_VERTEX_DATA, 0},
            {"COLOR", 0, DXGI_FORMAT_R32G32B32A32_FLOAT, 0, 12,
             D3D11_INPUT_PER_VERTEX_DATA, 0}
        };
        hr = device->lpVtbl->CreateInputLayout(
            device, elements, 2, &cpu_input_signature_marker,
            sizeof(cpu_input_signature_marker), &input_layout);
    }
    if (FAILED(hr) || !input_layout) {
        cleanup_graphics(device, context, swap_chain, back_buffer, rtv,
                         vertex_buffer, input_layout);
        ExitProcess(11);
    }

    context->lpVtbl->OMSetRenderTargets(context, 1, &rtv, 0);
    context->lpVtbl->IASetInputLayout(context, input_layout);
    {
        UINT stride = (UINT)sizeof(PcPs4SmokeVertex);
        UINT offset = 0;
        context->lpVtbl->IASetVertexBuffers(
            context, 0, 1, &vertex_buffer, &stride, &offset);
    }
    context->lpVtbl->IASetPrimitiveTopology(
        context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

    /* A coloured triangle is drawn over the pulsing background each frame. */
    for (i = 0; i < 240; ++i) {
        unsigned phase = i % 120;
        unsigned pulse = phase < 60 ? phase : (120 - phase);

        g_clear_color[0] = 0.02f;
        g_clear_color[1] = 0.12f + (float)pulse * (0.75f / 60.0f);
        g_clear_color[2] = 0.28f + (float)pulse * (0.70f / 60.0f);
        g_clear_color[3] = 1.0f;

        context->lpVtbl->ClearRenderTargetView(context, rtv, g_clear_color);
        context->lpVtbl->Draw(context, 3, 0);
        hr = swap_chain->lpVtbl->Present(swap_chain, 1, 0);
        if (FAILED(hr)) {
            cleanup_graphics(device, context, swap_chain, back_buffer, rtv,
                             vertex_buffer, input_layout);
            ExitProcess(4);
        }
    }

    /*
     * Release graphics objects before ExitProcess. PcPs4 catches ExitProcess
     * using a non-local return to the host loader, so normal CRT teardown
     * cannot be relied upon to release the VideoOut owner.
     */
    cleanup_graphics(device, context, swap_chain, back_buffer, rtv,
                     vertex_buffer, input_layout);
    ExitProcess(0);
}
