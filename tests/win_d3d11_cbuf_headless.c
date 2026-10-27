#include <windows.h>
#include <d3d11.h>

#include "win_d3d11_cbuf_move_shaders.h"

/*
 * Headless validator for the cbuffer draw path — runs under Wine+DXVK+MoltenVK
 * on the dev machine (no window, no swapchain), so the exact DXVK pipeline that
 * crashed on PS4 can be checked locally before shipping another package.
 *
 * It renders the player triangle (position/colour from cbuffer b0) to an
 * offscreen render target, copies to a staging texture, reads the centre pixel,
 * and reports via ExitProcess:
 *   0     centre pixel is the green fill  -> cbuffer + draw works
 *   0x50  centre pixel is background      -> draw/cbuffer produced nothing
 *   30/31 shader create, 32 cbuffer, 33 RT, 34 staging, 35 map
 *   0x8xxxxxxx a D3D11 HRESULT
 */

struct Params { float offset[2]; float halfsize[2]; float color[4]; };

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *ctx = 0;
    ID3D11VertexShader *vs = 0;
    ID3D11PixelShader *ps = 0;
    ID3D11Buffer *cbuf = 0;
    ID3D11Texture2D *rt = 0, *stg = 0;
    ID3D11RenderTargetView *rtv = 0;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0;
    D3D_FEATURE_LEVEL want[4] = { D3D_FEATURE_LEVEL_11_1, D3D_FEATURE_LEVEL_11_0,
                                  D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    D3D11_TEXTURE2D_DESC td = { 0 };
    D3D11_BUFFER_DESC bd = { 0 };
    D3D11_SUBRESOURCE_DATA srd = { 0 };
    D3D11_VIEWPORT vp;
    D3D11_MAPPED_SUBRESOURCE map;
    struct Params params;
    float bg[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
    HRESULT hr;

    hr = D3D11CreateDevice(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, want, 4,
                           D3D11_SDK_VERSION, &device, &fl, &ctx);
    if (FAILED(hr)) ExitProcess((UINT)hr);

    td.Width = 64; td.Height = 64; td.MipLevels = 1; td.ArraySize = 1;
    td.Format = DXGI_FORMAT_R8G8B8A8_UNORM; td.SampleDesc.Count = 1;
    td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    hr = device->lpVtbl->CreateTexture2D(device, &td, 0, &rt);
    if (FAILED(hr)) ExitProcess(33);
    hr = device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *)rt, 0, &rtv);
    if (FAILED(hr)) ExitProcess(33);

    td.BindFlags = 0; td.Usage = D3D11_USAGE_STAGING;
    td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
    hr = device->lpVtbl->CreateTexture2D(device, &td, 0, &stg);
    if (FAILED(hr)) ExitProcess(34);

    hr = device->lpVtbl->CreateVertexShader(device, g_game_vs_dxbc, sizeof(g_game_vs_dxbc), 0, &vs);
    if (FAILED(hr)) ExitProcess(30);
    hr = device->lpVtbl->CreatePixelShader(device, g_game_ps_dxbc, sizeof(g_game_ps_dxbc), 0, &ps);
    if (FAILED(hr)) ExitProcess(31);

    params.offset[0] = 0.0f; params.offset[1] = 0.0f;
    params.halfsize[0] = 0.6f; params.halfsize[1] = 0.7f;  /* big, covers centre */
    params.color[0] = 0.2f; params.color[1] = 0.9f; params.color[2] = 0.3f; params.color[3] = 1.0f;
    bd.ByteWidth = sizeof(struct Params); bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    srd.pSysMem = &params;
    hr = device->lpVtbl->CreateBuffer(device, &bd, &srd, &cbuf);
    if (FAILED(hr)) ExitProcess(32);

    vp.TopLeftX = 0; vp.TopLeftY = 0; vp.Width = 64; vp.Height = 64;
    vp.MinDepth = 0; vp.MaxDepth = 1;
    ctx->lpVtbl->OMSetRenderTargets(ctx, 1, &rtv, 0);
    ctx->lpVtbl->RSSetViewports(ctx, 1, &vp);
    ctx->lpVtbl->IASetPrimitiveTopology(ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->lpVtbl->VSSetShader(ctx, vs, 0, 0);
    ctx->lpVtbl->PSSetShader(ctx, ps, 0, 0);
    ctx->lpVtbl->VSSetConstantBuffers(ctx, 0, 1, &cbuf);

    ctx->lpVtbl->ClearRenderTargetView(ctx, rtv, bg);
    ctx->lpVtbl->Draw(ctx, 3, 0);
    ctx->lpVtbl->CopyResource(ctx, (ID3D11Resource *)stg, (ID3D11Resource *)rt);

    hr = ctx->lpVtbl->Map(ctx, (ID3D11Resource *)stg, 0, D3D11_MAP_READ, 0, &map);
    if (FAILED(hr)) ExitProcess(35);
    {
        /* centre pixel (32,32) */
        unsigned char *row = (unsigned char *)map.pData + 32 * map.RowPitch;
        unsigned char *px = row + 32 * 4;  /* R,G,B,A */
        UINT code = (px[1] > 0x80 && px[1] > px[2]) ? 0 : 0x50; /* green fill present? */
        ctx->lpVtbl->Unmap(ctx, (ID3D11Resource *)stg, 0);
        ExitProcess(code);
    }
}
