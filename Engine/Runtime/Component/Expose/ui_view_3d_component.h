// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>

#include "entity.h"
#include "rect.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Scene {

inline constexpr float kMinimumUIView3DSizePixelsUVE = 8.0F;
inline constexpr float kMaximumUIView3DSizePixelsUVE = 4096.0F;

/// A 3D View: a widget that shows an entity asset (a character, a weapon, a car ...) live in 3D,
/// lit and turning, inside the UI - the hero on a main menu, the item in an inventory. The runtime
/// stands the asset on a stage of its own, far from the level, and renders it from this widget's
/// camera into the widget's rect; where nothing is drawn the background (or what is behind the
/// widget, at `backgroundAlpha` 0) shows through.
/// Thread-safety: value type; trivially safe to copy/move.
struct UIView3DComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{360.0F, 480.0F}};
    /// The entity asset shown (`.uventity` or `.uvprefab`, as the project reads it). Empty shows nothing.
    std::string assetPath;
    /// The camera: how far from the asset, the height it looks at, its tilt and its lens.
    float cameraDistance = 3.6F;
    float lookAtHeight = 0.95F;
    float pitchDegrees = -4.0F;
    float fieldOfViewDegrees = 32.0F;
    /// How the asset is turned, and how fast it keeps turning (0: still).
    float yawDegrees = 20.0F;
    float spinDegreesPerSecond = 20.0F;
    Math::Vector3UVE backgroundColor{0.05F, 0.06F, 0.08F};
    /// 0: nothing behind the asset (the UI and the game show through); 1: the background colour.
    float backgroundAlpha = 0.0F;

    // ---- runtime, never saved: the stage the runtime made for it
    EntityUVE stageRoot = kInvalidEntityUVE;
    std::string stagedAssetPath;
    float spinYawDegrees = 0.0F;
    std::uint32_t stageSlot = 0U;
};

[[nodiscard]] bool IsUIView3DComponentValidUVE(const UIView3DComponentUVE& component) noexcept;

/// Where stage `slot` stands: far below the level, out of every game camera's reach.
[[nodiscard]] Math::Vector3UVE UIView3DStagePositionUVE(std::uint32_t slot) noexcept;

} // namespace UVE::Scene
