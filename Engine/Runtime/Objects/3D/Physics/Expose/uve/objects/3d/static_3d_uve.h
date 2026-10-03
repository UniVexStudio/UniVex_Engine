// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Static3D scene object: the component set and defaults a
/// freshly created Static3D entity attaches. The recipe itself is a plain default collider —
/// identical today to Collider3D's, because a collider-only entity (ColliderComponentUVE with no
/// Rigid3DComponentUVE) is exactly how this engine represents non-moving world geometry. The
/// kinds stay separate because their *intent* differs (a static body vs a bare shape), and when
/// static-body-specific defaults emerge, this file is their one home. The recipe used to be
/// hardcoded inline in EditorUVE's creation switch; per Engine/Runtime/Scene/README.md's
/// "one truth per concept" rule this holds the *recipe*, not a second copy of component storage.
struct Static3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty".)
    static constexpr std::string_view defaultName = "Static3D";

    /// Static-body authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
};

[[nodiscard]] bool IsStatic3DObjectDefinitionValidUVE(const Static3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyStatic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                        const Static3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
