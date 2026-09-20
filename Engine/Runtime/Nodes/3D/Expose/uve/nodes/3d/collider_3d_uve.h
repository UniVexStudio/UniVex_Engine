// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Collider3D scene node: the component set and defaults a freshly
/// created Collider3D entity attaches — one default box collider. This recipe used to be
/// hardcoded in EditorUVE's creation switch behind the legacy EditorEntityKindUVE::CollisionBox
/// case; it now has the same per-file home every other node kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: collision state itself still lives only in
/// ColliderComponentUVE (and is consumed by Physics/CollisionSystemUVE).
struct Collider3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. "Collision Box"
    /// preserves the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Collision Box";

    /// Box-collider authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
};

[[nodiscard]] bool IsCollider3DNodeDefinitionValidUVE(const Collider3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyCollider3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Collider3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
