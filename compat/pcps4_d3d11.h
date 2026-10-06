#ifndef PCPS4_D3D11_H
#define PCPS4_D3D11_H

#ifdef __cplusplus

#include <stddef.h>
#include <stdint.h>
#include "pcps4_gpu.h"

typedef int32_t HRESULT;
typedef uint32_t UINT;
typedef uint32_t D3D11_BIND_FLAG;
typedef uint32_t D3D11_USAGE;
typedef uint32_t D3D11_CPU_ACCESS_FLAG;
typedef uint32_t D3D11_CREATE_DEVICE_FLAG;
typedef uint32_t DXGI_FORMAT;
typedef uint32_t DXGI_SWAP_EFFECT;
typedef void *HWND;

#ifndef S_OK
#define S_OK ((HRESULT)0)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif

#define FAILED(hr) ((HRESULT)(hr) < 0)
#define SUCCEEDED(hr) ((HRESULT)(hr) >= 0)

enum {
    D3D11_BIND_VERTEX_BUFFER   = PCPS4_GPU_BIND_VERTEX_BUFFER,
    D3D11_BIND_INDEX_BUFFER    = PCPS4_GPU_BIND_INDEX_BUFFER,
    D3D11_BIND_CONSTANT_BUFFER = PCPS4_GPU_BIND_CONSTANT_BUFFER,
    D3D11_BIND_SHADER_RESOURCE = PCPS4_GPU_BIND_SHADER_RESOURCE,
    D3D11_BIND_RENDER_TARGET   = PCPS4_GPU_BIND_RENDER_TARGET
};

enum {
    D3D11_USAGE_DEFAULT = PCPS4_GPU_USAGE_DEFAULT,
    D3D11_USAGE_DYNAMIC = PCPS4_GPU_USAGE_DYNAMIC
};

enum {
    D3D11_CPU_ACCESS_WRITE = 1u
};

enum {
    DXGI_FORMAT_UNKNOWN = 0,
    DXGI_FORMAT_R32G32B32A32_FLOAT = 2,
    DXGI_FORMAT_R32G32B32_FLOAT = 6,
    DXGI_FORMAT_R16G16B16A16_FLOAT = 10,
    DXGI_FORMAT_R32G32_FLOAT = 16,
    DXGI_FORMAT_R16G16_FLOAT = 34,
    DXGI_FORMAT_R32_FLOAT = 41,
    DXGI_FORMAT_R32_UINT = 42,
    DXGI_FORMAT_R16_UINT = 57,
    DXGI_FORMAT_R8G8B8A8_UNORM = 28,
    DXGI_FORMAT_B8G8R8A8_UNORM = 87
};

enum {
    DXGI_SWAP_EFFECT_DISCARD = 0,
    DXGI_SWAP_EFFECT_FLIP_DISCARD = 4
};

typedef enum D3D11_MAP {
    D3D11_MAP_READ = 1,
    D3D11_MAP_WRITE = 2,
    D3D11_MAP_READ_WRITE = 3,
    D3D11_MAP_WRITE_DISCARD = 4,
    D3D11_MAP_WRITE_NO_OVERWRITE = 5
} D3D11_MAP;

typedef enum D3D11_PRIMITIVE_TOPOLOGY {
    D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED = 0,
    D3D11_PRIMITIVE_TOPOLOGY_POINTLIST = 1,
    D3D11_PRIMITIVE_TOPOLOGY_LINELIST = 2,
    D3D11_PRIMITIVE_TOPOLOGY_LINESTRIP = 3,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST = 4,
    D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP = 5
} D3D11_PRIMITIVE_TOPOLOGY;

typedef struct D3D11_MAPPED_SUBRESOURCE {
    void *pData;
    UINT RowPitch;
    UINT DepthPitch;
} D3D11_MAPPED_SUBRESOURCE;

typedef enum D3D_FEATURE_LEVEL {
    D3D_FEATURE_LEVEL_11_0 = 0xB000,
    D3D_FEATURE_LEVEL_10_1 = 0xA100,
    D3D_FEATURE_LEVEL_10_0 = 0xA000
} D3D_FEATURE_LEVEL;

typedef enum D3D_DRIVER_TYPE {
    D3D_DRIVER_TYPE_HARDWARE = 1
} D3D_DRIVER_TYPE;

typedef struct D3D11_BUFFER_DESC {
    UINT ByteWidth;
    D3D11_USAGE Usage;
    D3D11_BIND_FLAG BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
    UINT StructureByteStride;
} D3D11_BUFFER_DESC;

