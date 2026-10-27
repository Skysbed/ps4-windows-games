#include <windows.h>
#include <d3d11.h>

#include "win_d3d11_vbcbuf_shaders.h"

/*
 * Real-game render pattern: a triangle from a VERTEX BUFFER (POSITION+COLOR)
 * transformed by a CONSTANT BUFFER (offset) in the same VS, updated every frame.
 * This is what an actual D3D game's draw looks like, and it is the on-device
 * test of fetch-shader + descriptor coexistence (the s[0:1] question): if the
 * cbuffer and the vertex-fetch user-data collide, the triangle won't move
 * correctly or the GPU will fault.
 *
 * The triangle auto-bounces left<->right via the cbuffer. Center pixel should be
 * a moving multi-colored triangle on a dark background.
 * Exit: 20 window, 30/31 shader, 32 layout, 33 vbuf, 34 cbuf, 0x8xxxxxxx HRESULT.
 */

typedef struct Vtx { float pos[3]; float col[4]; } Vtx;
static const Vtx g_vertices[3] = {
    {{  0.0f,  0.5f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f }},  /* red   top   */
    {{ -0.5f, -0.5f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f }},  /* green left  */
    {{  0.5f, -0.5f, 0.0f }, { 0.2f, 0.4f, 1.0f, 1.0f }},  /* blue  right */
};
struct CB { float offset[2]; float pad[2]; };

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_bg[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{ (void)h;(void)m;(void)w;(void)l; return 0; }

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0; ID3D11DeviceContext *ctx = 0;
    IDXGISwapChain *sc = 0; ID3D11Texture2D *bb = 0; ID3D11RenderTargetView *rtv = 0;
    ID3D11VertexShader *vs = 0; ID3D11PixelShader *ps = 0;
    ID3D11InputLayout *layout = 0; ID3D11Buffer *vbuf = 0, *cbuf = 0;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, want[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11_INPUT_ELEMENT_DESC elems[2];
    D3D11_BUFFER_DESC bd = {0}; D3D11_SUBRESOURCE_DATA srd = {0};
    D3D11_VIEWPORT vp; struct CB cb = { {0,0}, {0,0} };
    UINT stride = sizeof(Vtx), offset = 0;
    HRESULT hr; HWND hwnd; WNDCLASSEXA wc; unsigned i;

    wc.cbSize=sizeof(wc); wc.style=0; wc.lpfnWndProc=wnd_proc; wc.cbClsExtra=0;
    wc.cbWndExtra=0; wc.hInstance=0; wc.hIcon=0; wc.hCursor=0; wc.hbrBackground=0;
    wc.lpszMenuName=0; wc.lpszClassName="PcPs4VbCbuf"; wc.hIconSm=0;
    RegisterClassExA(&wc);
    hwnd = CreateWindowExA(0,"PcPs4VbCbuf","PcPs4 VB+Cbuf",WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,0,0);
    if (!hwnd) ExitProcess(20);

    g_swap_desc.BufferDesc.Width=640; g_swap_desc.BufferDesc.Height=360;
    g_swap_desc.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    g_swap_desc.BufferDesc.RefreshRate.Numerator=60; g_swap_desc.BufferDesc.RefreshRate.Denominator=1;
    g_swap_desc.SampleDesc.Count=1; g_swap_desc.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT;
    g_swap_desc.BufferCount=2; g_swap_desc.OutputWindow=hwnd; g_swap_desc.Windowed=TRUE;
    g_swap_desc.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;

    hr=D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,want,1,D3D11_SDK_VERSION,
        &g_swap_desc,&sc,&device,&fl,&ctx); if(FAILED(hr)) ExitProcess((UINT)hr);
    hr=sc->lpVtbl->GetBuffer(sc,0,&IID_ID3D11Texture2D,(void**)&bb); if(FAILED(hr)) ExitProcess((UINT)hr);
    hr=device->lpVtbl->CreateRenderTargetView(device,(ID3D11Resource*)bb,0,&rtv); if(FAILED(hr)) ExitProcess((UINT)hr);
    hr=device->lpVtbl->CreateVertexShader(device,g_vbcbuf_vs_dxbc,sizeof(g_vbcbuf_vs_dxbc),0,&vs); if(FAILED(hr)) ExitProcess(30);
    hr=device->lpVtbl->CreatePixelShader(device,g_vbcbuf_ps_dxbc,sizeof(g_vbcbuf_ps_dxbc),0,&ps); if(FAILED(hr)) ExitProcess(31);

    elems[0].SemanticName="POSITION"; elems[0].SemanticIndex=0; elems[0].Format=DXGI_FORMAT_R32G32B32_FLOAT;
    elems[0].InputSlot=0; elems[0].AlignedByteOffset=0; elems[0].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA; elems[0].InstanceDataStepRate=0;
    elems[1].SemanticName="COLOR"; elems[1].SemanticIndex=0; elems[1].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;
    elems[1].InputSlot=0; elems[1].AlignedByteOffset=12; elems[1].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA; elems[1].InstanceDataStepRate=0;
    hr=device->lpVtbl->CreateInputLayout(device,elems,2,g_vbcbuf_vs_dxbc,sizeof(g_vbcbuf_vs_dxbc),&layout); if(FAILED(hr)) ExitProcess(32);

    bd.ByteWidth=sizeof(g_vertices); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER;
    srd.pSysMem=g_vertices; hr=device->lpVtbl->CreateBuffer(device,&bd,&srd,&vbuf); if(FAILED(hr)) ExitProcess(33);

    bd.ByteWidth=sizeof(struct CB); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    srd.pSysMem=&cb; hr=device->lpVtbl->CreateBuffer(device,&bd,&srd,&cbuf); if(FAILED(hr)) ExitProcess(34);

    vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=640; vp.Height=360; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->lpVtbl->OMSetRenderTargets(ctx,1,&rtv,0);
    ctx->lpVtbl->RSSetViewports(ctx,1,&vp);
    ctx->lpVtbl->IASetInputLayout(ctx,layout);
    ctx->lpVtbl->IASetVertexBuffers(ctx,0,1,&vbuf,&stride,&offset);
    ctx->lpVtbl->IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->lpVtbl->VSSetShader(ctx,vs,0,0);
    ctx->lpVtbl->PSSetShader(ctx,ps,0,0);

    for (i = 0; i < 3600; ++i) {
        unsigned phase = i % 240u;
        unsigned tri = phase < 120u ? phase : 240u - phase;
        cb.offset[0] = -0.5f + (tri / 120.0f) * 1.0f;   /* bounce -0.5..0.5 */
        ctx->lpVtbl->UpdateSubresource(ctx,(ID3D11Resource*)cbuf,0,0,&cb,0,0);
        ctx->lpVtbl->VSSetConstantBuffers(ctx,0,1,&cbuf);
        ctx->lpVtbl->ClearRenderTargetView(ctx,rtv,g_bg);
        ctx->lpVtbl->Draw(ctx,3,0);
        hr=sc->lpVtbl->Present(sc,1,0); if(FAILED(hr)) ExitProcess((UINT)hr);
    }

    cbuf->lpVtbl->Release(cbuf); vbuf->lpVtbl->Release(vbuf); layout->lpVtbl->Release(layout);
    ps->lpVtbl->Release(ps); vs->lpVtbl->Release(vs); rtv->lpVtbl->Release(rtv);
    bb->lpVtbl->Release(bb); ctx->lpVtbl->Release(ctx); sc->lpVtbl->Release(sc);
    device->lpVtbl->Release(device);
    ExitProcess(0);
}
