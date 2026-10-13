// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"
#include "uve/component/ui_tween_component_uve.h"

namespace UVE::Scene {
class IEntityManagerUVE;
} // namespace UVE::Scene

namespace UVE::UI {

/// The eased fraction for `ease` at linear time `t`: 0 at 0 and 1 at 1 for every curve except the
/// deliberate `OutBack` overshoot past 1 mid-flight. `t` clamps to [0, 1]; an unknown ease reads
/// as linear rather than failing.
[[nodiscard]] float EaseUITweenUVE(Scene::UITweenEaseUVE ease, float t) noexcept;

/// Advances every tween by `dt` seconds and applies the eased values: rect tweens rewrite each
/// positioned component on their entity (buttons, images, sliders, progress bars, containers take
/// the full rect; text takes the position), alpha tweens recompute `currentAlpha`. Runs after
/// anchors and layout so an active tween overrides the resting arrangement; a non-positive or
/// non-finite `dt` freezes every tween but still clears `completedThisFrame`. Invalid tweens park
/// neutrally - stopped, flags cleared, `currentAlpha` at 1 - and delay phases hold the
/// from-values. `completedThisFrame` fires on cycle boundaries: once at the end for `Once`, at
/// every wrap for `Loop` and `PingPong`.
void TickUITweensUVE(Scene::IEntityManagerUVE& entityManager, float dt);

/// The alpha quads for `entity` draw with: `baseAlpha` multiplied by the entity's alpha tween
/// when it carries a valid one, `baseAlpha` untouched otherwise. Entities without tweens,
/// rect-tweened entities, and invalid tweens all pass through unchanged.
[[nodiscard]] float TweenedAlphaUVE(Scene::IEntityManagerUVE& entityManager, Scene::EntityUVE entity,
                                   float baseAlpha);

} // namespace UVE::UI
