#include <windows.h>
#include <d3d11.h>

#include "win_d3d11_cube3d_shaders.h"

/*
 * Real 3D milestone: a spinning, depth-tested, perspective cube drawn from a
 * vertex buffer + index buffer, transformed by an MVP matrix in a constant
 * buffer updated every frame. This is the core of what any 3D game (and the
 * AIO-Graphics-Test cube) renders — it adds the features beyond the flat
 * triangle: index buffer, depth buffer, and a 3D perspective/MVP transform,
 * all through PcPs4 -> DXVK -> psbc -> PS4 GPU.
 *
 * You should see a multi-colored cube rotating on a dark background, with near
 * faces correctly covering far faces (depth test). Exit codes name the step.
 */

typedef struct Vtx { float pos[3]; float col[4]; } Vtx;
static const Vtx g_v[8] = {
    {{-1,-1,-1},{1,0,0,1}}, {{ 1,-1,-1},{0,1,0,1}},
    {{ 1, 1,-1},{0,0,1,1}}, {{-1, 1,-1},{1,1,0,1}},
    {{-1,-1, 1},{1,0,1,1}}, {{ 1,-1, 1},{0,1,1,1}},
    {{ 1, 1, 1},{1,1,1,1}}, {{-1, 1, 1},{1,0.5f,0,1}},
};
static const unsigned short g_idx[36] = {
    0,1,2, 0,2,3,  4,6,5, 4,7,6,  4,5,1, 4,1,0,
    3,2,6, 3,6,7,  1,5,6, 1,6,2,  4,0,3, 4,3,7,
};

/* --- tiny row-major 4x4 math, freestanding (no libm) --- */
static float msin(float x){ const float T=6.2831853f; while(x>3.14159f)x-=T; while(x<-3.14159f)x+=T;
    float x2=x*x; return x*(1.f - x2/6.f + x2*x2/120.f - x2*x2*x2/5040.f); }
static float mcos(float x){ const float T=6.2831853f; while(x>3.14159f)x-=T; while(x<-3.14159f)x+=T;
    float x2=x*x; return 1.f - x2/2.f + x2*x2/24.f - x2*x2*x2/720.f; }
static void mmul(const float a[16], const float b[16], float o[16]){
    for(int r=0;r<4;r++)for(int c=0;c<4;c++){ float s=0; for(int k=0;k<4;k++) s+=a[r*4+k]*b[k*4+c]; o[r*4+c]=s; } }

static DXGI_SWAP_CHAIN_DESC g_sd;
static float g_bg[4]={0.05f,0.06f,0.10f,1.0f};
static LRESULT CALLBACK wp(HWND h,UINT m,WPARAM w,LPARAM l){(void)h;(void)m;(void)w;(void)l;return 0;}

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *dev=0; ID3D11DeviceContext *ctx=0; IDXGISwapChain *sc=0;
    ID3D11Texture2D *bb=0,*depth=0; ID3D11RenderTargetView *rtv=0; ID3D11DepthStencilView *dsv=0;
    ID3D11VertexShader *vs=0; ID3D11PixelShader *ps=0; ID3D11InputLayout *il=0;
    ID3D11Buffer *vb=0,*ib=0,*cb=0; ID3D11DepthStencilState *dss=0;
    D3D_FEATURE_LEVEL fl=D3D_FEATURE_LEVEL_11_0, want[1]={D3D_FEATURE_LEVEL_11_0};
    D3D11_INPUT_ELEMENT_DESC el[2]; D3D11_BUFFER_DESC bd={0}; D3D11_SUBRESOURCE_DATA srd={0};
    D3D11_TEXTURE2D_DESC dtd={0}; D3D11_DEPTH_STENCIL_DESC dsd={0}; D3D11_VIEWPORT vp;
    UINT stride=sizeof(Vtx), off=0; HRESULT hr; HWND hwnd; WNDCLASSEXA wc; unsigned frame;
    float mvpT[16];

    wc.cbSize=sizeof(wc);wc.style=0;wc.lpfnWndProc=wp;wc.cbClsExtra=0;wc.cbWndExtra=0;wc.hInstance=0;
    wc.hIcon=0;wc.hCursor=0;wc.hbrBackground=0;wc.lpszMenuName=0;wc.lpszClassName="PcPs4Cube3D";wc.hIconSm=0;
    RegisterClassExA(&wc);
    hwnd=CreateWindowExA(0,"PcPs4Cube3D","PcPs4 Cube3D",WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,0,0);
    if(!hwnd) ExitProcess(20);

    g_sd.BufferDesc.Width=640; g_sd.BufferDesc.Height=360; g_sd.BufferDesc.Format=DXGI_FORMAT_R8G8B8A8_UNORM;
    g_sd.BufferDesc.RefreshRate.Numerator=60; g_sd.BufferDesc.RefreshRate.Denominator=1;
    g_sd.SampleDesc.Count=1; g_sd.BufferUsage=DXGI_USAGE_RENDER_TARGET_OUTPUT; g_sd.BufferCount=2;
    g_sd.OutputWindow=hwnd; g_sd.Windowed=TRUE; g_sd.SwapEffect=DXGI_SWAP_EFFECT_DISCARD;
    hr=D3D11CreateDeviceAndSwapChain(0,D3D_DRIVER_TYPE_HARDWARE,0,0,want,1,D3D11_SDK_VERSION,&g_sd,&sc,&dev,&fl,&ctx);
    if(FAILED(hr)) ExitProcess((UINT)hr);
    hr=sc->lpVtbl->GetBuffer(sc,0,&IID_ID3D11Texture2D,(void**)&bb); if(FAILED(hr)) ExitProcess((UINT)hr);
    hr=dev->lpVtbl->CreateRenderTargetView(dev,(ID3D11Resource*)bb,0,&rtv); if(FAILED(hr)) ExitProcess((UINT)hr);

