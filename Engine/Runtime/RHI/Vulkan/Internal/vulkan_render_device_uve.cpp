// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VulkanRenderDeviceUVE implementation — the M1 bootstrap bring-up pipeline:
//
//   1. LoadGlobalUVE()          (dlopen loader, global entry points)
//   2. vkCreateInstance         (with the window's required WSI extensions)
//   3. Physical device pickup   (first discrete-or-integrated GPU with a graphics+present queue)
//   4. vkCreateDevice           (single queue family, VK_KHR_swapchain enabled)
//   5. Window surface           (through the Window::IVulkanWindowSurfaceUVE capability bridge;
//                                opaque handles at the boundary, real VkSurfaceKHR here)
//   6. Swapchain + image views  (FIFO present mode, 8-bit RGBA/BGRA sRGB surface pick)
//   7. Render pass / framebuffers / per-frame sync (one frame in flight — header documents why)
//
// Every step's failure path is a logged bail returning nullptr from CreateUVE(); the destructor
// destroys strictly in reverse construction order, so a partially-completed bring-up is torn
// down by precisely the members that finished.
//
// Clear-colour provenance: the M1 device presents an engineering "visible life" clear — a
// muted blue-grey — as its only frame content. Deterministic, self-documenting in a screenshot,
// and impossible to confuse with real scene output; successive milestones replace it once the
// pipeline graph exists.


#include "uve/rhi_vulkan/vulkan_render_device_uve.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <span>
#include <vector>

#include <vulkan/vulkan_core.h>

#include "uve/logging/logging_macros_uve.h"
#include "uve/window/i_vulkan_window_surface_uve.h"
#include "vk_functions_uve.h"

namespace UVE::Render {
namespace {

// The M1 engineering clear colour: muted blue-grey, linear-space shortcuts acceptable here
// because the surface format is sRGB and the values are literal displays of "the device works".
constexpr float kBootstrapClearRedUVE = 0.05F;
constexpr float kBootstrapClearGreenUVE = 0.07F;
constexpr float kBootstrapClearBlueUVE = 0.12F;
constexpr float kBootstrapClearAlphaUVE = 1.0F;

[[nodiscard]] VkSurfaceKHR ToVkSurfaceUVE(const std::uintptr_t bits) noexcept {
    // The bridge contract: 0 = failure/unavailable; otherwise the exact VkSurfaceKHR bits or
    // pointer bytes. Vulkan's headers define non-dispatchable handles two ways — an opaque
    // struct pointer when VK_USE_64_BIT_PTR_DEFINES == 1 (the default on 64-bit builds) or a
    // plain uint64_t when 0 — and the bridge is ABI-safe either way, since both fit uintptr_t.
    return std::bit_cast<VkSurfaceKHR>(bits); // both forms are exactly pointer-sized/uint64-sized
}

} // namespace

struct VulkanRenderDeviceUVE::ImplUVE {
    ImplUVE(Window::IWindowManagerUVE* windowManagerIn, Window::IVulkanWindowSurfaceUVE* bridgeIn)
        : windowManager(windowManagerIn), bridge(bridgeIn) {}

    Window::IWindowManagerUVE* windowManager; // nullable: bridge-direct ("headless") construction
    Window::IVulkanWindowSurfaceUVE* bridge = nullptr;

    VkFunctionsUVE vk;

    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    std::uint32_t queueFamilyIndex = 0;
    VkQueue presentQueue = VK_NULL_HANDLE; // graphics-capable family also used for present
    VkSurfaceKHR surface = VK_NULL_HANDLE;

    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkFormat swapchainFormat = VK_FORMAT_UNDEFINED;
    VkExtent2D swapchainExtent{0U, 0U};
    std::vector<VkImage> swapchainImages;
    std::vector<VkImageView> swapchainImageViews;
    VkRenderPass renderPass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer> framebuffers;

    VkCommandPool commandPool = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkSemaphore renderFinishedSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;

    bool usable = false; // signed-off only by the very last bring-up step
    std::uint32_t lastPresentedImageIndex = UINT32_MAX; // UINT32_MAX = no frame presented yet

    [[nodiscard]] bool LogBailUVE(const char* reason) {
        UVE_WARNING("VulkanRenderDeviceUVE: {}", reason);
        return false;
    }

