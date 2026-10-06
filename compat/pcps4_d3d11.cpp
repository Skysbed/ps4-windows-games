#include <stdio.h>
#include "pcps4_d3d11.h"
#include "pcps4_shader.h"
#include "pcps4_gnm_renderer.h"

#include <limits.h>
#include <new>
#include <string.h>



static int map_dxgi_texture_format(
    DXGI_FORMAT format,
    pcps4_gpu_format_t *out)
{
    if (out == NULL)
        return 0;

    switch (format) {
        case DXGI_FORMAT_R8G8B8A8_UNORM:
            *out = PCPS4_GPU_FORMAT_RGBA8_UNORM;
            return 1;

        case DXGI_FORMAT_B8G8R8A8_UNORM:
            *out = PCPS4_GPU_FORMAT_BGRA8_UNORM;
            return 1;

        default:
            return 0;
    }
}

static DXGI_FORMAT map_gpu_texture_format(
    pcps4_gpu_format_t format)
{
    switch (format) {
        case PCPS4_GPU_FORMAT_RGBA8_UNORM:
            return DXGI_FORMAT_R8G8B8A8_UNORM;

        case PCPS4_GPU_FORMAT_BGRA8_UNORM:
            return DXGI_FORMAT_B8G8R8A8_UNORM;

        default:
            return DXGI_FORMAT_UNKNOWN;
    }
}

HRESULT ID3D11InputLayout::apply(
    pcps4_gpu_context_t *context)
{
    pcps4_gpu_vertex_element_t native_elements[16];

    if (context == NULL || count > 16)
        return E_INVALIDARG;

    memset(native_elements, 0, sizeof(native_elements));

    for (UINT i = 0; i < count; ++i) {
        native_elements[i].input_slot = elements[i].InputSlot;
        native_elements[i].aligned_byte_offset =
            elements[i].AlignedByteOffset;
        native_elements[i].format = elements[i].Format;
        native_elements[i].semantic_index =
            elements[i].SemanticIndex;
    }

    return pcps4_gpu_context_set_input_layout(
               context,
               native_elements,
               count) == PCPS4_GPU_OK
        ? S_OK
        : E_FAIL;
}

static int is_dxbc_blob(
    const void *data,
    size_t size)
{
    const uint8_t *bytes = (const uint8_t *)data;

    return bytes != NULL &&
           size >= 20 &&
           bytes[0] == 'D' &&
           bytes[1] == 'X' &&
           bytes[2] == 'B' &&
           bytes[3] == 'C';
}

static int try_load_shader_cache(
    const void *dxbc,
    size_t dxbc_size,
    const char *stage,
    void **out_data,
    size_t *out_size)
{
    const uint8_t *bytes;
    char path[96];
    FILE *file;
    long length;
    void *data;

    if (!is_dxbc_blob(dxbc, dxbc_size) ||
        stage == NULL ||
        out_data == NULL ||
        out_size == NULL)
        return 0;

    *out_data = NULL;
    *out_size = 0;

    bytes = (const uint8_t *)dxbc;

    /*
     * DXBC contains a 128-bit compiler-generated container key at bytes
     * 4..19. PcPs4 uses that stable key for the offline-converted .sb cache.
     */
    int written = snprintf(
        path,
        sizeof(path),
        "/app0/pcps4-shaders/");

    if (written <= 0 ||
        (size_t)written >= sizeof(path))
        return 0;

    size_t cursor = (size_t)written;

    for (size_t i = 0; i < 16; ++i) {
        if (cursor + 2 >= sizeof(path))
            return 0;

        written = snprintf(
            path + cursor,
            sizeof(path) - cursor,
            "%02x",
            (unsigned)bytes[4 + i]);

        if (written != 2)
            return 0;

        cursor += 2;
    }

    written = snprintf(
        path + cursor,
        sizeof(path) - cursor,
        "-%s.sb",
        stage);

    if (written < 0 ||
        (size_t)written >= sizeof(path))
        return 0;

    file = fopen(path, "rb");
    if (file == NULL)
        return 0;

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        return 0;
    }

    length = ftell(file);
    if (length <= 0) {
        fclose(file);
        return 0;
    }

    if (fseek(file, 0, SEEK_SET) != 0) {
        fclose(file);
        return 0;
    }

    data = malloc((size_t)length);
    if (data == NULL) {
        fclose(file);
        return 0;
    }

    if (fread(data, 1, (size_t)length, file) !=
        (size_t)length) {
        free(data);
        fclose(file);
        return 0;
    }

    fclose(file);

    *out_data = data;
    *out_size = (size_t)length;
    return 1;
}

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
    desc->Format = map_gpu_texture_format(native_desc.format);
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
    context = NULL;
    graphics = NULL;
    gnm_renderer = NULL;
}

