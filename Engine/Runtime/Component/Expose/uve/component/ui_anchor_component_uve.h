// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/math/vector2_uve.h"

namespace UVE::Scene {

/// Pins a widget's rect to its parent: each frame, before the stack/grid pass, the anchor pass
/// resolves `rect` from normalized `anchorMin`/`anchorMax` within the parent rect plus pixel
/// `offsetMin`/`offsetMax` margins (resolvedMin = parentMin + anchorMin * parentSize + offsetMin,
/// likewise for max; negative sizes clamp to zero per axis). Texts take the resolved minimum as
/// positionPixels and ignore the maximum - they have no size to stretch.
///
/// The parent rect comes from a hierarchy parent carrying a container, button, or image rect (in
/// that priority); anything else - no parent, a dead parent, a text or bare-entity parent -
/// anchors against the viewport itself. Anchor values outside [0, 1] are deliberate overflow,
/// not an error: (1, 1)-(1, 1) with negative offsets is how a fixed-size widget pins to the
/// bottom-right corner and follows resizes.
///
/// Adding this component re-resolves the rect: the default is full-stretch (min (0, 0), max
/// (1, 1), zero margins), so a fresh anchor fills its parent. Container children resolve their
/// anchors too, but the stack/grid pass then overwrites their positions - for those widgets the
/// anchors contribute size and the container contributes position. Persistence rides the
/// metadata-driven serializer alongside the layout container.
struct UIAnchorComponentUVE final {
    Math::Vector2UVE anchorMin{0.0F, 0.0F};
    Math::Vector2UVE anchorMax{1.0F, 1.0F};
    Math::Vector2UVE offsetMin{0.0F, 0.0F};
    Math::Vector2UVE offsetMax{0.0F, 0.0F};

    [[nodiscard]] bool operator==(const UIAnchorComponentUVE&) const = default;
};

/// All four corners finite. Out-of-range anchors validate: overflow is a feature.
[[nodiscard]] bool IsUIAnchorComponentValidUVE(const UIAnchorComponentUVE& value) noexcept;

} // namespace UVE::Scene
