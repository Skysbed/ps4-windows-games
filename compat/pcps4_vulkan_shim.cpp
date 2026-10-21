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
#include "pcps4_selftest_shaders.h"

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
    /* The console's VideoOut only accepts standard display modes (1920x1080,
     * 1280x720, ...) — it rejects a 640x360 scanout buffer (VideoOutOpen error
     * 0x80290005, confirmed v50). So the swapchain must be a full display mode
     * and the game's smaller backbuffer is scaled into it at present time. */
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

static void selftest_run(VkPhysicalDevice pd, VkDevice dev);
static VkPhysicalDevice g_host_pd;
static uint32_t selftest_pick_memory_any(uint32_t bits);

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
    g_host_pd = d;
    if (r == VK_SUCCESS) selftest_run(d, *out);
    free(names);
    return r;
}

/* ------------------------------------------------------------------------ */
/* hardware self-test                                                        */
/* ------------------------------------------------------------------------ */

/* Runs once after the first device is created and reports what the GPU really did, using
 * the driver's own Vulkan entry points: a render-pass clear of a small colour target,
 * a buffer fill, and a draw-free image clear, each read back through the CPU. */
extern "C" int pcps4_clear_variant;

static uint32_t selftest_pick_memory(VkPhysicalDevice pd, uint32_t bits) {
    VkPhysicalDeviceMemoryProperties mp;
    vkGetPhysicalDeviceMemoryProperties(pd, &mp);
    for (uint32_t i = 0; i < mp.memoryTypeCount; i += 1) {
        if (!(bits & (1u << i))) continue;
        const VkMemoryPropertyFlags want = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
        if ((mp.memoryTypes[i].propertyFlags & want) == want) return i;
    }
    return 0;
}

static uint32_t selftest_pick_memory_any(uint32_t bits) { return selftest_pick_memory(g_host_pd, bits); }

