#include <windows.h>
#include <d3d11.h>

#include "win_d3d11_tri_vid_shaders.h"

/*
 * DXVK app-shader geometry milestone (no vertex buffer).
 *
 * Isolates the app-shader draw path from vertex-attribute fetch: the vertex
 * shader generates the triangle positions from SV_VertexID (so no vertex
 * buffer, input layout, or GCN fetch shader is involved) and the pixel shader
 * outputs red. Clears blue, draws the triangle, presents 240 frames.
 *
 * If the scanout centre reads RED, real app shaders render geometry end to end
 * through DXVK -> psbc -> GCN, and the only remaining gap for vertex-buffer
 * geometry (win_d3d11_tri.c) is the vertex-fetch prolog in psbc.
 *
 * Exit: 20 window, 0x8xxxxxxx a D3D11/DXGI HRESULT, 30/31 shader creation.
 */

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_blue[4] = { 0.0f, 0.0f, 1.0f, 1.0f };

static LRESULT CALLBACK wnd_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    (void)hwnd; (void)msg; (void)wp; (void)lp;
    return 0;
}

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *context = 0;
    IDXGISwapChain *swap_chain = 0;
    ID3D11Texture2D *back_buffer = 0;
    ID3D11RenderTargetView *rtv = 0;
    ID3D11VertexShader *vs = 0;
    ID3D11PixelShader *ps = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL wanted[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11_VIEWPORT vp;
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
    wc.lpszClassName = "PcPs4TriVidTest";
    wc.hIconSm = 0;
    RegisterClassExA(&wc);

    hwnd = CreateWindowExA(0, "PcPs4TriVidTest", "PcPs4 DXVK Triangle (VID)",
                           WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, 0, 0, 0, 0);
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

    hr = device->lpVtbl->CreateVertexShader(device, g_vid_vs_dxbc, sizeof(g_vid_vs_dxbc), 0, &vs);
    if (FAILED(hr))
        ExitProcess(30);

    hr = device->lpVtbl->CreatePixelShader(device, g_vid_ps_dxbc, sizeof(g_vid_ps_dxbc), 0, &ps);
    if (FAILED(hr))
        ExitProcess(31);

    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = 640.0f;
    vp.Height = 360.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    context->lpVtbl->OMSetRenderTargets(context, 1, &rtv, 0);
    context->lpVtbl->RSSetViewports(context, 1, &vp);
    context->lpVtbl->IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->lpVtbl->VSSetShader(context, vs, 0, 0);
    context->lpVtbl->PSSetShader(context, ps, 0, 0);

    for (i = 0; i < 240; ++i) {
        context->lpVtbl->ClearRenderTargetView(context, rtv, g_blue);
        context->lpVtbl->Draw(context, 3, 0);
        hr = swap_chain->lpVtbl->Present(swap_chain, 1, 0);
        if (FAILED(hr))
            ExitProcess((UINT)hr);
    }

    ps->lpVtbl->Release(ps);
    vs->lpVtbl->Release(vs);
    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);

    ExitProcess(0);
}
