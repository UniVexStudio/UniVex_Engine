// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/light_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "uve/math/scalar_uve.h"

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

LightComponentUVE SanitizeLightComponentUVE(const LightComponentUVE& light) noexcept {
    LightComponentUVE out = light;
    if (!IsLightTypeValidUVE(out.type)) {
        out.type = LightTypeUVE::Directional;
    }
    out.color = Math::ColorUVE{
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

Math::ColorUVE EvaluateLightRadiantColorUVE(const LightComponentUVE& light) noexcept {
    const LightComponentUVE safe = SanitizeLightComponentUVE(light);
    return Math::ColorUVE{
        safe.color.r * safe.intensity,
        safe.color.g * safe.intensity,
        safe.color.b * safe.intensity,
    };
}

float EvaluateLightDistanceAttenuationUVE(const LightComponentUVE& light,
                                          const float distanceMeters) noexcept {
    const LightComponentUVE safe = SanitizeLightComponentUVE(light);
    if (safe.type == LightTypeUVE::Directional) {
        return 1.0F;
    }
    if (!std::isfinite(distanceMeters) || distanceMeters < 0.0F || distanceMeters >= safe.range) {
        return 0.0F;
    }
    const float normalized = distanceMeters / safe.range;
    const float window = std::max(0.0F, 1.0F - normalized * normalized);
    return window * window;
}

float ResolveSpotLightConeCosineUVE(const LightComponentUVE& light) noexcept {
    const LightComponentUVE safe = SanitizeLightComponentUVE(light);
    return std::cos(Math::DegToRadUVE(safe.spotAngleDegrees));
}

} // namespace UVE::Scene