#ifndef NO_DEPTH
    /* depth buffer */
    dtd.Width=640; dtd.Height=360; dtd.MipLevels=1; dtd.ArraySize=1; dtd.Format=DXGI_FORMAT_D32_FLOAT;
    dtd.SampleDesc.Count=1; dtd.Usage=D3D11_USAGE_DEFAULT; dtd.BindFlags=D3D11_BIND_DEPTH_STENCIL;
    hr=dev->lpVtbl->CreateTexture2D(dev,&dtd,0,&depth); if(FAILED(hr)) ExitProcess(21);
    hr=dev->lpVtbl->CreateDepthStencilView(dev,(ID3D11Resource*)depth,0,&dsv); if(FAILED(hr)) ExitProcess(22);
    dsd.DepthEnable=TRUE; dsd.DepthWriteMask=D3D11_DEPTH_WRITE_MASK_ALL; dsd.DepthFunc=D3D11_COMPARISON_LESS;
    hr=dev->lpVtbl->CreateDepthStencilState(dev,&dsd,&dss); if(FAILED(hr)) ExitProcess(23);
#endif

    hr=dev->lpVtbl->CreateVertexShader(dev,g_cube3d_vs_dxbc,sizeof(g_cube3d_vs_dxbc),0,&vs); if(FAILED(hr)) ExitProcess(30);
    hr=dev->lpVtbl->CreatePixelShader(dev,g_cube3d_ps_dxbc,sizeof(g_cube3d_ps_dxbc),0,&ps); if(FAILED(hr)) ExitProcess(31);
    el[0].SemanticName="POSITION";el[0].SemanticIndex=0;el[0].Format=DXGI_FORMAT_R32G32B32_FLOAT;el[0].InputSlot=0;el[0].AlignedByteOffset=0;el[0].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;el[0].InstanceDataStepRate=0;
    el[1].SemanticName="COLOR";el[1].SemanticIndex=0;el[1].Format=DXGI_FORMAT_R32G32B32A32_FLOAT;el[1].InputSlot=0;el[1].AlignedByteOffset=12;el[1].InputSlotClass=D3D11_INPUT_PER_VERTEX_DATA;el[1].InstanceDataStepRate=0;
    hr=dev->lpVtbl->CreateInputLayout(dev,el,2,g_cube3d_vs_dxbc,sizeof(g_cube3d_vs_dxbc),&il); if(FAILED(hr)) ExitProcess(32);

    /* Non-indexed: expand 8 corners x 36 indices into 36 vertices and draw with
     * the proven Draw() path (the DrawIndexed/DrawIndex2 packet path GPU-faults
     * with BAD_COMMAND and is a separate fix). */
    static Vtx tris[36];
    for (int t=0; t<36; t++) tris[t] = g_v[g_idx[t]];
    bd.ByteWidth=sizeof(tris); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_VERTEX_BUFFER; srd.pSysMem=tris;
    hr=dev->lpVtbl->CreateBuffer(dev,&bd,&srd,&vb); if(FAILED(hr)) ExitProcess(33);
    bd.ByteWidth=64; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER; srd.pSysMem=mvpT;
    /* seed cb with identity so first create is valid */
    for(int k=0;k<16;k++) mvpT[k]=(k%5==0)?1.f:0.f;
    hr=dev->lpVtbl->CreateBuffer(dev,&bd,&srd,&cb); if(FAILED(hr)) ExitProcess(35);

    vp.TopLeftX=0;vp.TopLeftY=0;vp.Width=640;vp.Height=360;vp.MinDepth=0;vp.MaxDepth=1;
