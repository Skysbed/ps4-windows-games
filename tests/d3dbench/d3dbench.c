/* d3dbench: what common Direct3D 11 calls cost through DXVK on the PS4.
 * "app" is the time on the game's own thread. "total" also waits for DXVK's worker thread, the driver and the GPU.
 * Results: Z:\data\pcps4\games\d3dbench\result.txt, last line starts with DONE. */
#define COBJMACROS
#define INITGUID
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d11.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "d3dbench_shaders.h"

#define RESULT "Z:\\data\\pcps4\\games\\d3dbench\\result.txt"
#define DRAWS 5000
#define ROUNDS 5
#define SIZE 64

static HANDLE g_log;
static volatile LONG g_beat;
static int g_pass, g_fail;

static void say( const char *fmt, ... )
{
    char b[300];
    DWORD done;
    int n;
    va_list a;
    va_start( a, fmt );
    n = _vsnprintf( b, sizeof(b) - 1, fmt, a );
    va_end( a );
    if (n < 0) n = sizeof(b) - 1;
    WriteFile( g_log, b, n, &done, NULL );
    FlushFileBuffers( g_log );
}

static void finish( const char *how )
{
    say( "DONE %s pass=%d fail=%d\n", how, g_pass, g_fail );
    ExitProcess( g_fail ? 1 : 0 );
}

static void must( HRESULT hr, const char *what )
{
    g_beat++;
    if (SUCCEEDED( hr )) return;
    g_fail++;
    say( "FAIL %s  hr %08lx\n", what, (unsigned long)hr );
    finish( "error" );
}

static DWORD WINAPI watchdog( void *arg )
{
    LONG last = -1;
    int idle = 0;
    for (;;)
    {
        LONG now;
        Sleep( 1000 );
        now = g_beat;
        if (now != last) { last = now; idle = 0; continue; }
        if (++idle < 25) continue;
        say( "FAIL HANG no progress for 25 s\nDONE hang pass=%d fail=%d\n", g_pass, g_fail + 1 );
        TerminateProcess( GetCurrentProcess(), 2 );
    }
    return 0;
}

static double now_ns( void )
{
    static LARGE_INTEGER f;
    LARGE_INTEGER c;
    if (!f.QuadPart) QueryPerformanceFrequency( &f );
    QueryPerformanceCounter( &c );
    return c.QuadPart * 1e9 / f.QuadPart;
}

static ID3D11Device *dev;
static ID3D11DeviceContext *ctx;
static ID3D11Buffer *vb[2], *ib, *cb_dyn, *cb_def;
static ID3D11ShaderResourceView *srv[2];
static ID3D11PixelShader *ps[2];
static ID3D11RasterizerState *rs[2];
static ID3D11BlendState *bs[2];
static ID3D11Query *evt;

static ID3D11Buffer *buffer( UINT size, UINT bind, D3D11_USAGE usage, const void *data )
{
    D3D11_BUFFER_DESC d;
    D3D11_SUBRESOURCE_DATA s;
    ID3D11Buffer *b;
    memset( &d, 0, sizeof(d) );
    d.ByteWidth = size; d.BindFlags = bind; d.Usage = usage;
    d.CPUAccessFlags = usage == D3D11_USAGE_DYNAMIC ? D3D11_CPU_ACCESS_WRITE : 0;
    s.pSysMem = data; s.SysMemPitch = s.SysMemSlicePitch = 0;
    must( ID3D11Device_CreateBuffer( dev, &d, data ? &s : NULL, &b ), "create buffer" );
    return b;
}

static void map_constants( int i )
{
    D3D11_MAPPED_SUBRESOURCE m;
    float *f;
    ID3D11DeviceContext_Map( ctx, (ID3D11Resource *)cb_dyn, 0, D3D11_MAP_WRITE_DISCARD, 0, &m );
    f = m.pData;
    f[0] = f[1] = 0.02f; f[2] = (i & 7) * 0.01f - 0.5f; f[3] = -0.5f;
    ID3D11DeviceContext_Unmap( ctx, (ID3D11Resource *)cb_dyn, 0 );
}

static void one_draw( int sc, int i )
{
    UINT stride = 8, offset = 0;
    switch (sc)
    {
    case 1: map_constants( i ); break;
    case 2:
    {
        float f[4] = { 0.02f, 0.02f, (i & 7) * 0.01f - 0.5f, -0.5f };
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb_def, 0, NULL, f, 0, 0 );
        break;
    }
    case 3: ID3D11DeviceContext_PSSetShaderResources( ctx, 0, 1, &srv[i & 1] ); break;
    case 4:
        ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &vb[i & 1], &stride, &offset );
        ID3D11DeviceContext_DrawIndexed( ctx, 3, 0, 0 );
        return;
    case 5:
        ID3D11DeviceContext_RSSetState( ctx, rs[i & 1] );
        ID3D11DeviceContext_OMSetBlendState( ctx, bs[i & 1], NULL, 0xffffffff );
        break;
    case 6: ID3D11DeviceContext_PSSetShader( ctx, ps[i & 1], NULL, 0 ); break;
    case 7:
        map_constants( i );
        ID3D11DeviceContext_PSSetShaderResources( ctx, 0, 1, &srv[i & 1] );
        if (!(i & 7)) ID3D11DeviceContext_PSSetShader( ctx, ps[(i >> 3) & 1], NULL, 0 );
        ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &vb[i & 1], &stride, &offset );
        ID3D11DeviceContext_DrawIndexed( ctx, 3, 0, 0 );
        return;
    }
    ID3D11DeviceContext_Draw( ctx, 3, 0 );
}

