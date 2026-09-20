// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/area_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Area3D scene node: the component set and defaults a freshly
/// created Area3D entity attaches. This recipe used to be hardcoded inline in EditorUVE's
/// creation switch; it now has the same per-file home every other node kind has, so the node's
/// creation behavior is customized in exactly one discoverable place. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: overlap/trigger state itself still lives only in
/// AreaComponentUVE (and is driven by Physics/AreaOverlapSystemUVE).
struct Area3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty" — the editor created Area3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Area3D";

    /// Area authored defaults; the entity's transform is attached by the creation shell.
    AreaComponentUVE area{};
};

[[nodiscard]] bool IsArea3DNodeDefinitionValidUVE(const Area3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyArea3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                  const Area3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
