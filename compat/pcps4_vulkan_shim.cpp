/*
 * "vulkan-1.dll" for Windows code (DXVK) running on the PS4.
 *
 * DXVK imports exactly one Vulkan function by name, vkGetInstanceProcAddr, and
 * fetches every other entry point through it. This file provides that export.
 * It returns Microsoft-x64 thunks that forward to vulkan-ps4's System V
 * functions (generated from the symbols the driver exports), and supplies the
 * pieces a Windows Vulkan loader normally provides itself:
 *
 *   - the Win32 surface functions (there is no window system here: the surface
 *     is a placeholder and the display is the console's fixed 1920x1080);
 *   - VK_KHR_win32_surface in the instance extension list;
 *   - instance/device creation that strips what vulkan-ps4 does not know.
 *
 * Optional extension entry points the driver does not export return NULL, which
 * DXVK treats as "extension unavailable".
 */
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

#include <vulkan/vulkan.h>

#include "pcps4_dxvk_support.h"
#include "pcps4_status.h"

#define MSA __attribute__((ms_abi))

template <typename Fn, Fn fn>
struct Ms;
template <typename R, typename... A, R (*fn)(A...)>
struct Ms<R (*)(A...), fn> {
    static MSA R call(A... a) { return fn(a...); }
};

/* ------------------------------------------------------------------------ */
/* the console's display                                                     */
/* ------------------------------------------------------------------------ */

static const uint32_t kWidth = 1920;
static const uint32_t kHeight = 1080;
static const uint64_t kSurface = 0x5043345355524601ull; /* placeholder handle */

static MSA VkResult shim_vkCreateWin32SurfaceKHR(VkInstance, const void *, const VkAllocationCallbacks *,
                                                 VkSurfaceKHR *out) {
    if (!out) return VK_ERROR_INITIALIZATION_FAILED;
    *out = (VkSurfaceKHR)(uintptr_t)kSurface;
    return VK_SUCCESS;
}
static MSA void shim_vkDestroySurfaceKHR(VkInstance, VkSurfaceKHR, const VkAllocationCallbacks *) {}
static MSA VkBool32 shim_vkGetPhysicalDeviceWin32PresentationSupportKHR(VkPhysicalDevice, uint32_t) {
    return VK_TRUE;
}
static MSA VkResult shim_vkGetPhysicalDeviceSurfaceSupportKHR(VkPhysicalDevice, uint32_t, VkSurfaceKHR,
                                                              VkBool32 *supported) {
    if (supported) *supported = VK_TRUE;
    return VK_SUCCESS;
}
static MSA VkResult shim_vkGetPhysicalDeviceSurfaceCapabilitiesKHR(VkPhysicalDevice, VkSurfaceKHR,
                                                                   VkSurfaceCapabilitiesKHR *c) {
    if (!c) return VK_ERROR_INITIALIZATION_FAILED;
    memset(c, 0, sizeof(*c));
    c->minImageCount = 2;
    c->maxImageCount = 3;
    /* The console scans out one fixed mode; pin every extent to it so DXVK
     * always creates a 1920x1080 swapchain and scales the game into it. */
    c->currentExtent = {kWidth, kHeight};
    c->minImageExtent = {kWidth, kHeight};
    c->maxImageExtent = {kWidth, kHeight};
    c->maxImageArrayLayers = 1;
    c->supportedTransforms = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    c->currentTransform = VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR;
    c->supportedCompositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    c->supportedUsageFlags = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                             VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    return VK_SUCCESS;
}
static MSA VkResult shim_vkGetPhysicalDeviceSurfaceFormatsKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count,
                                                              VkSurfaceFormatKHR *out) {
    static const VkSurfaceFormatKHR formats[] = {
        {VK_FORMAT_R8G8B8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_R8G8B8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
        {VK_FORMAT_B8G8R8A8_SRGB, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR},
    };
    const uint32_t n = sizeof(formats) / sizeof(formats[0]);
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    const uint32_t m = *count < n ? *count : n;
    for (uint32_t i = 0; i < m; i += 1) out[i] = formats[i];
    *count = m;
    return m < n ? VK_INCOMPLETE : VK_SUCCESS;
}
static MSA VkResult shim_vkGetPhysicalDeviceSurfacePresentModesKHR(VkPhysicalDevice, VkSurfaceKHR, uint32_t *count,
                                                                   VkPresentModeKHR *out) {
    static const VkPresentModeKHR modes[] = {VK_PRESENT_MODE_FIFO_KHR, VK_PRESENT_MODE_IMMEDIATE_KHR};
    const uint32_t n = 2;
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!out) {
        *count = n;
        return VK_SUCCESS;
    }
    const uint32_t m = *count < n ? *count : n;
    for (uint32_t i = 0; i < m; i += 1) out[i] = modes[i];
    *count = m;
    return m < n ? VK_INCOMPLETE : VK_SUCCESS;
}

