// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/spring_arm_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSpringArm3DObjectComponentValidUVE(const SpringArm3DComponentUVE& value) noexcept {
    return std::isfinite(value.armLength) && value.armLength > 0.0F && std::isfinite(value.margin) &&
           value.margin >= 0.0F && std::isfinite(value.smoothing) && value.smoothing >= 0.0F &&
           std::isfinite(value.currentLength) && value.currentLength >= 0.0F && value.currentLength <= value.armLength;
}

bool IsSpringArm3DObjectDefinitionValidUVE(const SpringArm3DObjectDefinitionUVE& value) noexcept {
    return IsSpringArm3DObjectComponentValidUVE(value.springArm);
}

void ApplySpringArm3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const SpringArm3DObjectDefinitionUVE& value) {
    // SpringArm3D is Object3D plus its own component: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureObject3DBaselineUVE(entityManager, entity, SpringArm3DObjectDefinitionUVE::defaultName);
    SpringArm3DComponentUVE springArm = value.springArm;
    // Armed at full reach on day one, the same seeding the deserializer applies on load: the
    // first simulation step resolves the truth, everything before that must still be valid.
    springArm.currentLength = springArm.armLength;
    entityManager.AddComponentUVE<SpringArm3DComponentUVE>(entity, springArm);
}

bool SpringArm3DUVE::IsCastingUVE(const SpringArm3DComponentUVE& springArm) noexcept {
    return springArm.enabled;
}

Math::Vector3UVE SpringArm3DUVE::ResolveWorldAxisUVE(const Math::QuaternionUVE& worldRotation) noexcept {
    Math::QuaternionUVE rotation{};
    if (!Math::TryNormalizeUVE(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVectorUVE(rotation, kArmAxisUVE);
}

float SpringArm3DUVE::ResolveTargetUVE(const std::optional<float> hitDistance, const float margin,
                                       const float armLength) noexcept {
    if (!hitDistance.has_value() || !std::isfinite(*hitDistance) || !std::isfinite(armLength) ||
        armLength <= 0.0F || !std::isfinite(margin) || margin < 0.0F) {
        return armLength;
    }
    return std::clamp(*hitDistance - margin, 0.0F, armLength);
}

float SpringArm3DUVE::ResolveLengthUVE(const float currentLength, const float targetLength,
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
    if (std::fabs(targetLength - resolved) <= kSpringArm3DCompletionToleranceUVE) {
        return targetLength;
    }
    return resolved;
}

float ResolveSpringArm3DTargetUVE(const std::optional<float> hitDistance, const float margin,
                                  const float armLength) noexcept {
    return SpringArm3DUVE::ResolveTargetUVE(hitDistance, margin, armLength);
}

float ResolveSpringArm3DLengthUVE(const float currentLength, const float targetLength,
                                  const float smoothing, const float dtSeconds) noexcept {
    return SpringArm3DUVE::ResolveLengthUVE(currentLength, targetLength, smoothing, dtSeconds);
}

} // namespace UVE::Scene
