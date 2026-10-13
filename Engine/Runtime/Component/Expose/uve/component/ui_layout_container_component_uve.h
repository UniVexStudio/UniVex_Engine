// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/rect_uve.h"

namespace UVE::Scene {

enum class UILayoutDirectionUVE : std::uint8_t {
    Vertical = 0,
    Horizontal = 1,
};

enum class UILayoutAlignmentUVE : std::uint8_t {
    Start = 0,
    Center = 1,
    End = 2,
};

/// A stack container: every frame, before hit-testing and batching, the layout pass positions
/// this entity's direct hierarchy children (siblingOrder first, the entity handle breaking ties)
/// inside `rect`, inset by `padding` on all four sides and separated by `spacing` along
/// `direction`. Only positionable children participate (buttons, images, nested containers,
/// text); anything else is ignored, not spaced. Child sizes are preserved by the position pass -
/// it writes positions only - while each `autoSizeWidth`/`autoSizeHeight` axis fits the
/// container to its content (children plus padding on both sides) in a deepest-first resize pass
/// ahead of positioning, so a nested auto-sized container settles before its parent measures it.
/// An empty auto-sized container keeps padding only, and an auto axis overwrites whatever the
/// anchor pass wrote for that axis: anchors contribute position, content contributes size, with
/// no iteration between them.
///
/// `wrapAfter` turns the stack into a grid: at most that many items per line before wrapping to
/// the next line (0 means never wrap - a pure stack). Wrapped lines use uniform cells sized to
/// the largest participating child in each axis, spacing applies between cells on both axes, and
/// `alignment` positions each child within its own cell; the grid itself always starts at the
/// inner edge. Any nonzero `wrapAfter` means uniform cells even when nothing actually wraps, so
/// a single short line is spaced as a grid, not packed as a stack.
///
/// The cross axis follows `alignment` within the inner rect (within the cell, when wrapping); a
/// child wider than its slot is clamped to the slot's edge rather than pushed outside. Overflow
/// along either axis is allowed and unclipped: there is no clipping anywhere in the UI system
/// yet.
///
/// Text participates with a laid-out extent of {measured width, fontSize}: the width comes from
/// the font atlas's advance sums, so horizontal stacks and grids pace text exactly as it draws.
/// Persistence rides the metadata-driven serializer: every field above is a declared property,
/// so no hand-written JSON exists for this component.
struct UILayoutContainerComponentUVE final {
    Math::RectUVE rect{Math::Vector2UVE{0.0F, 0.0F}, Math::Vector2UVE{320.0F, 240.0F}};
    UILayoutDirectionUVE direction = UILayoutDirectionUVE::Vertical;
    UILayoutAlignmentUVE alignment = UILayoutAlignmentUVE::Start;
    float padding = 0.0F;
    float spacing = 0.0F;
    std::uint32_t wrapAfter = 0U;
    bool autoSizeWidth = false;
    bool autoSizeHeight = false;

    [[nodiscard]] bool operator==(const UILayoutContainerComponentUVE&) const = default;
};

/// Finite rect position and size, non-negative size, finite non-negative padding and spacing,
/// and known direction/alignment values.
[[nodiscard]] bool IsUILayoutContainerComponentValidUVE(const UILayoutContainerComponentUVE& value) noexcept;

} // namespace UVE::Scene
