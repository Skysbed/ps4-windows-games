#ifndef PCPS4_DXVK_SUPPORT_H
#define PCPS4_DXVK_SUPPORT_H

#include "pcps4_pe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Windows functions DXVK's DLLs import that the base runtime lacked. */
void pcps4_dxvk_register_win32(pcps4_pe_resolver *resolver);

/* "vulkan-1.dll": vkGetInstanceProcAddr and friends, backed by vulkan-ps4. */
void pcps4_dxvk_register_vulkan(pcps4_pe_resolver *resolver);

#ifdef __cplusplus
}
#endif

#endif
