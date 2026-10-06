#include <windows.h>
#include <d3d11.h>

/*
 * Freestanding x64 Windows smoke binary, run as /app0/game/game.exe on PS4.
 *
 * Imports: D3D11CreateDeviceAndSwapChain, ExitProcess.
 * On success the TV shows a pulsing cyan screen from a native Windows
 * .exe. Close the app from the PS button menu.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_clear_color[4];

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

        if (!probe_fn || probe_fn() != 0xBEEF)
            ExitProcess(6);

        FreeLibrary(probe_module);
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

    if (FAILED(hr) || !device || !context || !swap_chain)
        ExitProcess(1);

    hr = swap_chain->lpVtbl->GetBuffer(
        swap_chain,
        0,
        0,
        (void **)&back_buffer);

    if (FAILED(hr) || !back_buffer)
        ExitProcess(2);

    hr = device->lpVtbl->CreateRenderTargetView(
        device,
        (ID3D11Resource *)back_buffer,
        0,
        &rtv);

    if (FAILED(hr) || !rtv)
        ExitProcess(3);

    context->lpVtbl->OMSetRenderTargets(
        context,
        1,
        &rtv,
        0);

    /* Pulse dark navy -> bright cyan forever so you can see a native
     * Windows .exe presenting on the PS4. Close it from the PS button. */
    for (i = 0; ; ++i) {
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

        if (FAILED(hr))
            ExitProcess(4);
    }
}