static void selftest_run(VkPhysicalDevice pd, VkDevice dev) {
    static int done;
    if (done) return;
    done = 1;
    pcps4_status_log("[selftest] begin");
    VkQueue q = VK_NULL_HANDLE;
    vkGetDeviceQueue(dev, 0, 0, &q);

    VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
    pci.queueFamilyIndex = 0;
    pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    VkCommandPool pool;
    if (vkCreateCommandPool(dev, &pci, nullptr, &pool) != VK_SUCCESS) { pcps4_status_log("[selftest] pool failed"); return; }
    VkCommandBufferAllocateInfo cai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
    cai.commandPool = pool;
    cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cai.commandBufferCount = 1;
    VkCommandBuffer cb;
    vkAllocateCommandBuffers(dev, &cai, &cb);

    /* target image */
    VkImageCreateInfo ici = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    ici.imageType = VK_IMAGE_TYPE_2D;
    ici.format = VK_FORMAT_R8G8B8A8_UNORM;
    ici.extent = {64, 64, 1};
    ici.mipLevels = 1;
    ici.arrayLayers = 1;
    ici.samples = VK_SAMPLE_COUNT_1_BIT;
    ici.tiling = VK_IMAGE_TILING_OPTIMAL;
    ici.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT |
                VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    VkImage img;
    if (vkCreateImage(dev, &ici, nullptr, &img) != VK_SUCCESS) { pcps4_status_log("[selftest] image failed"); return; }
    VkMemoryRequirements mr;
    vkGetImageMemoryRequirements(dev, img, &mr);
    VkMemoryAllocateInfo mai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    mai.allocationSize = mr.size;
    mai.memoryTypeIndex = selftest_pick_memory(pd, mr.memoryTypeBits);
    VkDeviceMemory mem;
    if (vkAllocateMemory(dev, &mai, nullptr, &mem) != VK_SUCCESS) { pcps4_status_log("[selftest] alloc failed"); return; }
    vkBindImageMemory(dev, img, mem, 0);
    volatile uint32_t *px = nullptr;
    vkMapMemory(dev, mem, 0, VK_WHOLE_SIZE, 0, (void **)&px);
    pcps4_status_log("[selftest] image 64x64 size=%llu mapped=%p", (unsigned long long)mr.size, (void *)px);
    if (px) for (int i = 0; i < 64; i += 1) px[i] = 0xDEADBEEF;

    VkImageViewCreateInfo vci = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    vci.image = img;
    vci.viewType = VK_IMAGE_VIEW_TYPE_2D;
    vci.format = ici.format;
    vci.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    VkImageView view;
    vkCreateImageView(dev, &vci, nullptr, &view);

    VkAttachmentDescription att = {};
    att.format = ici.format;
    att.samples = VK_SAMPLE_COUNT_1_BIT;
    att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    att.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    att.finalLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkSubpassDescription sp = {};
    sp.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    sp.colorAttachmentCount = 1;
    sp.pColorAttachments = &ref;
    VkRenderPassCreateInfo rpi = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};
    rpi.attachmentCount = 1;
    rpi.pAttachments = &att;
    rpi.subpassCount = 1;
    rpi.pSubpasses = &sp;
    VkRenderPass rp;
    vkCreateRenderPass(dev, &rpi, nullptr, &rp);
    VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};
    fci.renderPass = rp;
    fci.attachmentCount = 1;
    fci.pAttachments = &view;
    fci.width = 64;
    fci.height = 64;
    fci.layers = 1;
    VkFramebuffer fb;
    vkCreateFramebuffer(dev, &fci, nullptr, &fb);

    VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    VkFence fence;
    vkCreateFence(dev, &fi, nullptr, &fence);

    /* A: render-pass clear to (1.0, 0.5, 0.25, 1.0) = 0xff4080ff, once with the clear colour
     * placed inside the command buffer and once in separate GPU-visible memory. */
    VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
    VkClearValue cv;
    cv.color = {{1.0f, 0.5f, 0.25f, 1.0f}};
    VkRenderPassBeginInfo rbi = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    rbi.renderPass = rp;
    rbi.framebuffer = fb;
    rbi.renderArea = {{0, 0}, {64, 64}};
    rbi.clearValueCount = 1;
    rbi.pClearValues = &cv;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.commandBufferCount = 1;
    si.pCommandBuffers = &cb;
    VkResult r = VK_SUCCESS, w = VK_SUCCESS;
    for (int variant = 0; variant < 2; variant += 1) {
        if (px) {
            for (int i = 0; i < 64; i += 1) px[i] = 0xDEADBEEF;
            px[6000] = 0xCAFEF00D; /* control: outside the surface, GPU must not touch it */
        }
        uint32_t pre = px ? px[0] : 0;
        pcps4_clear_variant = variant;
        vkResetFences(dev, 1, &fence);
        vkBeginCommandBuffer(cb, &bi);
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
        r = vkQueueSubmit(q, 1, &si, fence);
        w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull);
        pcps4_status_log("[selftest] A%d render-pass clear: submit=%d wait=%d pre=%08x px[0]=%08x px[1]=%08x px[63]=%08x control=%08x (want ff4080ff, control cafef00d)",
                         variant, (int)r, (int)w, pre, px ? px[0] : 0, px ? px[1] : 0, px ? px[63] : 0, px ? px[6000] : 0);
        vkResetCommandBuffer(cb, 0);
    }
    pcps4_clear_variant = 0;

    /* B: buffer fill */
    VkBufferCreateInfo bci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
    bci.size = 4096;
    bci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer buf;
    vkCreateBuffer(dev, &bci, nullptr, &buf);
    VkMemoryRequirements bmr;
    vkGetBufferMemoryRequirements(dev, buf, &bmr);
    VkMemoryAllocateInfo bmai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
    bmai.allocationSize = bmr.size;
    bmai.memoryTypeIndex = selftest_pick_memory(pd, bmr.memoryTypeBits);
    VkDeviceMemory bmem;
    vkAllocateMemory(dev, &bmai, nullptr, &bmem);
    vkBindBufferMemory(dev, buf, bmem, 0);
    volatile uint32_t *bp = nullptr;
    vkMapMemory(dev, bmem, 0, VK_WHOLE_SIZE, 0, (void **)&bp);
    if (bp) for (int i = 0; i < 16; i += 1) bp[i] = 0xDEADBEEF;
    vkResetFences(dev, 1, &fence);
    vkResetCommandBuffer(cb, 0);
    vkBeginCommandBuffer(cb, &bi);
    vkCmdFillBuffer(cb, buf, 0, 4096, 0x12345678);
    vkEndCommandBuffer(cb);
    r = vkQueueSubmit(q, 1, &si, fence);
    w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull);
    pcps4_status_log("[selftest] B buffer fill: submit=%d wait=%d b[0]=%08x b[15]=%08x (want 12345678)", (int)r, (int)w,
                     bp ? bp[0] : 0, bp ? bp[15] : 0);

    /* C: vkCmdClearColorImage */
    if (px) for (int i = 0; i < 64; i += 1) px[i] = 0xDEADBEEF;
    vkResetFences(dev, 1, &fence);
    vkResetCommandBuffer(cb, 0);
    vkBeginCommandBuffer(cb, &bi);
    VkImageMemoryBarrier ib = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    ib.srcAccessMask = 0;
    ib.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    ib.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    ib.image = img;
    ib.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &ib);
    VkClearColorValue cc = {{0.0f, 1.0f, 0.0f, 1.0f}};
    VkImageSubresourceRange rr = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    vkCmdClearColorImage(cb, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, &cc, 1, &rr);
    vkEndCommandBuffer(cb);
    r = vkQueueSubmit(q, 1, &si, fence);
    w = vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull);
    pcps4_status_log("[selftest] C vkCmdClearColorImage: submit=%d wait=%d px[0]=%08x px[1]=%08x (want ff00ff00)", (int)r, (int)w,
                     px ? px[0] : 0, px ? px[1] : 0);

    /* ---- D: graphics pipelines of the kind DXVK uses -------------------------------------
     * Each variant draws a full-target triangle into a second 64x64 image and reads it back:
     * constant colour, interpolant, push constant, uniform buffer, sampled texture. */
    {
        /* destination image */
        VkImage img2;
        vkCreateImage(dev, &ici, nullptr, &img2);
        VkMemoryRequirements mr2;
        vkGetImageMemoryRequirements(dev, img2, &mr2);
        VkMemoryAllocateInfo mai2 = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        mai2.allocationSize = mr2.size;
        mai2.memoryTypeIndex = selftest_pick_memory(pd, mr2.memoryTypeBits);
        VkDeviceMemory mem2;
        vkAllocateMemory(dev, &mai2, nullptr, &mem2);
        vkBindImageMemory(dev, img2, mem2, 0);
        volatile uint32_t *px2 = nullptr;
        vkMapMemory(dev, mem2, 0, VK_WHOLE_SIZE, 0, (void **)&px2);
        VkImageViewCreateInfo vci2 = vci;
        vci2.image = img2;
        VkImageView view2;
        vkCreateImageView(dev, &vci2, nullptr, &view2);
        VkFramebufferCreateInfo fci2 = fci;
        fci2.pAttachments = &view2;
        VkFramebuffer fb2;
        vkCreateFramebuffer(dev, &fci2, nullptr, &fb2);

        /* source texture = img, refilled with ff4080ff by a render-pass clear */
        vkResetFences(dev, 1, &fence);
        vkBeginCommandBuffer(cb, &bi);
        vkCmdBeginRenderPass(cb, &rbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cb);
        vkEndCommandBuffer(cb);
        vkQueueSubmit(q, 1, &si, fence);
        vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull);
        vkResetCommandBuffer(cb, 0);

        VkSamplerCreateInfo smi = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
        smi.magFilter = smi.minFilter = VK_FILTER_NEAREST;
        smi.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
        smi.addressModeU = smi.addressModeV = smi.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        smi.maxLod = 0.0f;
        VkSampler sampler;
        vkCreateSampler(dev, &smi, nullptr, &sampler);

        /* uniform buffer holding (0,1,0,1) */
        VkBufferCreateInfo ubi = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
        ubi.size = 256;
        ubi.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
        ubi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        VkBuffer ubuf;
        vkCreateBuffer(dev, &ubi, nullptr, &ubuf);
        VkMemoryRequirements umr;
        vkGetBufferMemoryRequirements(dev, ubuf, &umr);
        VkMemoryAllocateInfo umai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        umai.allocationSize = umr.size;
        umai.memoryTypeIndex = selftest_pick_memory(pd, umr.memoryTypeBits);
        VkDeviceMemory umem;
        vkAllocateMemory(dev, &umai, nullptr, &umem);
        vkBindBufferMemory(dev, ubuf, umem, 0);
        volatile float *uf = nullptr;
        vkMapMemory(dev, umem, 0, VK_WHOLE_SIZE, 0, (void **)&uf);
        if (uf) { uf[0] = 0.0f; uf[1] = 1.0f; uf[2] = 0.0f; uf[3] = 1.0f; }

        /* descriptor set layouts / pool / sets */
        VkDescriptorSetLayoutBinding lb_tex = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutBinding lb_ubo = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, nullptr};
        VkDescriptorSetLayoutCreateInfo dli = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
        dli.bindingCount = 1;
        VkDescriptorSetLayout dsl_tex, dsl_ubo;
        dli.pBindings = &lb_tex;
        vkCreateDescriptorSetLayout(dev, &dli, nullptr, &dsl_tex);
        dli.pBindings = &lb_ubo;
        vkCreateDescriptorSetLayout(dev, &dli, nullptr, &dsl_ubo);
        VkDescriptorPoolSize dps[2] = {{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 1}};
        VkDescriptorPoolCreateInfo dpi = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
        dpi.maxSets = 2;
        dpi.poolSizeCount = 2;
        dpi.pPoolSizes = dps;
        VkDescriptorPool dpool;
        vkCreateDescriptorPool(dev, &dpi, nullptr, &dpool);
        VkDescriptorSetLayout both[2] = {dsl_tex, dsl_ubo};
        VkDescriptorSetAllocateInfo dai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
        dai.descriptorPool = dpool;
        dai.descriptorSetCount = 2;
        dai.pSetLayouts = both;
        VkDescriptorSet dsets[2];
        vkAllocateDescriptorSets(dev, &dai, dsets);
        VkDescriptorImageInfo dii = {sampler, view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
        VkDescriptorBufferInfo dbi = {ubuf, 0, 16};
        VkWriteDescriptorSet wr[2] = {};
        wr[0].sType = wr[1].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        wr[0].dstSet = dsets[0];
        wr[0].descriptorCount = 1;
        wr[0].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        wr[0].pImageInfo = &dii;
        wr[1].dstSet = dsets[1];
        wr[1].descriptorCount = 1;
        wr[1].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
        wr[1].pBufferInfo = &dbi;
        vkUpdateDescriptorSets(dev, 2, wr, 0, nullptr);

        struct Variant {
            const char *name;
            const uint32_t *fs;
            size_t fs_size;
            int layout; /* 0 none, 1 tex, 2 ubo, 3 push */
            uint32_t want;
        } variants[] = {
            {"const    ", spv_fs_const_frag, sizeof(spv_fs_const_frag), 0, 0xffffff00},
            {"interpol ", spv_fs_uv_frag, sizeof(spv_fs_uv_frag), 0, 0xff000000},
            {"push     ", spv_fs_push_frag, sizeof(spv_fs_push_frag), 3, 0xff4080ff},
            {"ubo      ", spv_fs_ubo_frag, sizeof(spv_fs_ubo_frag), 2, 0xff00ff00},
            {"texture  ", spv_fs_tex_frag, sizeof(spv_fs_tex_frag), 1, 0xff4080ff},
        };
        VkShaderModuleCreateInfo smci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
        smci.codeSize = sizeof(spv_vs_vert);
        smci.pCode = spv_vs_vert;
        VkShaderModule vsm;
        vkCreateShaderModule(dev, &smci, nullptr, &vsm);

        for (auto &v : variants) {
            VkPushConstantRange pcr = {VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16};
            VkPipelineLayoutCreateInfo pli = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
            VkDescriptorSetLayout use = v.layout == 1 ? dsl_tex : dsl_ubo;
            if (v.layout == 1 || v.layout == 2) { pli.setLayoutCount = 1; pli.pSetLayouts = &use; }
            if (v.layout == 3) { pli.pushConstantRangeCount = 1; pli.pPushConstantRanges = &pcr; }
            VkPipelineLayout pl;
            vkCreatePipelineLayout(dev, &pli, nullptr, &pl);
            smci.codeSize = v.fs_size;
            smci.pCode = v.fs;
            VkShaderModule fsm;
            vkCreateShaderModule(dev, &smci, nullptr, &fsm);
            VkPipelineShaderStageCreateInfo stages[2] = {};
            stages[0].sType = stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
            stages[0].module = vsm;
            stages[0].pName = "main";
            stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
            stages[1].module = fsm;
            stages[1].pName = "main";
            VkPipelineVertexInputStateCreateInfo vis = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
            VkPipelineInputAssemblyStateCreateInfo ias = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
            ias.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
            VkPipelineViewportStateCreateInfo vps = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
            vps.viewportCount = 1;
            vps.scissorCount = 1;
            VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
            rs.polygonMode = VK_POLYGON_MODE_FILL;
            rs.cullMode = VK_CULL_MODE_NONE;
            rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
            rs.lineWidth = 1.0f;
            VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
            ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
            VkPipelineColorBlendAttachmentState cba = {};
            cba.colorWriteMask = 0xf;
            VkPipelineColorBlendStateCreateInfo cbs = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
            cbs.attachmentCount = 1;
            cbs.pAttachments = &cba;
            VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
            VkPipelineDynamicStateCreateInfo dsi = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
            dsi.dynamicStateCount = 2;
            dsi.pDynamicStates = dyn;
            VkGraphicsPipelineCreateInfo gpi = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
            gpi.stageCount = 2;
            gpi.pStages = stages;
            gpi.pVertexInputState = &vis;
            gpi.pInputAssemblyState = &ias;
            gpi.pViewportState = &vps;
            gpi.pRasterizationState = &rs;
            gpi.pMultisampleState = &ms;
            gpi.pColorBlendState = &cbs;
            gpi.pDynamicState = &dsi;
            gpi.layout = pl;
            gpi.renderPass = rp;
            VkPipeline pipe = VK_NULL_HANDLE;
            VkResult pr = vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gpi, nullptr, &pipe);

            if (px2) for (int i = 0; i < 4096; i += 1) px2[i] = 0xDEADBEEF;
            VkRenderPassBeginInfo rbi2 = rbi;
            rbi2.framebuffer = fb2;
            vkResetFences(dev, 1, &fence);
            vkBeginCommandBuffer(cb, &bi);
            vkCmdBeginRenderPass(cb, &rbi2, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe);
            VkViewport vp = {0.0f, 0.0f, 64.0f, 64.0f, 0.0f, 1.0f};
            VkRect2D sc = {{0, 0}, {64, 64}};
            vkCmdSetViewport(cb, 0, 1, &vp);
            vkCmdSetScissor(cb, 0, 1, &sc);
            if (v.layout == 1) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &dsets[0], 0, nullptr);
            if (v.layout == 2) vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 1, &dsets[1], 0, nullptr);
            if (v.layout == 3) {
                float pc[4] = {1.0f, 0.5f, 0.25f, 1.0f};
                vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_FRAGMENT_BIT, 0, 16, pc);
            }
            vkCmdDraw(cb, 3, 1, 0, 0);
            vkCmdEndRenderPass(cb);
            vkEndCommandBuffer(cb);
            VkResult sr = vkQueueSubmit(q, 1, &si, fence);
            VkResult wr2 = vkWaitForFences(dev, 1, &fence, VK_TRUE, 2000000000ull);
            vkResetCommandBuffer(cb, 0);
            pcps4_status_log("[selftest] D %s pipeline=%d submit=%d wait=%d px[0]=%08x px[7]=%08x px[63]=%08x px[2000]=%08x (want %08x%s)",
                             v.name, (int)pr, (int)sr, (int)wr2, px2 ? px2[0] : 0, px2 ? px2[7] : 0, px2 ? px2[63] : 0,
                             px2 ? px2[2000] : 0, v.want, v.layout == 0 && v.want == 0xff000000 ? ", red varies with x" : "");
        }
    }
    pcps4_status_log("[selftest] end");
}


