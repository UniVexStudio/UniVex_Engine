// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

namespace UVE::Scene {

/// Whether an entity and its subtree are drawn.
///
/// WHY TWO FIELDS. `visible` is the author's switch - what the eye toggle in the outliner sets,
/// what a .uvescene stores, the only field anyone writes. `visibleInHierarchy` is the derived
/// answer that includes every ancestor: hiding a parent must hide its children, and a child that
/// was independently hidden must STAY hidden when its parent is shown again. One flag cannot do
/// both - overwriting the author's choice to propagate a parent's state loses the child's own
/// setting the moment the parent is toggled back.
///
/// Derived, not authored. SceneGraphUVE::UpdateUVE computes `visibleInHierarchy` during the same
/// root-first sweep that computes world transforms, because that sweep already guarantees a
/// parent is processed before its children - which is exactly the ordering inheritance needs, and
/// the reason this costs no extra walk.
///
/// The component is OPTIONAL. An entity without one is visible: that keeps every existing
/// document and every runtime-created entity working unchanged, and means the flag is only paid
/// for by entities that actually use it.
///
/// Thread-safety: value type; safe to copy and move freely with no shared state.
struct VisibilityComponentUVE final {
    /// The authored switch. Written by the editor and by scene loading; never by the scene graph.
    bool visible = true;

    /// Derived: this entity's own `visible` AND every ancestor's. Written only by
    /// SceneGraphUVE::UpdateUVE, read by the renderer and anything else that skips hidden
    /// objects.
    ///
    /// Stale until the first update after a change, exactly like WorldTransformComponentUVE -
    /// consumers already run after the scene graph updates, and one that does not is reading a
    /// stale transform too.
    bool visibleInHierarchy = true;
};

/// Always true: both fields are plain bools with no invalid state. Present so the component has
/// the same validate seam every other component has - a caller writing generic code should not
/// have to special-case which components can be checked.
[[nodiscard]] bool IsVisibilityComponentValidUVE(const VisibilityComponentUVE& component) noexcept;

} // namespace UVE::Scene
