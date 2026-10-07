#include <windows.h>
#include <d3d11.h>

/*
 * First DXVK milestone, run on PS4 as /app0/game/game.exe next to DXVK's real
 * d3d11.dll and dxgi.dll.
 *
 * It creates a window (DXVK refuses a null window handle), creates a D3D11
 * device and swap chain, then clears the back buffer to a colour that cycles
 * RED -> GREEN -> BLUE and presents 240 frames. No shaders of its own, so a
 * failure points at DXVK, the Vulkan layer or the GPU, not at the test.
 *
 * Every failing step exits with the failing HRESULT, or a small code:
 *   20 window creation failed
 *   0x8xxxxxxx a D3D11 / DXGI call failed (the HRESULT)
 * so the PcPs4 log ("binary called ExitProcess(...)") names the step.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_color[4];

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd;
    (void)msg;
    (void)wp;
    (void)lp;
    return 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *context = 0;
    IDXGISwapChain *swap_chain = 0;
    ID3D11Texture2D *back_buffer = 0;
    ID3D11RenderTargetView *rtv = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL wanted[1] = { D3D_FEATURE_LEVEL_11_0 };
    HRESULT hr;
    HWND hwnd;
    WNDCLASSEXA wc;
    unsigned i;

    wc.cbSize = sizeof(wc);
    wc.style = 0;
    wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0;
    wc.cbWndExtra = 0;
    wc.hInstance = 0;
    wc.hIcon = 0;
    wc.hCursor = 0;
    wc.hbrBackground = 0;
    wc.lpszMenuName = 0;
    wc.lpszClassName = "PcPs4DxvkTest";
    wc.hIconSm = 0;
    RegisterClassExA(&wc);

    hwnd = CreateWindowExA(0, "PcPs4DxvkTest", "PcPs4 DXVK", WS_OVERLAPPEDWINDOW,
                           0, 0, 640, 360, 0, 0, 0, 0);
    if (!hwnd)
        ExitProcess(20);

    g_swap_desc.BufferDesc.Width = 640;
    g_swap_desc.BufferDesc.Height = 360;
    g_swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_swap_desc.BufferDesc.RefreshRate.Numerator = 60;
    g_swap_desc.BufferDesc.RefreshRate.Denominator = 1;
    g_swap_desc.SampleDesc.Count = 1;
    g_swap_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
    g_swap_desc.BufferCount = 2;
    g_swap_desc.OutputWindow = hwnd;
    g_swap_desc.Windowed = TRUE;
    g_swap_desc.SwapEffect = DXGI_SWAP_EFFECT_DISCARD;

    hr = D3D11CreateDeviceAndSwapChain(
        0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, wanted, 1, D3D11_SDK_VERSION,
        &g_swap_desc, &swap_chain, &device, &feature_level, &context);
    if (FAILED(hr))
        ExitProcess((UINT)hr);

    hr = swap_chain->lpVtbl->GetBuffer(swap_chain, 0, &IID_ID3D11Texture2D,
                                       (void **)&back_buffer);
    if (FAILED(hr))
        ExitProcess((UINT)hr);

    hr = device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *)back_buffer, 0, &rtv);
    if (FAILED(hr))
        ExitProcess((UINT)hr);

    context->lpVtbl->OMSetRenderTargets(context, 1, &rtv, 0);

    for (i = 0; i < 240; ++i) {
        unsigned phase = (i / 20) % 3;
        g_color[0] = phase == 0 ? 1.0f : 0.0f;
        g_color[1] = phase == 1 ? 1.0f : 0.0f;
        g_color[2] = phase == 2 ? 1.0f : 0.0f;
        g_color[3] = 1.0f;
        context->lpVtbl->ClearRenderTargetView(context, rtv, g_color);
        hr = swap_chain->lpVtbl->Present(swap_chain, 1, 0);
        if (FAILED(hr))
            ExitProcess((UINT)hr);
    }

    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);

    ExitProcess(0);
}
