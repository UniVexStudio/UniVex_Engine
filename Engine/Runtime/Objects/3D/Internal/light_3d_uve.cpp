// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/light_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kDegreesToRadiansUVE = 0.01745329251994329577F;

} // namespace

bool IsLight3DObjectDefinitionValidUVE(const Light3DObjectDefinitionUVE& value) noexcept {
    return IsLightComponentValidUVE(value.light);
}

void ApplyLight3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                     const Light3DObjectDefinitionUVE& value) {
    ApplyLightEmitter3DBaseUVE(entityManager, entity, Light3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<LightComponentUVE>(entity, value.light);
}

bool Light3DUVE::IsEmittingUVE(const LightComponentUVE& light) noexcept {
    return IsLightComponentValidUVE(light) && light.intensity > 0.0F &&
           ComputeRec709LuminanceUVE(light.color) > 0.0F;
}

float Light3DUVE::ComputeRec709LuminanceUVE(const Math::ColorUVE& color) noexcept {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b)) {
        return 0.0F;
    }
    const float luminance = 0.2126F * color.r + 0.7152F * color.g + 0.0722F * color.b;
    return std::max(luminance, 0.0F);
}

Math::Vector3UVE Light3DUVE::ResolveWorldDirectionUVE(
    const Math::QuaternionUVE& normalizedWorldRotation) noexcept {
    return Math::RotateVectorUVE(normalizedWorldRotation, kLocalForwardDirectionUVE);
}

float Light3DUVE::EstimateViewContributionUVE(
    const LightTypeUVE type, const Math::ColorUVE& color, const float intensity, const float range,
    const Math::Vector3UVE& lightPosition, const Math::Vector3UVE& viewPosition) noexcept {
    if (type == LightTypeUVE::Directional) {
        return std::numeric_limits<float>::infinity();
    }
    if (!std::isfinite(intensity) || intensity <= 0.0F || !Math::IsFiniteUVE(lightPosition) ||
        !Math::IsFiniteUVE(viewPosition)) {
        return 0.0F;
    }
    const float emitted = intensity * ComputeRec709LuminanceUVE(color);
    if (emitted <= 0.0F) {
        return 0.0F;
    }
    const Math::Vector3UVE toLight{lightPosition.x - viewPosition.x,
                                   lightPosition.y - viewPosition.y,
                                   lightPosition.z - viewPosition.z};
    const float distanceSquared =
        toLight.x * toLight.x + toLight.y * toLight.y + toLight.z * toLight.z;
    if (range > 0.0F && distanceSquared > range * range) {
        return 0.0F;
    }
    return emitted / std::max(distanceSquared, 1.0F);
}

float Light3DUVE::EvaluateSpotConeWeightUVE(
    const float spotAngleDegrees, const Math::Vector3UVE& lightWorldDirection,
    const Math::Vector3UVE& lightToTargetDirection) noexcept {
    if (!std::isfinite(spotAngleDegrees) || spotAngleDegrees <= 0.0F || spotAngleDegrees > 179.0F ||
        !Math::IsFiniteUVE(lightWorldDirection) || !Math::IsFiniteUVE(lightToTargetDirection)) {
        return 0.0F;
    }
    const float halfAngleRadians = 0.5F * spotAngleDegrees * kDegreesToRadiansUVE;
    const float cosOuter = std::cos(halfAngleRadians);
    const float cosTheta = Math::DotUVE(lightWorldDirection, lightToTargetDirection);
    if (cosTheta <= cosOuter) {
        return 0.0F;
    }
    const float denom = std::max(1.0F - cosOuter, 1e-5F);
    return std::clamp((cosTheta - cosOuter) / denom, 0.0F, 1.0F);
}

} // namespace UVE::Scene
