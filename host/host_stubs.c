#include <stdint.h>
#include <stdio.h>
#include <errno.h>
int *__errno_location(void) { return &errno; }
int pcps4_XInputEnable(){return 0;} int pcps4_XInputGetCapabilities(){return 1167;}
int pcps4_XInputGetState(){return 1167;} int pcps4_XInputSetState(){return 1167;}
int pcps4_abi_CreateDXGIFactory(){return -1;} int pcps4_abi_CreateDXGIFactory1(){return -1;}
int pcps4_d3d11_com_create_device(){return -1;} int pcps4_d3d11_com_create_device_and_swap_chain(){return -1;}
#include "pcps4_pe.h"
static __attribute__((ms_abi)) void *vk_null(void*a,const char*n){fprintf(stderr,"[host] vkGetInstanceProcAddr(%s)\n",n?n:"?");return 0;}
void pcps4_dxvk_register_vulkan(pcps4_pe_resolver*r){
  pcps4_pe_resolver_register(r,"vulkan-1.dll","vkGetInstanceProcAddr",(void*)vk_null);
  pcps4_pe_resolver_register(r,"vulkan-1.dll","vkGetDeviceProcAddr",(void*)vk_null);
  pcps4_pe_resolver_register(r,"vulkan-1.dll","vkCreateInstance",(void*)vk_null);
  pcps4_pe_resolver_register(r,"vulkan-1.dll","vkEnumerateInstanceExtensionProperties",(void*)vk_null);}
int pcps4_gfx_clear(){return 0;} int pcps4_gfx_draw_rect(){return 0;} int pcps4_gfx_init(){return 0;}
int pcps4_gfx_present(){return 0;} int pcps4_gfx_shutdown(){return 0;}
void vk_ps4_log_open(const char*p){(void)p;}
FILE *host_stdout_alias __asm__("_stdout");
__attribute__((constructor)) static void host_init_stdout(void){ host_stdout_alias = __stdoutp; }
FILE *host_stdin_alias __asm__("_stdin");
FILE *host_stderr_alias __asm__("_stderr");
__attribute__((constructor)) static void host_init_std2(void){ host_stdin_alias = __stdinp; host_stderr_alias = __stderrp; }
