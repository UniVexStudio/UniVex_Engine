// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "application_window_settings.h"

namespace UVE::Window {

/// Resolves an initial desktop window mode when GLFW has no usable monitor/video mode.
/// Desktop fullscreen modes require a monitor; borderless remains a valid windowed style.
[[nodiscard]] constexpr Platform::WindowMode ResolveInitialWindowMode(
    const Platform::WindowMode requestedMode, const bool monitorVideoModeAvailable) noexcept {
    if (!monitorVideoModeAvailable &&
        (requestedMode == Platform::WindowMode::Fullscreen ||
         requestedMode == Platform::WindowMode::ExclusiveFullscreen)) {
        return Platform::WindowMode::Windowed;
    }
    return requestedMode;
}

/// Borderless is an undecorated windowed presentation, not monitor fullscreen. Only these modes
/// occupy a monitor in the GLFW backend and require fullscreen restore-geometry handling.
[[nodiscard]] constexpr bool IsFullscreenWindowMode(const Platform::WindowMode mode) noexcept {
    return mode == Platform::WindowMode::Fullscreen ||
           mode == Platform::WindowMode::ExclusiveFullscreen;
}

/// Windowed restore bounds should only be refreshed while the OS window is in a normal state.
/// Maximized and fullscreen bounds are presentation geometry, not the user's restore geometry.
[[nodiscard]] constexpr bool ShouldCaptureWindowedGeometry(const bool fullscreen,
                                                              const bool maximized) noexcept {
    return !fullscreen && !maximized;
}

} // namespace UVE::Window
