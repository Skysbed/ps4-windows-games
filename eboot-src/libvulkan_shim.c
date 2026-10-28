/* PS4 libvulkan: expose the Vulkan loader entry vkGetInstanceProcAddr by
 * forwarding to RADV's ICD entry. Wine's winevulkan dlopens libvulkan.so and
 * dlsyms vkGetInstanceProcAddr, then pulls every other function through it. */
typedef void (*PFN_vkVoidFunction)(void);
PFN_vkVoidFunction vk_icdGetInstanceProcAddr(void *instance, const char *pName);
__attribute__((visibility("default")))
PFN_vkVoidFunction vkGetInstanceProcAddr(void *instance, const char *pName) {
    return vk_icdGetInstanceProcAddr(instance, pName);
}
