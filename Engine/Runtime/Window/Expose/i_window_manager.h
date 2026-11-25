// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstdint>
#include <string_view>
#include <vector>

#include "i_input_system.h"
#include "application_window_settings.h"
#include "monitor_info.h"

namespace UVE::Window {

/// IWindowManager owns exactly one OS window and — for the real GLFW3 backend — the entire
/// lifecycle of the OpenGL context tied to it: initialization of the windowing library,
/// termination, context creation, context destruction, and context activation
/// (`glfwMakeContextCurrent`) all happen inside the concrete implementation, never in a render
/// device. A render device (e.g. Render::GlRenderDevice) is constructed *after* an
/// IWindowManager& and assumes a current context already exists — it never creates, destroys,
/// or activates a context itself, and never creates or destroys the window.
/// Never exposes a backend-specific type: GetNativeWindowHandle() is intentionally type-erased
/// (matching this codebase's established void* type-erasure precedent — ArchetypeChunk,
/// IAssetManager::RegisterLoader<T>) so a future backend (SDL3, a mobile-native window) can
/// satisfy this same interface without it ever naming GLFW.
/// Thread-safety: not thread-safe. Every method here must be called only from EngineCore's
/// single main-loop thread — this mirrors GLFW's own single-thread-per-window contract.
class IWindowManager {
public:
    virtual ~IWindowManager() = default;

    /// True iff the window (and, for the real backend, its GL context) was created successfully.
    /// False means every other method is still safe to call (no crashes) but reports a degraded,
    /// inert state — callers must not attempt to build a render device against an invalid window.
    [[nodiscard]] virtual bool IsValid() const noexcept = 0;

    /// Attaches a borrowed desktop input sink. The window backend never owns the sink and may
    /// feed its existing Set*State() injection seam during PollEvents().
    virtual void AttachInputSystem(Input::IInputSystem* inputSystem) noexcept = 0;

    /// Pumps OS window/input events (GLFW: glfwPollEvents()). Publishes
    /// WindowCloseRequestedEvent/WindowResizedEvent/WindowFocusChangedEvent for anything
    /// that changed since the last call. Intended to be called once per frame.
    virtual void PollEvents() = 0;

    /// Presents the window's back buffer (GLFW: glfwSwapBuffers()). Only meaningful for the real
    /// backend; a headless implementation no-ops.
    virtual void SwapBuffers() = 0;

    /// Attempts to recover a temporarily unavailable native presentation surface without changing
    /// the window manager's ownership model. Desktop and headless backends have no recovery work;
    /// Android may recreate its EGL window surface while retaining a usable context.
    [[nodiscard]] virtual bool TryRecoverSurface() noexcept { return false; }

    /// True iff the user has requested the window be closed (e.g. clicked the OS close button)
    /// since the last ResetCloseRequested() call, if any. EngineCore polls this once per
    /// frame to drive its own RequestQuit().
    [[nodiscard]] virtual bool IsCloseRequested() const noexcept = 0;

    /// Enables or disables vertical sync (GLFW: glfwSwapInterval(1 or 0)).
    virtual void SetVSyncEnabled(bool enabled) = 0;
    [[nodiscard]] virtual bool IsVSyncEnabled() const noexcept = 0;
    /// Requests the richer swap policy. A backend may fall back when its graphics API does not
    /// expose a requested mode (for example, OpenGL has no mailbox present mode).
    virtual void SetVSyncMode(Platform::VSyncMode mode) {
        SetVSyncEnabled(mode != Platform::VSyncMode::Off);
    }
    [[nodiscard]] virtual Platform::VSyncMode GetVSyncMode() const noexcept {
        return IsVSyncEnabled() ? Platform::VSyncMode::On : Platform::VSyncMode::Off;
    }

    /// Toggles the window between windowed and borderless-fullscreen-on-primary-monitor. The cached
    /// state changes only after the backend confirms the requested monitor transition; a failed
    /// transition preserves the previous state.
    virtual void SetFullscreen(bool fullscreen) = 0;
    [[nodiscard]] virtual bool IsFullscreen() const noexcept = 0;
    virtual void SetWindowMode(Platform::WindowMode mode) {
        SetFullscreen(mode == Platform::WindowMode::Fullscreen ||
                         mode == Platform::WindowMode::ExclusiveFullscreen);
    }
    [[nodiscard]] virtual Platform::WindowMode GetWindowMode() const noexcept {
        return IsFullscreen() ? Platform::WindowMode::Fullscreen : Platform::WindowMode::Windowed;
    }
    virtual void SetWindowTitle(std::string_view title) { static_cast<void>(title); }
    /// Shows the pointer and frees it (a menu, a HUD with buttons), or hides and captures it for
    /// the game (looking around). A window without a pointer ignores it.
    virtual void SetCursorVisible(bool visible) { static_cast<void>(visible); }
    [[nodiscard]] virtual bool IsFocused() const noexcept { return true; }
    /// Content scale reported by the host, unless a positive descriptor override was requested.
    virtual void GetContentScale(float& outX, float& outY) const noexcept {
        outX = 1.0F;
        outY = 1.0F;
    }
    /// Best-effort platform power hint. Backends without an OS integration leave this a no-op.
    virtual void SetDisplaySleepAllowed(bool allowed) noexcept { static_cast<void>(allowed); }

    /// The window's current framebuffer size in pixels. Always current — never cached stale
    /// across a resize, since GlRenderDevice polls this every frame to set the GL viewport
    /// rather than reacting inside a resize callback.
    [[nodiscard]] virtual std::uint32_t GetWidth() const noexcept = 0;
    [[nodiscard]] virtual std::uint32_t GetHeight() const noexcept = 0;

    /// Every currently connected monitor, in backend-defined order. A snapshot, not a live
    /// handle — re-enumerate to observe hot-plug changes. Empty for a headless implementation.
    [[nodiscard]] virtual std::vector<MonitorInfo> EnumerateMonitors() const = 0;

    /// A type-erased native window handle (GLFW: the GLFWwindow* reinterpret_cast to void*) —
    /// the one and only bridge a render device needs to attach its context calls to this window.
    /// nullptr for a headless implementation, or when IsValid() is false.
    [[nodiscard]] virtual void* GetNativeWindowHandle() const noexcept = 0;

    /// A short human-readable backend name (e.g. "GLFW3", "Null"), for logging/diagnostics.
    [[nodiscard]] virtual std::string_view GetBackendName() const noexcept = 0;
};

} // namespace UVE::Window
