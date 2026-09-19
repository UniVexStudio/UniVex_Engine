// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "uve/component/entity_uve.h"

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

    /// Inherit visibility from this entity instead of from the transform parent.
    ///
    /// WHY THIS IS SEPARATE FROM THE HIERARCHY. Visibility and transforms group things for
    /// different reasons and the groupings genuinely differ. A weapon is parented to the hand that
    /// carries it, but it should disappear when the whole CHARACTER is hidden, not when the hand
    /// is. A HUD marker sits under the entity it annotates and should follow that entity's
    /// visibility, not the camera rig it happens to be attached to. Forcing one hierarchy to serve
    /// both means re-parenting for a reason that has nothing to do with position.
    ///
    /// kInvalidEntityUVE - the default - means inherit from the transform parent, which is what
    /// almost everything wants and what every existing scene does.
    ///
    /// The target does NOT have to be an ancestor, or related at all. What it must not be is part
    /// of a cycle: SceneGraphUVE::UpdateUVE detects one and falls back to the transform parent for
    /// the entities involved rather than looping or picking an arbitrary winner.
    EntityUVE visibilityParent = kInvalidEntityUVE;
};

/// Always true: both fields are plain bools with no invalid state. Present so the component has
/// the same validate seam every other component has - a caller writing generic code should not
/// have to special-case which components can be checked.
[[nodiscard]] bool IsVisibilityComponentValidUVE(const VisibilityComponentUVE& component) noexcept;

} // namespace UVE::Scene
