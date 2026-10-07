// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/platform/application_window_settings_uve.h"

namespace UVE::Window {

/// OpenGL exposes a swap interval, not Vulkan-style present modes. Resolve the portable request
/// into the interval GLFW should receive and the policy the backend can actually honor.
struct OpenGlVSyncResolutionUVE final {
    Platform::VSyncModeUVE effectiveMode = Platform::VSyncModeUVE::On;
    int swapInterval = 1;

    [[nodiscard]] constexpr bool operator==(const OpenGlVSyncResolutionUVE&) const noexcept = default;
};

[[nodiscard]] constexpr OpenGlVSyncResolutionUVE ResolveOpenGlVSyncUVE(
    const Platform::VSyncModeUVE requestedMode, const bool adaptiveIntervalSupported) noexcept {
    switch (requestedMode) {
    case Platform::VSyncModeUVE::Off:
        return {Platform::VSyncModeUVE::Off, 0};
    case Platform::VSyncModeUVE::On:
        return {Platform::VSyncModeUVE::On, 1};
    case Platform::VSyncModeUVE::Adaptive:
        return adaptiveIntervalSupported
                   ? OpenGlVSyncResolutionUVE{Platform::VSyncModeUVE::Adaptive, -1}
                   : OpenGlVSyncResolutionUVE{Platform::VSyncModeUVE::On, 1};
    case Platform::VSyncModeUVE::Mailbox:
        return {Platform::VSyncModeUVE::Off, 0};
    }
    return {Platform::VSyncModeUVE::On, 1};
}

} // namespace UVE::Window
