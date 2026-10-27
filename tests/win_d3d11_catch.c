#include <windows.h>
#include <d3d11.h>
#include <xinput.h>

#include "win_d3d11_cbuf_move_shaders.h"

/*
 * PcPs4 CATCH — a real, playable game on the proven path.
 *
 * Move the paddle left/right with the left stick to catch the falling block.
 * Each catch flashes the screen green and speeds things up; a miss flashes red.
 * Everything renders through PcPs4 -> DXVK -> psbc -> PS4 GPU, with per-object
 * position/colour in constant buffers (two buffers, one per object) updated
 * every frame, and input from the DualShock via XInput. Options/Start exits.
 *
 * No text rendering yet, so score shows as screen-flash feedback + the log.
 * Geometry is SV_VertexID triangles (no vertex buffer), the path we validated.
 */

struct Params { float offset[2]; float halfsize[2]; float color[4]; };

static HANDLE g_out;
static void say(const char *s)
{ DWORD w=0,n=0; while(s[n])n++; if(g_out&&g_out!=INVALID_HANDLE_VALUE) WriteFile(g_out,s,n,&w,NULL); }

static DXGI_SWAP_CHAIN_DESC g_swap_desc;

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{ (void)h;(void)m;(void)w;(void)l; return 0; }

/* cheap PRNG for target spawn x */
static unsigned g_seed = 0x12345678u;
static float rand_x(void)
{
    g_seed = g_seed * 1103515245u + 12345u;
    return (((g_seed >> 16) & 0x7fff) / 32767.0f) * 1.6f - 0.8f;  /* -0.8..0.8 */
}

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0; ID3D11DeviceContext *ctx = 0;
    IDXGISwapChain *sc = 0; ID3D11Texture2D *bb = 0; ID3D11RenderTargetView *rtv = 0;
    ID3D11VertexShader *vs = 0; ID3D11PixelShader *ps = 0;
    ID3D11Buffer *cb_player = 0, *cb_target = 0;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, want[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11_VIEWPORT vp; D3D11_BUFFER_DESC bd = { 0 }; D3D11_SUBRESOURCE_DATA srd = { 0 };
    struct Params player, target;
    HRESULT hr; HWND hwnd; WNDCLASSEXA wc; unsigned frame;

    float player_x = 0.0f;
    float target_x, target_y = 0.95f;
    float fall = 0.010f;
    int score = 0, misses = 0, flash = 0; /* flash>0 green, flash<0 red */

    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    say("catch: start\n");

    wc.cbSize=sizeof(wc); wc.style=0; wc.lpfnWndProc=wnd_proc; wc.cbClsExtra=0;
    wc.cbWndExtra=0; wc.hInstance=0; wc.hIcon=0; wc.hCursor=0; wc.hbrBackground=0;
    wc.lpszMenuName=0; wc.lpszClassName="PcPs4Catch"; wc.hIconSm=0;
    RegisterClassExA(&wc);
    hwnd = CreateWindowExA(0,"PcPs4Catch","PcPs4 Catch",WS_OVERLAPPEDWINDOW,0,0,640,360,0,0,0,0);
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
    hr=device->lpVtbl->CreateVertexShader(device,g_game_vs_dxbc,sizeof(g_game_vs_dxbc),0,&vs); if(FAILED(hr)) ExitProcess(30);
    hr=device->lpVtbl->CreatePixelShader(device,g_game_ps_dxbc,sizeof(g_game_ps_dxbc),0,&ps); if(FAILED(hr)) ExitProcess(31);

    bd.ByteWidth=sizeof(struct Params); bd.Usage=D3D11_USAGE_DEFAULT; bd.BindFlags=D3D11_BIND_CONSTANT_BUFFER;
    player.offset[0]=0; player.offset[1]=-0.8f; player.halfsize[0]=0.16f; player.halfsize[1]=0.10f;
    player.color[0]=0.2f; player.color[1]=0.8f; player.color[2]=1.0f; player.color[3]=1.0f;
    srd.pSysMem=&player; hr=device->lpVtbl->CreateBuffer(device,&bd,&srd,&cb_player); if(FAILED(hr)) ExitProcess(32);
    target.offset[0]=0; target.offset[1]=target_y; target.halfsize[0]=0.08f; target.halfsize[1]=0.10f;
    target.color[0]=1.0f; target.color[1]=0.4f; target.color[2]=0.2f; target.color[3]=1.0f;
    srd.pSysMem=&target; hr=device->lpVtbl->CreateBuffer(device,&bd,&srd,&cb_target); if(FAILED(hr)) ExitProcess(33);

    target_x = rand_x();

    vp.TopLeftX=0; vp.TopLeftY=0; vp.Width=640; vp.Height=360; vp.MinDepth=0; vp.MaxDepth=1;
    ctx->lpVtbl->OMSetRenderTargets(ctx,1,&rtv,0);
    ctx->lpVtbl->RSSetViewports(ctx,1,&vp);
    ctx->lpVtbl->IASetPrimitiveTopology(ctx,D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    ctx->lpVtbl->VSSetShader(ctx,vs,0,0);
    ctx->lpVtbl->PSSetShader(ctx,ps,0,0);
    say("catch: entering game loop\n");

    for (frame = 0; frame < 36000u; ++frame) {
        XINPUT_STATE st; DWORD xrc = XInputGetState(0, &st);
        if (xrc == ERROR_SUCCESS) {
            short lx = st.Gamepad.sThumbLX;
            if (lx > 6000 || lx < -6000) player_x += (lx / 32767.0f) * 0.03f;
            if (player_x >  0.85f) player_x =  0.85f;
            if (player_x < -0.85f) player_x = -0.85f;
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_START) break;
        }

        /* advance target */
        target_y -= fall;
        if (target_y <= -0.8f) {
            float dx = target_x - player_x; if (dx < 0) dx = -dx;
            if (dx < (player.halfsize[0] + target.halfsize[0])) {
                score++; flash = 10;
                if (fall < 0.030f) fall += 0.0015f;   /* speed up */
                say("catch: CAUGHT!\n");
            } else {
                misses++; flash = -10;
                say("catch: missed\n");
            }
            target_y = 0.95f; target_x = rand_x();
        }

        /* background: flash green on catch, red on miss, else dark */
        {
            float bg[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
            if (flash > 0) { bg[1] = 0.45f; flash--; }
            else if (flash < 0) { bg[0] = 0.45f; flash++; }
            ctx->lpVtbl->ClearRenderTargetView(ctx, rtv, bg);
        }

        /* player */
        player.offset[0] = player_x;
        ctx->lpVtbl->UpdateSubresource(ctx,(ID3D11Resource*)cb_player,0,0,&player,0,0);
        ctx->lpVtbl->VSSetConstantBuffers(ctx,0,1,&cb_player);
        ctx->lpVtbl->Draw(ctx,3,0);

        /* target */
        target.offset[0] = target_x; target.offset[1] = target_y;
        ctx->lpVtbl->UpdateSubresource(ctx,(ID3D11Resource*)cb_target,0,0,&target,0,0);
        ctx->lpVtbl->VSSetConstantBuffers(ctx,0,1,&cb_target);
        ctx->lpVtbl->Draw(ctx,3,0);

        hr = sc->lpVtbl->Present(sc, 1, 0);
        if (FAILED(hr)) ExitProcess((UINT)hr);
    }

    say("catch: exiting\n");
    cb_target->lpVtbl->Release(cb_target); cb_player->lpVtbl->Release(cb_player);
    ps->lpVtbl->Release(ps); vs->lpVtbl->Release(vs); rtv->lpVtbl->Release(rtv);
    bb->lpVtbl->Release(bb); ctx->lpVtbl->Release(ctx); sc->lpVtbl->Release(sc);
    device->lpVtbl->Release(device);
    ExitProcess(0);
}