    void DestroySwapchainResourcesUVE();
    [[nodiscard]] bool CreateSwapchainResourcesUVE();
    [[nodiscard]] bool InitializeUVE();
};

void VulkanRenderDeviceUVE::ImplUVE::DestroySwapchainResourcesUVE() {
    // Caller guarantees the queue has been drained (vkQueueWaitIdle) before entry.
    for (const VkFramebuffer framebuffer : framebuffers) {
        if (framebuffer != VK_NULL_HANDLE) {
            vk.vkDestroyFramebuffer(device, framebuffer, nullptr);
        }
    }
    framebuffers.clear();
    // The render pass outlives individual recreations (it's format-dependent, not
    // extent-dependent) — destroyed only from the destructor, never here.
    for (const VkImageView imageView : swapchainImageViews) {
        if (imageView != VK_NULL_HANDLE) {
            vk.vkDestroyImageView(device, imageView, nullptr);
        }
    }
    swapchainImageViews.clear();
    swapchainImages.clear(); // the swapchain owns the images themselves; nothing to destroy here
    if (swapchain != VK_NULL_HANDLE) {
        vk.vkDestroySwapchainKHR(device, swapchain, nullptr);
        swapchain = VK_NULL_HANDLE;
    }
    swapchainExtent = {0U, 0U};
}

bool VulkanRenderDeviceUVE::ImplUVE::CreateSwapchainResourcesUVE() {
    // Extent: the surface capabilities win over the window's raw framebuffer size whenever the
    // driver reports a fixed extent (non-minimized Wayland and ioctl'd DRM clients do); only
    // the "currentExtent must be chosen" sentinel uses the framebuffer size, clamped into the
    // capabilities range.
    VkSurfaceCapabilitiesKHR capabilities{};
    if (vk.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physicalDevice, surface, &capabilities) != VK_SUCCESS) {
        return LogBailUVE("swapchain recreation: surface capabilities query failed");
    }
    VkExtent2D extent = capabilities.currentExtent;
    if (extent.width == 0xFFFFFFFFU) { // VK_WHOLE_SURFACE sentinel — pick from the window.
        std::uint32_t width = 0;
        std::uint32_t height = 0;
        bridge->GetVulkanFramebufferSizeUVE(width, height);
        extent.width = std::max(capabilities.minImageExtent.width,
                                std::min(capabilities.maxImageExtent.width, width));
        extent.height = std::max(capabilities.minImageExtent.height,
                                 std::min(capabilities.maxImageExtent.height, height));
    }
    if (extent.width == 0U || extent.height == 0U) {
        return LogBailUVE("swapchain recreation: zero-sized extent (minimized window?); refusing to build");
    }

    std::uint32_t imageCount = capabilities.minImageCount + 1U; // one ahead of the driver minimum
    if (capabilities.maxImageCount > 0U && imageCount > capabilities.maxImageCount) {
        imageCount = capabilities.maxImageCount;
    }

    VkSwapchainCreateInfoKHR swapchainInfo{};
    swapchainInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchainInfo.surface = surface;
    swapchainInfo.minImageCount = imageCount;
    swapchainInfo.imageFormat = swapchainFormat;
    swapchainInfo.imageColorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    swapchainInfo.imageExtent = extent;
    swapchainInfo.imageArrayLayers = 1U;
    swapchainInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchainInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE; // one queue family total (M1)
    swapchainInfo.preTransform = capabilities.currentTransform;
    swapchainInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    swapchainInfo.presentMode = VK_PRESENT_MODE_FIFO_KHR; // guaranteed present; FIFO == vsync
    swapchainInfo.clipped = VK_TRUE;
    swapchainInfo.oldSwapchain = VK_NULL_HANDLE;

    VkSwapchainKHR newSwapchain = VK_NULL_HANDLE;
    if (vk.vkCreateSwapchainKHR(device, &swapchainInfo, nullptr, &newSwapchain) != VK_SUCCESS ||
        newSwapchain == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateSwapchainKHR failed");
    }
    swapchain = newSwapchain;
    swapchainExtent = extent;

    std::uint32_t actualImageCount = 0;
    vk.vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, nullptr);
    swapchainImages.resize(actualImageCount);
    vk.vkGetSwapchainImagesKHR(device, swapchain, &actualImageCount, swapchainImages.data());

    swapchainImageViews.resize(actualImageCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < actualImageCount; ++index) {
        VkImageViewCreateInfo viewInfo{};
        viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        viewInfo.image = swapchainImages[index];
        viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
        viewInfo.format = swapchainFormat;
        viewInfo.components.r = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.g = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.b = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.components.a = VK_COMPONENT_SWIZZLE_IDENTITY;
        viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        viewInfo.subresourceRange.baseMipLevel = 0U;
        viewInfo.subresourceRange.levelCount = 1U;
        viewInfo.subresourceRange.baseArrayLayer = 0U;
        viewInfo.subresourceRange.layerCount = 1U;
        if (vk.vkCreateImageView(device, &viewInfo, nullptr, &swapchainImageViews[index]) != VK_SUCCESS) {
            return LogBailUVE("vkCreateImageView for a swapchain image failed");
        }
    }

    framebuffers.resize(actualImageCount, VK_NULL_HANDLE);
    for (std::uint32_t index = 0; index < actualImageCount; ++index) {
        VkFramebufferCreateInfo framebufferInfo{};
        framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebufferInfo.renderPass = renderPass;
        framebufferInfo.attachmentCount = 1U;
        framebufferInfo.pAttachments = &swapchainImageViews[index];
        framebufferInfo.width = extent.width;
        framebufferInfo.height = extent.height;
        framebufferInfo.layers = 1U;
        if (vk.vkCreateFramebuffer(device, &framebufferInfo, nullptr, &framebuffers[index]) != VK_SUCCESS) {
            return LogBailUVE("vkCreateFramebuffer for a swapchain image failed");
        }
    }
    return true;
}

