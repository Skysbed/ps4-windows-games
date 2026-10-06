#include <windows.h>
#include <d3d11.h>

/*
 * Freestanding x64 Windows smoke binary.
 *
 * No CRT entry point is used. The only imported OS API is ExitProcess; all
 * Direct3D work is reached through the D3D11CreateDeviceAndSwapChain import
 * and COM vtables. This is deliberately small so the PE import surface is
 * easy for PcPs4 to diagnose on PS4.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;

static const float g_clear_color[4] = {
    0.03f, 0.08f, 0.16f, 1.0f
};

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *context = 0;
    IDXGISwapChain *swap_chain = 0;
    ID3D11Texture2D *back_buffer = 0;
    ID3D11RenderTargetView *rtv = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    HRESULT hr;

    g_swap_desc.BufferDesc.Width = 640;
    g_swap_desc.BufferDesc.Height = 360;
    g_swap_desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    g_swap_desc.SampleDesc.Count = 1;
    g_swap_desc.BufferCount = 2;
    g_swap_desc.Windowed = TRUE;
    g_swap_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

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
        &IID_ID3D11Texture2D,
        (void **)&back_buffer);

    if (FAILED(hr) || !back_buffer)
        ExitProcess(2);

    hr = device->lpVtbl->CreateRenderTargetView(
        device,
        back_buffer,
        0,
        &rtv);

    if (FAILED(hr) || !rtv)
        ExitProcess(3);

    context->lpVtbl->OMSetRenderTargets(
        context,
        1,
        &rtv,
        0);

    context->lpVtbl->ClearRenderTargetView(
        context,
        rtv,
        g_clear_color);

    hr = swap_chain->lpVtbl->Present(
        swap_chain,
        1,
        0);

    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);

    ExitProcess(SUCCEEDED(hr) ? 0 : 4);
}
