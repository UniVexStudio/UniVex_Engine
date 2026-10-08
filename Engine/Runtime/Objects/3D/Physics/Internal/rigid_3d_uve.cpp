// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/rigid_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/collider_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {

bool IsRigid3DObjectDefinitionValidUVE(const Rigid3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) && IsRigid3DComponentValidUVE(value.body);
}

void ApplyRigid3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const Rigid3DObjectDefinitionUVE& value) {
    // Object3D > PhysicsObject3D > Rigid3D. The physics object base is what says what this body's
    // disabled state means - taken out of the world, kept as an immovable obstacle, or left
    // simulating - and how much of an overlap it yields next to another body.
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, Rigid3DObjectDefinitionUVE::defaultName);
    // Apply-if-missing, like the other body kinds: re-applying a definition must not wipe a
    // collider that was authored on the entity afterwards.
    if (!entityManager.HasComponentUVE<ColliderComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    }
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
}

bool Rigid3DUVE::IsDynamicUVE(const Rigid3DComponentUVE& rigidBody) noexcept {
    return !rigidBody.isKinematic;
}

float Rigid3DUVE::InverseMassUVE(const Rigid3DComponentUVE& rigidBody) noexcept {
    return (!IsDynamicUVE(rigidBody) || rigidBody.mass <= 0.0F) ? 0.0F : 1.0F / rigidBody.mass;
}

std::optional<Math::Vector3UVE> Rigid3DUVE::IntegrateLinearVelocityUVE(
    const Math::Vector3UVE& velocity, const Math::Vector3UVE& gravity, const float gravityScale,
    const float linearDamp, const float deltaTimeSeconds) noexcept {
    if (!Math::IsFiniteUVE(velocity) || !Math::IsFiniteUVE(gravity) || !std::isfinite(gravityScale) ||
        !std::isfinite(linearDamp) || !std::isfinite(deltaTimeSeconds) || deltaTimeSeconds < 0.0F) {
        return std::nullopt;
    }
    const float gravityStep = gravityScale * deltaTimeSeconds;
    if (!std::isfinite(gravityStep)) {
        return std::nullopt;
    }
    Math::Vector3UVE candidate = velocity + gravity * gravityStep;
    if (!Math::IsFiniteUVE(candidate)) {
        return std::nullopt;
    }
    if (linearDamp > 0.0F) {
        candidate *= std::max(0.0F, 1.0F - linearDamp * deltaTimeSeconds);
        if (!Math::IsFiniteUVE(candidate)) {
            return std::nullopt;
        }
    }
    return candidate;
}

Math::Vector3UVE Rigid3DUVE::DeflectVelocityUVE(const Math::Vector3UVE& velocity,
                                                const Math::Vector3UVE& towardOtherBody,
                                                const float friction, const float restitution) noexcept {
    const float intoSurface = Math::DotUVE(velocity, towardOtherBody);
    if (!(intoSurface > 0.0F)) {
        return velocity;
    }
    const Math::Vector3UVE normalVelocity = towardOtherBody * intoSurface;
    const Math::Vector3UVE tangentialVelocity = velocity - normalVelocity;
    const float frictionFactor = std::clamp(1.0F - friction, 0.0F, 1.0F);
    return tangentialVelocity * frictionFactor - normalVelocity * restitution;
}

} // namespace UVE::Scene
