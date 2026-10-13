// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/light_component_uve.h"

#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsLightTypeValidUVE(const LightTypeUVE type) noexcept {
    return type == LightTypeUVE::Directional || type == LightTypeUVE::Point || type == LightTypeUVE::Spot;
}

[[nodiscard]] bool IsLightComponentValidUVE(const LightComponentUVE& light) noexcept {
    return std::isfinite(light.color.r) && std::isfinite(light.color.g) && std::isfinite(light.color.b) &&
           light.color.r >= 0.0F && light.color.g >= 0.0F && light.color.b >= 0.0F &&
           std::isfinite(light.intensity) && light.intensity >= 0.0F && IsLightTypeValidUVE(light.type) &&
           std::isfinite(light.range) && light.range > 0.0F && std::isfinite(light.spotAngleDegrees) &&
           light.spotAngleDegrees > 0.0F && light.spotAngleDegrees < 180.0F;
}

} // namespace UVE::Scene
