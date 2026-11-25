// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "rigid_3d.h"

#include <algorithm>
#include <cmath>

#include "collider_component.h"
#include "i_entity_manager.h"
#include "abstract_physics_objects_3d.h"

namespace UVE::Scene {

bool IsRigid3DObjectDefinitionValid(const Rigid3DObjectDefinition& value) noexcept {
    return IsColliderComponentValid(value.collider) && IsRigid3DComponentValid(value.body);
}

void ApplyRigid3DObjectDefinition(IEntityManager& entityManager, const Entity entity, const Rigid3DObjectDefinition& value) {
    // Object3D > PhysicsObject3D > Rigid3D. The physics object base is what says what this body's
    // disabled state means - taken out of the world, kept as an immovable obstacle, or left
    // simulating - and how much of an overlap it yields next to another body.
    ApplyPhysicsObject3DBase(entityManager, entity, Rigid3DObjectDefinition::defaultName);
    // Apply-if-missing, like the other body kinds: re-applying a definition must not wipe a
    // collider that was authored on the entity afterwards.
    if (!entityManager.HasComponent<ColliderComponent>(entity)) {
        entityManager.AddComponent<ColliderComponent>(entity, value.collider);
    }
    entityManager.AddComponent<Rigid3DComponent>(entity, value.body);
}

bool Rigid3D::IsDynamic(const Rigid3DComponent& rigidBody) noexcept {
    return !rigidBody.isKinematic;
}

float Rigid3D::InverseMass(const Rigid3DComponent& rigidBody) noexcept {
    return ResolveRigid3DInverseMass(rigidBody);
}

bool Rigid3D::ApplyForce(IEntityManager& entityManager, const Entity entity,
                               const Math::Vector3& force) noexcept {
    if (!Math::IsFinite(force) || !entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Rigid3DComponent>(entity)) {
        return false;
    }
    entityManager.GetComponent<Rigid3DComponent>(entity).force = force;
    return true;
}

bool Rigid3D::ApplyImpulse(IEntityManager& entityManager, const Entity entity,
                                 const Math::Vector3& impulse) noexcept {
    if (!Math::IsFinite(impulse) || !entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Rigid3DComponent>(entity)) {
        return false;
    }
    Rigid3DComponent& body = entityManager.GetComponent<Rigid3DComponent>(entity);
    const float inverseMass = InverseMass(body);
    if (inverseMass <= 0.0F) {
        return false;
    }
    const Math::Vector3 candidate = body.velocity + impulse * inverseMass;
    if (!Math::IsFinite(candidate)) {
        return false;
    }
    body.velocity = candidate;
    return true;
}

bool Rigid3D::ApplyTorque(IEntityManager& entityManager, const Entity entity,
                                const Math::Vector3& torque) noexcept {
    if (!Math::IsFinite(torque) || !entityManager.IsAlive(entity) ||
        !entityManager.HasComponent<Rigid3DComponent>(entity)) {
        return false;
    }
    entityManager.GetComponent<Rigid3DComponent>(entity).torque = torque;
    return true;
}

std::optional<Math::Vector3> Rigid3D::IntegrateLinearVelocity(
    const Math::Vector3& velocity, const Math::Vector3& gravity, const float gravityScale,
    const float linearDamp, const float deltaTimeSeconds, const Math::Vector3& force,
    const float inverseMass) noexcept {
    if (!Math::IsFinite(velocity) || !Math::IsFinite(gravity) || !std::isfinite(gravityScale) ||
        !std::isfinite(linearDamp) || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0F ||
        !Math::IsFinite(force) || !std::isfinite(inverseMass) || inverseMass < 0.0F) {
        return std::nullopt;
    }
    const float gravityStep = gravityScale * deltaTimeSeconds;
    const float forceStep = inverseMass * deltaTimeSeconds;
    if (!std::isfinite(gravityStep) || !std::isfinite(forceStep)) {
        return std::nullopt;
    }
    // The force term deliberately ignores gravityScale: a thruster works in zero gravity.
    Math::Vector3 candidate = velocity + gravity * gravityStep + force * forceStep;
    if (!Math::IsFinite(candidate)) {
        return std::nullopt;
    }
    if (linearDamp > 0.0F) {
        candidate *= std::max(0.0F, 1.0F - linearDamp * deltaTimeSeconds);
        if (!Math::IsFinite(candidate)) {
            return std::nullopt;
        }
    }
    return candidate;
}

Math::Vector3 Rigid3D::DeflectVelocity(const Math::Vector3& velocity,
                                                const Math::Vector3& towardOtherBody,
                                                const float friction, const float restitution) noexcept {
    const float intoSurface = Math::Dot(velocity, towardOtherBody);
    if (!(intoSurface > 0.0F)) {
        return velocity;
    }
    const Math::Vector3 normalVelocity = towardOtherBody * intoSurface;
    const Math::Vector3 tangentialVelocity = velocity - normalVelocity;
    const float frictionFactor = std::clamp(1.0F - friction, 0.0F, 1.0F);
    return tangentialVelocity * frictionFactor - normalVelocity * restitution;
}

} // namespace UVE::Scene
