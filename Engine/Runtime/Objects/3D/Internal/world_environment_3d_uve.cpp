// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/world_environment_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] bool IsNonNegativeColorUVE(const Math::Vector3UVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value) && value.x >= 0.0F && value.y >= 0.0F && value.z >= 0.0F;
}

[[nodiscard]] Math::Vector3UVE ScaleColorUVE(const Math::Vector3UVE& color, const Math::Vector3UVE& tint,
                                             const float energy) noexcept {
    return Math::Vector3UVE{color.x * tint.x * energy, color.y * tint.y * energy, color.z * tint.z * energy};
}

} // namespace

bool IsWorldEnvironment3DObjectComponentValidUVE(const WorldEnvironment3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.skyAssetPath) && IsNonNegativeColorUVE(value.ambientColor) &&
           IsNonNegativeColorUVE(value.fogColor) && IsNonNegativeColorUVE(value.skyColor) &&
           IsNonNegativeColorUVE(value.horizonColor) && IsNonNegativeColorUVE(value.groundColor) &&
           IsNonNegativeColorUVE(value.colorFilter) && std::isfinite(value.ambientEnergy) && value.ambientEnergy >= 0.0F &&
           std::isfinite(value.exposure) && value.exposure > 0.0F && std::isfinite(value.fogDensity) &&
           value.fogDensity >= 0.0F && std::isfinite(value.skyCurve) && value.skyCurve > 0.0F &&
           std::isfinite(value.groundCurve) && value.groundCurve > 0.0F && std::isfinite(value.fogSkyAffect) &&
           value.fogSkyAffect >= 0.0F && value.fogSkyAffect <= 1.0F && std::isfinite(value.bloomIntensity) &&
           value.bloomIntensity >= 0.0F && std::isfinite(value.bloomThreshold) && value.bloomThreshold >= 0.0F &&
           std::isfinite(value.ssaoIntensity) && value.ssaoIntensity >= 0.0F && std::isfinite(value.ssaoRadius) &&
           value.ssaoRadius > 0.0F && std::isfinite(value.brightness) && std::isfinite(value.contrast) &&
           value.contrast >= 0.0F && std::isfinite(value.saturation) && value.saturation >= 0.0F &&
           std::isfinite(value.fogHeight) && std::isfinite(value.fogHeightFalloff) && value.fogHeightFalloff > 0.0F &&
           std::isfinite(value.fogSunScatter) && value.fogSunScatter >= 0.0F && value.fogSunScatter <= 1.0F;
}

WorldEnvironmentFrameUVE ResolveWorldEnvironmentFrameUVE(IEntityManagerUVE& entityManager,
                                                        const Math::Vector3UVE& fallbackAmbient) noexcept {
    WorldEnvironmentFrameUVE frame;
    const Math::Vector3UVE fallback = Math::IsFiniteUVE(fallbackAmbient) ? fallbackAmbient : Math::Vector3UVE{};
    frame.ambientColor = fallback;
    frame.skyAmbient = fallback;
    frame.groundAmbient = fallback;
    frame.backgroundColor = fallback;
    entityManager.ForEachUVE<WorldEnvironment3DComponentUVE>(
        [&frame](EntityUVE, const WorldEnvironment3DComponentUVE& environment) {
            if (frame.hasEnvironment || !IsWorldEnvironment3DObjectComponentValidUVE(environment)) {
                return;
            }
            const Math::Vector3UVE ambient{environment.ambientColor.x * environment.ambientEnergy,
                                           environment.ambientColor.y * environment.ambientEnergy,
                                           environment.ambientColor.z * environment.ambientEnergy};
            const Math::Vector3UVE skyAmbient =
                ScaleColorUVE(environment.skyColor, environment.ambientColor, environment.ambientEnergy);
            const Math::Vector3UVE groundAmbient =
                ScaleColorUVE(environment.groundColor, environment.ambientColor, environment.ambientEnergy);
            if (!Math::IsFiniteUVE(ambient) || !Math::IsFiniteUVE(skyAmbient) || !Math::IsFiniteUVE(groundAmbient)) {
                return;
            }
            frame.ambientColor = ambient;
            frame.skyAmbient = skyAmbient;
            frame.groundAmbient = groundAmbient;
            frame.skyColor = environment.skyColor;
            frame.horizonColor = environment.horizonColor;
            frame.groundColor = environment.groundColor;
            frame.skyCurve = environment.skyCurve;
            frame.groundCurve = environment.groundCurve;
            frame.fogColor = environment.fogColor;
            frame.fogDensity = environment.fogDensity;
            frame.fogSkyAffect = environment.fogSkyAffect;
            frame.fogHeight = environment.fogHeight;
            frame.fogHeightFalloff = environment.fogHeightFalloff;
            frame.fogSunScatter = environment.fogSunScatter;
            frame.fogEnabled = environment.fogEnabled;
            frame.exposure = environment.exposure;
            frame.postProcessingEnabled = environment.postProcessingEnabled;
            frame.bloomEnabled = environment.bloomEnabled;
            frame.bloomIntensity = environment.bloomIntensity;
            frame.bloomThreshold = environment.bloomThreshold;
            frame.ssaoEnabled = environment.ssaoEnabled;
            frame.ssaoIntensity = environment.ssaoIntensity;
            frame.ssaoRadius = environment.ssaoRadius;
            frame.brightness = environment.brightness;
            frame.contrast = environment.contrast;
            frame.saturation = environment.saturation;
            frame.colorFilter = environment.colorFilter;
            frame.backgroundColor = environment.horizonColor;
            frame.hasEnvironment = true;
        });
    return frame;
}

