#include "pcps4_d3d11.h"
#include "pcps4_shader.h"

#include <new>
#include <string.h>

static uint8_t component(float value)
{
    if (value <= 0.0f) return 0;
    if (value >= 1.0f) return 255;
    return (uint8_t)(value * 255.0f + 0.5f);
}

ID3D11Buffer::~ID3D11Buffer()
{
    pcps4_gpu_buffer_destroy(native);
    native = NULL;
}

ID3D11Texture2D::~ID3D11Texture2D()
{
    pcps4_gpu_texture_destroy(native);
    native = NULL;
}

void ID3D11Texture2D::GetDesc(
    D3D11_TEXTURE2D_DESC *desc) const
{
    if (desc == NULL || native == NULL)
        return;

    pcps4_gpu_texture_desc_t native_desc = {};
    if (pcps4_gpu_texture_get_desc(native, &native_desc) != PCPS4_GPU_OK)
        return;

    memset(desc, 0, sizeof(*desc));
    desc->Width = native_desc.width;
    desc->Height = native_desc.height;
    desc->MipLevels = native_desc.mip_levels;
    desc->ArraySize = 1;
    desc->Format = (DXGI_FORMAT)native_desc.format;
    desc->SampleDesc.Count = 1;
    desc->Usage = D3D11_USAGE_DEFAULT;
    desc->BindFlags = native_desc.bind_flags;
}

ID3D11VertexShader::~ID3D11VertexShader()
{
    pcps4_gpu_shader_destroy(native);
    native = NULL;
}

ID3D11PixelShader::~ID3D11PixelShader()
{
    pcps4_gpu_shader_destroy(native);
    native = NULL;
}

ID3D11RenderTargetView::~ID3D11RenderTargetView()
{
    pcps4_gpu_render_target_destroy(native);
    native = NULL;
}

IDXGISwapChain::~IDXGISwapChain()
{
    device = NULL;
    graphics = NULL;
}

HRESULT IDXGISwapChain::Present(UINT SyncInterval, UINT Flags)
{
    (void)SyncInterval;
    (void)Flags;

    if (graphics == NULL)
        return E_FAIL;

    int rc = pcps4_gfx_present(
        graphics,
        frame_id++);

    return rc == PCPS4_GFX_OK ? S_OK : E_FAIL;
}

