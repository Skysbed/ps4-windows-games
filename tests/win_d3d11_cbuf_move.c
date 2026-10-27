#include <windows.h>
#include <d3d11.h>
#include <xinput.h>

#include "win_d3d11_cbuf_move_shaders.h"

/*
 * First interactive milestone: a player triangle you MOVE with the DualShock.
 *
 * The triangle's position and colour live in a D3D11 constant buffer (cbuffer
 * b0) updated every frame from XInput — the same channel every real game uses
 * for transforms. Geometry comes from SV_VertexID (no vertex buffer), so it
 * renders on the current GPU path. This proves constant-buffer + input + render
 * end to end through PcPs4 -> DXVK -> psbc -> GCN: the gate to a full game.
 *
 * Controls: left stick moves the triangle; D-pad changes its colour;
 * Options/Start exits. Runs up to ~3 min otherwise.
 *
 * Exit: 20 window, 30/31 shader, 32 cbuffer, 0x8xxxxxxx a D3D11/DXGI HRESULT,
 * 0 clean.
 */

struct Params {
    float offset[2];    /* clip-space centre */
    float halfsize[2];
    float color[4];
};

static DXGI_SWAP_CHAIN_DESC g_swap_desc;
static float g_bg[4] = { 0.05f, 0.05f, 0.10f, 1.0f };
static HANDLE g_out;

/* Diagnostic breadcrumbs -> PcPs4 runtime log via the console path. */
static void say(const char *s)
{
    DWORD w = 0, n = 0;
    while (s[n]) n++;
    if (g_out && g_out != INVALID_HANDLE_VALUE) WriteFile(g_out, s, n, &w, NULL);
}

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l)
{ (void)h;(void)m;(void)w;(void)l; return 0; }

