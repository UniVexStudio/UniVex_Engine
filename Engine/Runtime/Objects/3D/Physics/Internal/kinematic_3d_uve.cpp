// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/kinematic_3d_uve.h"

#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_physics_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsUsableVelocityUVE(const Math::Vector3UVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value);
}

} // namespace

bool IsKinematic3DObjectComponentValidUVE(const Kinematic3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.targetVelocity) && std::isfinite(value.interpolation) &&
           value.interpolation >= 0.0F && value.interpolation <= 1.0F;
}

bool IsKinematic3DObjectDefinitionValidUVE(const Kinematic3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider) && IsRigid3DComponentValidUVE(value.body) &&
           value.body.isKinematic && IsKinematic3DObjectComponentValidUVE(value.animatableBody);
}

void ApplyKinematic3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const Kinematic3DObjectDefinitionUVE& value) {
    ApplyPhysicsObject3DBaseUVE(entityManager, entity, Kinematic3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(entity, value.body);
    entityManager.AddComponentUVE<Kinematic3DComponentUVE>(entity, value.animatableBody);
}

bool Kinematic3DUVE::IsDrivingUVE(const Kinematic3DComponentUVE& kinematic) noexcept {
    return kinematic.active;
}

float Kinematic3DUVE::EaseBlendUVE(const float interpolation, const float deltaTimeSeconds) noexcept {
    if (!std::isfinite(interpolation) || interpolation <= 0.0F) {
        return 0.0F;
    }
    if (interpolation >= 1.0F) {
        return 1.0F;
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0.0F;
    }
    return 1.0F - std::pow(1.0F - interpolation, deltaTimeSeconds);
}

Math::Vector3UVE Kinematic3DUVE::ResolveWorldTargetUVE(const Math::Vector3UVE& localTarget,
                                                       const Math::QuaternionUVE& worldRotation) noexcept {
    if (!IsUsableVelocityUVE(localTarget)) {
        return {};
    }
    Math::QuaternionUVE rotation{};
    if (!Math::TryNormalizeUVE(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVectorUVE(rotation, localTarget);
}

Math::Vector3UVE Kinematic3DUVE::EaseVelocityUVE(const Math::Vector3UVE& currentWorldVelocity,
                                                 const Math::Vector3UVE& worldTarget, const float interpolation,
                                                 const float deltaTimeSeconds) noexcept {
    Math::Vector3UVE velocity = IsUsableVelocityUVE(currentWorldVelocity) ? currentWorldVelocity : Math::Vector3UVE{};
    const Math::Vector3UVE target = IsUsableVelocityUVE(worldTarget) ? worldTarget : Math::Vector3UVE{};
    const float blend = EaseBlendUVE(interpolation, deltaTimeSeconds);
    if (blend >= 1.0F) {
        velocity = target;
    } else if (blend > 0.0F) {
        velocity += (target - velocity) * blend;
    }
    return IsUsableVelocityUVE(velocity) ? velocity : Math::Vector3UVE{};
}

} // namespace UVE::Scene
