// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumCamera2DZoomUVE = 0.01F;

/// A 2D scrolling camera: the view a 2D scene is authored through. `zoom` scales each axis
/// (1 = one world unit per pixel at a 1:1 view; larger zooms in), `offset` shifts the view
/// center from the node's origin in world units, and `current` marks the camera the 2D view
/// follows when several exist - persisted now, consumed by the 2D renderer when it lands.
/// Thread-safety: value type; trivially safe to copy/move.
struct Camera2DComponentUVE final {
    Math::Vector2UVE zoom = {1.0F, 1.0F};
    Math::Vector2UVE offset{};
    bool current = false;
};

[[nodiscard]] bool IsCamera2DComponentValidUVE(const Camera2DComponentUVE& camera) noexcept;

} // namespace UVE::Scene
