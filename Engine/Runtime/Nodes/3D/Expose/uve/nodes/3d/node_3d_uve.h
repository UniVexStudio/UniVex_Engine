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
/// Its recipe is the baseline every 3D scene node stands on - a TransformComponentUVE, a
/// WorldTransformComponentUVE, a HierarchyComponentUVE and a name. Every other kind is this
/// plus its own components, so BoxMesh3D is Node3D plus a primitive mesh and a collider,
/// Camera3D is Node3D plus a camera.
///
/// The capabilities people expect of it live on those shared components rather than here, which
/// is why this file stays small while the node gets richer: position/rotation/scale and Top Level
/// on TransformComponentUVE, Visible and Visibility Parent on VisibilityComponentUVE, notes on
/// EditorDescriptionComponentUVE. Putting them here instead would give them to ONE kind; putting
/// them on the components gives them to all 43.
///
/// NAMING. This kind used to be spelled `Empty` everywhere. The rename covers the enumerator,
/// the file and the on-disk type id, and the old spellings keep working at the two seams that
/// cross a persistence boundary: FindSceneNodeDescriptorUVE still resolves the legacy "empty"
/// type id to this kind, and the hierarchy icon table still maps the legacy "empty" name, so a
/// saved scene or layout written before the rename loads identically after it.
struct Node3DNodeDefinitionUVE final {
    /// Shown in the Add-Node library and used as a freshly created node's entity name.
    static constexpr std::string_view defaultName = "Node3D";
};

[[nodiscard]] bool IsNode3DNodeDefinitionValidUVE(const Node3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity`. Kept to the same validate-before-apply seam as
/// every other node kind; the body grows the baseline guarantee in the slice that wires the
/// scene root onto the same recipe.
void ApplyNode3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const Node3DNodeDefinitionUVE& value) noexcept;

} // namespace UVE::Scene
