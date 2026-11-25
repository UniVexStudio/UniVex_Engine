// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "light_3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "i_entity_manager.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;

} // namespace

bool IsLight3DObjectDefinitionValid(const Light3DObjectDefinition& value) noexcept {
    return IsLightComponentValid(value.light);
}

void ApplyLight3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                     const Light3DObjectDefinition& value) {
    ApplyLightEmitter3DBase(entityManager, entity, Light3DObjectDefinition::defaultName);
    entityManager.AddComponent<LightComponent>(entity, value.light);
}

bool Light3D::IsEmitting(const LightComponent& light) noexcept {
    return IsLightComponentValid(light) && light.intensity > 0.0F &&
           ComputeRec709Luminance(light.color) > 0.0F;
}

float Light3D::ComputeRec709Luminance(const Math::Color& color) noexcept {
    if (!std::isfinite(color.r) || !std::isfinite(color.g) || !std::isfinite(color.b)) {
        return 0.0F;
    }
    const float luminance = 0.2126F * color.r + 0.7152F * color.g + 0.0722F * color.b;
    return std::max(luminance, 0.0F);
}

Math::Vector3 Light3D::ResolveWorldDirection(
    const Math::Quaternion& normalizedWorldRotation) noexcept {
    return Math::RotateVector(normalizedWorldRotation, kLocalForwardDirection);
}

float Light3D::EstimateViewContribution(
    const LightType type, const Math::Color& color, const float intensity, const float range,
    const Math::Vector3& lightPosition, const Math::Vector3& viewPosition) noexcept {
    if (!std::isfinite(intensity) || intensity <= 0.0F) {
        return 0.0F;
    }
    const float emitted = intensity * ComputeRec709Luminance(color);
    if (emitted <= 0.0F) {
        return 0.0F;
    }
    if (type == LightType::Directional) {
        return std::numeric_limits<float>::infinity();
    }
    if (!Math::IsFinite(lightPosition) || !Math::IsFinite(viewPosition)) {
        return 0.0F;
    }
    const Math::Vector3 toLight{lightPosition.x - viewPosition.x,
                                   lightPosition.y - viewPosition.y,
                                   lightPosition.z - viewPosition.z};
    const float distanceSquared =
        toLight.x * toLight.x + toLight.y * toLight.y + toLight.z * toLight.z;
    if (range > 0.0F && distanceSquared > range * range) {
        return 0.0F;
    }
    return emitted / std::max(distanceSquared, 1.0F);
}

float Light3D::EvaluateSpotConeWeight(
    const float spotAngleDegrees, const Math::Vector3& lightWorldDirection,
    const Math::Vector3& lightToTargetDirection) noexcept {
    if (!std::isfinite(spotAngleDegrees) || spotAngleDegrees <= 0.0F || spotAngleDegrees > 179.0F ||
        !Math::IsFinite(lightWorldDirection) || !Math::IsFinite(lightToTargetDirection)) {
        return 0.0F;
    }
    const float halfAngleRadians = 0.5F * spotAngleDegrees * kDegreesToRadians;
    const float cosOuter = std::cos(halfAngleRadians);
    const float cosTheta = Math::Dot(lightWorldDirection, lightToTargetDirection);
    if (cosTheta <= cosOuter) {
        return 0.0F;
    }
    const float denom = std::max(1.0F - cosOuter, 1e-5F);
    return std::clamp((cosTheta - cosOuter) / denom, 0.0F, 1.0F);
}

} // namespace UVE::Scene