typedef struct D3D11_SUBRESOURCE_DATA {
    const void *pSysMem;
    UINT SysMemPitch;
    UINT SysMemSlicePitch;
} D3D11_SUBRESOURCE_DATA;

typedef struct D3D11_TEXTURE2D_DESC {
    UINT Width;
    UINT Height;
    UINT MipLevels;
    UINT ArraySize;
    DXGI_FORMAT Format;
    struct {
        UINT Count;
        UINT Quality;
    } SampleDesc;
    D3D11_USAGE Usage;
    D3D11_BIND_FLAG BindFlags;
    UINT CPUAccessFlags;
    UINT MiscFlags;
} D3D11_TEXTURE2D_DESC;

typedef struct DXGI_RATIONAL {
    UINT Numerator;
    UINT Denominator;
} DXGI_RATIONAL;

typedef struct DXGI_MODE_DESC {
    UINT Width;
    UINT Height;
    DXGI_RATIONAL RefreshRate;
    DXGI_FORMAT Format;
    UINT ScanlineOrdering;
    UINT Scaling;
} DXGI_MODE_DESC;

typedef struct DXGI_SAMPLE_DESC {
    UINT Count;
    UINT Quality;
} DXGI_SAMPLE_DESC;

typedef struct DXGI_SWAP_CHAIN_DESC {
    DXGI_MODE_DESC BufferDesc;
    DXGI_SAMPLE_DESC SampleDesc;
    UINT BufferUsage;
    UINT BufferCount;
    HWND OutputWindow;
    UINT Windowed;
    DXGI_SWAP_EFFECT SwapEffect;
    UINT Flags;
} DXGI_SWAP_CHAIN_DESC;

typedef struct D3D11_RENDER_TARGET_VIEW_DESC {
    DXGI_FORMAT Format;
    UINT ViewDimension;
    UINT MipSlice;
} D3D11_RENDER_TARGET_VIEW_DESC;

class ID3D11Buffer {
public:
    pcps4_gpu_buffer_t *native;
    uint32_t refs;
    explicit ID3D11Buffer(pcps4_gpu_buffer_t *value) : native(value), refs(1) {}
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11Buffer();
};

class ID3D11Texture2D {
public:
    pcps4_gpu_texture_t *native;
    uint32_t refs;
    explicit ID3D11Texture2D(pcps4_gpu_texture_t *value) : native(value), refs(1) {}
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
    void GetDesc(D3D11_TEXTURE2D_DESC *desc) const;
private:
    ~ID3D11Texture2D();
};

class ID3D11VertexShader {
public:
    pcps4_gpu_shader_t *native;
    uint32_t refs;
    explicit ID3D11VertexShader(pcps4_gpu_shader_t *value) : native(value), refs(1) {}
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11VertexShader();
};

class ID3D11PixelShader {
public:
    pcps4_gpu_shader_t *native;
    uint32_t refs;
    explicit ID3D11PixelShader(pcps4_gpu_shader_t *value) : native(value), refs(1) {}
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11PixelShader();
};

class ID3D11RenderTargetView {
public:
    pcps4_gpu_render_target_t *native;
    uint32_t refs;
    explicit ID3D11RenderTargetView(pcps4_gpu_render_target_t *value) : native(value), refs(1) {}
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11RenderTargetView();
};

class IDXGISwapChain {
public:
    pcps4_gpu_device_t *device;
    pcps4_gpu_context_t *context;
    pcps4_gfx_context_t *graphics;
    pcps4_gnm_renderer_t *gnm_renderer;
    uint32_t refs;
    uint64_t frame_id;
    uint32_t width;
    uint32_t height;

    IDXGISwapChain(pcps4_gpu_device_t *device_value,
                   pcps4_gpu_context_t *context_value,
                   pcps4_gfx_context_t *graphics_value,
                   pcps4_gnm_renderer_t *gnm_renderer_value)
        : device(device_value),
          context(context_value),
          graphics(graphics_value),
          gnm_renderer(gnm_renderer_value),
          refs(1),
          frame_id(1),
          width(0),
          height(0) {}

    HRESULT Present(UINT SyncInterval, UINT Flags);
    HRESULT GetBuffer(UINT Buffer, ID3D11Texture2D **Resource);
    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~IDXGISwapChain();
};

class ID3D11DeviceContext {
public:
    pcps4_gpu_context_t *native;
    uint32_t refs;
    explicit ID3D11DeviceContext(pcps4_gpu_context_t *value) : native(value), refs(1) {}

