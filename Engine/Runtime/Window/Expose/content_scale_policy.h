// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <optional>

#include "application_window_settings.h"

namespace UVE::Window {

struct ContentScale final {
    float x = 1.0F;
    float y = 1.0F;

    [[nodiscard]] constexpr bool operator==(const ContentScale&) const noexcept = default;
};

/// Returns a usable uniform override, or no value when automatic/native scaling is requested.
[[nodiscard]] inline std::optional<float> ResolveContentScaleOverride(const double requestedScale) noexcept {
    if (!std::isfinite(requestedScale) || requestedScale <= 0.0 ||
        requestedScale > Platform::kMaximumContentScaleOverride) {
        return std::nullopt;
    }
    const float scale = static_cast<float>(requestedScale);
    if (!(scale > 0.0F) || !std::isfinite(scale)) {
        return std::nullopt;
    }
    return scale;
}

/// Native content scale is accepted only when both axes are positive and finite. Invalid samples
/// fall back atomically to 1x rather than mixing a valid axis with a bad one.
[[nodiscard]] inline ContentScale SanitizeNativeContentScale(const float x, const float y) noexcept {
    if (!(x > 0.0F) || !(y > 0.0F) || !std::isfinite(x) || !std::isfinite(y)) {
        return {};
    }
    return {x, y};
}

} // namespace UVE::Window
