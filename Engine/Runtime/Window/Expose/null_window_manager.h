// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string>

#include "i_window_manager.h"
#include "window_desc.h"

namespace UVE::Window {

/// NullWindowManager is the headless IWindowManager backend: it never touches GLFW, X11, or
/// any OS window/GL API — it bookkeeps the portable window state requested through WindowDesc,
/// including size, V-sync, mode, title, and display-sleep policy, so headless integrations can
/// inspect the same settings without native side effects. Used whenever EngineConfig::headless
/// is true, so EngineServices's "every service is a live reference" contract needs no
/// headless-mode exception — mirrors NullRenderDevice/NullAudioDevice's role for their own
/// interfaces.
/// Thread-safety: not thread-safe, matching IWindowManager's own documented contract.
class NullWindowManager final : public IWindowManager {
public:
    explicit NullWindowManager(const WindowDesc& desc = WindowDesc{});

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
    /// Returns the last accepted title so headless integrations can verify title updates.
    [[nodiscard]] std::string_view GetWindowTitle() const noexcept;
    void SetDisplaySleepAllowed(bool allowed) noexcept override;
    /// Reports the retained request only; the headless backend never changes OS power state.
    [[nodiscard]] bool IsDisplaySleepAllowed() const noexcept;
    void GetContentScale(float& outX, float& outY) const noexcept override;
    [[nodiscard]] std::uint32_t GetWidth() const noexcept override;
    [[nodiscard]] std::uint32_t GetHeight() const noexcept override;
    [[nodiscard]] std::vector<MonitorInfo> EnumerateMonitors() const override;
    [[nodiscard]] void* GetNativeWindowHandle() const noexcept override;
    [[nodiscard]] std::string_view GetBackendName() const noexcept override;

private:
    std::uint32_t m_width;
    std::uint32_t m_height;
    bool m_vsyncEnabled;
    Platform::VSyncMode m_vsyncMode = Platform::VSyncMode::On;
    Platform::WindowMode m_windowMode = Platform::WindowMode::Windowed;
    double m_contentScaleOverride = 0.0;
    bool m_fullscreen = false;
    bool m_displaySleepAllowed = true;
    std::string m_windowTitle;
};

} // namespace UVE::Window