HRESULT IDXGISwapChain::Present(UINT SyncInterval, UINT Flags)
{
    (void)SyncInterval;
    (void)Flags;

    if (context == NULL)
        return E_FAIL;

    uint64_t current_frame = frame_id++;

    return pcps4_gpu_context_present(
               context,
               current_frame) == PCPS4_GPU_OK
        ? S_OK
        : E_FAIL;
}

HRESULT IDXGISwapChain::GetBuffer(
    UINT Buffer,
    ID3D11Texture2D **Resource)
{
    if (Buffer != 0 ||
        Resource == NULL ||
        context == NULL ||
        device == NULL)
        return E_INVALIDARG;

    *Resource = NULL;

    pcps4_gpu_texture_t *texture = NULL;

#if PCPS4_ENABLE_OPENGNM
    if (gnm_renderer != NULL) {
        texture = pcps4_gpu_texture_create_gnm_backbuffer(
            device,
            gnm_renderer,
            width,
            height);
    } else
#endif
    {
        texture = pcps4_gpu_texture_create_backbuffer(
            device,
            graphics);
    }

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

void ID3D11DeviceContext::IASetInputLayout(
    ID3D11InputLayout *pInputLayout)
{
    if (native == NULL)
        return;

    if (pInputLayout == NULL) {
        (void)pcps4_gpu_context_set_input_layout(
            native,
            NULL,
            0);
        return;
    }

    (void)pInputLayout->apply(native);
}

void ID3D11DeviceContext::IASetVertexBuffers(
    UINT StartSlot,
    UINT NumBuffers,
    ID3D11Buffer *const *ppVertexBuffers,
    const UINT *pStrides,
    const UINT *pOffsets)
{
    if (native == NULL ||
        NumBuffers == 0 ||
        ppVertexBuffers == NULL ||
        pStrides == NULL ||
        pOffsets == NULL ||
        StartSlot >= 16 ||
        NumBuffers > 16 - StartSlot)
        return;

    for (UINT i = 0; i < NumBuffers; ++i) {
        ID3D11Buffer *buffer = ppVertexBuffers[i];

        if (buffer == NULL)
            continue;

        (void)pcps4_gpu_set_vertex_buffer_slot(
            native,
            StartSlot + i,
            buffer->native,
            pStrides[i],
            pOffsets[i]);
    }
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

    if (pDstResource == NULL ||
        pDstResource->native == NULL ||
        pSrcData == NULL)
        return;

    D3D11_TEXTURE2D_DESC desc = {};
    pDstResource->GetDesc(&desc);

    if (desc.Width == 0 || desc.Height == 0)
        return;

    (void)pcps4_gpu_texture_update_rgba8(
        pDstResource->native,
        pSrcData,
        SrcRowPitch,
        desc.Width,
        desc.Height);
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

#if PCPS4_ENABLE_OPENGNM
    if (gnm_renderer != NULL) {
        pcps4_gnm_renderer_destroy(gnm_renderer);
        gnm_renderer = NULL;
    }
#else
    if (graphics != NULL) {
        pcps4_gfx_shutdown(graphics);
        delete graphics;
        graphics = NULL;
    }
#endif

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
    if (!map_dxgi_texture_format(
            pDesc->Format,
            &desc.format))
        return E_INVALIDARG;
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

HRESULT ID3D11Device::CreateInputLayout(
    const D3D11_INPUT_ELEMENT_DESC *pInputElementDescs,
    UINT NumElements,
    const void *pShaderBytecodeWithInputSignature,
    size_t BytecodeLength,
    ID3D11InputLayout **ppInputLayout)
{
    (void)pShaderBytecodeWithInputSignature;

    if (pInputElementDescs == NULL ||
        ppInputLayout == NULL ||
        NumElements == 0 ||
        NumElements > 16 ||
        BytecodeLength == 0)
        return E_INVALIDARG;

    *ppInputLayout = NULL;

    ID3D11InputLayout *layout =
        new (std::nothrow) ID3D11InputLayout();

    if (layout == NULL)
        return E_OUTOFMEMORY;

    UINT slot_last_offset[16] = {};
    UINT slot_last_size[16] = {};
    uint8_t slot_seen[16] = {};

    for (UINT i = 0; i < NumElements; ++i) {
        const D3D11_INPUT_ELEMENT_DESC &src =
            pInputElementDescs[i];

        if (src.SemanticName == NULL ||
            src.InputSlot >= 16 ||
            src.InputSlotClass != D3D11_INPUT_PER_VERTEX_DATA ||
            src.InstanceDataStepRate != 0)
        {
            delete layout;
            return E_INVALIDARG;
        }

        if (src.SemanticName[0] == '\0' ||
            strlen(src.SemanticName) >= sizeof(layout->semantic_names[0]))
        {
            delete layout;
            return E_INVALIDARG;
        }

        UINT element_size = 0;
        switch (src.Format) {
            case DXGI_FORMAT_R32G32B32A32_FLOAT:
                element_size = 16; break;
            case DXGI_FORMAT_R32G32B32_FLOAT:
                element_size = 12; break;
            case DXGI_FORMAT_R32G32_FLOAT:
                element_size = 8; break;
            case DXGI_FORMAT_R32_FLOAT:
                element_size = 4; break;
            case DXGI_FORMAT_R16G16_FLOAT:
                element_size = 4; break;
            case DXGI_FORMAT_R16G16B16A16_FLOAT:
                element_size = 8; break;
            case DXGI_FORMAT_R8G8B8A8_UNORM:
            case DXGI_FORMAT_B8G8R8A8_UNORM:
                element_size = 4; break;
            default:
                delete layout;
                return E_NOTIMPL;
        }

        UINT offset = src.AlignedByteOffset;
        if (offset == D3D11_APPEND_ALIGNED_ELEMENT) {
            offset = slot_seen[src.InputSlot]
                ? slot_last_offset[src.InputSlot] +
                  slot_last_size[src.InputSlot]
                : 0;
        }

        if (offset > UINT_MAX - element_size) {
            delete layout;
            return E_INVALIDARG;
        }

        memset(
            &layout->elements[i],
            0,
            sizeof(layout->elements[i]));

        strncpy(
            layout->semantic_names[i],
            src.SemanticName,
            sizeof(layout->semantic_names[i]) - 1);

        layout->elements[i] = src;
        layout->elements[i].SemanticName =
            layout->semantic_names[i];
        layout->elements[i].AlignedByteOffset = offset;

        slot_last_offset[src.InputSlot] = offset;
        slot_last_size[src.InputSlot] = element_size;
        slot_seen[src.InputSlot] = 1;
    }

    layout->count = NumElements;
    *ppInputLayout = layout;
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

    const uint8_t *bytes = (const uint8_t *)pShaderBytecode;
    const int is_gnm =
        BytecodeLength >= 16 &&
        bytes[0] == 'S' && bytes[1] == 'h' &&
        bytes[2] == 'd' && bytes[3] == 'r' &&
        bytes[8] == 1;

    if (!is_gnm) {
        pcps4_dxbc_info_t dxbc = {};
        if (pcps4_dxbc_inspect(
                pShaderBytecode,
                BytecodeLength,
                &dxbc) != PCPS4_DXBC_OK)
            return E_FAIL;
    }

    void *cached_shader = NULL;
    size_t cached_size = 0;
    const void *shader_data = pShaderBytecode;
    size_t shader_size = BytecodeLength;

#if PCPS4_ENABLE_OPENGNM
    if (!is_gnm &&
        try_load_shader_cache(
            pShaderBytecode,
            BytecodeLength,
            "vs",
            &cached_shader,
            &cached_size)) {
        shader_data = cached_shader;
        shader_size = cached_size;
    }
#endif

    pcps4_gpu_shader_desc_t desc = {
        shader_data,
        shader_size,
        PCPS4_GPU_SHADER_VERTEX
    };

    pcps4_gpu_shader_t *shader =
        pcps4_gpu_shader_create(native, &desc);

    free(cached_shader);

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

    const uint8_t *bytes = (const uint8_t *)pShaderBytecode;
    const int is_gnm =
        BytecodeLength >= 16 &&
        bytes[0] == 'S' && bytes[1] == 'h' &&
        bytes[2] == 'd' && bytes[3] == 'r' &&
        bytes[8] == 2;

    if (!is_gnm) {
        pcps4_dxbc_info_t dxbc = {};
        if (pcps4_dxbc_inspect(
                pShaderBytecode,
                BytecodeLength,
                &dxbc) != PCPS4_DXBC_OK)
            return E_FAIL;
    }

    void *cached_shader = NULL;
    size_t cached_size = 0;
    const void *shader_data = pShaderBytecode;
    size_t shader_size = BytecodeLength;

#if PCPS4_ENABLE_OPENGNM
    if (!is_gnm &&
        try_load_shader_cache(
            pShaderBytecode,
            BytecodeLength,
            "ps",
            &cached_shader,
            &cached_size)) {
        shader_data = cached_shader;
        shader_size = cached_size;
    }
#endif

    pcps4_gpu_shader_desc_t desc = {
        shader_data,
        shader_size,
        PCPS4_GPU_SHADER_PIXEL
    };

    pcps4_gpu_shader_t *shader =
        pcps4_gpu_shader_create(native, &desc);

    free(cached_shader);

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

    if (width == 0)
        width = 1280;
    if (height == 0)
        height = 720;
    if (buffers < 2)
        buffers = 2;
    if (buffers > 3)
        buffers = 3;

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

    pcps4_gfx_context_t *graphics = NULL;
    pcps4_gnm_renderer_t *gnm_renderer = NULL;

#if PCPS4_ENABLE_OPENGNM
    gnm_renderer =
        pcps4_gnm_renderer_create(width, height);

    if (gnm_renderer == NULL) {
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_FAIL;
    }

    if (pcps4_gpu_context_attach_gnm(
            gpu_context,
            gnm_renderer) != PCPS4_GPU_OK) {
        pcps4_gnm_renderer_destroy(gnm_renderer);
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_FAIL;
    }
#else
    graphics =
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
#endif

    ID3D11Device *d3d_device =
        new (std::nothrow) ID3D11Device(
            gpu,
            gpu_context,
            graphics);

    if (d3d_device == NULL) {
#if PCPS4_ENABLE_OPENGNM
        pcps4_gnm_renderer_destroy(gnm_renderer);
#else
        pcps4_gfx_shutdown(graphics);
        delete graphics;
#endif
        pcps4_gpu_context_destroy(gpu_context);
        pcps4_gpu_device_destroy(gpu);
        return E_OUTOFMEMORY;
    }

#if PCPS4_ENABLE_OPENGNM
    d3d_device->gnm_renderer = gnm_renderer;
#endif

    IDXGISwapChain *d3d_swap =
        new (std::nothrow) IDXGISwapChain(
            gpu,
            gpu_context,
            graphics,
            gnm_renderer);

    if (d3d_swap == NULL) {
        d3d_device->Release();
        return E_OUTOFMEMORY;
    }

    d3d_swap->width = width;
    d3d_swap->height = height;

    if (feature_level != NULL)
        *feature_level = D3D_FEATURE_LEVEL_11_0;

    /*
     * Keep the immediate context creation consistent with D3D11: callers
     * may request it via GetImmediateContext(), so the out pointer stays
     * NULL here.
     */
    *device = d3d_device;
    *swap_chain = d3d_swap;
    *context = NULL;

    return S_OK;
}
