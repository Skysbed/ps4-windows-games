/*
 * D3D9 fixed-function triangle, run on PS4 as /app0/game/game.exe next to
 * DXVK's real d3d9.dll. Unlike the D3D11 clear test this draws actual geometry:
 * a colour triangle via the fixed-function pipeline (D3DFVF_XYZRHW|DIFFUSE), so
 * DXVK generates + compiles the vertex/pixel shaders itself. It tests whether a
 * game's real draw (not just a clear) renders through psbc to the PS4 GPU.
 *
 * The triangle's colours rotate each frame so the scanout probe sees change.
 * Exit codes: 20 window, 0x8xxxxxxx a D3D9 HRESULT, 0 clean.
 */
#include <windows.h>
#include <d3d9.h>

/* Freestanding (-nostdlib): provide the mem helpers the compiler may emit,
 * matching mingw's <string.h> prototypes to avoid a conflicting-types error. */
void * __cdecl memset(void *d, int c, size_t n) { unsigned char *p = (unsigned char *)d; while (n--) *p++ = (unsigned char)c; return d; }
void * __cdecl memcpy(void * __restrict__ d, const void * __restrict__ s, size_t n) { unsigned char *a = (unsigned char *)d; const unsigned char *b = (const unsigned char *)s; while (n--) *a++ = *b++; return d; }

static LRESULT CALLBACK wnd_proc(HWND h, UINT m, WPARAM w, LPARAM l) { return DefWindowProcA(h, m, w, l); }

struct Vtx { float x, y, z, rhw; DWORD color; };

void WINAPI WinMainCRTStartup(void)
{
    WNDCLASSEXA wc;
    HWND hwnd;
    IDirect3D9 *d3d = 0;
    IDirect3DDevice9 *dev = 0;
    IDirect3DVertexBuffer9 *vb = 0;
    D3DPRESENT_PARAMETERS pp;
    HRESULT hr;
    unsigned i;

    memset(&wc, 0, sizeof(wc));
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = wnd_proc;
    wc.lpszClassName = "PcPs4D3D9Tri";
    RegisterClassExA(&wc);
    hwnd = CreateWindowExA(0, "PcPs4D3D9Tri", "PcPs4 D3D9", WS_OVERLAPPEDWINDOW,
                           0, 0, 1280, 720, 0, 0, 0, 0);
    if (!hwnd) ExitProcess(20);

    d3d = Direct3DCreate9(D3D_SDK_VERSION);
    if (!d3d) ExitProcess(21);

    memset(&pp, 0, sizeof(pp));
    pp.Windowed = TRUE;
    pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
    pp.BackBufferFormat = D3DFMT_X8R8G8B8;
    pp.BackBufferWidth = 1280;
    pp.BackBufferHeight = 720;
    pp.hDeviceWindow = hwnd;

    hr = d3d->lpVtbl->CreateDevice(d3d, D3DADAPTER_DEFAULT, D3DDEVTYPE_HAL, hwnd,
                                   D3DCREATE_HARDWARE_VERTEXPROCESSING, &pp, &dev);
    if (FAILED(hr)) ExitProcess((UINT)hr);

    hr = dev->lpVtbl->CreateVertexBuffer(dev, 3 * sizeof(struct Vtx), 0,
                                         (D3DFVF_XYZRHW | D3DFVF_DIFFUSE),
                                         D3DPOOL_MANAGED, &vb, 0);
    if (FAILED(hr)) ExitProcess((UINT)hr);

    for (i = 0; i < 240; ++i) {
        unsigned phase = (i / 20) % 3;
        DWORD c0 = phase == 0 ? 0xFFFF0000 : (phase == 1 ? 0xFF00FF00 : 0xFF0000FF);
        DWORD c1 = phase == 0 ? 0xFF00FF00 : (phase == 1 ? 0xFF0000FF : 0xFFFF0000);
        DWORD c2 = phase == 0 ? 0xFF0000FF : (phase == 1 ? 0xFFFF0000 : 0xFF00FF00);
        struct Vtx verts[3] = {
            { 640.0f,  80.0f, 0.5f, 1.0f, c0 },
            {1120.0f, 640.0f, 0.5f, 1.0f, c1 },
            { 160.0f, 640.0f, 0.5f, 1.0f, c2 },
        };
        void *p = 0;
        if (SUCCEEDED(vb->lpVtbl->Lock(vb, 0, sizeof(verts), &p, 0)) && p) {
            memcpy(p, verts, sizeof(verts));
            vb->lpVtbl->Unlock(vb);
        }

        dev->lpVtbl->Clear(dev, 0, 0, D3DCLEAR_TARGET, 0xFF202020, 1.0f, 0);
        if (SUCCEEDED(dev->lpVtbl->BeginScene(dev))) {
            dev->lpVtbl->SetFVF(dev, (D3DFVF_XYZRHW | D3DFVF_DIFFUSE));
            dev->lpVtbl->SetStreamSource(dev, 0, vb, 0, sizeof(struct Vtx));
            dev->lpVtbl->DrawPrimitive(dev, D3DPT_TRIANGLELIST, 0, 1);
            dev->lpVtbl->EndScene(dev);
        }
        hr = dev->lpVtbl->Present(dev, 0, 0, 0, 0);
        if (FAILED(hr)) ExitProcess((UINT)hr);
    }

    vb->lpVtbl->Release(vb);
    dev->lpVtbl->Release(dev);
    d3d->lpVtbl->Release(d3d);
    ExitProcess(0);
}
