#ifndef PCPS4_D3D11_COM_H
#define PCPS4_D3D11_COM_H

#include "pcps4_d3d11.h"

#ifndef __cplusplus
#include <stdint.h>
typedef int32_t HRESULT;
typedef uint32_t UINT;
typedef uint32_t D3D_DRIVER_TYPE;
typedef uint32_t D3D11_CREATE_DEVICE_FLAG;
typedef uint32_t D3D_FEATURE_LEVEL;
typedef struct DXGI_SWAP_CHAIN_DESC DXGI_SWAP_CHAIN_DESC;
#ifndef S_OK
#define S_OK ((HRESULT)0)
#endif
#endif

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Native Windows binaries see these objects as COM interfaces:
 * the first machine word is an lpVtbl pointer. The implementation behind
 * each proxy is the existing PcPs4 C++ D3D11 object.
 */
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
    void **ppImmediateContext);

HRESULT pcps4_d3d11_com_create_swap_chain(
    void *device,
    const void *swap_chain_desc,
    void **swap_chain);

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
    void **ppImmediateContext);

#ifdef __cplusplus
}
#endif

#endif /* PCPS4_D3D11_COM_H */