    void OMSetRenderTargets(UINT NumViews,
                            ID3D11RenderTargetView *const *ppRenderTargetViews,
                            void *pDepthStencilView);
    void ClearRenderTargetView(ID3D11RenderTargetView *pRenderTargetView,
                               const float ColorRGBA[4]);
    void IASetVertexBuffers(UINT StartSlot, UINT NumBuffers,
                            ID3D11Buffer *const *ppVertexBuffers,
                            const UINT *pStrides, const UINT *pOffsets);
    void IASetIndexBuffer(ID3D11Buffer *pIndexBuffer,
                          DXGI_FORMAT Format,
                          UINT Offset);
    void IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY Topology);

    HRESULT Map(ID3D11Buffer *pResource,
                UINT Subresource,
                D3D11_MAP MapType,
                UINT MapFlags,
                D3D11_MAPPED_SUBRESOURCE *pMappedResource);
    void Unmap(ID3D11Buffer *pResource, UINT Subresource);

    void VSSetShader(ID3D11VertexShader *pVertexShader,
                     void *const *ppClassInstances, UINT NumClassInstances);
    void PSSetShader(ID3D11PixelShader *pPixelShader,
                     void *const *ppClassInstances, UINT NumClassInstances);
    void Draw(UINT VertexCount, UINT StartVertexLocation);
    void DrawIndexed(UINT IndexCount, UINT StartIndexLocation,
                     int BaseVertexLocation);
    void UpdateSubresource(ID3D11Texture2D *pDstResource, UINT DstSubresource,
                           const void *pDstBox, const void *pSrcData,
                           UINT SrcRowPitch, UINT SrcDepthPitch);
    void Flush();

    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11DeviceContext();
};

class ID3D11Device {
public:
    pcps4_gpu_device_t *native;
    pcps4_gpu_context_t *context;
    pcps4_gfx_context_t *graphics;
    pcps4_gnm_renderer_t *gnm_renderer;
    uint32_t refs;

    ID3D11Device(pcps4_gpu_device_t *device_value,
                 pcps4_gpu_context_t *context_value,
                 pcps4_gfx_context_t *graphics_value)
        : native(device_value),
          context(context_value),
          graphics(graphics_value),
          gnm_renderer(NULL),
          refs(1) {}

    HRESULT CreateBuffer(const D3D11_BUFFER_DESC *pDesc,
                         const D3D11_SUBRESOURCE_DATA *pInitialData,
                         ID3D11Buffer **ppBuffer);
    HRESULT CreateTexture2D(const D3D11_TEXTURE2D_DESC *pDesc,
                            const D3D11_SUBRESOURCE_DATA *pInitialData,
                            ID3D11Texture2D **ppTexture2D);
    HRESULT CreateRenderTargetView(ID3D11Texture2D *pResource,
                                   const D3D11_RENDER_TARGET_VIEW_DESC *pDesc,
                                   ID3D11RenderTargetView **ppRTView);
    HRESULT CreateVertexShader(const void *pShaderBytecode,
                               size_t BytecodeLength, void *pClassLinkage,
                               ID3D11VertexShader **ppVertexShader);
    HRESULT CreatePixelShader(const void *pShaderBytecode,
                              size_t BytecodeLength, void *pClassLinkage,
                              ID3D11PixelShader **ppPixelShader);
    void GetImmediateContext(ID3D11DeviceContext **ppImmediateContext);

    UINT AddRef() { return ++refs; }
    UINT Release() { if (refs > 1) return --refs; delete this; return 0; }
private:
    ~ID3D11Device();
};

HRESULT pcps4_d3d11_create_device_and_swap_chain(
    const DXGI_SWAP_CHAIN_DESC *swap_chain_desc,
    ID3D11Device **device,
    D3D_FEATURE_LEVEL *feature_level,
    ID3D11DeviceContext **context,
    IDXGISwapChain **swap_chain);

inline HRESULT D3D11CreateDeviceAndSwapChain(
    void *pAdapter, D3D_DRIVER_TYPE DriverType, void *Software,
    D3D11_CREATE_DEVICE_FLAG Flags,
    const D3D_FEATURE_LEVEL *pFeatureLevels, UINT FeatureLevels,
    UINT SDKVersion, const DXGI_SWAP_CHAIN_DESC *pSwapChainDesc,
    IDXGISwapChain **ppSwapChain, ID3D11Device **ppDevice,
    D3D_FEATURE_LEVEL *pFeatureLevel,
    ID3D11DeviceContext **ppImmediateContext)
{
    (void)pAdapter; (void)DriverType; (void)Software; (void)Flags;
    (void)pFeatureLevels; (void)FeatureLevels; (void)SDKVersion;

    return pcps4_d3d11_create_device_and_swap_chain(
        pSwapChainDesc, ppDevice, pFeatureLevel,
        ppImmediateContext, ppSwapChain);
}

#endif
#endif