bool VulkanRenderDeviceUVE::ImplUVE::InitializeUVE() {
    // --- bridge capability: accepted directly or RTTI-queried off the window manager --------
    if (bridge == nullptr) {
        bridge = dynamic_cast<Window::IVulkanWindowSurfaceUVE*>(windowManager);
        if (bridge == nullptr) {
            return LogBailUVE("window manager offers no IVulkanWindowSurfaceUVE capability "
                              "(headless or stub backend); Vulkan windowed rendering needs it");
        }
    }
    if (windowManager != nullptr && !windowManager->IsValidUVE()) {
        return LogBailUVE("window manager is not valid; Vulkan needs a real window first");
    }

    // --- global level ----------------------------------------------------------------------
    if (!vk.LoadGlobalUVE()) {
        return LogBailUVE("Vulkan global entry points failed to resolve");
    }

    // API version: prefer the loader-reported ceiling, but only the MAJOR.MINOR the M1 code
    // was written against — newer minors remain valid to request at 1.x (loader validates).
    std::uint32_t apiVersion = VK_API_VERSION_1_0;
    if (vk.vkEnumerateInstanceVersion != nullptr &&
        vk.vkEnumerateInstanceVersion(&apiVersion) != VK_SUCCESS) {
        apiVersion = VK_API_VERSION_1_0; // query present but failed: pin to the guaranteed floor
    }
    if (apiVersion > VK_API_VERSION_1_3) {
        apiVersion = VK_API_VERSION_1_3;
    }

    // --- instance --------------------------------------------------------------------------
    const std::vector<const char*> instanceExtensions = bridge->GetRequiredVulkanInstanceExtensionsUVE();
    if (instanceExtensions.empty()) {
        return LogBailUVE("surface bridge reported no required Vulkan instance extensions — "
                          "Vulkan WSI support is unavailable on this platform");
    }

    VkApplicationInfo applicationInfo{};
    applicationInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    applicationInfo.pApplicationName = "UniVex Engine";
    applicationInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.pEngineName = "UniVex Engine";
    applicationInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
    applicationInfo.apiVersion = apiVersion;

    VkInstanceCreateInfo instanceInfo{};
    instanceInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    instanceInfo.pApplicationInfo = &applicationInfo;
    instanceInfo.enabledExtensionCount = static_cast<std::uint32_t>(instanceExtensions.size());
    instanceInfo.ppEnabledExtensionNames = instanceExtensions.data();
    // No validation layer is ever requested by engine code: correctness tooling belongs to the
    // developer's environment (VK_INSTANCE_LAYERS), never to a shipped engine's defaults.
    if (vk.vkCreateInstance(&instanceInfo, nullptr, &instance) != VK_SUCCESS || instance == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateInstance failed (is an ICD/driver installed?)");
    }
    if (!vk.LoadInstanceUVE(instance)) {
        return LogBailUVE("Vulkan instance-level entry points failed to resolve");
    }

    // --- window surface --------------------------------------------------------------------
    surface = ToVkSurfaceUVE(bridge->CreateVulkanWindowSurfaceUVE(
        reinterpret_cast<std::uintptr_t>(instance)));
    if (surface == VK_NULL_HANDLE) {
        return LogBailUVE("window surface creation failed through the surface bridge");
    }

    // --- physical device + one queue family that both draws and presents --------------------
    std::uint32_t physicalCount = 0;
    if (vk.vkEnumeratePhysicalDevices(instance, &physicalCount, nullptr) != VK_SUCCESS || physicalCount == 0U) {
        return LogBailUVE("no Vulkan physical devices found");
    }
    std::vector<VkPhysicalDevice> physicalDevices(physicalCount);
    vk.vkEnumeratePhysicalDevices(instance, &physicalCount, physicalDevices.data());

    bool found = false;
    for (const VkPhysicalDevice candidate : physicalDevices) {
        std::uint32_t queueFamilyCount = 0;
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, nullptr);
        std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
        vk.vkGetPhysicalDeviceQueueFamilyProperties(candidate, &queueFamilyCount, queueFamilies.data());
        for (std::uint32_t family = 0; family < queueFamilyCount; ++family) {
            if ((queueFamilies[family].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0U) {
                continue;
            }
            VkBool32 presentSupported = VK_FALSE;
            if (vk.vkGetPhysicalDeviceSurfaceSupportKHR(candidate, family, surface, &presentSupported) != VK_SUCCESS ||
                presentSupported != VK_TRUE) {
                continue;
            }
            // Swapchain extension must be advertised by this physical device.
            std::uint32_t extensionCount = 0;
            vk.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, nullptr);
            std::vector<VkExtensionProperties> extensions(extensionCount);
            vk.vkEnumerateDeviceExtensionProperties(candidate, nullptr, &extensionCount, extensions.data());
            const bool hasSwapchain = std::any_of(extensions.begin(), extensions.end(),
                [](const VkExtensionProperties& extension) {
                    return std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0;
                });
            if (!hasSwapchain) {
                continue;
            }
            physicalDevice = candidate;
            queueFamilyIndex = family;
            found = true;
            break;
        }
        if (found) {
            break;
        }
    }
    if (!found) {
        return LogBailUVE("no physical device with a graphics+present queue family and "
                          "VK_KHR_swapchain was found");
    }

    VkPhysicalDeviceProperties deviceProperties{};
    vk.vkGetPhysicalDeviceProperties(physicalDevice, &deviceProperties);
    UVE_INFO("VulkanRenderDeviceUVE: selected physical device \"{}\"", deviceProperties.deviceName);

    // --- logical device --------------------------------------------------------------------
    const float queuePriority = 1.0F;
    VkDeviceQueueCreateInfo queueInfo{};
    queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueInfo.queueFamilyIndex = queueFamilyIndex;
    queueInfo.queueCount = 1U;
    queueInfo.pQueuePriorities = &queuePriority;

    static constexpr const char* kDeviceExtensions[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
    VkDeviceCreateInfo deviceInfo{};
    deviceInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    deviceInfo.queueCreateInfoCount = 1U;
    deviceInfo.pQueueCreateInfos = &queueInfo;
    deviceInfo.enabledExtensionCount = 1U;
    deviceInfo.ppEnabledExtensionNames = kDeviceExtensions;

    if (vk.vkCreateDevice(physicalDevice, &deviceInfo, nullptr, &device) != VK_SUCCESS || device == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateDevice failed");
    }
    if (!vk.LoadDeviceUVE(device)) {
        return LogBailUVE("Vulkan device-level entry points failed to resolve");
    }
    vk.vkGetDeviceQueue(device, queueFamilyIndex, 0U, &presentQueue);

    // --- surface format/present-mode picks ---------------------------------------------------
    std::uint32_t formatCount = 0;
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vk.vkGetPhysicalDeviceSurfaceFormatsKHR(physicalDevice, surface, &formatCount, formats.data());
    VkSurfaceFormatKHR chosenFormat{};
    bool formatChosen = false;
    for (const VkSurfaceFormatKHR& format : formats) {
        if ((format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB) &&
            format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            chosenFormat = format;
            formatChosen = true;
            break;
        }
    }
    if (!formatChosen && !formats.empty()) {
        chosenFormat = formats.front(); // any advertised format beats failing the backend
        formatChosen = true;
    }
    if (!formatChosen) {
        return LogBailUVE("surface advertises no usable formats");
    }
    swapchainFormat = chosenFormat.format;

    // FIFO is mandatory by spec — no present-mode enumeration needed for M1 (mailbox relaxes
    // vsync; FIFO matches the GL device default exactly).

    // --- render pass + swapchain -------------------------------------------------------------
    VkAttachmentDescription colorAttachment{};
    colorAttachment.format = swapchainFormat;
    colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
    colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;   // the M1 "render": an initial clear
    colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE; // presented afterwards, so keep the bits
    colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

    VkAttachmentReference colorReference{};
    colorReference.attachment = 0U;
    colorReference.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1U;
    subpass.pColorAttachments = &colorReference;

    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0U;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0U;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo renderPassInfo{};
    renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    renderPassInfo.attachmentCount = 1U;
    renderPassInfo.pAttachments = &colorAttachment;
    renderPassInfo.subpassCount = 1U;
    renderPassInfo.pSubpasses = &subpass;
    renderPassInfo.dependencyCount = 1U;
    renderPassInfo.pDependencies = &dependency;

    if (vk.vkCreateRenderPass(device, &renderPassInfo, nullptr, &renderPass) != VK_SUCCESS || renderPass == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateRenderPass failed");
    }

    if (!CreateSwapchainResourcesUVE()) {
        return false; // already logged inside
    }

    // --- command pool + one-shot command buffer (single frame in flight) --------------------
    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = queueFamilyIndex;
    if (vk.vkCreateCommandPool(device, &poolInfo, nullptr, &commandPool) != VK_SUCCESS || commandPool == VK_NULL_HANDLE) {
        return LogBailUVE("vkCreateCommandPool failed");
    }

    VkCommandBufferAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocateInfo.commandPool = commandPool;
    allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocateInfo.commandBufferCount = 1U;
    if (vk.vkAllocateCommandBuffers(device, &allocateInfo, &commandBuffer) != VK_SUCCESS || commandBuffer == VK_NULL_HANDLE) {
        return LogBailUVE("vkAllocateCommandBuffers failed");
    }

    // --- synchronization: one image-available semaphore, one render-finished semaphore,
    //     one in-flight fence — deliberately the 1-in-flight shape the header documents. -----
    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT; // first PresentUVE()'s wait returns instantly
    if (vk.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &imageAvailableSemaphore) != VK_SUCCESS ||
        vk.vkCreateSemaphore(device, &semaphoreInfo, nullptr, &renderFinishedSemaphore) != VK_SUCCESS ||
        vk.vkCreateFence(device, &fenceInfo, nullptr, &inFlightFence) != VK_SUCCESS) {
        return LogBailUVE("sync primitive creation failed");
    }

    usable = true;
    UVE_INFO("VulkanRenderDeviceUVE: M1 bootstrap initialized ({}x{}, format {}, {} swapchain images)",
        swapchainExtent.width, swapchainExtent.height,
        static_cast<int>(swapchainFormat), swapchainImages.size());
    return true;
}

