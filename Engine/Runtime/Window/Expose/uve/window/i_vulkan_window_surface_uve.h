// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <vector>

namespace UVE::Window {

/// IVulkanWindowSurfaceUVE is the optional capability interface a WindowManager implementation
/// exposes **in addition to** IWindowManagerUVE when its real OS window can be presented to by a
/// Vulkan backend. Consumers (currently only Render::VulkanRenderDeviceUVE, selected by
/// EngineCoreUVE) query it with dynamic_cast<IVulkanWindowSurfaceUVE*>(iWindowManager); a null
/// cast result is the contract's own "Vulkan presentation not possible on this backend" answer —
/// NullWindowManagerUVE deliberately does not implement it, so headless runs never even attempt
/// device creation.
///
/// Opaque-handle discipline: every Vulkan type crossing this boundary travels as a raw
/// std::uintptr_t integer, so this public header names no Vulkan symbol and no GLFW symbol —
/// matching the codebase's third-party-header confinement discipline (the Window module keeps
/// both libraries confined to its implementation files, exactly as it already does for GLFW in
/// IWindowManagerUVE). On Vulkan's interpretation: `vulkanInstance` carries a VkInstance (typedef
/// of a pointer) and CreateVulkanWindowSurfaceUVE returns a VkSurfaceKHR, both round-tripped
/// losslessly through uintptr-sized storage. The Window implementation itself pulls in
/// GLFW_INCLUDE_VULKAN privately; nothing above it needs that knowledge.
///
/// WindowManagerUVE (the GLFW3 backend) implements all four operations by forwarding to the
/// exact GLFW entry points (glfwGetRequiredInstanceExtensions, glfwCreateWindowSurface,
/// glfwDestroyWindowSurface, glfwGetFramebufferSize), so its failure modes are GLFW's: extension
/// retrieval or surface creation can legitimately fail on platforms without Vulkan WSI support,
/// and the implementation reports that rather than aborting (see each override's doc comment in
/// window_manager_uve.h).
class IVulkanWindowSurfaceUVE {
public:
    virtual ~IVulkanWindowSurfaceUVE() = default;

    /// The instance extension names GLFW reports as mandatory for window-system integration
    /// (VK_KHR_surface plus the platform surface extension), or an empty vector when the platform
    /// build has no Vulkan WSI support at all — the caller treats empty as "Vulkan unavailable".
    [[nodiscard]] virtual std::vector<const char*> GetRequiredVulkanInstanceExtensionsUVE() const = 0;

    /// Creates a presentable surface for this manager's window inside `vulkanInstance`.
    /// Returns 0 (logging the reason) on failure — the caller treats 0 as "Vulkan unavailable".
    /// The results are only valid while both the instance and the window exist.
    [[nodiscard]] virtual std::uintptr_t CreateVulkanWindowSurfaceUVE(std::uintptr_t vulkanInstance) = 0;

    /// Destroys a surface previously returned by CreateVulkanWindowSurfaceUVE. Safe no-op for 0.
    virtual void DestroyVulkanWindowSurfaceUVE(std::uintptr_t vulkanInstance, std::uintptr_t vulkanSurface) = 0;

    /// Current drawable framebuffer size in pixels, for swapchain extent selection. Mirrors
    /// glfwGetFramebufferSize semantics (may differ from the window size on HiDPI displays).
    virtual void GetVulkanFramebufferSizeUVE(std::uint32_t& outWidth, std::uint32_t& outHeight) const = 0;
};

} // namespace UVE::Window
