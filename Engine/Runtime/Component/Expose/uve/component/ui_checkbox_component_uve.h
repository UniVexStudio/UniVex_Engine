// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>

#include "uve/math/rect_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

inline constexpr float kMinimumUICheckboxSizePixelsUVE = 1.0F;
inline constexpr float kMaximumUICheckboxSizePixelsUVE = 8192.0F;

/// A screen-space toggle: pressing inside `rect` flips `checked` and raises `wasToggledThisFrame`
/// for exactly that tick. The box draws in `boxColor` (`hoverColor` while hovered); a checked box
/// draws a centered check indicator inset by a quarter of the box on every side. Labels are not
/// built in - an entity carrying this plus a text component lays both out together, like any
/// other multi-widget entity.
///
/// `checked` is authored and persists (a default-on option stays on); `isHovered` and
/// `wasToggledThisFrame` are tick-recomputed runtime state that never round-trips, following the
/// slider's precedent rather than the button's legacy persisted hover. Draws one or two solid
/// quads on the button layer. Thread-safety: value type; trivially safe to copy/move.
struct UICheckboxComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{20.0F, 20.0F}};
    Math::Vector3UVE boxColor{0.16F, 0.16F, 0.18F};
    Math::Vector3UVE hoverColor{0.28F, 0.28F, 0.33F};
    Math::Vector3UVE checkColor{0.30F, 0.75F, 0.35F};
    bool checked = false;
    /// Runtime state, never persisted: true while the mouse cursor is over the box.
    bool isHovered = false;
    /// Runtime state, never persisted: true for exactly the tick a press flipped `checked`.
    bool wasToggledThisFrame = false;
};

[[nodiscard]] bool IsUICheckboxComponentValidUVE(const UICheckboxComponentUVE& component) noexcept;

} // namespace UVE::Scene
