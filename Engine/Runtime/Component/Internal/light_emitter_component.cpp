// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "light_emitter_component.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsLightEmitterComponentValid(const LightEmitterComponent& component) noexcept {
    return component.bakeMode <= LightBakeMode::Dynamic && IsNonNegative(component.color.r) &&
           IsNonNegative(component.color.g) && IsNonNegative(component.color.b) &&
           IsNonNegative(component.energy) && IsNonNegative(component.indirectEnergy) &&
           IsNonNegative(component.volumetricFogEnergy) && IsNonNegative(component.specular) &&
           std::isfinite(component.shadowBias) && std::isfinite(component.shadowNormalBias) &&
           std::isfinite(component.shadowOpacity) && component.shadowOpacity >= 0.0F && component.shadowOpacity <= 1.0F &&
           IsNonNegative(component.shadowBlur) && IsNonNegative(component.distanceFadeBegin) &&
           IsNonNegative(component.distanceFadeShadow) && IsNonNegative(component.distanceFadeLength);
}

LightEmitterComponent SanitizeLightEmitterComponent(
    const LightEmitterComponent& component) noexcept {
    LightEmitterComponent out = component;
    if (out.bakeMode > LightBakeMode::Dynamic) {
        out.bakeMode = LightBakeMode::Dynamic;
    }
    out.color = Math::Color{
        IsNonNegative(out.color.r) ? out.color.r : 1.0F,
        IsNonNegative(out.color.g) ? out.color.g : 1.0F,
        IsNonNegative(out.color.b) ? out.color.b : 1.0F,
    };
    out.energy = IsNonNegative(out.energy) ? out.energy : 1.0F;
    out.indirectEnergy = IsNonNegative(out.indirectEnergy) ? out.indirectEnergy : 1.0F;
    out.volumetricFogEnergy =
        IsNonNegative(out.volumetricFogEnergy) ? out.volumetricFogEnergy : 1.0F;
    out.specular = IsNonNegative(out.specular) ? out.specular : 0.5F;
    if (!std::isfinite(out.shadowBias)) {
        out.shadowBias = -1.0F;
    }
    if (!std::isfinite(out.shadowNormalBias)) {
        out.shadowNormalBias = -1.0F;
    }
    out.shadowOpacity =
        std::isfinite(out.shadowOpacity) ? std::clamp(out.shadowOpacity, 0.0F, 1.0F) : 1.0F;
    out.shadowBlur = IsNonNegative(out.shadowBlur) ? out.shadowBlur : 1.0F;
    out.distanceFadeBegin =
        IsNonNegative(out.distanceFadeBegin) ? out.distanceFadeBegin : 40.0F;
    out.distanceFadeShadow =
        IsNonNegative(out.distanceFadeShadow) ? out.distanceFadeShadow : 50.0F;
    out.distanceFadeLength =
        IsNonNegative(out.distanceFadeLength) ? out.distanceFadeLength : 10.0F;
    return out;
}

bool DoesLightEmitterAffectSurfaceLayerMask(const LightEmitterComponent& component,
                                               const std::uint32_t surfaceLayerMask) noexcept {
    return (component.cullMask & surfaceLayerMask) != 0U;
}

bool DoesLightEmitterCastActiveShadow(const LightEmitterComponent& component) noexcept {
    return component.shadowEnabled && std::isfinite(component.shadowOpacity) &&
           component.shadowOpacity > 0.0F && component.cullMask != 0U;
}

float EvaluateLightEmitterDistanceFade(const LightEmitterComponent& component,
                                          const float cameraDistanceMeters) noexcept {
    if (!component.distanceFadeEnabled || !std::isfinite(cameraDistanceMeters)) {
        return 1.0F;
    }
    const LightEmitterComponent safe = SanitizeLightEmitterComponent(component);
    if (cameraDistanceMeters <= safe.distanceFadeBegin) {
        return 1.0F;
    }
    if (safe.distanceFadeLength <= 1e-5F) {
        return 0.0F;
    }
    const float t = (cameraDistanceMeters - safe.distanceFadeBegin) / safe.distanceFadeLength;
    return std::clamp(1.0F - t, 0.0F, 1.0F);
}

} // namespace UVE::Scene
