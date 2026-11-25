// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include "vector2.h"

namespace UVE::Input {
class IInputSystem;
} // namespace UVE::Input

namespace UVE::Scene {
class IEntityManager;
} // namespace UVE::Scene

namespace UVE::UI {

class UIFontAtlas;

/// Scrolls and stacks every valid scroll container's children: the wheel (Shift+wheel for
/// horizontal) moves `scrollOffset` while the pointer is over the box, the offset clamps to the
/// measured content, and children stack vertically from padding-minus-offset. Runs with the
/// layout pass, before tweens, shallowest-first; shares the layout pass's child extents so a
/// widget measures the same inside either container. A scroll container stacks its children
/// itself - a layout container on the same entity is overridden, not merged.
void LayoutUIScrollContainers(Scene::IEntityManager& entityManager, const Input::IInputSystem& input,
                                 const UIFontAtlas& fontAtlas,
                                 std::optional<Math::Vector2> pointerPositionInAuthoredSpace = std::nullopt);

} // namespace UVE::UI
