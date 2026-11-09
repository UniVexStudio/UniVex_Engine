// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Input {
class IInputSystemUVE;
} // namespace UVE::Input

namespace UVE::Scene {
class IEntityManagerUVE;
} // namespace UVE::Scene

namespace UVE::UI {

class UIFontAtlasUVE;

/// Scrolls and stacks every valid scroll container's children: the wheel (Shift+wheel for
/// horizontal) moves `scrollOffset` while the pointer is over the box, the offset clamps to the
/// measured content, and children stack vertically from padding-minus-offset. Runs with the
/// layout pass, before tweens, shallowest-first; shares the layout pass's child extents so a
/// widget measures the same inside either container. A scroll container stacks its children
/// itself - a layout container on the same entity is overridden, not merged.
void LayoutUIScrollContainersUVE(Scene::IEntityManagerUVE& entityManager, const Input::IInputSystemUVE& input,
                                 const UIFontAtlasUVE& fontAtlas);

} // namespace UVE::UI
