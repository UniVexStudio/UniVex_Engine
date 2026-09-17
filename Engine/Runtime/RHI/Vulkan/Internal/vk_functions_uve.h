// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// VkFunctionsUVE — the Vulkan analogue of uve/rhi_opengl/gl_functions_uve.h: a deliberately
// minimal, hand-rolled function-pointer loader holding exactly the entry points the M1
// bootstrap device needs (see vulkan_render_device_uve.h for the milestone contract). No
// volk, no Vulkan-Hpp, no libvulkan link dependency: the Vulkan loader shared library
// (libvulkan.so.1 / the platform equivalent) is opened with dlopen() and the symbol
// vkGetInstanceProcAddr resolved from it — the one entry point guaranteed to be exported —
// and every other pointer is walked out of Vulkan's own dispatch chain. Why dlopen rather
// than a plain link, matching GlFunctionsUVE's resolution philosophy: the loader is a
// *deployment* dependency, not all hosts have it, and the engine's answer to "no loader" is
// the documented fallback to the next backend — never a process that fails to start because
// a linked shared object is absent.
//
// Instance-level and device-level entry points are split just as Vulkan itself splits them:
// vkGetInstanceProcAddr retrieves instance entry points (and vkGetDeviceProcAddr), and
// vkGetDeviceProcAddr retrieves device entry points — loading them from the correct level is
// what makes the pointers valid on loaders with actual dispatch layers.


#pragma once

#include <vulkan/vulkan_core.h>

namespace UVE::Render {

/// Table of the Vulkan entry points the M1 bootstrap device uses, resolved lazily by
/// LoadGlobalUVE()/LoadInstanceUVE()/LoadDeviceUVE() (every member must be non-null before
/// its level returns true — a partial table is never returned silently). Not copyable in
/// spirit but trivially copyable in fact; the device owns exactly one instance.
struct VkFunctionsUVE {
    // Global level (no VkInstance needed; resolvable from vkGetInstanceProcAddr directly).
    PFN_vkCreateInstance vkCreateInstance = nullptr;
    PFN_vkEnumerateInstanceVersion vkEnumerateInstanceVersion = nullptr; // optional (1.0 hosts)

    // Instance level.
    PFN_vkDestroyInstance vkDestroyInstance = nullptr;
    PFN_vkEnumeratePhysicalDevices vkEnumeratePhysicalDevices = nullptr;
    PFN_vkGetPhysicalDeviceProperties vkGetPhysicalDeviceProperties = nullptr;
    PFN_vkGetPhysicalDeviceQueueFamilyProperties vkGetPhysicalDeviceQueueFamilyProperties = nullptr;
    PFN_vkEnumerateDeviceExtensionProperties vkEnumerateDeviceExtensionProperties = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceSupportKHR vkGetPhysicalDeviceSurfaceSupportKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR vkGetPhysicalDeviceSurfaceCapabilitiesKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfaceFormatsKHR vkGetPhysicalDeviceSurfaceFormatsKHR = nullptr;
    PFN_vkGetPhysicalDeviceSurfacePresentModesKHR vkGetPhysicalDeviceSurfacePresentModesKHR = nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties vkGetPhysicalDeviceMemoryProperties = nullptr;
    PFN_vkCreateDevice vkCreateDevice = nullptr;
    PFN_vkGetDeviceProcAddr vkGetDeviceProcAddr = nullptr;
    PFN_vkDestroySurfaceKHR vkDestroySurfaceKHR = nullptr;

