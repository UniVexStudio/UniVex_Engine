// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "application_window_settings.h"

namespace UVE::Window {

/// OpenGL exposes a swap interval, not Vulkan-style present modes. Resolve the portable request
/// into the interval GLFW should receive and the policy the backend can actually honor.
struct OpenGlVSyncResolution final {
    Platform::VSyncMode effectiveMode = Platform::VSyncMode::On;
    int swapInterval = 1;

    [[nodiscard]] constexpr bool operator==(const OpenGlVSyncResolution&) const noexcept = default;
};

[[nodiscard]] constexpr OpenGlVSyncResolution ResolveOpenGlVSync(
    const Platform::VSyncMode requestedMode, const bool adaptiveIntervalSupported) noexcept {
    switch (requestedMode) {
    case Platform::VSyncMode::Off:
        return {Platform::VSyncMode::Off, 0};
    case Platform::VSyncMode::On:
        return {Platform::VSyncMode::On, 1};
    case Platform::VSyncMode::Adaptive:
        return adaptiveIntervalSupported
                   ? OpenGlVSyncResolution{Platform::VSyncMode::Adaptive, -1}
                   : OpenGlVSyncResolution{Platform::VSyncMode::On, 1};
    case Platform::VSyncMode::Mailbox:
        return {Platform::VSyncMode::Off, 0};
    }
    return {Platform::VSyncMode::On, 1};
}

} // namespace UVE::Window
