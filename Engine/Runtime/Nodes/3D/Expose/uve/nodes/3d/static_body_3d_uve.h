// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the StaticBody3D scene node: the component set and defaults a
/// freshly created StaticBody3D entity attaches. The recipe itself is a plain default collider —
/// identical today to Collider3D's, because a collider-only entity (ColliderComponentUVE with no
/// RigidBodyComponentUVE) is exactly how this engine represents non-moving world geometry. The
/// kinds stay separate because their *intent* differs (a static body vs a bare shape), and when
/// static-body-specific defaults emerge, this file is their one home. The recipe used to be
/// hardcoded inline in EditorUVE's creation switch; per Engine/Runtime/Scene/README.md's
/// "one truth per concept" rule this holds the *recipe*, not a second copy of component storage.
struct StaticBody3DNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind. (Previously the
    /// generic "Empty".)
    static constexpr std::string_view defaultName = "StaticBody3D";

    /// Static-body authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
};

[[nodiscard]] bool IsStaticBody3DNodeDefinitionValidUVE(const StaticBody3DNodeDefinitionUVE& value) noexcept;

/// Attaches this node's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Node3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureNode3DBaselineUVE - this kind is Node3D plus its recipe.
void ApplyStaticBody3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const StaticBody3DNodeDefinitionUVE& value);

} // namespace UVE::Scene
