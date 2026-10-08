// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Window {

/// Resolves an initial desktop window mode when GLFW has no usable monitor/video mode.
/// Desktop fullscreen modes require a monitor; borderless remains a valid windowed style.
[[nodiscard]] constexpr Platform::WindowModeUVE ResolveInitialWindowModeUVE(
    const Platform::WindowModeUVE requestedMode, const bool monitorVideoModeAvailable) noexcept {
    if (!monitorVideoModeAvailable &&
        (requestedMode == Platform::WindowModeUVE::Fullscreen ||
         requestedMode == Platform::WindowModeUVE::ExclusiveFullscreen)) {
        return Platform::WindowModeUVE::Windowed;
    }
    return requestedMode;
}

/// Borderless is an undecorated windowed presentation, not monitor fullscreen. Only these modes
/// occupy a monitor in the GLFW backend and require fullscreen restore-geometry handling.
[[nodiscard]] constexpr bool IsFullscreenWindowModeUVE(const Platform::WindowModeUVE mode) noexcept {
    return mode == Platform::WindowModeUVE::Fullscreen ||
           mode == Platform::WindowModeUVE::ExclusiveFullscreen;
}

/// Windowed restore bounds should only be refreshed while the OS window is in a normal state.
/// Maximized and fullscreen bounds are presentation geometry, not the user's restore geometry.
[[nodiscard]] constexpr bool ShouldCaptureWindowedGeometryUVE(const bool fullscreen,
                                                              const bool maximized) noexcept {
    return !fullscreen && !maximized;
}

} // namespace UVE::Window
