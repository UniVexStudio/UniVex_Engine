// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/light_emitter_component_uve.h"

#include <cmath>

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

} // namespace

bool IsLightEmitterComponentValidUVE(const LightEmitterComponentUVE& component) noexcept {
    return component.bakeMode <= LightBakeModeUVE::Dynamic && IsNonNegativeUVE(component.color.x) &&
           IsNonNegativeUVE(component.color.y) && IsNonNegativeUVE(component.color.z) &&
           IsNonNegativeUVE(component.energy) && IsNonNegativeUVE(component.indirectEnergy) &&
           IsNonNegativeUVE(component.volumetricFogEnergy) && IsNonNegativeUVE(component.specular) &&
           std::isfinite(component.shadowBias) && std::isfinite(component.shadowNormalBias) &&
           std::isfinite(component.shadowOpacity) && component.shadowOpacity >= 0.0F && component.shadowOpacity <= 1.0F &&
           IsNonNegativeUVE(component.shadowBlur) && IsNonNegativeUVE(component.distanceFadeBegin) &&
           IsNonNegativeUVE(component.distanceFadeShadow) && IsNonNegativeUVE(component.distanceFadeLength);
}

} // namespace UVE::Scene
