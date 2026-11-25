// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "rect.h"
#include "vector2.h"

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
struct UIAnchorComponent final {
    Math::Vector2 anchorMin{0.0F, 0.0F};
    Math::Vector2 anchorMax{1.0F, 1.0F};
    Math::Vector2 offsetMin{0.0F, 0.0F};
    Math::Vector2 offsetMax{0.0F, 0.0F};

    [[nodiscard]] bool operator==(const UIAnchorComponent&) const = default;
};

/// All four corners finite. Out-of-range anchors validate: overflow is a feature.
[[nodiscard]] bool IsUIAnchorComponentValid(const UIAnchorComponent& value) noexcept;

/// Replaces non-finite anchor/offset fields with default full-stretch `(0,0)->(1,1)` values.
[[nodiscard]] UIAnchorComponent SanitizeUIAnchorComponent(
    const UIAnchorComponent& value) noexcept;

/// True when `anchorMin != anchorMax` along either X or Y (stretches with parent resize).
[[nodiscard]] bool IsUIAnchorStretching(const UIAnchorComponent& value) noexcept;

/// Resolves the anchored rectangle inside `parentRect` using `anchorMin/Max` and `offsetMin/Max`.
[[nodiscard]] Math::Rect ResolveUIAnchoredRect(const UIAnchorComponent& value,
                                                     const Math::Rect& parentRect) noexcept;

/// Resolves the top-left anchored position (used by point-anchored widgets such as `UITextComponent`).
[[nodiscard]] Math::Vector2 ResolveUIAnchoredPosition(
    const UIAnchorComponent& value, const Math::Rect& parentRect) noexcept;

} // namespace UVE::Scene