VulkanRenderDeviceUVE::VulkanRenderDeviceUVE(Window::IWindowManagerUVE* windowManager,
                                             Window::IVulkanWindowSurfaceUVE* bridge)
    : m_impl(std::make_unique<ImplUVE>(windowManager, bridge)) {}

VulkanRenderDeviceUVE::~VulkanRenderDeviceUVE() {
    if (m_impl->device != VK_NULL_HANDLE) {
        // Everything below requires a drained queue — never destroy objects mid-frame.
        if (m_impl->presentQueue != VK_NULL_HANDLE) {
            m_impl->vk.vkQueueWaitIdle(m_impl->presentQueue);
        }
        if (m_impl->inFlightFence != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroyFence(m_impl->device, m_impl->inFlightFence, nullptr);
        }
        if (m_impl->renderFinishedSemaphore != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroySemaphore(m_impl->device, m_impl->renderFinishedSemaphore, nullptr);
        }
        if (m_impl->imageAvailableSemaphore != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroySemaphore(m_impl->device, m_impl->imageAvailableSemaphore, nullptr);
        }
        if (m_impl->commandPool != VK_NULL_HANDLE) {
            // Destroying the pool implicitly frees every command buffer allocated from it —
            // an explicit vkFreeCommandBuffers call for commandBuffer is therefore omitted.
            m_impl->vk.vkDestroyCommandPool(m_impl->device, m_impl->commandPool, nullptr);
        }
        m_impl->DestroySwapchainResourcesUVE();
        if (m_impl->renderPass != VK_NULL_HANDLE) {
            m_impl->vk.vkDestroyRenderPass(m_impl->device, m_impl->renderPass, nullptr);
        }
        m_impl->vk.vkDestroyDevice(m_impl->device, nullptr);
    }
    if (m_impl->surface != VK_NULL_HANDLE && m_impl->instance != VK_NULL_HANDLE) {
        // Surface destruction goes through this device's own function table, not the bridge
        // (see DestroyVulkanWindowSurfaceUVE's documented bridge no-op contract).
        m_impl->vk.vkDestroySurfaceKHR(m_impl->instance, m_impl->surface, nullptr);
    }
    if (m_impl->instance != VK_NULL_HANDLE) {
        m_impl->vk.vkDestroyInstance(m_impl->instance, nullptr);
    }
}