    // Device level.
    PFN_vkDestroyDevice vkDestroyDevice = nullptr;
    PFN_vkGetDeviceQueue vkGetDeviceQueue = nullptr;
    PFN_vkCreateSwapchainKHR vkCreateSwapchainKHR = nullptr;
    PFN_vkDestroySwapchainKHR vkDestroySwapchainKHR = nullptr;
    PFN_vkGetSwapchainImagesKHR vkGetSwapchainImagesKHR = nullptr;
    PFN_vkAcquireNextImageKHR vkAcquireNextImageKHR = nullptr;
    PFN_vkCreateImageView vkCreateImageView = nullptr;
    PFN_vkDestroyImageView vkDestroyImageView = nullptr;
    PFN_vkCreateRenderPass vkCreateRenderPass = nullptr;
    PFN_vkDestroyRenderPass vkDestroyRenderPass = nullptr;
    PFN_vkCreateFramebuffer vkCreateFramebuffer = nullptr;
    PFN_vkDestroyFramebuffer vkDestroyFramebuffer = nullptr;
    PFN_vkCreateCommandPool vkCreateCommandPool = nullptr;
    PFN_vkDestroyCommandPool vkDestroyCommandPool = nullptr;
    PFN_vkAllocateCommandBuffers vkAllocateCommandBuffers = nullptr;
    PFN_vkFreeCommandBuffers vkFreeCommandBuffers = nullptr;
    PFN_vkBeginCommandBuffer vkBeginCommandBuffer = nullptr;
    PFN_vkEndCommandBuffer vkEndCommandBuffer = nullptr;
    PFN_vkResetCommandBuffer vkResetCommandBuffer = nullptr;
    PFN_vkCmdBeginRenderPass vkCmdBeginRenderPass = nullptr;
    PFN_vkCmdEndRenderPass vkCmdEndRenderPass = nullptr;
    PFN_vkCmdPipelineBarrier vkCmdPipelineBarrier = nullptr;
    PFN_vkCmdCopyImageToBuffer vkCmdCopyImageToBuffer = nullptr;
    PFN_vkCreateBuffer vkCreateBuffer = nullptr;
    PFN_vkDestroyBuffer vkDestroyBuffer = nullptr;
    PFN_vkGetBufferMemoryRequirements vkGetBufferMemoryRequirements = nullptr;
    PFN_vkAllocateMemory vkAllocateMemory = nullptr;
    PFN_vkFreeMemory vkFreeMemory = nullptr;
    PFN_vkBindBufferMemory vkBindBufferMemory = nullptr;
    PFN_vkMapMemory vkMapMemory = nullptr;
    PFN_vkUnmapMemory vkUnmapMemory = nullptr;
    PFN_vkCreateSemaphore vkCreateSemaphore = nullptr;
    PFN_vkDestroySemaphore vkDestroySemaphore = nullptr;
    PFN_vkCreateFence vkCreateFence = nullptr;
    PFN_vkDestroyFence vkDestroyFence = nullptr;
    PFN_vkWaitForFences vkWaitForFences = nullptr;
    PFN_vkResetFences vkResetFences = nullptr;
    PFN_vkQueueSubmit vkQueueSubmit = nullptr;
    PFN_vkQueuePresentKHR vkQueuePresentKHR = nullptr;
    PFN_vkQueueWaitIdle vkQueueWaitIdle = nullptr;

    // M2a "draw slice" additions: shader/pipeline objects and per-draw commands.
    PFN_vkCreateShaderModule vkCreateShaderModule = nullptr;
    PFN_vkDestroyShaderModule vkDestroyShaderModule = nullptr;
    PFN_vkCreatePipelineLayout vkCreatePipelineLayout = nullptr;
    PFN_vkDestroyPipelineLayout vkDestroyPipelineLayout = nullptr;
    PFN_vkCreateGraphicsPipelines vkCreateGraphicsPipelines = nullptr;
    PFN_vkDestroyPipeline vkDestroyPipeline = nullptr;
    PFN_vkCmdBindPipeline vkCmdBindPipeline = nullptr;
    PFN_vkCmdBindVertexBuffers vkCmdBindVertexBuffers = nullptr;
    PFN_vkCmdBindIndexBuffer vkCmdBindIndexBuffer = nullptr;
    PFN_vkCmdDraw vkCmdDraw = nullptr;
    PFN_vkCmdDrawIndexed vkCmdDrawIndexed = nullptr;
    PFN_vkCmdSetViewport vkCmdSetViewport = nullptr;
    PFN_vkCmdSetScissor vkCmdSetScissor = nullptr;

    /// Opens the platform Vulkan loader library by its conventional sonames and resolves
    /// vkGetInstanceProcAddr plus the global-level entry points above. `lsan`-clean as well:
    /// the loader handle is intentionally leaked for the process lifetime (dlclose on a
    /// library Vulkan registers atexit hooks against is the classic crash recipe; this is the
    /// same policy GLFW documents for its own glfwWindowHint'd Vulkan path).
    /// Returns false — logging why — when no loader library had the entry point.
    [[nodiscard]] bool LoadGlobalUVE();

    /// Resolves the instance-level entry points for `instance`. Must be called after the
    /// instance exists; LoadGlobalUVE() must have succeeded first.
    [[nodiscard]] bool LoadInstanceUVE(VkInstance instance);

    /// Resolves the device-level entry points for `device`. LoadInstanceUVE() must have
    /// succeeded first (this uses its vkGetDeviceProcAddr).
    [[nodiscard]] bool LoadDeviceUVE(VkDevice device);

    /// True after LoadGlobalUVE() succeeded — the device gates further work on this so a
    /// mid-cascade failure cannot leave partially-resolved entry points in use.
    [[nodiscard]] bool AreGlobalsLoadedUVE() const noexcept { return m_getInstanceProcAddr != nullptr; }

private:
    PFN_vkGetInstanceProcAddr m_getInstanceProcAddr = nullptr;
};

} // namespace UVE::Render
