#include "pcps4_d3d11_com.h"
#include "pcps4_abi.h"
#include "pcps4_fault.h"  /* pcps4_fault_stage: crash breadcrumb */

#include <new>
#include <stdlib.h>
#include <string.h>

typedef struct PCPS4Guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} PCPS4Guid;

static const PCPS4Guid IID_IUnknown_PCPS4 = {
    0x00000000u, 0x0000u, 0x0000u,
    {0xC0,0x00,0x00,0x00,0x00,0x00,0x00,0x46}
};

static const PCPS4Guid IID_ID3D11Device_PCPS4 = {
    0xdb6f6ddbu, 0xac77u, 0x4e88u,
    {0x82,0x53,0x81,0x9d,0xf9,0xbb,0xf1,0x40}
};

static const PCPS4Guid IID_ID3D11DeviceContext_PCPS4 = {
    0xc0bfa96cu, 0xe089u, 0x44fbu,
    {0x8e,0xaf,0x26,0xf8,0x79,0x61,0x90,0xda}
};

static const PCPS4Guid IID_IDXGISwapChain_PCPS4 = {
    0x310d36a0u, 0xd2e7u, 0x4c0au,
    {0xaa,0x04,0x6a,0x9d,0x23,0xb8,0x88,0x6a}
};

static PCPS4_MS_ABI int pcps4_guid_equal(
    const void *a,
    const PCPS4Guid *b)
{
    return a != NULL && b != NULL &&
           memcmp(a, b, sizeof(*b)) == 0;
}

enum {
    PCPS4_COM_RES_BUFFER = 1,
    PCPS4_COM_RES_TEXTURE2D,
    PCPS4_COM_RES_VERTEX_SHADER,
    PCPS4_COM_RES_PIXEL_SHADER,
    PCPS4_COM_RES_INPUT_LAYOUT,
    PCPS4_COM_RES_SRV,
    PCPS4_COM_RES_SAMPLER,
    PCPS4_COM_RES_RASTERIZER,
    PCPS4_COM_RES_BLEND,
    PCPS4_COM_RES_DEPTH_STATE,
    PCPS4_COM_RES_RTV,
    PCPS4_COM_RES_DSV
};

struct PCPS4ComDevice;
struct PCPS4ComContext;

struct PCPS4ComResource {
    const void **lpVtbl;
    uint32_t refs;
    uint32_t kind;
    PCPS4ComDevice *device;
    void *impl;
    PCPS4ComResource *resource;
};

struct PCPS4ComDevice {
    const void **lpVtbl;
    uint32_t refs;
    ID3D11Device *impl;
    PCPS4ComContext *context;
};

struct PCPS4ComContext {
    const void **lpVtbl;
    uint32_t refs;
    ID3D11DeviceContext *impl;
    PCPS4ComDevice *device;
};

struct PCPS4ComSwapChain {
    const void **lpVtbl;
    uint32_t refs;
    IDXGISwapChain *impl;
    PCPS4ComDevice *device;
    PCPS4ComContext *context;
};

/* Forward declarations used by proxy release paths. */
static PCPS4_MS_ABI UINT com_device_release(PCPS4ComDevice *self);
static PCPS4_MS_ABI UINT com_context_release(PCPS4ComContext *self);
static PCPS4_MS_ABI UINT com_resource_release(PCPS4ComResource *self);

#define PCPS4_COM_DEVICE_SLOTS 43
#define PCPS4_COM_CONTEXT_SLOTS 114
#define PCPS4_COM_SWAP_SLOTS 18

#define PCPS4_COM_RESOURCE_DEVICECHILD_SLOTS 7
#define PCPS4_COM_RESOURCE_VIEW_SLOTS 9
#define PCPS4_COM_RESOURCE_DESC_SLOTS 8
#define PCPS4_COM_RESOURCE_DESC_FULL_SLOTS 11

static PCPS4_MS_ABI void com_noop(void) {}
static PCPS4_MS_ABI HRESULT com_nyi(void) { return E_NOTIMPL; }

static PCPS4_MS_ABI UINT com_device_addref(PCPS4ComDevice *self)
{
    return ++self->refs;
}

static PCPS4_MS_ABI UINT com_context_addref(PCPS4ComContext *self)
{
    return ++self->refs;
}

static PCPS4_MS_ABI UINT com_swap_addref(PCPS4ComSwapChain *self)
{
    return ++self->refs;
}

static PCPS4_MS_ABI HRESULT com_resource_query(
    PCPS4ComResource *self,
    void *riid,
    void **out)
{
    (void)self;
    (void)riid;
    if (out != NULL)
        *out = NULL;
    return E_NOTIMPL;
}

static PCPS4_MS_ABI UINT com_resource_addref(PCPS4ComResource *self)
{
    return ++self->refs;
}