std::unique_ptr<VulkanRenderDeviceUVE> VulkanRenderDeviceUVE::CreateUVE(
    Window::IWindowManagerUVE& windowManager) {
    auto device = std::unique_ptr<VulkanRenderDeviceUVE>(
        new VulkanRenderDeviceUVE(&windowManager, nullptr));
    if (!device->m_impl->InitializeUVE()) {
        // Partially-constructed state is torn down by the destructor — every bail in
        // InitializeUVE() has already logged its own reason.
        return nullptr;
    }
    return device;
}

std::unique_ptr<VulkanRenderDeviceUVE> VulkanRenderDeviceUVE::CreateFromBridgeUVE(
    Window::IVulkanWindowSurfaceUVE& surfaceBridge) {
    auto device = std::unique_ptr<VulkanRenderDeviceUVE>(
        new VulkanRenderDeviceUVE(nullptr, &surfaceBridge));
    if (!device->m_impl->InitializeUVE()) {
        return nullptr; // partially-initialized state torn down by the destructor
    }
    return device;
}

// ---------------------------------------------------------------------------
// M1-out-of-scope resource methods: honest invalid/false/empty results, one
// distinct log line each so the caller sees WHICH milestone gap it hit.
// ---------------------------------------------------------------------------

BufferHandleUVE VulkanRenderDeviceUVE::CreateBufferUVE(const BufferDescUVE& /*desc*/,
                                                       std::span<const std::byte> /*initialData*/) {
    UVE_WARNING("VulkanRenderDeviceUVE::CreateBufferUVE: buffer resources are a later milestone (M1 is bootstrapping only)");
    return kInvalidBufferHandleUVE;
}

void VulkanRenderDeviceUVE::DestroyBufferUVE(BufferHandleUVE /*buffer*/) {}

bool VulkanRenderDeviceUVE::UpdateBufferUVE(BufferHandleUVE /*buffer*/,
                                            std::span<const std::byte> /*data*/,
                                            std::size_t /*offset*/) {
    return false; // silent: a buffer never exists for this device to update
}

TextureHandleUVE VulkanRenderDeviceUVE::CreateTextureUVE(const TextureDescUVE& /*desc*/,
                                                         std::span<const std::byte> /*initialData*/) {
    UVE_WARNING("VulkanRenderDeviceUVE::CreateTextureUVE: texture resources are a later milestone (M1 is bootstrapping only)");
    return kInvalidTextureHandleUVE;
}

void VulkanRenderDeviceUVE::DestroyTextureUVE(TextureHandleUVE /*texture*/) {}

ShaderHandleUVE VulkanRenderDeviceUVE::CreateShaderUVE(const ShaderDescUVE& /*desc*/,
                                                       std::string* outInfoLog) {
    if (outInfoLog != nullptr) {
        *outInfoLog = "Vulkan M1 device does not compile shaders yet (later milestone)";
    }
    return kInvalidShaderHandleUVE;
}

void VulkanRenderDeviceUVE::DestroyShaderUVE(ShaderHandleUVE /*shader*/) {}

PipelineHandleUVE VulkanRenderDeviceUVE::CreatePipelineUVE(const PipelineDescUVE& /*desc*/,
                                                           std::string* outInfoLog) {
    if (outInfoLog != nullptr) {
        *outInfoLog = "Vulkan M1 device does not create pipelines yet (later milestone)";
    }
    return kInvalidPipelineHandleUVE;
}

void VulkanRenderDeviceUVE::DestroyPipelineUVE(PipelineHandleUVE /*pipeline*/) {}

std::vector<UniformReflectionUVE> VulkanRenderDeviceUVE::GetPipelineUniformsUVE(
    PipelineHandleUVE /*pipeline*/) const {
    return {};
}

bool VulkanRenderDeviceUVE::GetPipelineBinaryUVE(PipelineHandleUVE /*pipeline*/,
                                                 std::vector<std::byte>& /*outBinary*/,
                                                 std::uint32_t& /*outFormat*/) const {
    return false;
}