void WINAPI WinMainCRTStartup(void)
{
    ID3D11Device *device = 0;
    ID3D11DeviceContext *context = 0;
    IDXGISwapChain *swap_chain = 0;
    ID3D11Texture2D *back_buffer = 0;
    ID3D11RenderTargetView *rtv = 0;
    ID3D11VertexShader *vs = 0;
    ID3D11PixelShader *ps = 0;
    ID3D11Buffer *cbuf = 0;
    D3D_FEATURE_LEVEL fl = D3D_FEATURE_LEVEL_11_0, want[1] = { D3D_FEATURE_LEVEL_11_0 };
    D3D11_VIEWPORT vp;
    D3D11_BUFFER_DESC bd = { 0 };
    D3D11_SUBRESOURCE_DATA srd;
    struct Params params;
    HRESULT hr;
    HWND hwnd;
    WNDCLASSEXA wc;
    unsigned frame;
    int ci = 0;
    int dpad_prev = 0;
    static const float palette[4][4] = {
        { 0.2f, 0.9f, 0.3f, 1.0f },  /* green  */
        { 0.9f, 0.3f, 0.2f, 1.0f },  /* red    */
        { 0.3f, 0.5f, 1.0f, 1.0f },  /* blue   */
        { 1.0f, 0.85f, 0.2f, 1.0f }, /* yellow */
    };

    wc.cbSize = sizeof(wc); wc.style = 0; wc.lpfnWndProc = wnd_proc;
    wc.cbClsExtra = 0; wc.cbWndExtra = 0; wc.hInstance = 0; wc.hIcon = 0;
    wc.hCursor = 0; wc.hbrBackground = 0; wc.lpszMenuName = 0;
    wc.lpszClassName = "PcPs4CbufMove"; wc.hIconSm = 0;
    g_out = GetStdHandle(STD_OUTPUT_HANDLE);
    say("move-demo: start\n");
    RegisterClassExA(&wc);
    hwnd = CreateWindowExA(0, "PcPs4CbufMove", "PcPs4 Move Demo",
                           WS_OVERLAPPEDWINDOW, 0, 0, 640, 360, 0, 0, 0, 0);
    if (!hwnd) ExitProcess(20);
    say("move-demo: window + about to create device\n");

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

    hr = D3D11CreateDeviceAndSwapChain(0, D3D_DRIVER_TYPE_HARDWARE, 0, 0, want, 1,
            D3D11_SDK_VERSION, &g_swap_desc, &swap_chain, &device, &fl, &context);
    if (FAILED(hr)) ExitProcess((UINT)hr);

    hr = swap_chain->lpVtbl->GetBuffer(swap_chain, 0, &IID_ID3D11Texture2D, (void **)&back_buffer);
    if (FAILED(hr)) ExitProcess((UINT)hr);
    hr = device->lpVtbl->CreateRenderTargetView(device, (ID3D11Resource *)back_buffer, 0, &rtv);
    if (FAILED(hr)) ExitProcess((UINT)hr);
    hr = device->lpVtbl->CreateVertexShader(device, g_game_vs_dxbc, sizeof(g_game_vs_dxbc), 0, &vs);
    if (FAILED(hr)) ExitProcess(30);
    hr = device->lpVtbl->CreatePixelShader(device, g_game_ps_dxbc, sizeof(g_game_ps_dxbc), 0, &ps);
    if (FAILED(hr)) ExitProcess(31);

    /* Constant buffer (32 bytes = 2 x float4). */
    params.offset[0] = 0.0f; params.offset[1] = 0.0f;
    params.halfsize[0] = 0.12f; params.halfsize[1] = 0.16f;
    params.color[0] = palette[0][0]; params.color[1] = palette[0][1];
    params.color[2] = palette[0][2]; params.color[3] = palette[0][3];
    bd.ByteWidth = sizeof(struct Params);
    bd.Usage = D3D11_USAGE_DEFAULT;
    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    srd.pSysMem = &params; srd.SysMemPitch = 0; srd.SysMemSlicePitch = 0;
    hr = device->lpVtbl->CreateBuffer(device, &bd, &srd, &cbuf);
    if (FAILED(hr)) ExitProcess(32);

    vp.TopLeftX = 0.0f; vp.TopLeftY = 0.0f; vp.Width = 640.0f; vp.Height = 360.0f;
    vp.MinDepth = 0.0f; vp.MaxDepth = 1.0f;
    context->lpVtbl->OMSetRenderTargets(context, 1, &rtv, 0);
    context->lpVtbl->RSSetViewports(context, 1, &vp);
    context->lpVtbl->IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    context->lpVtbl->VSSetShader(context, vs, 0, 0);
    context->lpVtbl->PSSetShader(context, ps, 0, 0);

#ifndef MOVE_FRAMES
#define MOVE_FRAMES 10800u
#endif
    say("loop: entering render loop\n");
    for (frame = 0; frame < MOVE_FRAMES; ++frame) {
        XINPUT_STATE st;
        if (frame == 0) say("f0: pre-XInput\n");
        if (XInputGetState(0, &st) == ERROR_SUCCESS) {
            short lx = st.Gamepad.sThumbLX, ly = st.Gamepad.sThumbLY;
            if (lx > 6000 || lx < -6000) params.offset[0] += (lx / 32767.0f) * 0.025f;
            if (ly > 6000 || ly < -6000) params.offset[1] += (ly / 32767.0f) * 0.025f;
            if (params.offset[0] >  0.88f) params.offset[0] =  0.88f;
            if (params.offset[0] < -0.88f) params.offset[0] = -0.88f;
            if (params.offset[1] >  0.82f) params.offset[1] =  0.82f;
            if (params.offset[1] < -0.82f) params.offset[1] = -0.82f;

            if (!dpad_prev && (st.Gamepad.wButtons &
                 (XINPUT_GAMEPAD_DPAD_UP | XINPUT_GAMEPAD_DPAD_DOWN |
                  XINPUT_GAMEPAD_DPAD_LEFT | XINPUT_GAMEPAD_DPAD_RIGHT))) {
                ci = (ci + 1) & 3;
                params.color[0] = palette[ci][0]; params.color[1] = palette[ci][1];
                params.color[2] = palette[ci][2]; params.color[3] = palette[ci][3];
            }
            dpad_prev = (st.Gamepad.wButtons & 0x000F) != 0;
            if (st.Gamepad.wButtons & XINPUT_GAMEPAD_START) break;
        }

        if (frame == 0) say("f0: post-XInput, pre-UpdateSubresource\n");
        context->lpVtbl->UpdateSubresource(context, (ID3D11Resource *)cbuf, 0, 0, &params, 0, 0);
        context->lpVtbl->VSSetConstantBuffers(context, 0, 1, &cbuf);
        if (frame == 0) say("f0: pre-Clear\n");
        context->lpVtbl->ClearRenderTargetView(context, rtv, g_bg);
        if (frame == 0) say("f0: pre-Draw\n");
        context->lpVtbl->Draw(context, 3, 0);
        if (frame == 0) say("f0: pre-Present\n");
        hr = swap_chain->lpVtbl->Present(swap_chain, 1, 0);
        if (frame == 0) say("f0: post-Present (first frame OK)\n");
        if (FAILED(hr)) ExitProcess((UINT)hr);
    }
    say("loop: completed\n");

    cbuf->lpVtbl->Release(cbuf);
    ps->lpVtbl->Release(ps);
    vs->lpVtbl->Release(vs);
    rtv->lpVtbl->Release(rtv);
    back_buffer->lpVtbl->Release(back_buffer);
    context->lpVtbl->Release(context);
    swap_chain->lpVtbl->Release(swap_chain);
    device->lpVtbl->Release(device);
    ExitProcess(0);
}
