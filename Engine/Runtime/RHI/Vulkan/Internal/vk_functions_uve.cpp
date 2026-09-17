// Copyright (c) 2026 UniVex Studios. All Rights Reserved.



#include "vk_functions_uve.h"

#include <dlfcn.h>

#include "uve/logging/logging_macros_uve.h"

namespace UVE::Render {
namespace {

void* g_vulkanLibraryHandle = nullptr;

template <typename T>
[[nodiscard]] bool ResolveGlobal(T& outPointer, const PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                                 const char* name) {
    // Vulkan entry points are resolved via vkGetInstanceProcAddr from the very start (never
    // dlsym directly — only vkGetInstanceProcAddr and the "base" globals are guaranteed to be
    // exported symbols at all on all loaders), passing VK_NULL_HANDLE for the instance per the
    // VK_EXT_debug_utils-era specification for instance-checking queries.
    void* proc = reinterpret_cast<void*>(getInstanceProcAddr(nullptr, name));
    outPointer = reinterpret_cast<T>(proc);
    if (proc == nullptr) {
        UVE_WARNING("VkFunctionsUVE: required Vulkan entry point '{}' is unavailable", name);
        return false;
    }
    return true;
}

template <typename T>
[[nodiscard]] bool ResolveInstance(T& outPointer, const PFN_vkGetInstanceProcAddr getInstanceProcAddr,
                                   const VkInstance instance, const char* name) {
    void* proc = reinterpret_cast<void*>(getInstanceProcAddr(instance, name));
    outPointer = reinterpret_cast<T>(proc);
    if (proc == nullptr) {
        UVE_WARNING("VkFunctionsUVE: instance entry point '{}' is unavailable", name);
        return false;
    }
    return true;
}

template <typename T>
[[nodiscard]] bool ResolveDevice(T& outPointer, const PFN_vkGetDeviceProcAddr getDeviceProcAddr,
                                 const VkDevice device, const char* name) {
    void* proc = reinterpret_cast<void*>(getDeviceProcAddr(device, name));
    outPointer = reinterpret_cast<T>(proc);
    if (proc == nullptr) {
        UVE_WARNING("VkFunctionsUVE: device entry point '{}' is unavailable", name);
        return false;
    }
    return true;
}

} // namespace

bool VkFunctionsUVE::LoadGlobalUVE() {
    // Conventional Vulkan loader sonames, most-specific first. The try-list duplicates exactly
    // what GLFW itself probes in its own Vulkan path, so hosts GLFW can already enumerate for
    // glfwVulkanSupported() are by construction hosts this loader covers. The handle is
    // deliberately never dlclose'd — see the header for the documented leak rationale.
    static constexpr const char* kLoaderNames[] = {
        "libvulkan.so.1",
        "libvulkan.so",
        "vulkan-1.dll",       // Windows loader, for a future port — dlopen never matches here.
        "libvulkan.1.dylib",  // MoltenVK-era macOS, same future-port rationale.
    };
    for (const char* loaderName : kLoaderNames) {
        g_vulkanLibraryHandle = dlopen(loaderName, RTLD_NOW | RTLD_LOCAL);
        if (g_vulkanLibraryHandle != nullptr) {
            break;
        }
    }
    if (g_vulkanLibraryHandle == nullptr) {
        UVE_WARNING("VkFunctionsUVE: no Vulkan loader library found (tried libvulkan.so.1 et al.) — "
                    "is an ICD such as mesa-vulkan-drivers installed?");
        return false;
    }
    void* gipaRaw = dlsym(g_vulkanLibraryHandle, "vkGetInstanceProcAddr");
    if (gipaRaw == nullptr) {
        UVE_WARNING("VkFunctionsUVE: Vulkan loader exports no vkGetInstanceProcAddr — unusable loader");
        return false;
    }
    auto gipa = reinterpret_cast<PFN_vkGetInstanceProcAddr>(gipaRaw);
    m_getInstanceProcAddr = gipa;

    bool allResolved = true;
    allResolved &= ResolveGlobal(vkCreateInstance, gipa, "vkCreateInstance");
    // vkEnumerateInstanceVersion is OPTIONAL: introduced in Vulkan 1.1 — a legacy 1.0 loader
    // correctly lacks it, and the caller then assumes API version 1.0. Resolve, but a null
    // here must not fail the table.
    void* enumerateVersion = reinterpret_cast<void*>(gipa(nullptr, "vkEnumerateInstanceVersion"));
    vkEnumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(enumerateVersion);
    return allResolved;
}

bool VkFunctionsUVE::LoadInstanceUVE(const VkInstance instance) {
    const PFN_vkGetInstanceProcAddr gipa = m_getInstanceProcAddr;
    bool allResolved = true;
    allResolved &= ResolveInstance(vkDestroyInstance, gipa, instance, "vkDestroyInstance");
    allResolved &= ResolveInstance(vkEnumeratePhysicalDevices, gipa, instance, "vkEnumeratePhysicalDevices");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceProperties, gipa, instance, "vkGetPhysicalDeviceProperties");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceQueueFamilyProperties, gipa, instance, "vkGetPhysicalDeviceQueueFamilyProperties");
    allResolved &= ResolveInstance(vkEnumerateDeviceExtensionProperties, gipa, instance, "vkEnumerateDeviceExtensionProperties");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceSurfaceSupportKHR, gipa, instance, "vkGetPhysicalDeviceSurfaceSupportKHR");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceSurfaceCapabilitiesKHR, gipa, instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceSurfaceFormatsKHR, gipa, instance, "vkGetPhysicalDeviceSurfaceFormatsKHR");
    allResolved &= ResolveInstance(vkGetPhysicalDeviceSurfacePresentModesKHR, gipa, instance, "vkGetPhysicalDeviceSurfacePresentModesKHR");
    allResolved &= ResolveInstance(vkCreateDevice, gipa, instance, "vkCreateDevice");
    allResolved &= ResolveInstance(vkGetDeviceProcAddr, gipa, instance, "vkGetDeviceProcAddr");
    allResolved &= ResolveInstance(vkDestroySurfaceKHR, gipa, instance, "vkDestroySurfaceKHR");
    return allResolved;
}

