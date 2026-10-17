// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/light_emitter_component_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsLightEmitterComponentValidUVE(const LightEmitterComponentUVE& component) noexcept {
    return component.bakeMode <= LightBakeModeUVE::Dynamic && IsNonNegativeUVE(component.color.r) &&
           IsNonNegativeUVE(component.color.g) && IsNonNegativeUVE(component.color.b) &&
           IsNonNegativeUVE(component.energy) && IsNonNegativeUVE(component.indirectEnergy) &&
           IsNonNegativeUVE(component.volumetricFogEnergy) && IsNonNegativeUVE(component.specular) &&
           std::isfinite(component.shadowBias) && std::isfinite(component.shadowNormalBias) &&
           std::isfinite(component.shadowOpacity) && component.shadowOpacity >= 0.0F && component.shadowOpacity <= 1.0F &&
           IsNonNegativeUVE(component.shadowBlur) && IsNonNegativeUVE(component.distanceFadeBegin) &&
           IsNonNegativeUVE(component.distanceFadeShadow) && IsNonNegativeUVE(component.distanceFadeLength);
}

LightEmitterComponentUVE SanitizeLightEmitterComponentUVE(
    const LightEmitterComponentUVE& component) noexcept {
    LightEmitterComponentUVE out = component;
    if (out.bakeMode > LightBakeModeUVE::Dynamic) {
        out.bakeMode = LightBakeModeUVE::Dynamic;
    }
    out.color = Math::ColorUVE{
        IsNonNegativeUVE(out.color.r) ? out.color.r : 1.0F,
        IsNonNegativeUVE(out.color.g) ? out.color.g : 1.0F,
        IsNonNegativeUVE(out.color.b) ? out.color.b : 1.0F,
    };
    out.energy = IsNonNegativeUVE(out.energy) ? out.energy : 1.0F;
    out.indirectEnergy = IsNonNegativeUVE(out.indirectEnergy) ? out.indirectEnergy : 1.0F;
    out.volumetricFogEnergy =
        IsNonNegativeUVE(out.volumetricFogEnergy) ? out.volumetricFogEnergy : 1.0F;
    out.specular = IsNonNegativeUVE(out.specular) ? out.specular : 0.5F;
    if (!std::isfinite(out.shadowBias)) {
        out.shadowBias = -1.0F;
    }
    if (!std::isfinite(out.shadowNormalBias)) {
        out.shadowNormalBias = -1.0F;
    }
    out.shadowOpacity =
        std::isfinite(out.shadowOpacity) ? std::clamp(out.shadowOpacity, 0.0F, 1.0F) : 1.0F;
    out.shadowBlur = IsNonNegativeUVE(out.shadowBlur) ? out.shadowBlur : 1.0F;
    out.distanceFadeBegin =
        IsNonNegativeUVE(out.distanceFadeBegin) ? out.distanceFadeBegin : 40.0F;
    out.distanceFadeShadow =
        IsNonNegativeUVE(out.distanceFadeShadow) ? out.distanceFadeShadow : 50.0F;
    out.distanceFadeLength =
        IsNonNegativeUVE(out.distanceFadeLength) ? out.distanceFadeLength : 10.0F;
    return out;
}

bool DoesLightEmitterAffectSurfaceLayerMaskUVE(const LightEmitterComponentUVE& component,
                                               const std::uint32_t surfaceLayerMask) noexcept {
    return (component.cullMask & surfaceLayerMask) != 0U;
}

bool DoesLightEmitterCastActiveShadowUVE(const LightEmitterComponentUVE& component) noexcept {
    return component.shadowEnabled && std::isfinite(component.shadowOpacity) &&
           component.shadowOpacity > 0.0F && component.cullMask != 0U;
}

float EvaluateLightEmitterDistanceFadeUVE(const LightEmitterComponentUVE& component,
                                          const float cameraDistanceMeters) noexcept {
    if (!component.distanceFadeEnabled || !std::isfinite(cameraDistanceMeters)) {
        return 1.0F;
    }
    const LightEmitterComponentUVE safe = SanitizeLightEmitterComponentUVE(component);
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
