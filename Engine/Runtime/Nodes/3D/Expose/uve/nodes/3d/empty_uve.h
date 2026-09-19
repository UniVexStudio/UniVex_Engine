// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Node3D scene node: the transform-only base of every scene
/// hierarchy, and the node you reach for when you need a pivot or a grouping parent with no
/// appearance of its own.
///
/// It attaches nothing beyond what the creation shell already gives every node - a
/// TransformComponentUVE, a WorldTransformComponentUVE, a HierarchyComponentUVE and a name. That
/// IS the recipe: every other kind is this plus its own components, so BoxMesh3D is Node3D plus a
/// primitive mesh and a collider, Camera3D is Node3D plus a camera.
///
/// The capabilities people expect of it live on those shared components rather than here, which
/// is why this file stays small while the node gets richer: position/rotation/scale and Top Level
/// on TransformComponentUVE, Visible and Visibility Parent on VisibilityComponentUVE, notes on
/// EditorDescriptionComponentUVE. Putting them here instead would give them to ONE kind; putting
/// them on the components gives them to all 43.
///
/// NAMING. The kind enumerator and the on-disk string are still `Empty`, deliberately: every
/// .uvescene ever saved has that string in it, and renaming the kind would either break those
/// files or require a migration to buy nothing but a different word. Only the display name
/// changed, which is the part an author actually reads.
struct EmptyNodeDefinitionUVE final {
    /// Shown in the Add-Node library and used as a freshly created node's entity name.
    static constexpr std::string_view defaultName = "Node3D";
};

[[nodiscard]] bool IsEmptyNodeDefinitionValidUVE(const EmptyNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity`. Empty is transform-only by design, so this is a
/// documented no-op kept for uniformity: every node kind's creation path reads the same way.
void ApplyEmptyNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                 const EmptyNodeDefinitionUVE& value) noexcept;

} // namespace UVE::Scene
