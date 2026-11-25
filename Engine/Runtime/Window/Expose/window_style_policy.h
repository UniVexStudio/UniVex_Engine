// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "application_window_settings.h"

namespace UVE::Window {

/// Window modes that fill a monitor are always undecorated. An explicit borderless setting also
/// removes decorations from ordinary windowed/maximized modes.
[[nodiscard]] constexpr bool ShouldDecorateWindow(
    const bool borderlessSetting, const Platform::WindowMode mode) noexcept {
    return !borderlessSetting && mode != Platform::WindowMode::Borderless &&
           mode != Platform::WindowMode::Fullscreen &&
           mode != Platform::WindowMode::ExclusiveFullscreen;
}

} // namespace UVE::Window
