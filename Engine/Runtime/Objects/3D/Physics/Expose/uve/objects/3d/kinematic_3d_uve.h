// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

struct Kinematic3DComponentUVE final {
    /// Where the body is going, in metres per second, in the entity's own local axes. The mover
    /// drives the body at this velocity every fixed step (see Physics/kinematic_body_uve.h).
    Math::Vector3UVE targetVelocity{};
    /// How quickly the body reaches `targetVelocity`: 1 (the authored default) is at speed on the
    /// first step, lower values ease in over roughly a second, and 0 never eases on its own -
    /// which is how a script drives the body by writing its velocity instead. The curve is
    /// per-second, so a lift eases the same at any fixed rate.
    float interpolation = 1.0F;
    /// Off leaves the body exactly where it is, with no velocity left to hand a rider.
    bool active = true;
};

[[nodiscard]] bool IsKinematic3DObjectComponentValidUVE(const Kinematic3DComponentUVE& value) noexcept;

/// Authoring definition for the Kinematic3D scene object: the component set and defaults a
/// freshly created Kinematic3D entity attaches — the PhysicsObject3D base, a collider, a kinematic
/// rigid body (so physics never fights the authored target-velocity motion), and the animatable
/// body's own object component. This recipe used to be the one remaining inline multi-component
/// recipe hardcoded in EditorUVE's creation switch; it now lives in the kind's own home file like every other
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

class Kinematic3DUVE final {
public:
    [[nodiscard]] static bool IsDrivingUVE(const Kinematic3DComponentUVE& kinematic) noexcept;

    [[nodiscard]] static float EaseBlendUVE(float interpolation, float deltaTimeSeconds) noexcept;

    [[nodiscard]] static Math::Vector3UVE ResolveWorldTargetUVE(const Math::Vector3UVE& localTarget,
                                                                const Math::QuaternionUVE& worldRotation) noexcept;

    [[nodiscard]] static Math::Vector3UVE EaseVelocityUVE(const Math::Vector3UVE& currentWorldVelocity,
                                                          const Math::Vector3UVE& worldTarget,
                                                          float interpolation, float deltaTimeSeconds) noexcept;
};

} // namespace UVE::Scene