void ApplySunToWorldEnvironmentFrameUVE(WorldEnvironmentFrameUVE& frame, const Math::Vector3UVE& sunDirection,
                                        const Math::Vector3UVE& sunColor, const float sunEnergy) noexcept {
    if (!frame.hasEnvironment || !(sunEnergy > 0.0F) || !Math::IsFiniteUVE(sunDirection) ||
        !Math::IsFiniteUVE(sunColor) || Math::LengthSquaredUVE(sunDirection) < 1.0e-8F) {
        return;
    }
    const Math::Vector3UVE sun = Math::NormalizeUVE(sunDirection);
    const float sunY = sun.y;
    const float dayT = std::clamp((sunY + 0.08F) / 0.30F, 0.0F, 1.0F);
    const float day = dayT * dayT * (3.0F - 2.0F * dayT);
    const float sunset = std::exp(-(sunY * 6.0F) * (sunY * 6.0F)) * std::clamp((sunY + 0.15F) / 0.15F, 0.0F, 1.0F);
    const float nightScale = 0.06F + 0.94F * day;
    frame.skyAmbient = Math::Vector3UVE{frame.skyAmbient.x * nightScale, frame.skyAmbient.y * nightScale,
                                        frame.skyAmbient.z * nightScale};
    frame.groundAmbient = Math::Vector3UVE{frame.groundAmbient.x * nightScale, frame.groundAmbient.y * nightScale,
                                           frame.groundAmbient.z * nightScale};
    if (sunset > 0.001F) {
        const Math::Vector3UVE warm{1.0F, 0.48F, 0.18F};
        const float skyBlend = sunset * 0.55F;
        const float groundBlend = sunset * 0.40F;
        frame.skyAmbient = Math::Vector3UVE{
            frame.skyAmbient.x + (frame.skyAmbient.x * warm.x - frame.skyAmbient.x) * skyBlend,
            frame.skyAmbient.y + (frame.skyAmbient.y * warm.y - frame.skyAmbient.y) * skyBlend,
            frame.skyAmbient.z + (frame.skyAmbient.z * warm.z - frame.skyAmbient.z) * skyBlend};
        frame.groundAmbient = Math::Vector3UVE{
            frame.groundAmbient.x + (frame.groundAmbient.x * warm.x - frame.groundAmbient.x) * groundBlend,
            frame.groundAmbient.y + (frame.groundAmbient.y * warm.y - frame.groundAmbient.y) * groundBlend,
            frame.groundAmbient.z + (frame.groundAmbient.z * warm.z - frame.groundAmbient.z) * groundBlend};
        const float fill = sunset * sunEnergy * 0.04F;
        frame.skyAmbient = Math::Vector3UVE{frame.skyAmbient.x + sunColor.x * fill,
                                            frame.skyAmbient.y + sunColor.y * fill,
                                            frame.skyAmbient.z + sunColor.z * fill};
    }
}

void ApplyWorldEnvironmentObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                            const WorldEnvironmentObjectDefinitionUVE& value) {
    EnsureObjectBaselineUVE(entityManager, entity, WorldEnvironmentObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<WorldEnvironment3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<WorldEnvironment3DComponentUVE>(entity, value.environment);
    }
}

} // namespace UVE::Scene
