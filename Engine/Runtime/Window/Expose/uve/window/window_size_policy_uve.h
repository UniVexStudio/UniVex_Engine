// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

namespace UVE::Window {

struct WindowSizeLimitsUVE final {
    int minimumWidth = 0;
    int minimumHeight = 0;
    int maximumWidth = 0;
    int maximumHeight = 0;

    [[nodiscard]] constexpr bool operator==(const WindowSizeLimitsUVE&) const noexcept = default;
};

/// Converts validated descriptor limits to a backend's integer dimensions. Zero means no limit
/// in WindowDescUVE and is replaced with the backend-specific "don't care" sentinel. Callers must
/// validate the descriptor first so nonzero values fit in `int`.
[[nodiscard]] constexpr WindowSizeLimitsUVE ResolveWindowSizeLimitsUVE(
    const std::uint32_t minimumWidth, const std::uint32_t minimumHeight,
    const std::uint32_t maximumWidth, const std::uint32_t maximumHeight,
    const int backendDontCare) noexcept {
    return {
        minimumWidth == 0U ? backendDontCare : static_cast<int>(minimumWidth),
        minimumHeight == 0U ? backendDontCare : static_cast<int>(minimumHeight),
        maximumWidth == 0U ? backendDontCare : static_cast<int>(maximumWidth),
        maximumHeight == 0U ? backendDontCare : static_cast<int>(maximumHeight),
    };
}

} // namespace UVE::Window
