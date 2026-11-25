// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "rigid_3d_component.h"
#include "quaternion.h"
#include "object_3d_common.h"

namespace UVE::Scene {

class IEntityManager;

struct Kinematic3DComponent final {
    /// Where the body is going, in metres per second, in the entity's own local axes. The mover
    /// drives the body at this velocity every fixed step (see Physics/kinematic_body_uve.h).
    Math::Vector3 targetVelocity{};
    /// How quickly the body reaches `targetVelocity`: 1 (the authored default) is at speed on the
    /// first step, lower values ease in over roughly a second, and 0 never eases on its own -
    /// which is how a script drives the body by writing its velocity instead. The curve is
    /// per-second, so a lift eases the same at any fixed rate.
    float interpolation = 1.0F;
    /// Off leaves the body exactly where it is, with no velocity left to hand a rider.
    bool active = true;
};

[[nodiscard]] bool IsKinematic3DObjectComponentValid(const Kinematic3DComponent& value) noexcept;

/// Authoring definition for the Kinematic3D scene object: the component set and defaults a
/// freshly created Kinematic3D entity attaches — the PhysicsObject3D base, a collider, a kinematic
/// rigid body (so physics never fights the authored target-velocity motion), and the animatable
/// body's own object component. This recipe used to be the one remaining inline multi-component
/// recipe hardcoded in Editor's creation switch; it now lives in the kind's own home file like every other
/// kind. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this holds the
/// *recipe*, not a second copy of component storage.
struct Kinematic3DObjectDefinition final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty" — the editor created this kind as a bare entity plus three components.)
    static constexpr std::string_view defaultName = "Kinematic3D";

    /// Collision authored defaults; the entity's transform is attached by the creation shell.
    ColliderComponent collider{};
    /// Kinematic by contract — the authored target velocity drives the body, never gravity
    /// (mirrors Character3DObjectDefinition's own kinematic contract).
    Rigid3DComponent body = MakeDefaultBody();
    /// The animatable body's own authored defaults (zero target velocity, full interpolation).
    Kinematic3DComponent animatableBody{};

    [[nodiscard]] static Rigid3DComponent MakeDefaultBody() noexcept {
        Rigid3DComponent body{};
        body.isKinematic = true;
        return body;
    }
};

[[nodiscard]] bool IsKinematic3DObjectDefinitionValid(const Kinematic3DObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaseline - this kind is Object3D plus its recipe.
void ApplyKinematic3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                            const Kinematic3DObjectDefinition& value);

class Kinematic3D final {
public:
    [[nodiscard]] static bool IsDriving(const Kinematic3DComponent& kinematic) noexcept;

    [[nodiscard]] static float EaseBlend(float interpolation, float deltaTimeSeconds) noexcept;

    [[nodiscard]] static Math::Vector3 ResolveWorldTarget(const Math::Vector3& localTarget,
                                                                const Math::Quaternion& worldRotation) noexcept;

    [[nodiscard]] static Math::Vector3 EaseVelocity(const Math::Vector3& currentWorldVelocity,
                                                          const Math::Vector3& worldTarget,
                                                          float interpolation, float deltaTimeSeconds) noexcept;
};

} // namespace UVE::Scene