#ifdef PCPS4_HOST
/* ------------------------------------------------------------------------ */
/* headless swapchain for the Mac development harness                        */
/* ------------------------------------------------------------------------ */

/* The harness has no window, so the fake surface cannot back a real swapchain. Provide one made of
 * ordinary images so the full DXVK frame loop (acquire, render, present) runs on a Mac. */
struct HostSwap {
    VkDevice dev;
    VkQueue q;
    uint32_t n;
    uint32_t next;
    VkImage img[4];
    VkDeviceMemory mem[4];
};

static MSA VkResult shim_vkCreateSwapchainKHR(VkDevice dev, const VkSwapchainCreateInfoKHR *ci,
                                              const VkAllocationCallbacks *, VkSwapchainKHR *out) {
    HostSwap *sw = (HostSwap *)calloc(1, sizeof(*sw));
    sw->dev = dev;
    vkGetDeviceQueue(dev, 0, 0, &sw->q);
    sw->n = ci->minImageCount < 4 ? (ci->minImageCount < 2 ? 2 : ci->minImageCount) : 4;
    for (uint32_t i = 0; i < sw->n; i += 1) {
        VkImageCreateInfo ic = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
        ic.imageType = VK_IMAGE_TYPE_2D;
        ic.format = ci->imageFormat;
        ic.extent = {ci->imageExtent.width, ci->imageExtent.height, 1};
        ic.mipLevels = 1;
        ic.arrayLayers = 1;
        ic.samples = VK_SAMPLE_COUNT_1_BIT;
        ic.tiling = VK_IMAGE_TILING_OPTIMAL;
        ic.usage = ci->imageUsage | VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
        ic.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        if (vkCreateImage(dev, &ic, nullptr, &sw->img[i]) != VK_SUCCESS) return VK_ERROR_INITIALIZATION_FAILED;
        VkMemoryRequirements mr;
        vkGetImageMemoryRequirements(dev, sw->img[i], &mr);
        VkMemoryAllocateInfo ma = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};
        ma.allocationSize = mr.size;
        ma.memoryTypeIndex = selftest_pick_memory_any(mr.memoryTypeBits);
        if (vkAllocateMemory(dev, &ma, nullptr, &sw->mem[i]) != VK_SUCCESS) return VK_ERROR_INITIALIZATION_FAILED;
        vkBindImageMemory(dev, sw->img[i], sw->mem[i], 0);
    }
    *out = (VkSwapchainKHR)sw;
    return VK_SUCCESS;
}
static MSA void shim_vkDestroySwapchainKHR(VkDevice, VkSwapchainKHR, const VkAllocationCallbacks *) {}
static MSA VkResult shim_vkGetSwapchainImagesKHR(VkDevice, VkSwapchainKHR s, uint32_t *count, VkImage *imgs) {
    HostSwap *sw = (HostSwap *)s;
    if (!imgs) { *count = sw->n; return VK_SUCCESS; }
    uint32_t m = *count < sw->n ? *count : sw->n;
    for (uint32_t i = 0; i < m; i += 1) imgs[i] = sw->img[i];
    *count = m;
    return m < sw->n ? VK_INCOMPLETE : VK_SUCCESS;
}
static MSA VkResult shim_vkAcquireNextImageKHR(VkDevice, VkSwapchainKHR s, uint64_t, VkSemaphore sem, VkFence fence,
                                               uint32_t *idx) {
    HostSwap *sw = (HostSwap *)s;
    *idx = sw->next++ % sw->n;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.signalSemaphoreCount = sem ? 1 : 0;
    si.pSignalSemaphores = &sem;
    vkQueueSubmit(sw->q, 1, &si, fence);
    return VK_SUCCESS;
}
static MSA VkResult shim_vkQueuePresentKHR(VkQueue q, const VkPresentInfoKHR *pi) {
    static uint32_t frame;
    VkPipelineStageFlags stage[8];
    for (int i = 0; i < 8; i += 1) stage[i] = VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;
    VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO};
    si.waitSemaphoreCount = pi->waitSemaphoreCount < 8 ? pi->waitSemaphoreCount : 8;
    si.pWaitSemaphores = pi->pWaitSemaphores;
    si.pWaitDstStageMask = stage;
    vkQueueSubmit(q, 1, &si, VK_NULL_HANDLE);
    if ((++frame % 30) == 0) pcps4_status_log("[host] present frame %u", frame);
    return VK_SUCCESS;
}
#endif

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
#ifdef PCPS4_HOST
    {"vkCreateSwapchainKHR", (void *)shim_vkCreateSwapchainKHR},
    {"vkDestroySwapchainKHR", (void *)shim_vkDestroySwapchainKHR},
    {"vkGetSwapchainImagesKHR", (void *)shim_vkGetSwapchainImagesKHR},
    {"vkAcquireNextImageKHR", (void *)shim_vkAcquireNextImageKHR},
    {"vkQueuePresentKHR", (void *)shim_vkQueuePresentKHR},
#endif
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
