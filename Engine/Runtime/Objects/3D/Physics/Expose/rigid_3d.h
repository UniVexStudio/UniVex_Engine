// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <string_view>

#include "collider_component.h"
#include "entity.h"
#include "rigid_3d_component.h"
#include "vector3.h"

namespace UVE::Scene {

class IEntityManager;

/// Authoring definition for the Rigid3D scene object: the component set and defaults a freshly
/// created Rigid3D entity attaches — the PhysicsObject3D base, the shape other bodies meet it
/// through, and one simulated rigid body (dynamic by default; gravity
/// and collision response move it, via Physics/PhysicsSystem). This recipe used to be
/// hardcoded inline in Editor's creation switch; it now has the same per-file home every
/// other object kind has. Per Engine/Runtime/Scene/README.md's "one truth per concept" rule this
/// holds the *recipe*, not a second copy of component storage: body state itself still lives
/// only in Rigid3DComponent.
struct Rigid3DObjectDefinition final {
    /// Default document-entity name for a freshly created object of this kind. (Previously the
    /// generic "Empty" — the editor created Rigid3D as a bare entity plus one component.)
    static constexpr std::string_view defaultName = "Rigid3D";

    /// The body's shape. A simulated body with no collider is a body nothing can hit and nothing
    /// can land on, so the kind carries one the same way Static3D and Kinematic3D do; its layer
    /// and mask are the ones the collision systems read.
    ColliderComponent collider{};
    /// Simulated-body authored defaults; the entity's transform is attached by the creation shell.
    Rigid3DComponent body{};
};

[[nodiscard]] bool IsRigid3DObjectDefinitionValid(const Rigid3DObjectDefinition& value) noexcept;

/// Attaches this object's components to `entity` using the definition's authored defaults. The
/// entity must be alive and must not already have any of the attached component types.
/// Every application first guarantees the Object3D baseline (Transform/WorldTransform/
/// Hierarchy/Name) through EnsureObject3DBaseline - this kind is Object3D plus its recipe.
void ApplyRigid3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const Rigid3DObjectDefinition& value);

class Rigid3D final {
public:
    [[nodiscard]] static bool IsDynamic(const Rigid3DComponent& rigidBody) noexcept;

    [[nodiscard]] static float InverseMass(const Rigid3DComponent& rigidBody) noexcept;

    [[nodiscard]] static std::optional<Math::Vector3> IntegrateLinearVelocity(
        const Math::Vector3& velocity, const Math::Vector3& gravity, float gravityScale,
        float linearDamp, float deltaTimeSeconds, const Math::Vector3& force = Math::Vector3{},
        float inverseMass = 0.0F) noexcept;

    /// Stores a caller-owned persistent force on the body's component, integrated as
    /// force/mass every step until changed. Answers false - storing nothing - without a body or
    /// with a non-finite force; a kinematic or massless body stores the force like any other
    /// (it reads back, and takes effect if the body ever becomes dynamic).
    [[nodiscard]] static bool ApplyForce(IEntityManager& entityManager, Entity entity,
                                            const Math::Vector3& force) noexcept;
    /// Adds an instant velocity kick: velocity += impulse * inverseMass. Answers false - writing
    /// nothing - without a body, with a non-finite impulse, or when the body cannot move
    /// (kinematic or massless): an impulse is an instant with nothing to store, so applying one
    /// to an immovable body is meaningless, unlike storing a persistent force on it.
    [[nodiscard]] static bool ApplyImpulse(IEntityManager& entityManager, Entity entity,
                                              const Math::Vector3& impulse) noexcept;
    /// Stores a caller-owned persistent torque, the angular twin of ApplyForce, with the same
    /// fail-closed answers.
    [[nodiscard]] static bool ApplyTorque(IEntityManager& entityManager, Entity entity,
                                             const Math::Vector3& torque) noexcept;

    [[nodiscard]] static Math::Vector3 DeflectVelocity(const Math::Vector3& velocity,
                                                             const Math::Vector3& towardOtherBody,
                                                             float friction, float restitution) noexcept;
};

} // namespace UVE::Scene