/* ------------------------------------------------------------------------ */
/* instance / device creation                                                */
/* ------------------------------------------------------------------------ */

static const char *kFauxInstanceExt = "VK_KHR_win32_surface";

static MSA VkResult shim_vkEnumerateInstanceExtensionProperties(const char *layer, uint32_t *count,
                                                                VkExtensionProperties *out) {
    uint32_t real = 0;
    VkResult r = vkEnumerateInstanceExtensionProperties(layer, &real, nullptr);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return r;
    if (layer) { /* layers: pass straight through */
        return vkEnumerateInstanceExtensionProperties(layer, count, out);
    }
    const uint32_t total = real + 1;
    if (!count) return VK_ERROR_INITIALIZATION_FAILED;
    if (!out) {
        *count = total;
        return VK_SUCCESS;
    }
    VkExtensionProperties *tmp = (VkExtensionProperties *)calloc(total, sizeof(*tmp));
    if (!tmp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t got = real;
    vkEnumerateInstanceExtensionProperties(nullptr, &got, tmp);
    strncpy(tmp[got].extensionName, kFauxInstanceExt, VK_MAX_EXTENSION_NAME_SIZE - 1);
    tmp[got].specVersion = 6;
    const uint32_t n = got + 1;
    const uint32_t m = *count < n ? *count : n;
    for (uint32_t i = 0; i < m; i += 1) out[i] = tmp[i];
    free(tmp);
    *count = m;
    return m < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static MSA VkResult shim_vkCreateInstance(const VkInstanceCreateInfo *ci, const VkAllocationCallbacks *alloc,
                                          VkInstance *out) {
    if (!ci) return VK_ERROR_INITIALIZATION_FAILED;
    VkInstanceCreateInfo copy = *ci;
    const char **names = nullptr;
    if (ci->enabledExtensionCount) {
        names = (const char **)calloc(ci->enabledExtensionCount, sizeof(*names));
        if (!names) return VK_ERROR_OUT_OF_HOST_MEMORY;
        uint32_t k = 0;
        for (uint32_t i = 0; i < ci->enabledExtensionCount; i += 1) {
            if (strcmp(ci->ppEnabledExtensionNames[i], kFauxInstanceExt) != 0)
                names[k++] = ci->ppEnabledExtensionNames[i];
        }
        copy.enabledExtensionCount = k;
        copy.ppEnabledExtensionNames = names;
    }
    VkResult r = vkCreateInstance(&copy, alloc, out);
    free(names);
    return r;
}

/* ------------------------------------------------------------------------ */
/* features                                                                  */
/* ------------------------------------------------------------------------ */

/* DXVK refuses Direct3D 11 feature level 11_0 unless the device reports a long list of
 * core features (geometry/tessellation shaders, BC textures, ...). The console GPU is
 * GCN, which has the hardware for them, but the Vulkan layer does not advertise them
 * all yet. Report them so device creation proceeds; the driver ignores the request. */
static void claim_all_features(VkPhysicalDeviceFeatures *f) {
    VkBool32 *b = (VkBool32 *)f;
    for (size_t i = 0; i < sizeof(*f) / sizeof(VkBool32); i += 1) b[i] = VK_TRUE;
}
static MSA void shim_vkGetPhysicalDeviceFeatures(VkPhysicalDevice d, VkPhysicalDeviceFeatures *f) {
    vkGetPhysicalDeviceFeatures(d, f);
    if (f) claim_all_features(f);
}
static MSA void shim_vkGetPhysicalDeviceFeatures2(VkPhysicalDevice d, VkPhysicalDeviceFeatures2 *f) {
    vkGetPhysicalDeviceFeatures2(d, f);
    if (f) claim_all_features(&f->features);
    /* DXVK's feature level 11_0 hard-requires these extension feature bits. */
    for (VkBaseOutStructure *c = (VkBaseOutStructure *)(f ? f->pNext : nullptr); c; c = c->pNext) {
        switch ((int)c->sType) {
        case 1000063000: /* ShaderDrawParametersFeatures */
            ((VkPhysicalDeviceShaderDrawParametersFeatures *)c)->shaderDrawParameters = VK_TRUE;
            break;
        case 1000261000: /* HostQueryResetFeatures */
            ((VkPhysicalDeviceHostQueryResetFeatures *)c)->hostQueryReset = VK_TRUE;
            break;
        case 1000028000: /* TransformFeedbackFeaturesEXT */
            ((VkPhysicalDeviceTransformFeedbackFeaturesEXT *)c)->transformFeedback = VK_TRUE;
            ((VkPhysicalDeviceTransformFeedbackFeaturesEXT *)c)->geometryStreams = VK_TRUE;
            break;
        default:
            break;
        }
    }
    pcps4_status_log("[vk] GetPhysicalDeviceFeatures2 (claimed all core features)");
    for (const VkBaseOutStructure *c = (const VkBaseOutStructure *)(f ? f->pNext : nullptr); c; c = c->pNext)
        pcps4_status_log("[vk]   chain sType=%d", (int)c->sType);
}

/* VK_EXT_transform_feedback is required by DXVK for D3D11 stream output. Advertise it
 * (nothing uses it unless a game does stream-out) and strip it before the driver sees it. */
static const char *kFauxDeviceExt = "VK_EXT_transform_feedback";

static MSA VkResult shim_vkEnumerateDeviceExtensionProperties(VkPhysicalDevice d, const char *layer,
                                                              uint32_t *count, VkExtensionProperties *out) {
    if (layer || !count) return vkEnumerateDeviceExtensionProperties(d, layer, count, out);
    uint32_t real = 0;
    VkResult r = vkEnumerateDeviceExtensionProperties(d, nullptr, &real, nullptr);
    if (r != VK_SUCCESS && r != VK_INCOMPLETE) return r;
    const uint32_t total = real + 1;
    if (!out) {
        *count = total;
        return VK_SUCCESS;
    }
    VkExtensionProperties *tmp = (VkExtensionProperties *)calloc(total, sizeof(*tmp));
    if (!tmp) return VK_ERROR_OUT_OF_HOST_MEMORY;
    uint32_t got = real;
    vkEnumerateDeviceExtensionProperties(d, nullptr, &got, tmp);
    strncpy(tmp[got].extensionName, kFauxDeviceExt, VK_MAX_EXTENSION_NAME_SIZE - 1);
    tmp[got].specVersion = 1;
    const uint32_t n = got + 1;
    const uint32_t m = *count < n ? *count : n;
    for (uint32_t i = 0; i < m; i += 1) out[i] = tmp[i];
    free(tmp);
    *count = m;
    return m < n ? VK_INCOMPLETE : VK_SUCCESS;
}

static MSA VkResult shim_vkCreateDevice(VkPhysicalDevice d, const VkDeviceCreateInfo *ci,
                                        const VkAllocationCallbacks *alloc, VkDevice *out) {
    if (!ci) return VK_ERROR_INITIALIZATION_FAILED;
    VkDeviceCreateInfo copy = *ci;
    const char **names = nullptr;
    if (ci->enabledExtensionCount) {
        names = (const char **)calloc(ci->enabledExtensionCount, sizeof(*names));
        if (!names) return VK_ERROR_OUT_OF_HOST_MEMORY;
        uint32_t k = 0;
        for (uint32_t i = 0; i < ci->enabledExtensionCount; i += 1) {
            pcps4_status_log("[vk] CreateDevice ext: %s", ci->ppEnabledExtensionNames[i]);
            if (strcmp(ci->ppEnabledExtensionNames[i], kFauxDeviceExt) != 0)
                names[k++] = ci->ppEnabledExtensionNames[i];
        }
        copy.enabledExtensionCount = k;
        copy.ppEnabledExtensionNames = names;
    }
    /* We told DXVK every core feature exists; a strict driver (or MoltenVK on the dev machine)
     * rejects requests for ones it lacks, so mask the request down to what is really there. */
    VkPhysicalDeviceFeatures real;
    vkGetPhysicalDeviceFeatures(d, &real);
    VkPhysicalDeviceFeatures masked;
    VkPhysicalDeviceFeatures2 masked2;
    if (ci->pEnabledFeatures) {
        masked = *ci->pEnabledFeatures;
        for (size_t i = 0; i < sizeof(masked) / sizeof(VkBool32); i += 1)
            ((VkBool32 *)&masked)[i] &= ((VkBool32 *)&real)[i];
        copy.pEnabledFeatures = &masked;
    }
    VkBaseInStructure *chain_head = (VkBaseInStructure *)ci->pNext;
    for (VkBaseInStructure *c = chain_head; c; c = (VkBaseInStructure *)c->pNext) {
        if (c->sType == VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2) {
            VkPhysicalDeviceFeatures2 *f2 = (VkPhysicalDeviceFeatures2 *)c;
            masked2 = *f2;
            for (size_t i = 0; i < sizeof(VkPhysicalDeviceFeatures) / sizeof(VkBool32); i += 1)
                ((VkBool32 *)&f2->features)[i] &= ((VkBool32 *)&real)[i];
        }
    }
    (void)masked2;
    VkResult r = vkCreateDevice(d, &copy, alloc, out);
    pcps4_status_log("[vk] vkCreateDevice -> %d", (int)r);
    free(names);
    return r;
}

/* ------------------------------------------------------------------------ */
/* the export                                                                */
/* ------------------------------------------------------------------------ */

struct Entry {
    const char *name;
    void *fn;
};

#define X(n) {#n, (void *)&Ms<decltype(&n), &n>::call},
static const Entry kTable[] = {
#include "pcps4_vulkan_shim_table.inc"
};
#undef X

static const Entry kOverrides[] = {
    {"vkEnumerateDeviceExtensionProperties", (void *)shim_vkEnumerateDeviceExtensionProperties},
    {"vkCreateDevice", (void *)shim_vkCreateDevice},
    {"vkGetPhysicalDeviceFeatures", (void *)shim_vkGetPhysicalDeviceFeatures},
    {"vkGetPhysicalDeviceFeatures2", (void *)shim_vkGetPhysicalDeviceFeatures2},
    {"vkGetPhysicalDeviceFeatures2KHR", (void *)shim_vkGetPhysicalDeviceFeatures2},
    {"vkCreateInstance", (void *)shim_vkCreateInstance},
    {"vkEnumerateInstanceExtensionProperties", (void *)shim_vkEnumerateInstanceExtensionProperties},
    {"vkCreateWin32SurfaceKHR", (void *)shim_vkCreateWin32SurfaceKHR},
    {"vkDestroySurfaceKHR", (void *)shim_vkDestroySurfaceKHR},
    {"vkGetPhysicalDeviceWin32PresentationSupportKHR", (void *)shim_vkGetPhysicalDeviceWin32PresentationSupportKHR},
    {"vkGetPhysicalDeviceSurfaceSupportKHR", (void *)shim_vkGetPhysicalDeviceSurfaceSupportKHR},
    {"vkGetPhysicalDeviceSurfaceCapabilitiesKHR", (void *)shim_vkGetPhysicalDeviceSurfaceCapabilitiesKHR},
    {"vkGetPhysicalDeviceSurfaceFormatsKHR", (void *)shim_vkGetPhysicalDeviceSurfaceFormatsKHR},
    {"vkGetPhysicalDeviceSurfacePresentModesKHR", (void *)shim_vkGetPhysicalDeviceSurfacePresentModesKHR},
};

static MSA void *shim_vkGetInstanceProcAddr(VkInstance, const char *name);
static MSA void *shim_vkGetDeviceProcAddr(VkDevice, const char *name);

static void *lookup(const char *name) {
    if (!name) return nullptr;
    if (strcmp(name, "vkGetInstanceProcAddr") == 0) return (void *)shim_vkGetInstanceProcAddr;
    if (strcmp(name, "vkGetDeviceProcAddr") == 0) return (void *)shim_vkGetDeviceProcAddr;
    for (const Entry &e : kOverrides)
        if (strcmp(e.name, name) == 0) return e.fn;
    for (const Entry &e : kTable)
        if (strcmp(e.name, name) == 0) return e.fn;
    return nullptr;
}

static MSA void *shim_vkGetInstanceProcAddr(VkInstance, const char *name) {
    void *p = lookup(name);
    pcps4_status_log("[vk] GetInstanceProcAddr %s -> %s", name ? name : "(null)", p ? "ok" : "MISSING");
    return p;
}
static MSA void *shim_vkGetDeviceProcAddr(VkDevice, const char *name) {
    void *p = lookup(name);
    if (!p) pcps4_status_log("[vk] GetDeviceProcAddr %s -> MISSING", name ? name : "(null)");
    return p;
}

extern "C" void pcps4_dxvk_register_vulkan(pcps4_pe_resolver *resolver) {
    static const char *const dlls[] = {"vulkan-1.dll", "VULKAN-1.dll"};
    for (const char *d : dlls) {
        (void)pcps4_pe_resolver_register(resolver, d, "vkGetInstanceProcAddr", (void *)shim_vkGetInstanceProcAddr);
        (void)pcps4_pe_resolver_register(resolver, d, "vkGetDeviceProcAddr", (void *)shim_vkGetDeviceProcAddr);
        (void)pcps4_pe_resolver_register(resolver, d, "vkCreateInstance", (void *)shim_vkCreateInstance);
        (void)pcps4_pe_resolver_register(resolver, d, "vkEnumerateInstanceExtensionProperties",
                                         (void *)shim_vkEnumerateInstanceExtensionProperties);
    }
}