PipelineHandleUVE VulkanRenderDeviceUVE::CreatePipelineFromBinaryUVE(std::span<const std::byte> /*binary*/,
                                                                     std::uint32_t /*format*/,
                                                                     const PipelineBinaryDescUVE& /*desc*/) {
    // Not logged as loudly as the other M1 gaps: the shader-cache caller treats a miss here as
    // an ordinary cache miss (documented in IRenderDeviceUVE) — log at info level for traces
    // but keep the warning channel reserved for truly unimplemented paths.
    UVE_INFO("VulkanRenderDeviceUVE::CreatePipelineFromBinaryUVE: no Vulkan pipeline binaries yet (M1)");
    return kInvalidPipelineHandleUVE;
}

std::unique_ptr<ICommandBufferUVE> VulkanRenderDeviceUVE::CreateCommandBufferUVE() {
    UVE_WARNING("VulkanRenderDeviceUVE::CreateCommandBufferUVE: recorded command buffers are a "
                "later milestone (M1 renders its own internal clear-present command buffer)");
    return nullptr;
}

void VulkanRenderDeviceUVE::SubmitUVE(std::unique_ptr<ICommandBufferUVE> /*commandBuffer*/) {}

void VulkanRenderDeviceUVE::PresentUVE() {
    ImplUVE& impl = *m_impl;
    if (!impl.usable) {
        return;
    }

    // Single frame in flight: wait for the previous frame's fence, but NEVER reset it yet —
    // every bail-out below (minimized window, acquire failure, mid-frame error) must leave the
    // fence still signaled by the previous frame's submit so the next PresentUVE()'s wait also
    // completes. Only the path that truly reaches vkQueueSubmit resets it first. (Simplicity
    // over pipelining is the documented M1 trade-off; the fence starts signaled at creation so
    // the very first frame also passes.)
    (void)impl.vk.vkWaitForFences(impl.device, 1U, &impl.inFlightFence, VK_TRUE, UINT64_MAX);

    // Minimized window: skip the frame silently — mirrors the GL device contract that size
    // changes come from the WindowResizedEventUVE poll and zero-size means "not drawable".
    std::uint32_t framebufferWidth = 0;
    std::uint32_t framebufferHeight = 0;
    impl.bridge->GetVulkanFramebufferSizeUVE(framebufferWidth, framebufferHeight);
    if (framebufferWidth == 0U || framebufferHeight == 0U) {
        return;
    }
    if (framebufferWidth != impl.swapchainExtent.width ||
        framebufferHeight != impl.swapchainExtent.height) {
        // Same pattern as GlRenderDeviceUVE's on-resize FBO rebuild: drain, destroy, recreate.
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false; // logged inside; device falls back to inert for later frames
            return;
        }
    }

    std::uint32_t imageIndex = 0;
    const VkResult acquireResult = impl.vk.vkAcquireNextImageKHR(
        impl.device, impl.swapchain, UINT64_MAX, impl.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
    if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR) {
        // Driver-side repaint judgment (e.g. display mode change): rebuild now, retry next frame.
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false;
        }
        return; // fence left signaled — see the invariant at the top of this method
    }
    if (acquireResult != VK_SUCCESS && acquireResult != VK_SUBOPTIMAL_KHR) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkAcquireNextImageKHR failed (result {})",
                    static_cast<int>(acquireResult));
        return;
    }

    // Committing to a submit this frame: reset the fence to unsignaled for the queue to signal.
    (void)impl.vk.vkResetFences(impl.device, 1U, &impl.inFlightFence);
    (void)impl.vk.vkResetCommandBuffer(impl.commandBuffer, 0U); // pool has RESET_COMMAND_BUFFER_BIT

    // Record this frame's clear: a one-shot command buffer bound to the acquired image.
    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    if (impl.vk.vkBeginCommandBuffer(impl.commandBuffer, &beginInfo) != VK_SUCCESS) {
        // Fence is already reset for this frame and no submit will re-signal it; the device
        // goes inert here so no later frame deadlocks waiting for that fence (recovering is an
        // M2 concern — a record-time failure indicates driver trouble, not transient state).
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkBeginCommandBuffer failed; device going inert");
        impl.usable = false;
        return;
    }
    VkClearValue clearValue{};
    clearValue.color.float32[0] = kBootstrapClearRedUVE;
    clearValue.color.float32[1] = kBootstrapClearGreenUVE;
    clearValue.color.float32[2] = kBootstrapClearBlueUVE;
    clearValue.color.float32[3] = kBootstrapClearAlphaUVE;
    VkRenderPassBeginInfo renderPassBegin{};
    renderPassBegin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    renderPassBegin.renderPass = impl.renderPass;
    renderPassBegin.framebuffer = impl.framebuffers[imageIndex];
    renderPassBegin.renderArea.offset = {0, 0};
    renderPassBegin.renderArea.extent = impl.swapchainExtent;
    renderPassBegin.clearValueCount = 1U;
    renderPassBegin.pClearValues = &clearValue;
    impl.vk.vkCmdBeginRenderPass(impl.commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);
    // M1 records NOTHING between begin/end — attachment's LOAD_OP_CLEAR is the whole frame.
    impl.vk.vkCmdEndRenderPass(impl.commandBuffer);
    if (impl.vk.vkEndCommandBuffer(impl.commandBuffer) != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkEndCommandBuffer failed; device going inert");
        impl.usable = false;
        return;
    }

    VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submitInfo{};
    submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submitInfo.waitSemaphoreCount = 1U;
    submitInfo.pWaitSemaphores = &impl.imageAvailableSemaphore;
    submitInfo.pWaitDstStageMask = &waitStage;
    submitInfo.commandBufferCount = 1U;
    submitInfo.pCommandBuffers = &impl.commandBuffer;
    submitInfo.signalSemaphoreCount = 1U;
    submitInfo.pSignalSemaphores = &impl.renderFinishedSemaphore;
    if (impl.vk.vkQueueSubmit(impl.presentQueue, 1U, &submitInfo, impl.inFlightFence) != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkQueueSubmit failed; device going inert");
        impl.usable = false;
        return;
    }

    VkPresentInfoKHR presentInfo{};
    presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    presentInfo.waitSemaphoreCount = 1U;
    presentInfo.pWaitSemaphores = &impl.renderFinishedSemaphore;
    presentInfo.swapchainCount = 1U;
    presentInfo.pSwapchains = &impl.swapchain;
    presentInfo.pImageIndices = &imageIndex;
    const VkResult presentResult = impl.vk.vkQueuePresentKHR(impl.presentQueue, &presentInfo);
    if (presentResult == VK_SUCCESS || presentResult == VK_SUBOPTIMAL_KHR) {
        impl.lastPresentedImageIndex = imageIndex;
    }
    if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || presentResult == VK_SUBOPTIMAL_KHR) {
        // Resize visible to the driver after acquire: rebuild now so next frame uses a valid
        // swapchain (the just-completed present attempt was still in sync — the fence stands).
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        impl.DestroySwapchainResourcesUVE();
        if (!impl.CreateSwapchainResourcesUVE()) {
            impl.usable = false;
        }
    } else if (presentResult != VK_SUCCESS) {
        UVE_WARNING("VulkanRenderDeviceUVE::PresentUVE: vkQueuePresentKHR failed (result {})",
                    static_cast<int>(presentResult));
    }
}

