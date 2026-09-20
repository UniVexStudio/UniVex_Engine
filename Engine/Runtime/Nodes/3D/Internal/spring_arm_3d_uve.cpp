// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/spring_arm_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsSpringArm3DNodeComponentValidUVE(const SpringArm3DNodeComponentUVE& value) noexcept {
    return std::isfinite(value.armLength) && value.armLength > 0.0F && std::isfinite(value.margin) &&
           value.margin >= 0.0F && std::isfinite(value.smoothing) && value.smoothing >= 0.0F &&
           std::isfinite(value.currentLength) && value.currentLength >= 0.0F && value.currentLength <= value.armLength;
}

bool IsSpringArm3DNodeDefinitionValidUVE(const SpringArm3DNodeDefinitionUVE& value) noexcept {
    return IsSpringArm3DNodeComponentValidUVE(value.springArm);
}

void ApplySpringArm3DNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const SpringArm3DNodeDefinitionUVE& value) {
    // SpringArm3D is Node3D plus its own component: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, SpringArm3DNodeDefinitionUVE::defaultName);
    SpringArm3DNodeComponentUVE springArm = value.springArm;
    // Armed at full reach on day one, the same seeding the deserializer applies on load: the
    // first simulation step resolves the truth, everything before that must still be valid.
    springArm.currentLength = springArm.armLength;
    entityManager.AddComponentUVE<SpringArm3DNodeComponentUVE>(entity, springArm);
}

float ResolveSpringArm3DTargetUVE(const std::optional<float> hitDistance, const float margin,
                                  const float armLength) noexcept {
    if (!hitDistance.has_value() || !std::isfinite(*hitDistance) || !std::isfinite(armLength) ||
        armLength <= 0.0F || !std::isfinite(margin) || margin < 0.0F) {
        return armLength;
    }
    return std::clamp(*hitDistance - margin, 0.0F, armLength);
}

float ResolveSpringArm3DLengthUVE(const float currentLength, const float targetLength,
                                  const float smoothing, const float dtSeconds) noexcept {
    if (!std::isfinite(currentLength) || !std::isfinite(targetLength) || !std::isfinite(smoothing) ||
        !std::isfinite(dtSeconds) || dtSeconds <= 0.0F) {
        return currentLength;
    }
    // Retraction is a snap: blending INTO a wall is what "camera collision with smoothing"
    // ships as clipping in other engines. Extension blends out.
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

} // namespace UVE::Scene
