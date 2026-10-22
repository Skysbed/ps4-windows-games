#include <windows.h>
#include <d3d11.h>

#include "win_d3d11_tri_shaders.h"

/*
 * DXVK shaded-triangle milestone, run on PS4 as /app0/game/game.exe next to
 * DXVK's real d3d11.dll / dxgi.dll.
 *
 * Unlike win_dxvk_clear.c (which only clears) and win_d3d11_smoke.c (which uses
 * a CPU geometry path with a fake shader signature), this test runs REAL
 * app-provided HLSL shaders compiled to DXBC (tools/shadercompile, via Wine's
 * d3dcompiler): a vertex shader passing through POSITION/COLOR and a pixel
 * shader emitting the interpolated colour. DXVK converts that DXBC to SPIR-V
 * and the vulkan-ps4 driver compiles it to GCN - the full shader path a real
 * game depends on.
 *
 * The frame clears the back buffer to BLUE and draws one large RED triangle
 * covering the centre. Verification is headless via the driver's scanout probe:
 * the centre pixel (px[mid]) must read RED and a corner (px[0]) BLUE, which
 * proves app shaders + vertex input + rasterisation all work, not just a clear.
 *
 * Exit codes: 20 window, 0x8xxxxxxx a failing D3D11/DXGI HRESULT, 30..36 a
 * specific resource-creation failure, so the PcPs4 log names the step.
 */

typedef struct TriVertex {
    float pos[3];
    float col[4];
} TriVertex;

/* NDC: a large triangle covering the centre of the 640x360 back buffer. Red. */
static const TriVertex g_vertices[3] = {
    {{  0.0f,  0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
    {{ -0.8f, -0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
    {{  0.8f, -0.8f, 0.0f }, { 1.0f, 0.0f, 0.0f, 1.0f }},
};

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
    ID3D11InputLayout *layout = 0;
    ID3D11Buffer *vbuf = 0;
    D3D_FEATURE_LEVEL feature_level = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL wanted[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11_INPUT_ELEMENT_DESC elems[2];
    D3D11_BUFFER_DESC bd;
    D3D11_SUBRESOURCE_DATA srd;
    D3D11_VIEWPORT vp;
    UINT stride = sizeof(TriVertex), offset = 0;
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
    wc.lpszClassName = "PcPs4TriTest";
    wc.hIconSm = 0;
    RegisterClassExA(&wc);

    hwnd = CreateWindowExA(0, "PcPs4TriTest", "PcPs4 DXVK Triangle",
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

    hr = device->lpVtbl->CreateVertexShader(device, g_tri_vs_dxbc, sizeof(g_tri_vs_dxbc), 0, &vs);
    if (FAILED(hr))
        ExitProcess(30);

    hr = device->lpVtbl->CreatePixelShader(device, g_tri_ps_dxbc, sizeof(g_tri_ps_dxbc), 0, &ps);
    if (FAILED(hr))
        ExitProcess(31);

    elems[0].SemanticName = "POSITION";
    elems[0].SemanticIndex = 0;
    elems[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elems[0].InputSlot = 0;
    elems[0].AlignedByteOffset = 0;
    elems[0].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    elems[0].InstanceDataStepRate = 0;
    elems[1].SemanticName = "COLOR";
    elems[1].SemanticIndex = 0;
    elems[1].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[1].InputSlot = 0;
    elems[1].AlignedByteOffset = 12;
    elems[1].InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
    elems[1].InstanceDataStepRate = 0;

    hr = device->lpVtbl->CreateInputLayout(device, elems, 2,
                                           g_tri_vs_dxbc, sizeof(g_tri_vs_dxbc), &layout);
    if (FAILED(hr))
        ExitProcess(32);

    bd.ByteWidth = sizeof(g_vertices);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;
    bd.CPUAccessFlags = 0;
    bd.MiscFlags = 0;
    bd.StructureByteStride = 0;
    srd.pSysMem = g_vertices;
    srd.SysMemPitch = 0;
    srd.SysMemSlicePitch = 0;
    hr = device->lpVtbl->CreateBuffer(device, &bd, &srd, &vbuf);
    if (FAILED(hr))
        ExitProcess(33);

    vp.TopLeftX = 0.0f;
    vp.TopLeftY = 0.0f;
    vp.Width = 640.0f;
    vp.Height = 360.0f;
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;

    context->lpVtbl->OMSetRenderTargets(context, 1, &rtv, 0);
    context->lpVtbl->RSSetViewports(context, 1, &vp);
    context->lpVtbl->IASetInputLayout(context, layout);
    context->lpVtbl->IASetVertexBuffers(context, 0, 1, &vbuf, &stride, &offset);
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

    vbuf->lpVtbl->Release(vbuf);
    layout->lpVtbl->Release(layout);
    ps->lpVtbl->Release(ps);
    vs->lpVtbl->Release(vs);
    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);

    ExitProcess(0);
}
