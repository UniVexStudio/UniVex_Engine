// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Collider3D scene object: the component set and defaults a freshly
/// created Collider3D entity attaches — one default box collider. This recipe used to be
/// hardcoded in EditorUVE's creation switch behind the legacy EditorEntityKindUVE::CollisionBox
/// case; it now has the same per-file home every other object kind has. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the *recipe*, not a
/// second copy of component storage: collision state itself still lives only in
/// ColliderComponentUVE (and is consumed by Physics/CollisionSystemUVE).
struct Collider3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. "Collision Box"
    /// preserves the exact name the legacy editor entity kind has always produced.
    static constexpr std::string_view defaultName = "Collision Box";

    /// Box-collider authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
};

[[nodiscard]] bool IsCollider3DObjectDefinitionValidUVE(const Collider3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyCollider3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                      const Collider3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