static void reset_state( int sc )
{
    ID3D11Buffer *c = sc == 2 ? cb_def : cb_dyn;
    UINT stride = 8, offset = 0;
    ID3D11DeviceContext_VSSetConstantBuffers( ctx, 0, 1, &c );
    ID3D11DeviceContext_PSSetConstantBuffers( ctx, 0, 1, &c );
    ID3D11DeviceContext_PSSetShaderResources( ctx, 0, 1, &srv[0] );
    ID3D11DeviceContext_PSSetShader( ctx, ps[0], NULL, 0 );
    ID3D11DeviceContext_RSSetState( ctx, rs[0] );
    ID3D11DeviceContext_OMSetBlendState( ctx, bs[0], NULL, 0xffffffff );
    ID3D11DeviceContext_IASetVertexBuffers( ctx, 0, 1, &vb[0], &stride, &offset );
    if (sc != 2) map_constants( 0 );
    else
    {
        float f[4] = { 0.02f, 0.02f, -0.5f, -0.5f };
        ID3D11DeviceContext_UpdateSubresource( ctx, (ID3D11Resource *)cb_def, 0, NULL, f, 0, 0 );
    }
}

static void wait_gpu( void )
{
    BOOL done = FALSE;
    double start = now_ns();
    ID3D11DeviceContext_End( ctx, (ID3D11Asynchronous *)evt );
    ID3D11DeviceContext_Flush( ctx );
    while (ID3D11DeviceContext_GetData( ctx, (ID3D11Asynchronous *)evt, &done, sizeof(done), 0 ) != S_OK || !done)
    {
        if (now_ns() - start > 15e9) must( E_FAIL, "GPU wait timed out" );
        SwitchToThread();
    }
}

