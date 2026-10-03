// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct Kinematic3DComponentUVE final {
    Math::Vector3UVE targetVelocity{};
    float interpolation = 1.0F;
    bool active = true;
};

[[nodiscard]] bool IsKinematic3DObjectComponentValidUVE(const Kinematic3DComponentUVE& value) noexcept;

/// Authoring definition for the Kinematic3D scene object: the component set and defaults a
/// freshly created Kinematic3D entity attaches — a collider, a kinematic rigid body (so
/// physics never fights the authored target-velocity motion), and the animatable body's own object
/// component. This recipe used to be the one remaining inline multi-component recipe hardcoded
/// in EditorUVE's creation switch; it now lives in the kind's own home file like every other
/// kind. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the
/// *recipe*, not a second copy of component storage.
struct Kinematic3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty" — the editor created this kind as a bare entity plus three components.)
    static constexpr std::string_view defaultName = "Kinematic3D";

    /// Collision authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponentUVE collider{};
    /// Kinematic by contract — the authored target velocity drives the body, never gravity
    /// (mirrors Character3DObjectDefinitionUVE's own kinematic contract).
    Rigid3DComponentUVE body = MakeDefaultBodyUVE();
    /// The animatable body's own authored defaults (zero target velocity, full interpolation).
    Kinematic3DComponentUVE animatableBody{};

    [[nodiscard]] static Rigid3DComponentUVE MakeDefaultBodyUVE() noexcept {
        Rigid3DComponentUVE body{};
        body.isKinematic = true;
        return body;
    }
};

[[nodiscard]] bool IsKinematic3DObjectDefinitionValidUVE(const Kinematic3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyKinematic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const Kinematic3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
