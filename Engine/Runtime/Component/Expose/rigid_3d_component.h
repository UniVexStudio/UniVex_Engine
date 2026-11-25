// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cmath>

#include "vector3.h"

namespace UVE::Scene {

/// One of the master spec's named built-in components (Part 7.3), extended in Part 7.5's
/// PhysicsSystem (Increment 15) exactly as this component's own doc comment predicted:
/// velocity and drag. `velocity` is the one piece of state PhysicsSystem needs to persist
/// across frames — storing it here (rather than inside PhysicsSystem itself) is what lets
/// PhysicsSystem stay a stateless service, matching CameraSystem/MeshRenderer's
/// precedent. A collider-only entity (ColliderComponent with no Rigid3DComponent) is
/// static world geometry: detected and collided against, never moved.
struct Rigid3DComponent final {
    float mass = 1.0F;
    bool isKinematic = false;
    Math::Vector3 velocity{};
    /// Caller-owned persistent linear force in newtons; integrated as force/mass each step until
    /// changed, zero means gravity-and-drag only. Mirrors `torque`'s contract on the angular side -
    /// scripts hold a thruster with `physics.apply_force` and fire one-shots with impulses instead.
    Math::Vector3 force{};
    /// Local-space angular velocity in radians/second. Zero preserves legacy orientation behavior.
    Math::Vector3 angularVelocity{};
    /// Caller-owned persistent torque in local-space force units; zero means no angular acceleration.
    Math::Vector3 torque{};
    /// Diagonal local-space inverse inertia. Zero components make the corresponding axis immovable.
    Math::Vector3 inverseInertia{};
    /// Simple linear damping: velocity *= (1 - drag * dt) each physics step. 0 = no damping.
    float drag = 0.0F;
    /// Multiplies PhysicsSystem's gravity before integration — 1 = normal gravity, 0 =
    /// unaffected by gravity (but still collides/integrates other forces), > 1 = falls faster.
    float gravityScale = 1.0F;
};

/// Validates the value-only rigid-body contract before scene persistence and physics integration.
/// Zero mass remains valid and means an immovable body under the existing inverse-mass policy;
/// kinematic state is orthogonal to mass and remains an explicit authoring choice.
[[nodiscard]] bool IsRigid3DComponentValid(const Rigid3DComponent& rigidBody) noexcept;

/// Sanitizes `rigidBody` so mass, drag, gravityScale, inverseInertia, and motion vectors are valid.
[[nodiscard]] Rigid3DComponent SanitizeRigid3DComponent(
    const Rigid3DComponent& rigidBody) noexcept;

/// Returns the effective inverse mass (`0` when `isKinematic` or `mass <= 0`, otherwise `1 / mass`).
[[nodiscard]] float ResolveRigid3DInverseMass(const Rigid3DComponent& rigidBody) noexcept;

/// Applies an instantaneous linear impulse (`impulseNs * inverseMass`) when dynamic (`!isKinematic && mass > 0`).
void ApplyRigid3DLinearImpulse(Rigid3DComponent& rigidBody,
                                  const Math::Vector3& impulseNs) noexcept;

/// Clears accumulated persistent `force` and `torque` vectors to zero.
void ClearRigid3DForcesAndTorques(Rigid3DComponent& rigidBody) noexcept;

} // namespace UVE::Scene
