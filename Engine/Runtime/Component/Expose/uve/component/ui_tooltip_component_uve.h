// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cmath>
#include <cstddef>
#include <string>

#include "uve/component/ui_text_component_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// A hover popup for the rect widget on the same entity (buttons, sliders, checkboxes, images,
/// and progress bars qualify, in that priority; text-only entities show nothing). While the
/// pointer stays inside the widget's rect, `hoverTime` accumulates frame delta; once it reaches
/// `delay`, the tooltip draws until the pointer leaves - leaving resets the clock, so showing
/// again costs another full delay. Empty text never shows.
///
/// The popup measures its own background from the font atlas (`text` width plus `padding` on all
/// four sides, `fontSize` tall plus padding), sits at the pointer plus `offset`, and clamps into
/// the viewport. Text runs through the same localization service plain labels use. The tooltip
/// positions itself every frame, so layout and anchors ignore it - only the entity's widget
/// stacks and stretches. `hoverTime` and `visibleThisFrame` are runtime state that never
/// persists. Draws a background plus one glyph quad per character on the text layer.
/// Thread-safety: value type; trivially safe to copy/move.
struct UITooltipComponentUVE final {
    std::string text;
    float delay = 0.5F;
    Math::Vector2UVE offset{12.0F, 12.0F};
    float padding = 4.0F;
    float fontSize = 16.0F;
    Math::Vector3UVE backgroundColor{0.08F, 0.08F, 0.10F};
    Math::Vector3UVE textColor{0.95F, 0.95F, 0.95F};
    /// Runtime state, never persisted: seconds the pointer has held inside the widget's rect.
    float hoverTime = 0.0F;
    /// Runtime state, never persisted: true for exactly the ticks the popup draws.
    bool visibleThisFrame = false;
};

/// Text within the shared UI byte cap, non-negative finite delay/padding/hoverTime, finite
/// offset, font size within the shared UI range, and finite colors.
[[nodiscard]] bool IsUITooltipComponentValidUVE(const UITooltipComponentUVE& component) noexcept;

} // namespace UVE::Scene