int main( void )
{
    static const char *const names[] = { "same state", "Map discard constants", "UpdateSubresource", "texture swap",
                                         "vertex buffer + indexed", "raster + blend swap", "pixel shader swap", "game-like mix" };
    static const float verts[6] = { 0, 0, 1, 0, 0, 1 }, verts2[6] = { 0, 0, 0, 1, 1, 0 };
    static const uint16_t idx[3] = { 0, 1, 2 };
    static const D3D_FEATURE_LEVEL levels[] = { D3D_FEATURE_LEVEL_11_0 };
    D3D_FEATURE_LEVEL got;
    ID3D11Texture2D *rt, *tex[2];
    ID3D11RenderTargetView *rtv;
    ID3D11VertexShader *vs;
    ID3D11InputLayout *il;
    ID3D11SamplerState *sampler;
    D3D11_TEXTURE2D_DESC td;
    D3D11_VIEWPORT vp = { 0, 0, SIZE, SIZE, 0, 1 };
    D3D11_INPUT_ELEMENT_DESC el = { "POSITION", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0 };
    D3D11_RASTERIZER_DESC rd;
    D3D11_BLEND_DESC bd;
    D3D11_SAMPLER_DESC sd;
    D3D11_QUERY_DESC qd = { D3D11_QUERY_EVENT, 0 };
    uint32_t pixels[16];
    D3D11_SUBRESOURCE_DATA init = { pixels, 16, 0 };
    int i, sc, r;

    g_log = CreateFileA( RESULT, GENERIC_WRITE, FILE_SHARE_READ, NULL, CREATE_ALWAYS, 0, NULL );
    if (g_log == INVALID_HANDLE_VALUE) return 3;
    say( "INFO d3dbench 1, %d draws per scenario, best of %d\n", DRAWS, ROUNDS );
    CloseHandle( CreateThread( NULL, 0, watchdog, NULL, 0, NULL ) );
    CreateWindowExA( 0, "STATIC", "d3dbench", WS_POPUP, 0, 0, 64, 64, NULL, NULL, GetModuleHandleA( NULL ), NULL );

    must( D3D11CreateDevice( NULL, D3D_DRIVER_TYPE_HARDWARE, NULL, 0, levels, 1, D3D11_SDK_VERSION, &dev, &got, &ctx ),
          "create D3D11 device" );
    say( "INFO device up, feature level %x\n", (unsigned)got );

    memset( &td, 0, sizeof(td) );
    td.Width = td.Height = SIZE; td.MipLevels = td.ArraySize = 1; td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    td.SampleDesc.Count = 1; td.Usage = D3D11_USAGE_DEFAULT; td.BindFlags = D3D11_BIND_RENDER_TARGET;
    must( ID3D11Device_CreateTexture2D( dev, &td, NULL, &rt ), "render target" );
    must( ID3D11Device_CreateRenderTargetView( dev, (ID3D11Resource *)rt, NULL, &rtv ), "render target view" );
    td.Width = td.Height = 4; td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
    for (i = 0; i < 2; i++)
    {
        for (r = 0; r < 16; r++) pixels[r] = i ? 0xff204060 : 0xff604020;
        must( ID3D11Device_CreateTexture2D( dev, &td, &init, &tex[i] ), "texture" );
        must( ID3D11Device_CreateShaderResourceView( dev, (ID3D11Resource *)tex[i], NULL, &srv[i] ), "texture view" );
    }
    vb[0] = buffer( sizeof(verts), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, verts );
    vb[1] = buffer( sizeof(verts2), D3D11_BIND_VERTEX_BUFFER, D3D11_USAGE_DEFAULT, verts2 );
    ib = buffer( sizeof(idx), D3D11_BIND_INDEX_BUFFER, D3D11_USAGE_DEFAULT, idx );
    cb_dyn = buffer( 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DYNAMIC, NULL );
    cb_def = buffer( 16, D3D11_BIND_CONSTANT_BUFFER, D3D11_USAGE_DEFAULT, NULL );
    must( ID3D11Device_CreateVertexShader( dev, g_vs, sizeof(g_vs), NULL, &vs ), "vertex shader" );
    must( ID3D11Device_CreatePixelShader( dev, g_ps_a, sizeof(g_ps_a), NULL, &ps[0] ), "pixel shader A" );
    must( ID3D11Device_CreatePixelShader( dev, g_ps_b, sizeof(g_ps_b), NULL, &ps[1] ), "pixel shader B" );
    must( ID3D11Device_CreateInputLayout( dev, &el, 1, g_vs, sizeof(g_vs), &il ), "input layout" );
    memset( &rd, 0, sizeof(rd) );
    rd.FillMode = D3D11_FILL_SOLID; rd.CullMode = D3D11_CULL_NONE; rd.DepthClipEnable = TRUE;
    must( ID3D11Device_CreateRasterizerState( dev, &rd, &rs[0] ), "raster state" );
    rd.ScissorEnable = FALSE; rd.FrontCounterClockwise = TRUE;
    must( ID3D11Device_CreateRasterizerState( dev, &rd, &rs[1] ), "raster state 2" );
    memset( &bd, 0, sizeof(bd) );
    bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    bd.RenderTarget[0].SrcBlend = bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
    bd.RenderTarget[0].DestBlend = bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_ZERO;
    bd.RenderTarget[0].BlendOp = bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
    must( ID3D11Device_CreateBlendState( dev, &bd, &bs[0] ), "blend state" );
    bd.RenderTarget[0].BlendEnable = TRUE;
    must( ID3D11Device_CreateBlendState( dev, &bd, &bs[1] ), "blend state 2" );
    memset( &sd, 0, sizeof(sd) );
    sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
    sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP; sd.MaxLOD = D3D11_FLOAT32_MAX;
    must( ID3D11Device_CreateSamplerState( dev, &sd, &sampler ), "sampler" );
    must( ID3D11Device_CreateQuery( dev, &qd, &evt ), "event query" );

    ID3D11DeviceContext_OMSetRenderTargets( ctx, 1, &rtv, NULL );
    ID3D11DeviceContext_RSSetViewports( ctx, 1, &vp );
    ID3D11DeviceContext_IASetInputLayout( ctx, il );
    ID3D11DeviceContext_IASetPrimitiveTopology( ctx, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST );
    ID3D11DeviceContext_IASetIndexBuffer( ctx, ib, DXGI_FORMAT_R16_UINT, 0 );
    ID3D11DeviceContext_VSSetShader( ctx, vs, NULL, 0 );
    ID3D11DeviceContext_PSSetSamplers( ctx, 0, 1, &sampler );

    /* warm every pipeline so the numbers are not shader compilation */
    for (sc = 0; sc < 8; sc++)
    {
        reset_state( sc );
        for (i = 0; i < 64; i++) one_draw( sc, i );
        wait_gpu();
    }
    Sleep( 1500 );

    say( "INFO %-26s %10s %10s\n", "scenario", "app ns", "total ns" );
    for (sc = 0; sc < 8; sc++)
    {
        double best_app = 1e18, best_total = 1e18;
        for (r = 0; r < ROUNDS; r++)
        {
            double t0, t1, t2;
            reset_state( sc );
            wait_gpu();
            t0 = now_ns();
            for (i = 0; i < DRAWS; i++) one_draw( sc, i );
            t1 = now_ns();
            wait_gpu();
            t2 = now_ns();
            if (t1 - t0 < best_app) best_app = t1 - t0;
            if (t2 - t0 < best_total) best_total = t2 - t0;
            g_beat++;
        }
        say( "INFO %-26s %10.0f %10.0f\n", names[sc], best_app / DRAWS, best_total / DRAWS );
        g_pass++;
    }
    finish( "ok" );
    return 0;
}
