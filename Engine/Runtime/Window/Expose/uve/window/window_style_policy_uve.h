// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Window {

/// Window modes that fill a monitor are always undecorated. An explicit borderless setting also
/// removes decorations from ordinary windowed/maximized modes.
[[nodiscard]] constexpr bool ShouldDecorateWindowUVE(
    const bool borderlessSetting, const Platform::WindowModeUVE mode) noexcept {
    return !borderlessSetting && mode != Platform::WindowModeUVE::Borderless &&
           mode != Platform::WindowModeUVE::Fullscreen &&
           mode != Platform::WindowModeUVE::ExclusiveFullscreen;
}

} // namespace UVE::Window