static PCPS4_MS_ABI void com_release_internal_resource(PCPS4ComResource *self)
{
    if (self->impl == NULL)
        return;

    switch (self->kind) {
        case PCPS4_COM_RES_BUFFER:
            ((ID3D11Buffer *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_TEXTURE2D:
            ((ID3D11Texture2D *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_VERTEX_SHADER:
            ((ID3D11VertexShader *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_PIXEL_SHADER:
            ((ID3D11PixelShader *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_INPUT_LAYOUT:
            ((ID3D11InputLayout *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_SRV:
            ((ID3D11ShaderResourceView *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_SAMPLER:
            ((ID3D11SamplerState *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_RASTERIZER:
            ((ID3D11RasterizerState *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_BLEND:
            ((ID3D11BlendState *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_DEPTH_STATE:
            ((ID3D11DepthStencilState *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_RTV:
            ((ID3D11RenderTargetView *)self->impl)->Release();
            break;
        case PCPS4_COM_RES_DSV:
            ((ID3D11DepthStencilView *)self->impl)->Release();
            break;
        default:
            break;
    }
}

static PCPS4_MS_ABI UINT com_resource_release(PCPS4ComResource *self)
{
    if (self == NULL || self->refs == 0)
        return 0;

    if (--self->refs != 0)
        return self->refs;

    if (self->resource != NULL) {
        com_resource_release(self->resource);
        self->resource = NULL;
    }

    com_release_internal_resource(self);

    if (self->device != NULL) {
        PCPS4ComDevice *device = self->device;
        self->device = NULL;
        /* Device proxy release is defined below. */
        com_device_release(device);
    }

    free(self);
    return 0;
}

static PCPS4_MS_ABI void com_resource_get_device(
    PCPS4ComResource *self,
    void **out)
{
    if (out == NULL)
        return;

    *out = NULL;

    if (self->device == NULL)
        return;

    ++self->device->refs;
    *out = self->device;
}

static PCPS4_MS_ABI void com_resource_get_type(
    PCPS4ComResource *self,
    UINT *out)
{
    if (out == NULL)
        return;

    switch (self->kind) {
        case PCPS4_COM_RES_BUFFER:
        case PCPS4_COM_RES_TEXTURE2D:
            *out = (self->kind == PCPS4_COM_RES_BUFFER) ? 1u : 2u;
            break;
        default:
            *out = 0u;
            break;
    }
}

static PCPS4_MS_ABI void com_buffer_get_desc(
    PCPS4ComResource *self,
    D3D11_BUFFER_DESC *desc)
{
    size_t size = 0;

    if (desc == NULL)
        return;

    memset(desc, 0, sizeof(*desc));

    if (self->impl == NULL ||
        pcps4_gpu_buffer_get_size(
            ((ID3D11Buffer *)self->impl)->native, &size) != PCPS4_GPU_OK)
        return;

    desc->ByteWidth = (UINT)size;
    desc->Usage = D3D11_USAGE_DEFAULT;
    desc->BindFlags = D3D11_BIND_VERTEX_BUFFER |
                      D3D11_BIND_INDEX_BUFFER |
                      D3D11_BIND_CONSTANT_BUFFER;
}

static PCPS4_MS_ABI void com_texture_get_desc(
    PCPS4ComResource *self,
    D3D11_TEXTURE2D_DESC *desc)
{
    if (desc == NULL)
        return;

    memset(desc, 0, sizeof(*desc));
    if (self->impl != NULL)
        ((ID3D11Texture2D *)self->impl)->GetDesc(desc);
}

static PCPS4_MS_ABI HRESULT com_view_get_resource(
    PCPS4ComResource *self,
    void **out)
{
    if (out == NULL)
        return E_INVALIDARG;

    *out = NULL;

    if (self->resource == NULL)
        return E_FAIL;

    com_resource_addref(self->resource);
    *out = self->resource;
    return S_OK;
}

static PCPS4_MS_ABI UINT com_texture_release(PCPS4ComResource *self)
{
    return com_resource_release(self);
}

static PCPS4_MS_ABI PCPS4ComResource *wrap_resource(
    PCPS4ComDevice *device,
    uint32_t kind,
    void *impl,
    PCPS4ComResource *resource)
{
    PCPS4ComResource *out;

    if (impl == NULL || device == NULL)
        return NULL;

    out = (PCPS4ComResource *)calloc(1, sizeof(*out));
    if (out == NULL)
        return NULL;

    out->refs = 1;
    out->kind = kind;
    out->device = device;
    out->impl = impl;
    out->resource = resource;

    ++device->refs;
    if (resource != NULL)
        com_resource_addref(resource);

    return out;
}

/* Forward declarations for the static PCPS4_MS_ABI vtables. */
static const void *g_device_vtbl[PCPS4_COM_DEVICE_SLOTS];
static const void *g_context_vtbl[PCPS4_COM_CONTEXT_SLOTS];
static const void *g_swap_vtbl[PCPS4_COM_SWAP_SLOTS];
static const void *g_devicechild_vtbl[PCPS4_COM_RESOURCE_DEVICECHILD_SLOTS];
static const void *g_view_vtbl[PCPS4_COM_RESOURCE_VIEW_SLOTS];
static const void *g_desc_vtbl[PCPS4_COM_RESOURCE_DESC_SLOTS];
static const void *g_resource_full_vtbl[PCPS4_COM_RESOURCE_DESC_FULL_SLOTS];

static PCPS4_MS_ABI HRESULT com_device_query(
    PCPS4ComDevice *self,
    void *riid,
    void **out)
{
    if (self == NULL || out == NULL)
        return E_INVALIDARG;

    *out = NULL;

    if (pcps4_guid_equal(riid, &IID_IUnknown_PCPS4) ||
        pcps4_guid_equal(riid, &IID_ID3D11Device_PCPS4)) {
        ++self->refs;
        *out = self;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static PCPS4_MS_ABI UINT com_device_release(PCPS4ComDevice *self)
{
    if (self == NULL || self->refs == 0)
        return 0;

    if (--self->refs != 0)
        return self->refs;

    if (self->context != NULL)
        self->context = NULL;

    if (self->impl != NULL)
        self->impl->Release();

    free(self);
    return 0;
}

static PCPS4_MS_ABI void com_context_get_device(
    PCPS4ComContext *self,
    void **out)
{
    if (out == NULL)
        return;
    *out = NULL;
    if (self != NULL && self->device != NULL) {
        ++self->device->refs;
        *out = self->device;
    }
}

static PCPS4_MS_ABI HRESULT com_context_query(
    PCPS4ComContext *self,
    void *riid,
    void **out)
{
    if (self == NULL || out == NULL)
        return E_INVALIDARG;

    *out = NULL;

    if (pcps4_guid_equal(riid, &IID_IUnknown_PCPS4) ||
        pcps4_guid_equal(riid, &IID_ID3D11DeviceContext_PCPS4)) {
        ++self->refs;
        *out = self;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static PCPS4_MS_ABI UINT com_context_release(PCPS4ComContext *self)
{
    if (self == NULL || self->refs == 0)
        return 0;

    if (--self->refs != 0)
        return self->refs;

    if (self->device != NULL) {
        PCPS4ComDevice *device = self->device;
        self->device = NULL;
        com_device_release(device);
    }

    if (self->impl != NULL)
        self->impl->Release();

    free(self);
    return 0;
}

static PCPS4_MS_ABI HRESULT com_swap_query(
    PCPS4ComSwapChain *self,
    void *riid,
    void **out)
{
    if (self == NULL || out == NULL)
        return E_INVALIDARG;

    *out = NULL;

    if (pcps4_guid_equal(riid, &IID_IUnknown_PCPS4) ||
        pcps4_guid_equal(riid, &IID_IDXGISwapChain_PCPS4)) {
        ++self->refs;
        *out = self;
        return S_OK;
    }

    return E_NOINTERFACE;
}

static PCPS4_MS_ABI UINT com_swap_release(PCPS4ComSwapChain *self)
{
    if (self == NULL || self->refs == 0)
        return 0;

    if (--self->refs != 0)
        return self->refs;

    if (self->context != NULL) {
        PCPS4ComContext *context = self->context;
        self->context = NULL;
        com_context_release(context);
    }

    if (self->device != NULL) {
        PCPS4ComDevice *device = self->device;
        self->device = NULL;
        com_device_release(device);
    }

    if (self->impl != NULL)
        self->impl->Release();

    free(self);
    return 0;
}

/* IUnknown / DXGI base methods. */
static PCPS4_MS_ABI HRESULT swap_private_data(PCPS4ComSwapChain *self, void *a, UINT b, const void *c)
{ (void)self; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_private_data_i(PCPS4ComSwapChain *self, void *a, const void *b)
{ (void)self; (void)a; (void)b; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_private_data(PCPS4ComSwapChain *self, void *a, UINT *b, void *c)
{ (void)self; (void)a; (void)b; (void)c; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_parent(PCPS4ComSwapChain *self, void *a, void **b)
{ (void)self; (void)a; if (b) *b=NULL; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_device(PCPS4ComSwapChain *self, void *a, void **b)
{ (void)a; if (b == NULL) return E_INVALIDARG; *b=NULL; if (self->device) { ++self->device->refs; *b=self->device; return S_OK; } return E_FAIL; }

static PCPS4_MS_ABI HRESULT swap_present(PCPS4ComSwapChain *self, UINT sync, UINT flags)
{
    if (self == NULL || self->impl == NULL)
        return E_FAIL;
    return self->impl->Present(sync, flags);
}

static PCPS4_MS_ABI HRESULT swap_get_buffer(
    PCPS4ComSwapChain *self,
    UINT buffer,
    void *riid,
    void **surface)
{
    ID3D11Texture2D *texture = NULL;
    PCPS4ComResource *wrapped;

    (void)riid;

    pcps4_fault_stage("com: IDXGISwapChain::GetBuffer");

    if (self == NULL || self->impl == NULL || surface == NULL)
        return E_INVALIDARG;

    *surface = NULL;

    if (self->impl->GetBuffer(buffer, &texture) != S_OK || texture == NULL)
        return E_FAIL;

    wrapped = wrap_resource(
        self->device,
        PCPS4_COM_RES_TEXTURE2D,
        texture,
        NULL);

    if (wrapped == NULL) {
        texture->Release();
        return E_OUTOFMEMORY;
    }

    wrapped->lpVtbl = g_resource_full_vtbl;
    *surface = wrapped;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT swap_set_fullscreen(
    PCPS4ComSwapChain *self, int fullscreen, void *target)
{
    (void)self; (void)fullscreen; (void)target; return S_OK;
}
static PCPS4_MS_ABI HRESULT swap_get_fullscreen(
    PCPS4ComSwapChain *self, int *fullscreen, void **target)
{
    if (fullscreen) *fullscreen = 0;
    if (target) *target = NULL;
    (void)self;
    return S_OK;
}
static PCPS4_MS_ABI HRESULT swap_get_desc(
    PCPS4ComSwapChain *self, DXGI_SWAP_CHAIN_DESC *desc)
{
    if (self == NULL || desc == NULL)
        return E_INVALIDARG;
    memset(desc, 0, sizeof(*desc));
    desc->BufferDesc.Width = self->impl->width;
    desc->BufferDesc.Height = self->impl->height;
    desc->BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc->SampleDesc.Count = 1;
    desc->BufferCount = 2;
    desc->Windowed = 1;
    desc->SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
    return S_OK;
}
static PCPS4_MS_ABI HRESULT swap_resize_buffers(
    PCPS4ComSwapChain *self, UINT count, UINT width, UINT height,
    DXGI_FORMAT format, UINT flags)
{
    (void)self; (void)count; (void)width; (void)height; (void)format; (void)flags;
    return E_NOTIMPL;
}
static PCPS4_MS_ABI HRESULT swap_resize_target(PCPS4ComSwapChain *self, const DXGI_MODE_DESC *desc)
{ (void)self; (void)desc; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_output(PCPS4ComSwapChain *self, void **output)
{ if (output) *output=NULL; (void)self; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_stats(PCPS4ComSwapChain *self, void *stats)
{ (void)self; (void)stats; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT swap_get_last_present(PCPS4ComSwapChain *self, UINT *count)
{ if (count) *count=0; (void)self; return S_OK; }

/* Resource vtable entries. */
static PCPS4_MS_ABI HRESULT res_get_private(PCPS4ComResource *s, void *g, UINT *n, void *p)
{ (void)s; (void)g; (void)n; (void)p; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT res_set_private(PCPS4ComResource *s, void *g, UINT n, const void *p)
{ (void)s; (void)g; (void)n; (void)p; return E_NOTIMPL; }
static PCPS4_MS_ABI HRESULT res_set_private_i(PCPS4ComResource *s, void *g, const void *p)
{ (void)s; (void)g; (void)p; return E_NOTIMPL; }
static PCPS4_MS_ABI void res_set_eviction(PCPS4ComResource *s, UINT p)
{ (void)s; (void)p; }
static PCPS4_MS_ABI UINT res_get_eviction(PCPS4ComResource *s)
{ (void)s; return 0; }
static PCPS4_MS_ABI void res_get_desc(PCPS4ComResource *s, void *desc)
{
    if (s->kind == PCPS4_COM_RES_BUFFER)
        com_buffer_get_desc(s, (D3D11_BUFFER_DESC *)desc);
    else if (s->kind == PCPS4_COM_RES_TEXTURE2D)
        com_texture_get_desc(s, (D3D11_TEXTURE2D_DESC *)desc);
}

static PCPS4_MS_ABI HRESULT res_view_get_resource(PCPS4ComResource *s, void **out)
{ return com_view_get_resource(s, out); }

static PCPS4_MS_ABI HRESULT device_create_buffer(
    PCPS4ComDevice *self,
    const D3D11_BUFFER_DESC *desc,
    const D3D11_SUBRESOURCE_DATA *data,
    void **out)
{
    ID3D11Buffer *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL)
        return E_INVALIDARG;
    *out = NULL;

    if (self == NULL || self->impl == NULL)
        return E_FAIL;

    if (self->impl->CreateBuffer(desc, data, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_BUFFER, impl, NULL);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_resource_full_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_texture2d(
    PCPS4ComDevice *self,
    const D3D11_TEXTURE2D_DESC *desc,
    const D3D11_SUBRESOURCE_DATA *data,
    void **out)
{
    ID3D11Texture2D *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL)
        return E_INVALIDARG;
    *out = NULL;

    if (self == NULL || self->impl == NULL)
        return E_FAIL;

    if (self->impl->CreateTexture2D(desc, data, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_TEXTURE2D, impl, NULL);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_resource_full_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_rtv(
    PCPS4ComDevice *self,
    void *resource,
    const D3D11_RENDER_TARGET_VIEW_DESC *desc,
    void **out)
{
    PCPS4ComResource *src = (PCPS4ComResource *)resource;
    ID3D11RenderTargetView *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL || src == NULL || src->kind != PCPS4_COM_RES_TEXTURE2D)
        return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateRenderTargetView(
            (ID3D11Texture2D *)src->impl, desc, &impl) != S_OK ||
        impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_RTV, impl, src);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_view_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_dsv(
    PCPS4ComDevice *self,
    void *resource,
    const D3D11_DEPTH_STENCIL_VIEW_DESC *desc,
    void **out)
{
    PCPS4ComResource *src = (PCPS4ComResource *)resource;
    ID3D11DepthStencilView *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL || src == NULL || src->kind != PCPS4_COM_RES_TEXTURE2D)
        return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateDepthStencilView(
            (ID3D11Texture2D *)src->impl, desc, &impl) != S_OK ||
        impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_DSV, impl, src);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_view_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_srv(
    PCPS4ComDevice *self,
    void *resource,
    const D3D11_SHADER_RESOURCE_VIEW_DESC *desc,
    void **out)
{
    PCPS4ComResource *src = (PCPS4ComResource *)resource;
    ID3D11ShaderResourceView *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL || src == NULL || src->kind != PCPS4_COM_RES_TEXTURE2D)
        return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateShaderResourceView(
            (ID3D11Texture2D *)src->impl, desc, &impl) != S_OK ||
        impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_SRV, impl, src);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_view_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_sampler(
    PCPS4ComDevice *self,
    const D3D11_SAMPLER_DESC *desc,
    void **out)
{
    ID3D11SamplerState *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL)
        return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateSamplerState(desc, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_SAMPLER, impl, NULL);
    if (r == NULL) {
        impl->Release();
        return E_OUTOFMEMORY;
    }

    r->lpVtbl = g_desc_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_rasterizer(
    PCPS4ComDevice *self,
    const D3D11_RASTERIZER_DESC *desc,
    void **out)
{
    ID3D11RasterizerState *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateRasterizerState(desc, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_RASTERIZER, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_desc_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_blend(
    PCPS4ComDevice *self,
    const D3D11_BLEND_DESC *desc,
    void **out)
{
    ID3D11BlendState *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateBlendState(desc, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_BLEND, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_desc_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_depth_state(
    PCPS4ComDevice *self,
    const D3D11_DEPTH_STENCIL_DESC *desc,
    void **out)
{
    ID3D11DepthStencilState *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateDepthStencilState(desc, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_DEPTH_STATE, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_desc_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_vs(
    PCPS4ComDevice *self,
    const void *bytecode,
    size_t size,
    void *linkage,
    void **out)
{
    ID3D11VertexShader *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateVertexShader(
            bytecode, size, linkage, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_VERTEX_SHADER, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_devicechild_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_ps(
    PCPS4ComDevice *self,
    const void *bytecode,
    size_t size,
    void *linkage,
    void **out)
{
    ID3D11PixelShader *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreatePixelShader(
            bytecode, size, linkage, &impl) != S_OK || impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_PIXEL_SHADER, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_devicechild_vtbl;
    *out = r;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_create_input_layout(
    PCPS4ComDevice *self,
    const D3D11_INPUT_ELEMENT_DESC *descs,
    UINT count,
    const void *signature,
    size_t signature_size,
    void **out)
{
    ID3D11InputLayout *impl = NULL;
    PCPS4ComResource *r;

    if (out == NULL) return E_INVALIDARG;
    *out = NULL;

    if (self->impl->CreateInputLayout(
            descs, count, signature, signature_size, &impl) != S_OK ||
        impl == NULL)
        return E_FAIL;

    r = wrap_resource(self, PCPS4_COM_RES_INPUT_LAYOUT, impl, NULL);
    if (r == NULL) { impl->Release(); return E_OUTOFMEMORY; }

    r->lpVtbl = g_desc_vtbl;
    *out = r;
    return S_OK;
}

/* Context forwarding. */
static PCPS4_MS_ABI void ctx_VSSetConstantBuffers(
    PCPS4ComContext *self, UINT start, UINT count, void *const *buffers)
{
    ID3D11Buffer *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0;i<n;++i) tmp[i] = buffers && buffers[i] ? (ID3D11Buffer *)((PCPS4ComResource *)buffers[i])->impl : NULL;
    self->impl->VSSetConstantBuffers(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_PSSetShaderResources(
    PCPS4ComContext *self, UINT start, UINT count, void *const *views)
{
    ID3D11ShaderResourceView *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0;i<n;++i) tmp[i] = views && views[i] ? (ID3D11ShaderResourceView *)((PCPS4ComResource *)views[i])->impl : NULL;
    self->impl->PSSetShaderResources(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_PSSetShader(
    PCPS4ComContext *self, void *shader, void *const *classes, UINT count)
{
    (void)classes; (void)count;
    self->impl->PSSetShader(
        shader ? (ID3D11PixelShader *)((PCPS4ComResource *)shader)->impl : NULL,
        NULL, 0);
}
static PCPS4_MS_ABI void ctx_PSSetSamplers(
    PCPS4ComContext *self, UINT start, UINT count, void *const *samplers)
{
    ID3D11SamplerState *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0;i<n;++i) tmp[i] = samplers && samplers[i] ? (ID3D11SamplerState *)((PCPS4ComResource *)samplers[i])->impl : NULL;
    self->impl->PSSetSamplers(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_VSSetShader(
    PCPS4ComContext *self, void *shader, void *const *classes, UINT count)
{
    (void)classes; (void)count;
    self->impl->VSSetShader(
        shader ? (ID3D11VertexShader *)((PCPS4ComResource *)shader)->impl : NULL,
        NULL, 0);
}
static PCPS4_MS_ABI void ctx_DrawIndexed(PCPS4ComContext *self, UINT a, UINT b, int c)
{ self->impl->DrawIndexed(a,b,c); }
static PCPS4_MS_ABI void ctx_Draw(PCPS4ComContext *self, UINT a, UINT b)
{ self->impl->Draw(a,b); }
static PCPS4_MS_ABI void ctx_DrawIndexedInstanced(
    PCPS4ComContext *self, UINT index_count_per_instance, UINT instance_count,
    UINT start_index_location, int base_vertex_location, UINT start_instance_location)
{
    (void)start_instance_location;
    if (self == NULL || self->impl == NULL || instance_count == 0)
        return;
    /* Preserve draws for shaders that do not consume per-instance inputs. */
    for (UINT i = 0; i < instance_count; ++i)
        self->impl->DrawIndexed(index_count_per_instance,
                                start_index_location,
                                base_vertex_location);
}

static PCPS4_MS_ABI void ctx_DrawInstanced(
    PCPS4ComContext *self, UINT vertex_count_per_instance, UINT instance_count,
    UINT start_vertex_location, UINT start_instance_location)
{
    (void)start_instance_location;
    if (self == NULL || self->impl == NULL || instance_count == 0)
        return;
    for (UINT i = 0; i < instance_count; ++i)
        self->impl->Draw(vertex_count_per_instance, start_vertex_location);
}

static PCPS4_MS_ABI HRESULT ctx_Map(
    PCPS4ComContext *self, void *resource, UINT sub, D3D11_MAP map, UINT flags,
    D3D11_MAPPED_SUBRESOURCE *mapped)
{
    PCPS4ComResource *r=(PCPS4ComResource *)resource;
    return r && r->kind==PCPS4_COM_RES_BUFFER
        ? self->impl->Map((ID3D11Buffer *)r->impl,sub,map,flags,mapped)
        : E_NOTIMPL;
}
static PCPS4_MS_ABI void ctx_Unmap(PCPS4ComContext *self, void *resource, UINT sub)
{
    PCPS4ComResource *r=(PCPS4ComResource *)resource;
    if (r && r->kind==PCPS4_COM_RES_BUFFER) self->impl->Unmap((ID3D11Buffer *)r->impl,sub);
}
static PCPS4_MS_ABI void ctx_PSSetConstantBuffers(
    PCPS4ComContext *self, UINT start, UINT count, void *const *buffers)
{
    ID3D11Buffer *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0; i<n; ++i)
        tmp[i] = buffers && buffers[i]
            ? (ID3D11Buffer *)((PCPS4ComResource *)buffers[i])->impl
            : NULL;
    self->impl->PSSetConstantBuffers(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_IASetInputLayout(PCPS4ComContext *self, void *layout)
{ self->impl->IASetInputLayout(layout ? (ID3D11InputLayout *)((PCPS4ComResource *)layout)->impl : NULL); }
static PCPS4_MS_ABI void ctx_IASetVertexBuffers(
    PCPS4ComContext *self, UINT start, UINT count, void *const *buffers,
    const UINT *strides, const UINT *offsets)
{
    ID3D11Buffer *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0; i<n; ++i)
        tmp[i] = buffers && buffers[i]
            ? (ID3D11Buffer *)((PCPS4ComResource *)buffers[i])->impl
            : NULL;
    self->impl->IASetVertexBuffers(start, n, tmp, strides, offsets);
}
static PCPS4_MS_ABI void ctx_IASetIndexBuffer(PCPS4ComContext *self, void *buffer, DXGI_FORMAT fmt, UINT offset)
{ self->impl->IASetIndexBuffer(buffer ? (ID3D11Buffer *)((PCPS4ComResource *)buffer)->impl : NULL,fmt,offset); }
static PCPS4_MS_ABI void ctx_IASetPrimitiveTopology(PCPS4ComContext *self, D3D11_PRIMITIVE_TOPOLOGY topology)
{ self->impl->IASetPrimitiveTopology(topology); }
static PCPS4_MS_ABI void ctx_VSSetShaderResources(
    PCPS4ComContext *self, UINT start, UINT count, void *const *views)
{
    ID3D11ShaderResourceView *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0; i<n; ++i)
        tmp[i] = views && views[i]
            ? (ID3D11ShaderResourceView *)((PCPS4ComResource *)views[i])->impl
            : NULL;
    self->impl->VSSetShaderResources(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_VSSetSamplers(
    PCPS4ComContext *self, UINT start, UINT count, void *const *samplers)
{
    ID3D11SamplerState *tmp[16];
    UINT n = count > 16 ? 16 : count;
    for (UINT i=0; i<n; ++i)
        tmp[i] = samplers && samplers[i]
            ? (ID3D11SamplerState *)((PCPS4ComResource *)samplers[i])->impl
            : NULL;
    self->impl->VSSetSamplers(start, n, tmp);
}
static PCPS4_MS_ABI void ctx_OMSetRenderTargets(
    PCPS4ComContext *self, UINT count, void *const *rtvs, void *dsv)
{
    ID3D11RenderTargetView *tmp[8];
    UINT n = count > 8 ? 8 : count;
    for (UINT i=0;i<n;++i) tmp[i]=rtvs&&rtvs[i]?(ID3D11RenderTargetView *)((PCPS4ComResource *)rtvs[i])->impl:NULL;
    self->impl->OMSetRenderTargets(
        n, tmp,
        dsv ? ((ID3D11DepthStencilView *)((PCPS4ComResource *)dsv)->impl) : NULL);
}
static PCPS4_MS_ABI void ctx_OMSetBlendState(PCPS4ComContext *self, void *state, const float *factor, UINT mask)
{ self->impl->OMSetBlendState(state?(ID3D11BlendState *)((PCPS4ComResource *)state)->impl:NULL,factor,mask); }
static PCPS4_MS_ABI void ctx_OMSetDepthStencilState(PCPS4ComContext *self, void *state, UINT ref)
{ self->impl->OMSetDepthStencilState(state?(ID3D11DepthStencilState *)((PCPS4ComResource *)state)->impl:NULL,(uint8_t)ref); }
static PCPS4_MS_ABI void ctx_RSSetState(PCPS4ComContext *self, void *state)
{ self->impl->RSSetState(state?(ID3D11RasterizerState *)((PCPS4ComResource *)state)->impl:NULL); }
static PCPS4_MS_ABI void ctx_RSSetViewports(
    PCPS4ComContext *self,
    UINT count,
    const D3D11_VIEWPORT *viewports)
{
    if (self == NULL || self->impl == NULL ||
        viewports == NULL || count == 0)
        return;

    self->impl->RSSetViewports(count, viewports);
}

static PCPS4_MS_ABI void ctx_UpdateSubresource(
    PCPS4ComContext *self, void *dst, UINT sub, const void *box, const void *src, UINT row, UINT depth)
{
    PCPS4ComResource *r=(PCPS4ComResource *)dst;
    if (!r) return;
    if (r->kind==PCPS4_COM_RES_BUFFER)
        self->impl->UpdateSubresource((ID3D11Buffer *)r->impl,sub,box,src,row,depth);
    else if (r->kind==PCPS4_COM_RES_TEXTURE2D)
        self->impl->UpdateSubresource((ID3D11Texture2D *)r->impl,sub,box,src,row,depth);
}
static PCPS4_MS_ABI void ctx_ClearRenderTargetView(PCPS4ComContext *self, void *rtv, const float c[4])
{ if(rtv) self->impl->ClearRenderTargetView((ID3D11RenderTargetView *)((PCPS4ComResource *)rtv)->impl,c); }
static PCPS4_MS_ABI void ctx_ClearDepthStencilView(PCPS4ComContext *self, void *dsv, UINT flags, float depth, uint8_t stencil)
{ if(dsv) self->impl->ClearDepthStencilView((ID3D11DepthStencilView *)((PCPS4ComResource *)dsv)->impl,flags,depth,stencil); }
static PCPS4_MS_ABI void ctx_Flush(PCPS4ComContext *self)
{ self->impl->Flush(); }

static PCPS4_MS_ABI HRESULT device_check_format_support(
    PCPS4ComDevice *self, DXGI_FORMAT format, UINT *support)
{
    (void)self;
    if (support == NULL)
        return E_INVALIDARG;
    *support = 0;
    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
        case DXGI_FORMAT_B8G8R8A8_UNORM:
            *support = 0x20u | 0x100u | 0x200u | 0x4000u | 0x8000u;
            return S_OK;
        case DXGI_FORMAT_R32_FLOAT:
            *support = 0x20u | 0x100u | 0x200u | 0x4000u;
            return S_OK;
        case DXGI_FORMAT_R32_UINT:
        case DXGI_FORMAT_R16_UINT:
            *support = 0x4u;
            return S_OK;
        default:
            return E_NOTIMPL;
    }
}

static PCPS4_MS_ABI HRESULT device_check_multisample_quality_levels(
    PCPS4ComDevice *self, DXGI_FORMAT format, UINT sample_count, UINT *quality_levels)
{
    (void)self;
    (void)format;
    if (quality_levels == NULL || sample_count == 0)
        return E_INVALIDARG;
    *quality_levels = sample_count == 1 ? 1u : 0u;
    return S_OK;
}

static PCPS4_MS_ABI HRESULT device_check_feature_support(
    PCPS4ComDevice *self, UINT feature, void *data, UINT data_size)
{
    (void)self;
    if (feature == 0u && data != NULL && data_size == 8u) {
        /* D3D11_FEATURE_THREADING: conservative capability report. */
        memset(data, 0, 8);
        return S_OK;
    }
    return E_NOTIMPL;
}

static PCPS4_MS_ABI D3D_FEATURE_LEVEL device_get_feature_level(PCPS4ComDevice *self)
{
    (void)self;
    return D3D_FEATURE_LEVEL_11_0;
}

static PCPS4_MS_ABI UINT device_get_creation_flags(PCPS4ComDevice *self)
{
    (void)self;
    return 0;
}

static PCPS4_MS_ABI HRESULT device_get_removed_reason(PCPS4ComDevice *self)
{
    (void)self;
    return S_OK;
}

static PCPS4_MS_ABI void device_get_immediate_context(
    PCPS4ComDevice *self,
    void **out)
{
    if (out == NULL)
        return;
    *out = NULL;

    if (self != NULL && self->context != NULL) {
        ++self->context->refs;
        *out = self->context;
    }
}

static PCPS4_MS_ABI HRESULT device_set_exception_mode(
    PCPS4ComDevice *self, UINT flags)
{
    (void)self;
    (void)flags;
    return S_OK;
}

static PCPS4_MS_ABI UINT device_get_exception_mode(PCPS4ComDevice *self)
{
    (void)self;
    return 0;
}

static PCPS4_MS_ABI void ctx_clear_state(PCPS4ComContext *self)
{
    if (self != NULL && self->impl != NULL)
        self->impl->Flush();
}

static PCPS4_MS_ABI UINT ctx_get_type(PCPS4ComContext *self)
{
    (void)self;
    return 0;
}

static PCPS4_MS_ABI UINT ctx_get_flags(PCPS4ComContext *self)
{
    (void)self;
    return 0;
}

/* vtable population helpers. */
static PCPS4_MS_ABI void vtbl_clear(const void **v, size_t n)
{
    for (size_t i=0;i<n;++i) v[i]=(const void *)com_noop;
}
static PCPS4_MS_ABI void vtbl_fill_hresult(const void **v, size_t n)
{
    for (size_t i=0;i<n;++i) v[i]=(const void *)com_nyi;
}

static PCPS4_MS_ABI void init_vtables(void)
{
    static int initialized = 0;
    if (initialized) return;
    initialized = 1;

    vtbl_fill_hresult(g_device_vtbl, PCPS4_COM_DEVICE_SLOTS);
    vtbl_fill_hresult(g_context_vtbl, PCPS4_COM_CONTEXT_SLOTS);
    vtbl_fill_hresult(g_swap_vtbl, PCPS4_COM_SWAP_SLOTS);
    vtbl_fill_hresult(g_devicechild_vtbl, PCPS4_COM_RESOURCE_DEVICECHILD_SLOTS);
    vtbl_fill_hresult(g_view_vtbl, PCPS4_COM_RESOURCE_VIEW_SLOTS);
    vtbl_fill_hresult(g_desc_vtbl, PCPS4_COM_RESOURCE_DESC_SLOTS);
    vtbl_fill_hresult(g_resource_full_vtbl, PCPS4_COM_RESOURCE_DESC_FULL_SLOTS);

    /* IDXGISwapChain: IUnknown + IDXGIObject + GetDevice + swap methods. */
    g_swap_vtbl[0]=(const void *)com_swap_query;
    g_swap_vtbl[1]=(const void *)com_swap_addref;
    g_swap_vtbl[2]=(const void *)com_swap_release;
    g_swap_vtbl[3]=(const void *)swap_private_data;
    g_swap_vtbl[4]=(const void *)swap_private_data_i;
    g_swap_vtbl[5]=(const void *)swap_get_private_data;
    g_swap_vtbl[6]=(const void *)swap_get_parent;
    g_swap_vtbl[7]=(const void *)swap_get_device;
    g_swap_vtbl[8]=(const void *)swap_present;
    g_swap_vtbl[9]=(const void *)swap_get_buffer;
    g_swap_vtbl[10]=(const void *)swap_set_fullscreen;
    g_swap_vtbl[11]=(const void *)swap_get_fullscreen;
    g_swap_vtbl[12]=(const void *)swap_get_desc;
    g_swap_vtbl[13]=(const void *)swap_resize_buffers;
    g_swap_vtbl[14]=(const void *)swap_resize_target;
    g_swap_vtbl[15]=(const void *)swap_get_output;
    g_swap_vtbl[16]=(const void *)swap_get_stats;
    g_swap_vtbl[17]=(const void *)swap_get_last_present;

    /* ID3D11Device: QI/AddRef/Release + selected creation methods. */
    g_device_vtbl[0]=(const void *)com_device_query;
    g_device_vtbl[1]=(const void *)com_device_addref;
    g_device_vtbl[2]=(const void *)com_device_release;
    g_device_vtbl[3]=(const void *)device_create_buffer;
    g_device_vtbl[5]=(const void *)device_create_texture2d;
    g_device_vtbl[7]=(const void *)device_create_srv;
    g_device_vtbl[9]=(const void *)device_create_rtv;
    g_device_vtbl[10]=(const void *)device_create_dsv;
    g_device_vtbl[11]=(const void *)device_create_input_layout;
    g_device_vtbl[12]=(const void *)device_create_vs;
    g_device_vtbl[15]=(const void *)device_create_ps;
    g_device_vtbl[20]=(const void *)device_create_blend;
    g_device_vtbl[21]=(const void *)device_create_depth_state;
    g_device_vtbl[22]=(const void *)device_create_rasterizer;
    g_device_vtbl[23]=(const void *)device_create_sampler;
    g_device_vtbl[29]=(const void *)device_check_format_support;
    g_device_vtbl[30]=(const void *)device_check_multisample_quality_levels;
    g_device_vtbl[33]=(const void *)device_check_feature_support;
    g_device_vtbl[37]=(const void *)device_get_feature_level;
    g_device_vtbl[38]=(const void *)device_get_creation_flags;
    g_device_vtbl[39]=(const void *)device_get_removed_reason;
    g_device_vtbl[40]=(const void *)device_get_immediate_context;
    g_device_vtbl[41]=(const void *)device_set_exception_mode;
    g_device_vtbl[42]=(const void *)device_get_exception_mode;

    /* Context methods: indices match the public D3D11 base vtable. */
    g_context_vtbl[0]=(const void *)com_context_query;
    g_context_vtbl[1]=(const void *)com_context_addref;
    g_context_vtbl[2]=(const void *)com_context_release;
    g_context_vtbl[3]=(const void *)com_context_get_device;
    g_context_vtbl[7]=(const void *)ctx_VSSetConstantBuffers;
    g_context_vtbl[8]=(const void *)ctx_PSSetShaderResources;
    g_context_vtbl[9]=(const void *)ctx_PSSetShader;
    g_context_vtbl[10]=(const void *)ctx_PSSetSamplers;
    g_context_vtbl[11]=(const void *)ctx_VSSetShader;
    g_context_vtbl[12]=(const void *)ctx_DrawIndexed;
    g_context_vtbl[13]=(const void *)ctx_Draw;
    g_context_vtbl[14]=(const void *)ctx_Map;
    g_context_vtbl[15]=(const void *)ctx_Unmap;
    g_context_vtbl[16]=(const void *)ctx_PSSetConstantBuffers;
    g_context_vtbl[17]=(const void *)ctx_IASetInputLayout;
    g_context_vtbl[18]=(const void *)ctx_IASetVertexBuffers;
    g_context_vtbl[19]=(const void *)ctx_IASetIndexBuffer;
    g_context_vtbl[20]=(const void *)ctx_DrawIndexedInstanced;
    g_context_vtbl[21]=(const void *)ctx_DrawInstanced;
    g_context_vtbl[24]=(const void *)ctx_IASetPrimitiveTopology;
    g_context_vtbl[25]=(const void *)ctx_VSSetShaderResources;
    g_context_vtbl[26]=(const void *)ctx_VSSetSamplers;
    g_context_vtbl[33]=(const void *)ctx_OMSetRenderTargets;
    g_context_vtbl[35]=(const void *)ctx_OMSetBlendState;
    g_context_vtbl[36]=(const void *)ctx_OMSetDepthStencilState;
    g_context_vtbl[43]=(const void *)ctx_RSSetState;
    g_context_vtbl[44]=(const void *)ctx_RSSetViewports;
    g_context_vtbl[48]=(const void *)ctx_UpdateSubresource;
    g_context_vtbl[50]=(const void *)ctx_ClearRenderTargetView;
    g_context_vtbl[53]=(const void *)ctx_ClearDepthStencilView;
    g_context_vtbl[110]=(const void *)ctx_clear_state;
    g_context_vtbl[111]=(const void *)ctx_Flush;

    /* Resource vtables. */
    for (size_t i=0;i<PCPS4_COM_RESOURCE_DEVICECHILD_SLOTS;++i)
        g_devicechild_vtbl[i]=(const void *)com_nyi;
    g_devicechild_vtbl[0]=(const void *)com_resource_query;
    g_devicechild_vtbl[1]=(const void *)com_resource_addref;
    g_devicechild_vtbl[2]=(const void *)com_resource_release;
    g_devicechild_vtbl[3]=(const void *)com_resource_get_device;
    g_devicechild_vtbl[4]=(const void *)res_get_private;
    g_devicechild_vtbl[5]=(const void *)res_set_private;
    g_devicechild_vtbl[6]=(const void *)res_set_private_i;

    for (size_t i=0;i<PCPS4_COM_RESOURCE_DESC_SLOTS;++i)
        g_desc_vtbl[i]=g_devicechild_vtbl[i < 7 ? i : 6];
    g_desc_vtbl[7]=(const void *)res_get_desc;

    for (size_t i=0;i<PCPS4_COM_RESOURCE_VIEW_SLOTS;++i)
        g_view_vtbl[i]=g_devicechild_vtbl[i < 7 ? i : 6];
    g_view_vtbl[7]=(const void *)res_view_get_resource;
    g_view_vtbl[8]=(const void *)res_get_desc;

    for (size_t i=0;i<PCPS4_COM_RESOURCE_DESC_FULL_SLOTS;++i)
        g_resource_full_vtbl[i]=g_desc_vtbl[i < 8 ? i : 7];
    g_resource_full_vtbl[7]=(const void *)res_get_desc;
    g_resource_full_vtbl[8]=(const void *)res_set_eviction;
    g_resource_full_vtbl[9]=(const void *)res_get_eviction;
    g_resource_full_vtbl[10]=(const void *)res_get_desc;
}

static PCPS4_MS_ABI HRESULT com_create_wrappers(
    ID3D11Device *impl_device,
    ID3D11DeviceContext *impl_context,
    IDXGISwapChain *impl_swap,
    void **ppSwapChain,
    void **ppDevice,
    void **ppImmediateContext)
{
    PCPS4ComDevice *device;
    PCPS4ComContext *context;
    PCPS4ComSwapChain *swap;

    if (impl_device == NULL || impl_context == NULL)
        return E_FAIL;

    pcps4_fault_stage("com: create wrappers");
    init_vtables();

    device = (PCPS4ComDevice *)calloc(1, sizeof(*device));
    context = (PCPS4ComContext *)calloc(1, sizeof(*context));
    swap = impl_swap != NULL
        ? (PCPS4ComSwapChain *)calloc(1, sizeof(*swap))
        : NULL;

    if (device == NULL || context == NULL ||
        (impl_swap != NULL && swap == NULL)) {
        free(device);
        free(context);
        free(swap);
        return E_OUTOFMEMORY;
    }

    device->lpVtbl = g_device_vtbl;
    device->refs = 1;
    device->impl = impl_device;
    device->context = context;

    context->lpVtbl = g_context_vtbl;
    context->refs = 1;
    context->impl = impl_context;
    context->device = device;
    ++device->refs;

    if (swap != NULL) {
        swap->lpVtbl = g_swap_vtbl;
        swap->refs = 1;
        swap->impl = impl_swap;
        swap->device = device;
        swap->context = context;
        ++device->refs;
        ++context->refs;
    }

    if (ppDevice) *ppDevice = device;
    if (ppImmediateContext) *ppImmediateContext = context;
    if (ppSwapChain) *ppSwapChain = swap;

    pcps4_fault_stage("com: wrappers ready");
    return S_OK;
}

HRESULT pcps4_d3d11_com_create_device_and_swap_chain(
    void *pAdapter,
    D3D_DRIVER_TYPE DriverType,
    void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    void **ppSwapChain,
    void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    void **ppImmediateContext)
{
    ID3D11Device *device = NULL;
    ID3D11DeviceContext *context = NULL;
    IDXGISwapChain *swap = NULL;
    HRESULT hr;

    pcps4_fault_stage("com: create_device_and_swap_chain (ABI crossing ok)");

    (void)pAdapter;
    (void)DriverType;
    (void)Software;
    (void)Flags;
    (void)pFeatureLevels;
    (void)FeatureLevels;
    (void)SDKVersion;

    if (ppSwapChain) *ppSwapChain = NULL;
    if (ppDevice) *ppDevice = NULL;
    if (ppImmediateContext) *ppImmediateContext = NULL;

    hr = pcps4_d3d11_create_device_and_swap_chain(
        pSwapChainDesc,
        &device,
        pFeatureLevel,
        &context,
        &swap);
    if (FAILED(hr)) {
        return hr;
    }

    hr = com_create_wrappers(
        device, context, swap,
        ppSwapChain, ppDevice, ppImmediateContext);
    if (FAILED(hr)) {
        if (swap) swap->Release();
        if (context) context->Release();
        if (device) device->Release();
        return hr;
    }

    return S_OK;
}

HRESULT pcps4_d3d11_com_create_device(
    void *pAdapter,
    D3D_DRIVER_TYPE DriverType,
    void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels,
    UINT FeatureLevels,
    UINT SDKVersion,
    void **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    void **ppImmediateContext)
{
    DXGI_SWAP_CHAIN_DESC desc = {};
    IDXGISwapChain *unused_swap = NULL;

    if (ppDevice) *ppDevice = NULL;
    if (ppImmediateContext) *ppImmediateContext = NULL;
    desc.BufferDesc.Width = 1280;
    desc.BufferDesc.Height = 720;
    desc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.BufferCount = 2;
    desc.Windowed = 1;
    desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

    HRESULT hr = pcps4_d3d11_com_create_device_and_swap_chain(
        pAdapter, DriverType, Software, Flags,
        pFeatureLevels, FeatureLevels, SDKVersion, &desc,
        (void **)&unused_swap, ppDevice, pFeatureLevel, ppImmediateContext);

    if (unused_swap)
        com_swap_release((PCPS4ComSwapChain *)unused_swap);

    return hr;
}
