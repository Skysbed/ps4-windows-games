#include <windows.h>
#include <d3d11.h>

/*
 * Freestanding x64 Windows smoke binary, run as /app0/game/game.exe on PS4.
 *
 * Imports: D3D11CreateDeviceAndSwapChain, ExitProcess.
 * On success the TV shows a pulsing cyan screen from a native Windows
 * .exe, then returns to the PcPs4 diagnostic result screen.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_clear_color[4];

static void cleanup_graphics(
    ID3D11Device *device,
    ID3D11DeviceContext *context,
    IDXGISwapChain *swap_chain,
    ID3D11Texture2D *back_buffer,
    ID3D11RenderTargetView *rtv)
{
    if (rtv != 0)
        rtv->lpVtbl->Release(rtv);
    if (back_buffer != 0)
        back_buffer->lpVtbl->Release(back_buffer);
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
     * First exercise the runtime PE-DLL path. This keeps the hardware test
     * single-install: LoadLibrary/GetProcAddress must map a real adjacent
     * Windows DLL before D3D11 presentation is attempted.
     */
    {
        HMODULE probe_module = LoadLibraryA("winprobe.dll");
        int (*probe_fn)(void);

        if (!probe_module)
            ExitProcess(5);

        probe_fn = (int (*)(void))GetProcAddress(
            probe_module, "GameProbe");

        if (!probe_fn || probe_fn() != 0xBEEF) {
            FreeLibrary(probe_module);
            ExitProcess(6);
        }

        FreeLibrary(probe_module);
    }

    /*
     * Exercise the normal DXGI discovery path used by D3D11 applications:
     * create a DXGI 1.1 factory, enumerate adapter zero, and read its
     * descriptor before creating the D3D11 device.
     */
    {
        IDXGIFactory1 *factory = 0;
        static const GUID pcps4_iid_idxgifactory1 = {
            0x770aae78, 0xf26f, 0x4dba,
            {0xa8, 0x29, 0x25, 0x3c, 0x83, 0xd1, 0xb3, 0x87}
        };
        IDXGIAdapter1 *adapter = 0;
        DXGI_ADAPTER_DESC1 adapter_desc;

        hr = CreateDXGIFactory1(
            &pcps4_iid_idxgifactory1,
            (void **)&factory);
        if (FAILED(hr) || !factory)
            ExitProcess(7);

        hr = factory->lpVtbl->EnumAdapters1(
            factory,
            0,
            &adapter);
        if (FAILED(hr) || !adapter) {
            factory->lpVtbl->Release(factory);
            ExitProcess(8);
        }

        hr = adapter->lpVtbl->GetDesc1(
            adapter,
            &adapter_desc);
        if (FAILED(hr)) {
            adapter->lpVtbl->Release(adapter);
            factory->lpVtbl->Release(factory);
            ExitProcess(9);
        }

        adapter->lpVtbl->Release(adapter);
        factory->lpVtbl->Release(factory);
    }

    hr = D3D11CreateDeviceAndSwapChain(
        0,
        D3D_DRIVER_TYPE_HARDWARE,
        0,
        0,
        0,
        0,
        7,
        &g_swap_desc,
        &swap_chain,
        &device,
        &feature_level,
        &context);

    if (FAILED(hr) || !device || !context || !swap_chain) {
        cleanup_graphics(device, context, swap_chain, 0, 0);
        ExitProcess(1);
    }

    hr = swap_chain->lpVtbl->GetBuffer(
        swap_chain,
        0,
        0,
        (void **)&back_buffer);

    if (FAILED(hr) || !back_buffer) {
        cleanup_graphics(device, context, swap_chain, back_buffer, 0);
        ExitProcess(2);
    }

    hr = device->lpVtbl->CreateRenderTargetView(
        device,
        (ID3D11Resource *)back_buffer,
        0,
        &rtv);

    if (FAILED(hr) || !rtv) {
        cleanup_graphics(device, context, swap_chain, back_buffer, rtv);
        ExitProcess(3);
    }

    context->lpVtbl->OMSetRenderTargets(
        context,
        1,
        &rtv,
        0);

    /* Pulse dark navy -> bright cyan for 240 presented frames. */
    for (i = 0; i < 240; ++i) {
        unsigned phase = i % 120;
        unsigned pulse = phase < 60 ? phase : (120 - phase);

        g_clear_color[0] = 0.02f;
        g_clear_color[1] = 0.12f + (float)pulse * (0.75f / 60.0f);
        g_clear_color[2] = 0.28f + (float)pulse * (0.70f / 60.0f);
        g_clear_color[3] = 1.0f;

        context->lpVtbl->ClearRenderTargetView(
            context,
            rtv,
            g_clear_color);

        hr = swap_chain->lpVtbl->Present(
            swap_chain,
            1,
            0);

        if (FAILED(hr)) {
            cleanup_graphics(
                device, context, swap_chain, back_buffer, rtv);
            ExitProcess(4);
        }
    }

    /*
     * Release every Windows graphics object before ExitProcess. PcPs4
     * catches ExitProcess with a longjmp, so relying on normal C++/CRT
     * teardown here would leave the PS4 VideoOut surface alive and could
     * prevent the PcPs4 result screen from being reacquired.
     */
    cleanup_graphics(
        device, context, swap_chain, back_buffer, rtv);

    /* Returns control to the PcPs4 diagnostic result screen. */
    ExitProcess(0);
}
