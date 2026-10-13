// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "uve/component/collider_component_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Authoring definition for the Rigid3D scene object: the component set and defaults a freshly
/// created Rigid3D entity attaches — the PhysicsObject3D base, the shape other bodies meet it
/// through, and one simulated rigid body (dynamic by default; gravity
/// and collision response move it, via Physics/PhysicsSystemUVE). This recipe used to be
/// hardcoded inline in EditorUVE's creation switch; it now has the same per-file home every
/// other object kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: body state itself still lives
/// only in Rigid3DComponentUVE.
struct Rigid3DObjectDefinitionUVE final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty" — the editor created Rigid3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Rigid3D";

    /// The body's shape. A simulated body with no collider is a body nothing can hit and nothing
    /// can land on, so the kind carries one the same way Static3D and Kinematic3D do; its layer
    /// and mask are the ones the collision systems read.
    ColliderComponentUVE collider{};
    /// Simulated-body authored defaults; the entity's transform is attached by the creation shell.
    Rigid3DComponentUVE body{};
};

[[nodiscard]] bool IsRigid3DObjectDefinitionValidUVE(const Rigid3DObjectDefinitionUVE& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaselineUVE - this kind is Object3D plus its recipe.
void ApplyRigid3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Rigid3DObjectDefinitionUVE& value);

class Rigid3DUVE final {
public:
    [[nodiscard]] static bool IsDynamicUVE(const Rigid3DComponentUVE& rigidBody) noexcept;

    [[nodiscard]] static float InverseMassUVE(const Rigid3DComponentUVE& rigidBody) noexcept;

    [[nodiscard]] static std::optional<Math::Vector3UVE> IntegrateLinearVelocityUVE(
        const Math::Vector3UVE& velocity, const Math::Vector3UVE& gravity, float gravityScale,
        float linearDamp, float deltaTimeSeconds, const Math::Vector3UVE& force = Math::Vector3UVE{},
        float inverseMass = 0.0F) noexcept;

    /// Stores a caller-owned persistent force on the body's component, integrated as
    /// force/mass every step until changed. Answers false - storing nothing - without a body or
    /// with a non-finite force; a kinematic or massless body stores the force like any other
    /// (it reads back, and takes effect if the body ever becomes dynamic).
    [[nodiscard]] static bool ApplyForceUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const Math::Vector3UVE& force) noexcept;
    /// Adds an instant velocity kick: velocity += impulse * inverseMass. Answers false - writing
    /// nothing - without a body, with a non-finite impulse, or when the body cannot move
    /// (kinematic or massless): an impulse is an instant with nothing to store, so applying one
    /// to an immovable body is meaningless, unlike storing a persistent force on it.
    [[nodiscard]] static bool ApplyImpulseUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                              const Math::Vector3UVE& impulse) noexcept;
    /// Stores a caller-owned persistent torque, the angular twin of ApplyForceUVE, with the same
    /// fail-closed answers.
    [[nodiscard]] static bool ApplyTorqueUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                             const Math::Vector3UVE& torque) noexcept;

    [[nodiscard]] static Math::Vector3UVE DeflectVelocityUVE(const Math::Vector3UVE& velocity,
                                                             const Math::Vector3UVE& towardOtherBody,
                                                             float friction, float restitution) noexcept;
};

} // namespace UVE::Scene
