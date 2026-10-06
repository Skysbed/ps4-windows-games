#ifndef PCPS4_DXGI_H
#define PCPS4_DXGI_H

#include "pcps4_compat.h"

#if defined(__x86_64__) && (defined(__clang__) || defined(__GNUC__))
#define PCPS4_MS_ABI __attribute__((ms_abi))
#else
#define PCPS4_MS_ABI
#ifdef __cplusplus
}
#endif

#endif

#ifndef S_OK
#define S_OK ((HRESULT)0)
#endif
#ifndef E_FAIL
#define E_FAIL ((HRESULT)0x80004005L)
#endif
#ifndef E_NOINTERFACE
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#endif
#ifndef E_NOTIMPL
#define E_NOTIMPL ((HRESULT)0x80004001L)
#endif
#ifndef E_OUTOFMEMORY
#define E_OUTOFMEMORY ((HRESULT)0x8007000EL)
#endif
#ifndef E_INVALIDARG
#define E_INVALIDARG ((HRESULT)0x80070057L)
#endif

typedef struct PCPS4_DXGI_GUID {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} PCPS4_DXGI_GUID;

typedef struct PCPS4_DXGI_RATIONAL {
    UINT Numerator;
    UINT Denominator;
} PCPS4_DXGI_RATIONAL;

typedef struct PCPS4_DXGI_MODE_DESC {
    UINT Width;
    UINT Height;
    PCPS4_DXGI_RATIONAL RefreshRate;
    UINT Format;
    UINT ScanlineOrdering;
    UINT Scaling;
} PCPS4_DXGI_MODE_DESC;

typedef struct PCPS4_DXGI_SWAP_CHAIN_DESC {
    PCPS4_DXGI_MODE_DESC BufferDesc;
    struct { UINT Count; UINT Quality; } SampleDesc;
    UINT BufferUsage;
    UINT BufferCount;
    HANDLE OutputWindow;
    BOOL Windowed;
    UINT SwapEffect;
    UINT Flags;
} PCPS4_DXGI_SWAP_CHAIN_DESC;

#define PCPS4_DXGI_ERROR_INVALID_CALL ((HRESULT)0x887A0001L)
#define PCPS4_DXGI_ERROR_NOT_FOUND    ((HRESULT)0x887A0002L)
#define PCPS4_DXGI_ERROR_UNSUPPORTED  ((HRESULT)0x887A0004L)

#ifdef __cplusplus
extern "C" {
#endif

PCPS4_MS_ABI HRESULT pcps4_dxgi_create_device(
    const void *riid,
    void **device);


PCPS4_MS_ABI HRESULT pcps4_abi_CreateDXGIFactory(
    const void *riid, void **factory);

PCPS4_MS_ABI HRESULT pcps4_abi_CreateDXGIFactory1(
    const void *riid, void **factory);

#endif
