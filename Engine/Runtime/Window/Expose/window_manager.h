// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <memory>

#include "i_event_system.h"
#include "i_vulkan_window_surface.h"
#include "i_window_manager.h"
#include "window_desc.h"

namespace UVE::Window {

/// WindowManager is the concrete, GLFW3-backed implementation of IWindowManager — the only
/// backend that ever actually calls glfwInit/glfwCreateWindow/glfwMakeContextCurrent/
/// glfwTerminate. Every GLFW type and header is confined to window_manager_uve.cpp (PIMPL); this
/// header never includes GLFW/glfw3.h, matching the codebase's established third-party-library
/// confinement discipline (ConfigManager's nlohmann::json PIMPL).
/// Owns the entire GL context lifecycle: glfwInit() (refcounted static — only the first live
/// WindowManager calls it, only the last one destroyed calls glfwTerminate()),
/// glfwCreateWindow(), and glfwMakeContextCurrent() all happen inside the constructor, in that
/// order. A render device built against this window (e.g. Render::GlRenderDevice) is
/// constructed strictly afterward and never creates, destroys, or activates the context itself.
class WindowManager final : public IWindowManager, public IVulkanWindowSurface {
public:
    /// `eventSystem` must outlive this WindowManager. On any GLFW failure (glfwInit or
    /// glfwCreateWindow), logs UVE_ERROR/UVE_FATAL internally and leaves IsValid() == false —
    /// never throws.
    WindowManager(Events::IEventSystem& eventSystem, const WindowDesc& desc);
    ~WindowManager() override;

    WindowManager(const WindowManager&) = delete;
    WindowManager& operator=(const WindowManager&) = delete;

    [[nodiscard]] bool IsValid() const noexcept override;
    void AttachInputSystem(Input::IInputSystem* inputSystem) noexcept override;
    void PollEvents() override;
    void SwapBuffers() override;
    [[nodiscard]] bool IsCloseRequested() const noexcept override;
    void SetVSyncEnabled(bool enabled) override;
    [[nodiscard]] bool IsVSyncEnabled() const noexcept override;
    void SetVSyncMode(Platform::VSyncMode mode) override;
    [[nodiscard]] Platform::VSyncMode GetVSyncMode() const noexcept override;
    void SetFullscreen(bool fullscreen) override;
    [[nodiscard]] bool IsFullscreen() const noexcept override;
    void SetWindowMode(Platform::WindowMode mode) override;
    [[nodiscard]] Platform::WindowMode GetWindowMode() const noexcept override;
    void SetWindowTitle(std::string_view title) override;
    void SetCursorVisible(bool visible) override;
    [[nodiscard]] bool IsFocused() const noexcept override;
    void GetContentScale(float& outX, float& outY) const noexcept override;
    void SetDisplaySleepAllowed(bool allowed) noexcept override;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept override;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept override;
    [[nodiscard]] std::vector<MonitorInfo> EnumerateMonitors() const override;
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

    // IVulkanWindowSurface (implemented in window_manager_uve.cpp): GLFW WSI forwarding for a
    // Vulkan backend's surface needs, expressed entirely in opaque handles — Vulkan SDK headers
    // never enter this module, mirroring the GL-header confinement the audit called for.
    // EngineCore queries this interface via dynamic_cast when a Vulkan render device is
    // configured; NullWindowManager deliberately never implements it.
    [[nodiscard]] std::vector<const char*> GetRequiredVulkanInstanceExtensions() const override;
    [[nodiscard]] std::uintptr_t CreateVulkanWindowSurface(std::uintptr_t vulkanInstance) override;
    void DestroyVulkanWindowSurface(std::uintptr_t vulkanInstance, std::uintptr_t vulkanSurface) override;
    void GetVulkanFramebufferSize(std::uint32_t& outWidth, std::uint32_t& outHeight) const override;
    [[nodiscard]] Platform::VSyncMode GetRequestedVSyncMode() const noexcept override;
    [[nodiscard]] bool IsTransparentFramebufferRequested() const noexcept override;

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace UVE::Window