bool VkFunctionsUVE::LoadDeviceUVE(const VkDevice device) {
    if (vkGetDeviceProcAddr == nullptr) {
        return false;
    }
    const PFN_vkGetDeviceProcAddr gdpa = vkGetDeviceProcAddr;
    bool allResolved = true;
    allResolved &= ResolveDevice(vkDestroyDevice, gdpa, device, "vkDestroyDevice");
    allResolved &= ResolveDevice(vkGetDeviceQueue, gdpa, device, "vkGetDeviceQueue");
    allResolved &= ResolveDevice(vkCreateSwapchainKHR, gdpa, device, "vkCreateSwapchainKHR");
    allResolved &= ResolveDevice(vkDestroySwapchainKHR, gdpa, device, "vkDestroySwapchainKHR");
    allResolved &= ResolveDevice(vkGetSwapchainImagesKHR, gdpa, device, "vkGetSwapchainImagesKHR");
    allResolved &= ResolveDevice(vkAcquireNextImageKHR, gdpa, device, "vkAcquireNextImageKHR");
    allResolved &= ResolveDevice(vkCreateImageView, gdpa, device, "vkCreateImageView");
    allResolved &= ResolveDevice(vkDestroyImageView, gdpa, device, "vkDestroyImageView");
    allResolved &= ResolveDevice(vkCreateRenderPass, gdpa, device, "vkCreateRenderPass");
    allResolved &= ResolveDevice(vkDestroyRenderPass, gdpa, device, "vkDestroyRenderPass");
    allResolved &= ResolveDevice(vkCreateFramebuffer, gdpa, device, "vkCreateFramebuffer");
    allResolved &= ResolveDevice(vkDestroyFramebuffer, gdpa, device, "vkDestroyFramebuffer");
    allResolved &= ResolveDevice(vkCreateCommandPool, gdpa, device, "vkCreateCommandPool");
    allResolved &= ResolveDevice(vkDestroyCommandPool, gdpa, device, "vkDestroyCommandPool");
    allResolved &= ResolveDevice(vkAllocateCommandBuffers, gdpa, device, "vkAllocateCommandBuffers");
    allResolved &= ResolveDevice(vkFreeCommandBuffers, gdpa, device, "vkFreeCommandBuffers");
    allResolved &= ResolveDevice(vkBeginCommandBuffer, gdpa, device, "vkBeginCommandBuffer");
    allResolved &= ResolveDevice(vkEndCommandBuffer, gdpa, device, "vkEndCommandBuffer");
    allResolved &= ResolveDevice(vkResetCommandBuffer, gdpa, device, "vkResetCommandBuffer");
    allResolved &= ResolveDevice(vkCmdBeginRenderPass, gdpa, device, "vkCmdBeginRenderPass");
    allResolved &= ResolveDevice(vkCmdEndRenderPass, gdpa, device, "vkCmdEndRenderPass");
    allResolved &= ResolveDevice(vkCreateSemaphore, gdpa, device, "vkCreateSemaphore");
    allResolved &= ResolveDevice(vkDestroySemaphore, gdpa, device, "vkDestroySemaphore");
    allResolved &= ResolveDevice(vkCreateFence, gdpa, device, "vkCreateFence");
    allResolved &= ResolveDevice(vkDestroyFence, gdpa, device, "vkDestroyFence");
    allResolved &= ResolveDevice(vkWaitForFences, gdpa, device, "vkWaitForFences");
    allResolved &= ResolveDevice(vkResetFences, gdpa, device, "vkResetFences");
    allResolved &= ResolveDevice(vkQueueSubmit, gdpa, device, "vkQueueSubmit");
    allResolved &= ResolveDevice(vkQueuePresentKHR, gdpa, device, "vkQueuePresentKHR");
    allResolved &= ResolveDevice(vkQueueWaitIdle, gdpa, device, "vkQueueWaitIdle");
    return allResolved;
}

} // namespace UVE::Render