HRESULT IDXGISwapChain::GetBuffer(
    UINT Buffer,
    ID3D11Texture2D **Resource)
{
    if (Buffer != 0 ||
        Resource == NULL ||
        graphics == NULL ||
        device == NULL) {
        return E_INVALIDARG;
    }

    *Resource = NULL;

    pcps4_gpu_texture_t *texture =
        pcps4_gpu_texture_create_backbuffer(
            device,
            graphics);

    if (texture == NULL)
        return E_OUTOFMEMORY;

    *Resource =
        new (std::nothrow) ID3D11Texture2D(texture);

    if (*Resource == NULL) {
        pcps4_gpu_texture_destroy(texture);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

void ID3D11DeviceContext::OMSetRenderTargets(
    UINT NumViews,
    ID3D11RenderTargetView *const *ppRenderTargetViews,
    void *pDepthStencilView)
{
    (void)pDepthStencilView;

    if (native == NULL)
        return;

    if (NumViews == 0 ||
        ppRenderTargetViews == NULL ||
        ppRenderTargetViews[0] == NULL) {
        (void)pcps4_gpu_context_set_render_target(
            native,
            NULL);
        return;
    }

    (void)pcps4_gpu_context_set_render_target(
        native,
        ppRenderTargetViews[0]->native);
}

void ID3D11DeviceContext::ClearRenderTargetView(
    ID3D11RenderTargetView *pRenderTargetView,
    const float ColorRGBA[4])
{
    if (native == NULL || ColorRGBA == NULL)
        return;

    if (pRenderTargetView != NULL) {
        if (pcps4_gpu_context_set_render_target(
                native,
                pRenderTargetView->native) != PCPS4_GPU_OK)
            return;
    }

    pcps4_gfx_color_t color = {
        component(ColorRGBA[0]),
        component(ColorRGBA[1]),
        component(ColorRGBA[2]),
        component(ColorRGBA[3])
    };

    (void)pcps4_gpu_context_clear(native, color);
}

void ID3D11DeviceContext::IASetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer *const *ppVertexBuffers,
    const UINT *pStrides,
    const UINT *pOffsets)
{
    if (native == NULL ||
        StartSlot != 0 ||
        NumBuffers == 0 ||
        ppVertexBuffers == NULL ||
        ppVertexBuffers[0] == NULL ||
        pStrides == NULL ||
        pOffsets == NULL)
        return;

    (void)pcps4_gpu_set_vertex_buffer(
        native,
        ppVertexBuffers[0]->native,
        pStrides[0],
        pOffsets[0]);
}


void ID3D11DeviceContext::IASetIndexBuffer(
    ID3D11Buffer *pIndexBuffer,
    DXGI_FORMAT Format,
    UINT Offset)
{
    if (native == NULL || pIndexBuffer == NULL)
        return;

    (void)pcps4_gpu_set_index_buffer(
        native,
        pIndexBuffer->native,
        (uint32_t)Format,
        Offset);
}

void ID3D11DeviceContext::IASetPrimitiveTopology(
    D3D11_PRIMITIVE_TOPOLOGY Topology)
{
    /*
     * Topology is retained by the command backend in the next GPU stage.
     * Rejecting an unknown value is preferable to fabricating commands.
     */
    if (native == NULL)
        return;

    if (Topology < D3D11_PRIMITIVE_TOPOLOGY_POINTLIST ||
        Topology > D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP)
        return;
}

HRESULT ID3D11DeviceContext::Map(
    ID3D11Buffer *pResource,
    UINT Subresource,
    D3D11_MAP MapType,
    UINT MapFlags,
    D3D11_MAPPED_SUBRESOURCE *pMappedResource)
{
    (void)Subresource;
    (void)MapFlags;

    if (native == NULL ||
        pResource == NULL ||
        pMappedResource == NULL)
        return E_INVALIDARG;

    *pMappedResource = {};

    void *data = NULL;
    int rc = pcps4_gpu_buffer_map(
        pResource->native,
        (uint32_t)MapType,
        &data);

    if (rc != PCPS4_GPU_OK)
        return rc == PCPS4_GPU_ERR_UNSUPPORTED
            ? E_NOTIMPL
            : E_FAIL;

    pMappedResource->pData = data;
    pMappedResource->RowPitch = 0;
    pMappedResource->DepthPitch = 0;
    return S_OK;
}

void ID3D11DeviceContext::Unmap(
    ID3D11Buffer *pResource,
    UINT Subresource)
{
    (void)Subresource;

    if (pResource != NULL)
        (void)pcps4_gpu_buffer_unmap(pResource->native);
}

void ID3D11DeviceContext::VSSetShader(
    ID3D11VertexShader *pVertexShader,
    void *const *ppClassInstances,
    UINT NumClassInstances)
{
    (void)ppClassInstances;
    (void)NumClassInstances;

    if (native != NULL && pVertexShader != NULL)
        (void)pcps4_gpu_set_vertex_shader(
            native,
            pVertexShader->native);
}

void ID3D11DeviceContext::PSSetShader(
    ID3D11PixelShader *pPixelShader,
    void *const *ppClassInstances,
    UINT NumClassInstances)
{
    (void)ppClassInstances;
    (void)NumClassInstances;

    if (native != NULL && pPixelShader != NULL)
        (void)pcps4_gpu_set_pixel_shader(
            native,
            pPixelShader->native);
}

void ID3D11DeviceContext::Draw(
    UINT VertexCount,
    UINT StartVertexLocation)
{
    if (native == NULL)
        return;

    (void)pcps4_gpu_context_draw(
        native,
        VertexCount,
        StartVertexLocation);
}

void ID3D11DeviceContext::DrawIndexed(
    UINT IndexCount,
    UINT StartIndexLocation,
    int BaseVertexLocation)
{
    if (native == NULL)
        return;

    (void)pcps4_gpu_context_draw_indexed(
        native,
        IndexCount,
        StartIndexLocation,
        BaseVertexLocation);
}

void ID3D11DeviceContext::UpdateSubresource(
    ID3D11Texture2D *pDstResource,
    UINT DstSubresource,
    const void *pDstBox,
    const void *pSrcData,
    UINT SrcRowPitch,
    UINT SrcDepthPitch)
{
    (void)DstSubresource;
    (void)pDstBox;
    (void)SrcDepthPitch;

    if (pDstResource == NULL || pSrcData == NULL)
        return;

    (void)pcps4_gpu_texture_update_rgba8(
        pDstResource->native,
        pSrcData,
        SrcRowPitch,
        ({ pcps4_gpu_texture_desc_t d = {}; pcps4_gpu_texture_get_desc(pDstResource->native, &d); d.width; }),
        ({ pcps4_gpu_texture_desc_t d = {}; pcps4_gpu_texture_get_desc(pDstResource->native, &d); d.height; }));
}

void ID3D11DeviceContext::Flush()
{
    /*
     * CPU resources are immediately visible. A future GNM path will provide
     * the actual GPU synchronization semantics.
     */
}

ID3D11DeviceContext::~ID3D11DeviceContext()
{
    pcps4_gpu_context_destroy(native);
    native = NULL;
}

ID3D11Device::~ID3D11Device()
{
    /*
     * The immediate context wrapper takes ownership of the native context
     * when GetImmediateContext() is called.
     */
    if (context != NULL) {
        pcps4_gpu_context_destroy(context);
        context = NULL;
    }

    if (graphics != NULL) {
        pcps4_gfx_shutdown(graphics);
        delete graphics;
        graphics = NULL;
    }

    if (native != NULL) {
        pcps4_gpu_device_destroy(native);
        native = NULL;
    }
}

HRESULT ID3D11Device::CreateBuffer(
    const D3D11_BUFFER_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Buffer **ppBuffer)
{
    if (pDesc == NULL ||
        ppBuffer == NULL ||
        pDesc->ByteWidth == 0)
        return E_INVALIDARG;

    *ppBuffer = NULL;

    pcps4_gpu_buffer_desc_t desc = {};
    desc.size = pDesc->ByteWidth;
    desc.bind_flags = pDesc->BindFlags;
    desc.usage = pDesc->Usage;
    desc.cpu_write = pDesc->CPUAccessFlags;

    const void *initial =
        pInitialData != NULL
            ? pInitialData->pSysMem
            : NULL;

    pcps4_gpu_buffer_t *buffer =
        pcps4_gpu_buffer_create(
            native,
            &desc,
            initial);

    if (buffer == NULL)
        return E_OUTOFMEMORY;

    *ppBuffer =
        new (std::nothrow) ID3D11Buffer(buffer);

    if (*ppBuffer == NULL) {
        pcps4_gpu_buffer_destroy(buffer);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT ID3D11Device::CreateTexture2D(
    const D3D11_TEXTURE2D_DESC *pDesc,
    const D3D11_SUBRESOURCE_DATA *pInitialData,
    ID3D11Texture2D **ppTexture2D)
{
    if (pDesc == NULL ||
        ppTexture2D == NULL ||
        pDesc->Width == 0 ||
        pDesc->Height == 0 ||
        pDesc->MipLevels != 1 ||
        pDesc->ArraySize != 1)
        return E_INVALIDARG;

    *ppTexture2D = NULL;

    pcps4_gpu_texture_desc_t desc = {};
    desc.width = pDesc->Width;
    desc.height = pDesc->Height;
    desc.mip_levels = pDesc->MipLevels;
    desc.format = (pcps4_gpu_format_t)pDesc->Format;
    desc.bind_flags = pDesc->BindFlags;

    const void *initial =
        pInitialData != NULL
            ? pInitialData->pSysMem
            : NULL;

    size_t pitch =
        pInitialData != NULL
            ? (size_t)pInitialData->SysMemPitch
            : 0;

    pcps4_gpu_texture_t *texture =
        pcps4_gpu_texture_create(
            native,
            &desc,
            initial,
            pitch);

    if (texture == NULL)
        return E_OUTOFMEMORY;

    *ppTexture2D =
        new (std::nothrow) ID3D11Texture2D(texture);

    if (*ppTexture2D == NULL) {
        pcps4_gpu_texture_destroy(texture);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT ID3D11Device::CreateRenderTargetView(
    ID3D11Texture2D *pResource,
    const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
    ID3D11RenderTargetView **ppRTView)
{
    (void)pDesc;

    if (pResource == NULL ||
        pResource->native == NULL ||
        ppRTView == NULL)
        return E_INVALIDARG;

    *ppRTView = NULL;

    pcps4_gpu_render_target_desc_t desc = {};
    desc.texture = pResource->native;

    pcps4_gpu_render_target_t *rt =
        pcps4_gpu_render_target_create(
            native,
            &desc);

    if (rt == NULL)
        return E_OUTOFMEMORY;

    *ppRTView =
        new (std::nothrow) ID3D11RenderTargetView(rt);

    if (*ppRTView == NULL) {
        pcps4_gpu_render_target_destroy(rt);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT ID3D11Device::CreateVertexShader(
    const void *pShaderBytecode,
    size_t BytecodeLength,
    void *pClassLinkage,
    ID3D11VertexShader **ppVertexShader)
{
    (void)pClassLinkage;

    if (pShaderBytecode == NULL ||
        BytecodeLength == 0 ||
        ppVertexShader == NULL)
        return E_INVALIDARG;

    *ppVertexShader = NULL;

    pcps4_dxbc_info_t dxbc = {};
    if (pcps4_dxbc_inspect(
            pShaderBytecode,
            BytecodeLength,
            &dxbc) != PCPS4_DXBC_OK)
        return E_FAIL;

    pcps4_gpu_shader_desc_t desc = {
        pShaderBytecode,
        BytecodeLength,
        PCPS4_GPU_SHADER_VERTEX
    };

    pcps4_gpu_shader_t *shader =
        pcps4_gpu_shader_create(native, &desc);

    if (shader == NULL)
        return E_OUTOFMEMORY;

    *ppVertexShader =
        new (std::nothrow) ID3D11VertexShader(shader);

    if (*ppVertexShader == NULL) {
        pcps4_gpu_shader_destroy(shader);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

HRESULT ID3D11Device::CreatePixelShader(
    const void *pShaderBytecode,
    size_t BytecodeLength,
    void *pClassLinkage,
    ID3D11PixelShader **ppPixelShader)
{
    (void)pClassLinkage;

    if (pShaderBytecode == NULL ||
        BytecodeLength == 0 ||
        ppPixelShader == NULL)
        return E_INVALIDARG;

    *ppPixelShader = NULL;

    pcps4_dxbc_info_t dxbc = {};
    if (pcps4_dxbc_inspect(
            pShaderBytecode,
            BytecodeLength,
            &dxbc) != PCPS4_DXBC_OK)
        return E_FAIL;

    pcps4_gpu_shader_desc_t desc = {
        pShaderBytecode,
        BytecodeLength,
        PCPS4_GPU_SHADER_PIXEL
    };

    pcps4_gpu_shader_t *shader =
        pcps4_gpu_shader_create(native, &desc);

    if (shader == NULL)
        return E_OUTOFMEMORY;

    *ppPixelShader =
        new (std::nothrow) ID3D11PixelShader(shader);

    if (*ppPixelShader == NULL) {
        pcps4_gpu_shader_destroy(shader);
        return E_OUTOFMEMORY;
    }

    return S_OK;
}

void ID3D11Device::GetImmediateContext(
    ID3D11DeviceContext **ppImmediateContext)
{
    if (ppImmediateContext == NULL ||
        context == NULL)
        return;

    *ppImmediateContext =
        new (std::nothrow) ID3D11DeviceContext(context);

    if (*ppImmediateContext != NULL)
        context = NULL;
}

HRESULT pcps4_d3d11_create_device_and_swap_chain(
    const DXGI_SWAP_CHAIN_DESC *swap_chain_desc,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context,
    IDXGISwapChain **swap_chain)
{
    if (swap_chain_desc == NULL ||
        device == NULL ||
        context == NULL ||
        swap_chain == NULL)
        return E_INVALIDARG;

    *device = NULL;
    *context = NULL;
    *swap_chain = NULL;

    uint32_t width = swap_chain_desc->BufferDesc.Width;
    uint32_t height = swap_chain_desc->BufferDesc.Height;
    uint32_t buffers = swap_chain_desc->BufferCount;

    if (width == 0) width = 1280;
    if (height == 0) height = 720;
    if (buffers < 2) buffers = 2;
    if (buffers > 3) buffers = 3;

    pcps4_gpu_device_t *gpu =
        pcps4_gpu_device_create();

    if (gpu == NULL)
        return E_OUTOFMEMORY;

    pcps4_gpu_context_t *gpu_context =
        pcps4_gpu_context_create(gpu);

    if (gpu_context == NULL) {
        pcps4_gpu_device_destroy(gpu);
        return E_OUTOFMEMORY;
    }

    pcps4_gfx_context_t *graphics =
        new (std::nothrow) pcps4_gfx_context_t();

    if (graphics == NULL) {
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_OUTOFMEMORY;
    }

    int gfx_rc = pcps4_gfx_init(
        graphics,
        width,
        height,
        buffers);

    if (gfx_rc != PCPS4_GFX_OK) {
        delete graphics;
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_FAIL;
    }

    if (pcps4_gpu_context_attach_graphics(
            gpu_context,
            graphics) != PCPS4_GPU_OK) {
        pcps4_gfx_shutdown(graphics);
        delete graphics;
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_FAIL;
    }

    ID3D11Device *d3d_device =
        new (std::nothrow) ID3D11Device(
            gpu,
            gpu_context,
            graphics);

    if (d3d_device == NULL) {
        pcps4_gfx_shutdown(graphics);
        delete graphics;
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_OUTOFMEMORY;
    }

    IDXGISwapChain *d3d_swap =
        new (std::nothrow) IDXGISwapChain(
            gpu,
            graphics);

    if (d3d_swap == NULL) {
        d3d_device->Release();
        return E_OUTOFMEMORY;
    }

    if (feature_level != NULL)
        *feature_level = D3D_FEATURE_LEVEL_11_0;

    *device = d3d_device;
    *swap_chain = d3d_swap;
    *context = NULL;

    return S_OK;
}