bool VulkanRenderDeviceUVE::IsUsableUVE() const noexcept {
    return m_impl->usable;
}

bool VulkanRenderDeviceUVE::ReadbackLatestPresentedImageUVE(std::span<std::byte> outRGBA8,
                                                            std::uint32_t& outWidth,
                                                            std::uint32_t& outHeight) {
    ImplUVE& impl = *m_impl;
    outWidth = 0U;
    outHeight = 0U;

    const auto fail = [](const char* reason) {
        UVE_WARNING("VulkanRenderDeviceUVE::ReadbackLatestPresentedImageUVE: {}", reason);
        return false;
    };

    if (!impl.usable) {
        return fail("device is not usable");
    }
    if (impl.lastPresentedImageIndex == UINT32_MAX ||
        impl.lastPresentedImageIndex >= impl.swapchainImages.size()) {
        return fail("no frame has been presented yet");
    }
    const std::size_t requiredBytes =
        static_cast<std::size_t>(impl.swapchainExtent.width) * impl.swapchainExtent.height * 4U;
    if (outRGBA8.size() != requiredBytes) {
        // Fill the extent out-parameters even on this documented failure: drivers that lock the
        // surface extent to their own pick (SwiftShader's headless surface, for one) mean the
        // real size is only knowable here, and the two-call pattern (query with an empty span,
        // resize, call again) must stay possible for cold-path tooling.
        outWidth = impl.swapchainExtent.width;
        outHeight = impl.swapchainExtent.height;
        return fail("output span must be exactly width*height*4 bytes of the framebuffer extent");
    }
    outWidth = impl.swapchainExtent.width;
    outHeight = impl.swapchainExtent.height;

    const VkImage sourceImage = impl.swapchainImages[impl.lastPresentedImageIndex];

    // Staging buffer: host-visible since the goal is a CPU-side pixel copy, never performance.
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = requiredBytes;
    bufferInfo.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    VkBuffer stagingBuffer = VK_NULL_HANDLE;
    if (impl.vk.vkCreateBuffer(impl.device, &bufferInfo, nullptr, &stagingBuffer) != VK_SUCCESS) {
        return fail("vkCreateBuffer for the staging buffer failed");
    }
    VkMemoryRequirements memoryRequirements{};
    impl.vk.vkGetBufferMemoryRequirements(impl.device, stagingBuffer, &memoryRequirements);

    VkPhysicalDeviceMemoryProperties memoryProperties{};
    impl.vk.vkGetPhysicalDeviceMemoryProperties(impl.physicalDevice, &memoryProperties);
    std::uint32_t memoryTypeIndex = UINT32_MAX;
    for (std::uint32_t index = 0; index < memoryProperties.memoryTypeCount; ++index) {
        const VkMemoryPropertyFlags wanted = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        if ((memoryRequirements.memoryTypeBits & (1U << index)) != 0U &&
            (memoryProperties.memoryTypes[index].propertyFlags & wanted) == wanted) {
            memoryTypeIndex = index;
            break;
        }
    }
    if (memoryTypeIndex == UINT32_MAX) {
        impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        return fail("no host-visible+coherent memory type reported by the physical device");
    }
    VkMemoryAllocateInfo allocateInfo{};
    allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocateInfo.allocationSize = memoryRequirements.size;
    allocateInfo.memoryTypeIndex = memoryTypeIndex;
    VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
    if (impl.vk.vkAllocateMemory(impl.device, &allocateInfo, nullptr, &stagingMemory) != VK_SUCCESS) {
        impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
        return fail("vkAllocateMemory for the staging buffer failed");
    }
    impl.vk.vkBindBufferMemory(impl.device, stagingBuffer, stagingMemory, 0U);

    // Transient command buffer: PRESENT_SRC -> TRANSFER_SRC barrier, copy, barrier back.
    VkCommandBuffer copyCommands = VK_NULL_HANDLE;
    VkCommandBufferAllocateInfo commandAllocateInfo{};
    commandAllocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    commandAllocateInfo.commandPool = impl.commandPool;
    commandAllocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    commandAllocateInfo.commandBufferCount = 1U;
    bool ok = impl.vk.vkAllocateCommandBuffers(impl.device, &commandAllocateInfo, &copyCommands) == VK_SUCCESS;
    if (ok) {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        ok = impl.vk.vkBeginCommandBuffer(copyCommands, &beginInfo) == VK_SUCCESS;
    }
    if (ok) {
        VkImageMemoryBarrier toTransfer{};
        toTransfer.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        toTransfer.srcAccessMask = 0U; // queue drained below — nothing pending to order against
        toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        toTransfer.oldLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toTransfer.image = sourceImage;
        toTransfer.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        toTransfer.subresourceRange.baseMipLevel = 0U;
        toTransfer.subresourceRange.levelCount = 1U;
        toTransfer.subresourceRange.baseArrayLayer = 0U;
        toTransfer.subresourceRange.layerCount = 1U;
        impl.vk.vkCmdPipelineBarrier(copyCommands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &toTransfer);

        VkBufferImageCopy copyRegion{};
        copyRegion.bufferOffset = 0U;
        copyRegion.bufferRowLength = 0U;  // tightly packed
        copyRegion.bufferImageHeight = 0U;
        copyRegion.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copyRegion.imageSubresource.mipLevel = 0U;
        copyRegion.imageSubresource.baseArrayLayer = 0U;
        copyRegion.imageSubresource.layerCount = 1U;
        copyRegion.imageOffset = {0, 0, 0};
        copyRegion.imageExtent = {impl.swapchainExtent.width, impl.swapchainExtent.height, 1U};
        impl.vk.vkCmdCopyImageToBuffer(copyCommands, sourceImage,
            VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, stagingBuffer, 1U, &copyRegion);

        VkImageMemoryBarrier backToPresent = toTransfer;
        backToPresent.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        backToPresent.dstAccessMask = 0U;
        backToPresent.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
        backToPresent.newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        impl.vk.vkCmdPipelineBarrier(copyCommands, VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_TRANSFER_BIT, 0U, 0U, nullptr, 0U, nullptr, 1U, &backToPresent);

        ok = impl.vk.vkEndCommandBuffer(copyCommands) == VK_SUCCESS;
    }
    if (ok) {
        // Drain first so the frame fence stays meaningful, then queue the readback on the
        // same fence (the in-flight fence is stored-present-state, so reuse is deliberate:
        // next PresentUVE() will wait on exactly this submission's completion as well).
        (void)impl.vk.vkQueueWaitIdle(impl.presentQueue);
        (void)impl.vk.vkResetFences(impl.device, 1U, &impl.inFlightFence);
        VkSubmitInfo submitInfo{};
        submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submitInfo.commandBufferCount = 1U;
        submitInfo.pCommandBuffers = &copyCommands;
        ok = impl.vk.vkQueueSubmit(impl.presentQueue, 1U, &submitInfo, impl.inFlightFence) == VK_SUCCESS;
    }
    if (ok) {
        ok = impl.vk.vkWaitForFences(impl.device, 1U, &impl.inFlightFence, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    }
    if (ok) {
        void* mapped = nullptr;
        if (impl.vk.vkMapMemory(impl.device, stagingMemory, 0U, requiredBytes, 0U, &mapped) == VK_SUCCESS &&
            mapped != nullptr) {
            std::memcpy(outRGBA8.data(), mapped, requiredBytes);
            impl.vk.vkUnmapMemory(impl.device, stagingMemory);
            if (impl.swapchainFormat == VK_FORMAT_B8G8R8A8_SRGB) {
                // Surface is B,G,R,A-ordered; the contract promises R,G,B,A bytes — swap in place.
                for (std::size_t px = 0; px + 3U < outRGBA8.size(); px += 4U) {
                    std::swap(outRGBA8[px], outRGBA8[px + 2]);
                }
            }
            outWidth = impl.swapchainExtent.width;
            outHeight = impl.swapchainExtent.height;
        } else {
            ok = false;
        }
    }
    if (copyCommands != VK_NULL_HANDLE) {
        impl.vk.vkFreeCommandBuffers(impl.device, impl.commandPool, 1U, &copyCommands);
    }
    impl.vk.vkDestroyBuffer(impl.device, stagingBuffer, nullptr);
    impl.vk.vkFreeMemory(impl.device, stagingMemory, nullptr);
    if (!ok) {
        return fail("a Vulkan call in the staging/copy path failed (see device logs)");
    }
    return true;
}

std::string_view VulkanRenderDeviceUVE::GetBackendNameUVE() const noexcept {
    // Never the unqualified "Vulkan": the bootstrap must be identifiable in editor overlays
    // and bug reports (see the header's capability-reporting contract).
    return "Vulkan (M1 bootstrap)";
}

} // namespace UVE::Render
