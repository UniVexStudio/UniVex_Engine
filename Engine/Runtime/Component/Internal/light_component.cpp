// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "light_component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "scalar.h"

namespace UVE::Scene {

[[nodiscard]] bool IsLightTypeValid(const LightType type) noexcept {
    return type == LightType::Directional || type == LightType::Point || type == LightType::Spot;
}

[[nodiscard]] bool IsLightComponentValid(const LightComponent& light) noexcept {
    return std::isfinite(light.color.r) && std::isfinite(light.color.g) && std::isfinite(light.color.b) &&
           light.color.r >= 0.0F && light.color.g >= 0.0F && light.color.b >= 0.0F &&
           std::isfinite(light.intensity) && light.intensity >= 0.0F && IsLightTypeValid(light.type) &&
           std::isfinite(light.range) && light.range > 0.0F && std::isfinite(light.spotAngleDegrees) &&
           light.spotAngleDegrees > 0.0F && light.spotAngleDegrees < 180.0F;
}

LightComponent SanitizeLightComponent(const LightComponent& light) noexcept {
    LightComponent out = light;
    if (!IsLightTypeValid(out.type)) {
        out.type = LightType::Directional;
    }
    out.color = Math::Color{
        (std::isfinite(out.color.r) && out.color.r >= 0.0F) ? out.color.r : 1.0F,
        (std::isfinite(out.color.g) && out.color.g >= 0.0F) ? out.color.g : 1.0F,
        (std::isfinite(out.color.b) && out.color.b >= 0.0F) ? out.color.b : 1.0F,
    };
    out.intensity =
        (std::isfinite(out.intensity) && out.intensity >= 0.0F) ? out.intensity : 1.0F;
    out.range = (std::isfinite(out.range) && out.range > 0.0F) ? out.range : 10.0F;
    out.spotAngleDegrees = std::isfinite(out.spotAngleDegrees)
                               ? std::clamp(out.spotAngleDegrees, 1.0F, 179.0F)
                               : 45.0F;
    return out;
}

Math::Color EvaluateLightRadiantColor(const LightComponent& light) noexcept {
    const LightComponent safe = SanitizeLightComponent(light);
    return Math::Color{
        safe.color.r * safe.intensity,
        safe.color.g * safe.intensity,
        safe.color.b * safe.intensity,
    };
}

float EvaluateLightDistanceAttenuation(const LightComponent& light,
                                          const float distanceMeters) noexcept {
    const LightComponent safe = SanitizeLightComponent(light);
    if (safe.type == LightType::Directional) {
        return 1.0F;
    }
    if (!std::isfinite(distanceMeters) || distanceMeters < 0.0F || distanceMeters >= safe.range) {
        return 0.0F;
    }
    const float normalized = distanceMeters / safe.range;
    const float window = std::max(0.0F, 1.0F - normalized * normalized);
    return window * window;
}

float ResolveSpotLightConeCosine(const LightComponent& light) noexcept {
    const LightComponent safe = SanitizeLightComponent(light);
    return std::cos(Math::DegToRad(safe.spotAngleDegrees));
}

} // namespace UVE::Scene