#ifndef NO_DEPTH
    ctx->lpVtbl->OMSetRenderTargets(ctx,1,&rtv,dsv);
    ctx->lpVtbl->OMSetDepthStencilState(ctx,dss,0);
#else
    ctx->lpVtbl->OMSetRenderTargets(ctx,1,&rtv,0);
#endif
    ctx->lpVtbl->RSSetViewports(ctx,1,&vp);
    ctx->lpVtbl->IASetInputLayout(ctx,il);
    ctx->lpVtbl->IASetVertexBuffers(ctx,0,1,&vb,&stride,&off);
    ctx->lpVtbl->IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->lpVtbl->VSSetShader(ctx,vs,0,0);
    ctx->lpVtbl->PSSetShader(ctx,ps,0,0);

    /* static perspective * view (row-major). proj: fov ~60deg, aspect 16:9, n0.1 f100. */
    float f=1.73205f, a=640.f/360.f, n=0.1f, fa=100.f;
    float proj[16]={ f/a,0,0,0,  0,f,0,0,  0,0,fa/(fa-n),1,  0,0,(-fa*n)/(fa-n),0 };
    float view[16]={ 1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,6,1 }; /* translate +z 6 (move cube away) */
    float pv[16]; mmul(view,proj,pv); /* row-vector convention: v * view * proj */

    for(frame=0; frame<36000u; ++frame){
        float ang=frame*0.02f, cx=mcos(0.5f), sx=msin(0.5f), cy=mcos(ang), sy=msin(ang);
        float rx[16]={1,0,0,0, 0,cx,sx,0, 0,-sx,cx,0, 0,0,0,1};
        float ry[16]={cy,0,-sy,0, 0,1,0,0, sy,0,cy,0, 0,0,0,1};
        float model[16], mvp[16];
        /* Orthographic: rotation + 0.5 scale, w stays 1. No perspective divide,
         * so this isolates the MVP-matrix shader path from projection math. */
        float scl[16]={0.5f,0,0,0, 0,0.5f,0,0, 0,0,0.5f,0, 0,0,0,1};
        mmul(ry,rx,model); mmul(model,scl,mvp);
        (void)pv;
        /* HLSL mul(mvp,v) is column-major; our mvp is row-major for row-vector
         * math, which is already the transpose column-major needs. Upload as-is. */
        for(int k=0;k<16;k++) mvpT[k]=mvp[k];
        ctx->lpVtbl->UpdateSubresource(ctx,(ID3D11Resource*)cb,0,0,mvpT,0,0);
        ctx->lpVtbl->VSSetConstantBuffers(ctx,0,1,&cb);
        ctx->lpVtbl->ClearRenderTargetView(ctx,rtv,g_bg);
#ifndef NO_DEPTH
        ctx->lpVtbl->ClearDepthStencilView(ctx,dsv,D3D11_CLEAR_DEPTH,1.0f,0);
#endif
        ctx->lpVtbl->Draw(ctx,36,0);
        hr=sc->lpVtbl->Present(sc,1,0); if(FAILED(hr)) ExitProcess((UINT)hr);
    }

    if(dss) dss->lpVtbl->Release(dss); cb->lpVtbl->Release(cb); vb->lpVtbl->Release(vb);
    il->lpVtbl->Release(il); ps->lpVtbl->Release(ps); vs->lpVtbl->Release(vs);
    if(dsv) dsv->lpVtbl->Release(dsv); if(depth) depth->lpVtbl->Release(depth); rtv->lpVtbl->Release(rtv);
    bb->lpVtbl->Release(bb); ctx->lpVtbl->Release(ctx); sc->lpVtbl->Release(sc); dev->lpVtbl->Release(dev);
    ExitProcess(0);
}
