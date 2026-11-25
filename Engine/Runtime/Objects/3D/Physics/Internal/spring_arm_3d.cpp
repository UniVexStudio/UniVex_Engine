// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spring_arm_3d.h"

#include <algorithm>
#include <cmath>

#include "i_entity_manager.h"
#include "object_3d.h"

namespace UVE::Scene {

bool IsSpringArm3DObjectComponentValid(const SpringArm3DComponent& value) noexcept {
    return std::isfinite(value.armLength) && value.armLength > 0.0F && std::isfinite(value.margin) &&
           value.margin >= 0.0F && std::isfinite(value.smoothing) && value.smoothing >= 0.0F &&
           std::isfinite(value.currentLength) && value.currentLength >= 0.0F && value.currentLength <= value.armLength;
}

bool IsSpringArm3DObjectDefinitionValid(const SpringArm3DObjectDefinition& value) noexcept {
    return IsSpringArm3DObjectComponentValid(value.springArm);
}

void ApplySpringArm3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const SpringArm3DObjectDefinition& value) {
    // SpringArm3D is Object3D plus its own component: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaseline(entityManager, entity, SpringArm3DObjectDefinition::defaultName);
    SpringArm3DComponent springArm = value.springArm;
    // Armed at full reach on day one, the same seeding the deserializer applies on load: the
    // first simulation step resolves the truth, everything before that must still be valid.
    springArm.currentLength = springArm.armLength;
    entityManager.AddComponent<SpringArm3DComponent>(entity, springArm);
}

bool SpringArm3D::IsCasting(const SpringArm3DComponent& springArm) noexcept {
    return springArm.enabled;
}

Math::Vector3 SpringArm3D::ResolveWorldAxis(const Math::Quaternion& worldRotation) noexcept {
    Math::Quaternion rotation{};
    if (!Math::TryNormalize(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVector(rotation, kArmAxis);
}

float SpringArm3D::ResolveTarget(const std::optional<float> hitDistance, const float margin,
                                       const float armLength) noexcept {
    if (!hitDistance.has_value() || !std::isfinite(*hitDistance) || !std::isfinite(armLength) ||
        armLength <= 0.0F || !std::isfinite(margin) || margin < 0.0F) {
        return armLength;
    }
    return std::clamp(*hitDistance - margin, 0.0F, armLength);
}

float SpringArm3D::ResolveLength(const float currentLength, const float targetLength,
                                       const float smoothing, const float dtSeconds) noexcept {
    if (!std::isfinite(currentLength) || !std::isfinite(targetLength) || !std::isfinite(smoothing) ||
        !std::isfinite(dtSeconds) || dtSeconds <= 0.0F) {
        return currentLength;
    }
    if (targetLength <= currentLength || smoothing <= 0.0F) {
        return targetLength;
    }
    const float blend = 1.0F - std::exp(-smoothing * dtSeconds);
    const float resolved = currentLength + (targetLength - currentLength) * blend;
    if (std::fabs(targetLength - resolved) <= kSpringArm3DCompletionTolerance) {
        return targetLength;
    }
    return resolved;
}

float ResolveSpringArm3DTarget(const std::optional<float> hitDistance, const float margin,
                                  const float armLength) noexcept {
    return SpringArm3D::ResolveTarget(hitDistance, margin, armLength);
}

float ResolveSpringArm3DLength(const float currentLength, const float targetLength,
                                  const float smoothing, const float dtSeconds) noexcept {
    return SpringArm3D::ResolveLength(currentLength, targetLength, smoothing, dtSeconds);
}

} // namespace UVE::Scene
