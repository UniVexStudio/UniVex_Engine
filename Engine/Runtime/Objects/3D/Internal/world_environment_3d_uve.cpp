// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/world_environment_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/sky_3d_uve.h"

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
    return IsBounded3DObjectStringUVE(value.skyAssetPath) &&
           (value.ambientSource == WorldEnvironmentAmbientSourceUVE::None ||
            value.ambientSource == WorldEnvironmentAmbientSourceUVE::FlatColor ||
            value.ambientSource == WorldEnvironmentAmbientSourceUVE::Sky ||
            value.ambientSource == WorldEnvironmentAmbientSourceUVE::EnvironmentMap) &&
           IsNonNegativeColorUVE(value.ambientColor) &&
           IsNonNegativeColorUVE(value.fogColor) && IsNonNegativeColorUVE(value.skyColor) &&
           IsNonNegativeColorUVE(value.horizonColor) && IsNonNegativeColorUVE(value.groundColor) &&
           IsNonNegativeColorUVE(value.colorFilter) && std::isfinite(value.ambientEnergy) && value.ambientEnergy >= 0.0F &&
           std::isfinite(value.exposure) && value.exposure > 0.0F && std::isfinite(value.fogDensity) &&
           value.fogDensity >= 0.0F &&
           (value.fogMode == WorldEnvironmentFogModeUVE::Linear ||
            value.fogMode == WorldEnvironmentFogModeUVE::Exponential ||
            value.fogMode == WorldEnvironmentFogModeUVE::Height) && std::isfinite(value.fogStart) &&
           value.fogStart >= 0.0F && std::isfinite(value.fogEnd) && value.fogEnd > value.fogStart &&
           std::isfinite(value.skyCurve) && value.skyCurve > 0.0F &&
           std::isfinite(value.groundCurve) && value.groundCurve > 0.0F && std::isfinite(value.fogSkyAffect) &&
           value.fogSkyAffect >= 0.0F && value.fogSkyAffect <= 1.0F && std::isfinite(value.bloomIntensity) &&
           value.bloomIntensity >= 0.0F && std::isfinite(value.bloomThreshold) && value.bloomThreshold >= 0.0F &&
           std::isfinite(value.bloomSoftKnee) && value.bloomSoftKnee >= 0.0F && value.bloomSoftKnee <= 1.0F &&
           value.bloomMipCount >= 1U && value.bloomMipCount <= kMaximumWorldEnvironmentBloomMipCountUVE &&
           std::isfinite(value.ssaoIntensity) && value.ssaoIntensity >= 0.0F && std::isfinite(value.ssaoRadius) &&
           value.ssaoRadius > 0.0F && std::isfinite(value.brightness) && std::isfinite(value.contrast) &&
           value.contrast >= 0.0F && std::isfinite(value.saturation) && value.saturation >= 0.0F &&
           std::isfinite(value.vignetteIntensity) && value.vignetteIntensity >= 0.0F &&
           value.vignetteIntensity <= 1.0F && std::isfinite(value.vignetteRadius) &&
           value.vignetteRadius >= 0.0F && value.vignetteRadius <= 1.0F &&
           std::isfinite(value.chromaticAberrationIntensity) && value.chromaticAberrationIntensity >= 0.0F &&
           value.chromaticAberrationIntensity <= 1.0F && std::isfinite(value.filmGrainIntensity) &&
           value.filmGrainIntensity >= 0.0F && value.filmGrainIntensity <= 1.0F &&
           std::isfinite(value.lensDistortionIntensity) && value.lensDistortionIntensity >= 0.0F &&
           value.lensDistortionIntensity <= 1.0F &&
           (value.depthOfFieldFocusMode == WorldEnvironmentDepthOfFieldFocusModeUVE::Manual ||
            value.depthOfFieldFocusMode == WorldEnvironmentDepthOfFieldFocusModeUVE::ScreenCenter) &&
           (value.depthOfFieldBokehShape == WorldEnvironmentDepthOfFieldBokehShapeUVE::Circular ||
            value.depthOfFieldBokehShape == WorldEnvironmentDepthOfFieldBokehShapeUVE::Hexagonal) &&
           std::isfinite(value.depthOfFieldFocusDistance) && value.depthOfFieldFocusDistance >= 0.1F &&
           std::isfinite(value.depthOfFieldAperture) &&
           value.depthOfFieldAperture >= 0.0F && value.depthOfFieldAperture <= 1.0F &&
           value.depthOfFieldQuality <= 2U && std::isfinite(value.motionBlurStrength) &&
           value.motionBlurStrength >= 0.0F && value.motionBlurStrength <= 1.0F &&
           (value.motionBlurSampleCount == 4U || value.motionBlurSampleCount == 8U ||
            value.motionBlurSampleCount == 12U) && std::isfinite(value.fogHeight) &&
           std::isfinite(value.fogHeightFalloff) && value.fogHeightFalloff > 0.0F &&
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
            frame.ambientSource = environment.ambientSource;
            frame.ambientColor = environment.ambientSource == WorldEnvironmentAmbientSourceUVE::None
                                     ? Math::Vector3UVE{}
                                     : ambient;
            frame.skyAmbient = environment.ambientSource == WorldEnvironmentAmbientSourceUVE::Sky ||
                                       environment.ambientSource == WorldEnvironmentAmbientSourceUVE::EnvironmentMap
                                   ? skyAmbient
                                   : Math::Vector3UVE{};
            frame.groundAmbient = environment.ambientSource == WorldEnvironmentAmbientSourceUVE::Sky ||
                                          environment.ambientSource == WorldEnvironmentAmbientSourceUVE::EnvironmentMap
                                      ? groundAmbient
                                      : Math::Vector3UVE{};
            frame.skyColor = environment.skyColor;
            frame.horizonColor = environment.horizonColor;
            frame.groundColor = environment.groundColor;
            frame.skyCurve = environment.skyCurve;
            frame.groundCurve = environment.groundCurve;
            frame.fogColor = environment.fogColor;
            frame.fogDensity = environment.fogDensity;
            frame.fogMode = environment.fogMode;
            frame.fogStart = environment.fogStart;
            frame.fogEnd = environment.fogEnd;
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
            frame.bloomSoftKnee = environment.bloomSoftKnee;
            frame.bloomMipCount = environment.bloomMipCount;
            frame.ssaoEnabled = environment.ssaoEnabled;
            frame.ssaoIntensity = environment.ssaoIntensity;
            frame.ssaoRadius = environment.ssaoRadius;
            frame.brightness = environment.brightness;
            frame.contrast = environment.contrast;
            frame.saturation = environment.saturation;
            frame.vignetteIntensity = environment.vignetteIntensity;
            frame.vignetteRadius = environment.vignetteRadius;
            frame.chromaticAberrationIntensity = environment.chromaticAberrationIntensity;
            frame.filmGrainIntensity = environment.filmGrainIntensity;
            frame.lensDistortionIntensity = environment.lensDistortionIntensity;
            frame.depthOfFieldEnabled = environment.depthOfFieldEnabled;
            frame.depthOfFieldFocusMode = environment.depthOfFieldFocusMode;
            frame.depthOfFieldBokehShape = environment.depthOfFieldBokehShape;
            frame.depthOfFieldFocusDistance = environment.depthOfFieldFocusDistance;
            frame.depthOfFieldAperture = environment.depthOfFieldAperture;
            frame.depthOfFieldQuality = environment.depthOfFieldQuality;
            frame.motionBlurEnabled = environment.motionBlurEnabled;
            frame.motionBlurStrength = environment.motionBlurStrength;
            frame.motionBlurSampleCount = environment.motionBlurSampleCount;
            frame.colorFilter = environment.colorFilter;
            frame.backgroundColor = environment.horizonColor;
            frame.hasEnvironment = true;
            frame.skyAssetPath = environment.skyAssetPath;
        });

    // The Sky's own fields win over the environment's. A scene that has both is saying two
    // different things - "this is what the sky looks like" and "this is how the finished frame is
    // processed" - and they are edited at different rates, so the sky half comes from the Sky and
    // every other field above stays the environment's.
    //
    // Only sky fields are touched, and only by the first valid, enabled Sky: a second Sky in the
    // same scene is not blended in, because two skies over one horizon has no meaning. A Sky with
    // no enabled state or a field out of range is skipped rather than half-applied.
    bool skyApplied = false;
    entityManager.ForEachUVE<Sky3DComponentUVE>([&frame, &skyApplied](EntityUVE, const Sky3DComponentUVE& sky) {
        if (skyApplied || !sky.enabled || !IsSky3DObjectComponentValidUVE(sky)) {
            return;
        }
        const SkySourceUVE source = sky.source;
        // A panorama with no image has nothing to sample. Falling through to the gradient would be
        // a different sky than the one selected, so the whole Sky is skipped and the scene keeps
        // whatever it had - the same answer as an empty Sky asset on the environment.
        if (source == SkySourceUVE::Panorama && sky.panoramaPath.empty()) {
            return;
        }
        frame.skyColor = sky.skyColor;
        frame.horizonColor = sky.horizonColor;
        frame.groundColor = sky.groundColor;
        frame.skyCurve = sky.skyCurve;
        frame.groundCurve = sky.groundCurve;
        frame.backgroundColor = sky.horizonColor;
        frame.skyAssetPath = source == SkySourceUVE::Panorama ? sky.panoramaPath : std::string{};
        frame.atmosphericSky = source == SkySourceUVE::Atmosphere;
        frame.atmosphereRayleigh = sky.atmosphereRayleigh;
        frame.atmosphereMie = sky.atmosphereMie;
        frame.atmosphereOzone = sky.atmosphereOzone;
        frame.atmosphereMieAnisotropy = sky.atmosphereMieAnisotropy;
        frame.atmosphereMultiScatter = sky.atmosphereMultiScatter;
        frame.atmosphereIntensity = sky.atmosphereIntensity;
        frame.atmosphereSamples = sky.atmosphereSamples;
        frame.starDensity = sky.starDensity;
        frame.starBrightness = sky.starBrightness;
        frame.starSeed = sky.starSeed;
        frame.milkyWayStrength = sky.milkyWayStrength;
        frame.cloudsEnabled = sky.cloudsEnabled;
        frame.cloudCoverage = sky.cloudCoverage;
        frame.cloudDensity = sky.cloudDensity;
        frame.cloudAltitude = sky.cloudAltitude;
        // Bearing resolved to metres per second here, once, rather than in the render path: the
        // shader wants a velocity it can multiply by time, not an angle it would convert and
        // convert again every frame.
        const float windRadians = sky.cloudWindDirectionDegrees * 3.14159265358979323846F / 180.0F;
        frame.cloudWind = Math::Vector2UVE{std::cos(windRadians) * sky.cloudWindSpeed,
                                           std::sin(windRadians) * sky.cloudWindSpeed};
        frame.cloudSoftness = sky.cloudSoftness;
        frame.cloudSilverLining = sky.cloudSilverLining;
        frame.skyAmbientLinked = sky.ambientFromSky;
        frame.skyAmbientStrength = sky.ambientStrength;
        frame.skyRayleighDensity = sky.atmosphereRayleigh;
        frame.skyReflectionsLinked = sky.reflectionsFromSky;
        // Fog is a blend, not a replacement: strength 0 leaves the environment's fog colour
        // exactly as authored, which is what makes the switch safe to flip back and forth.
        if (sky.fogFromSky) {
            const Math::Vector3UVE skyFog = ResolveSkyFogColorUVE(sky.groundColor, sky.horizonColor);
            const float amount = std::clamp(sky.fogStrength, 0.0F, 1.0F);
            frame.fogColor = Math::Vector3UVE{frame.fogColor.x + (skyFog.x - frame.fogColor.x) * amount,
                                              frame.fogColor.y + (skyFog.y - frame.fogColor.y) * amount,
                                              frame.fogColor.z + (skyFog.z - frame.fogColor.z) * amount};
            frame.skyFogLinked = true;
        }
        frame.hasEnvironment = true;
        skyApplied = true;
    });
    return frame;
}

bool TryMakeSkyEquirectUvUVE(const Math::Vector3UVE& direction, Math::Vector2UVE& outUv) noexcept {
    outUv = Math::Vector2UVE{};
    if (!Math::IsFiniteUVE(direction) || Math::LengthSquaredUVE(direction) < 1.0e-20F) {
        return false;
    }
    const Math::Vector3UVE n = Math::NormalizeUVE(direction);
    if (!Math::IsFiniteUVE(n)) {
        return false;
    }
    float longitude = 0.0F;
    if (std::fabs(n.x) + std::fabs(n.z) >= 1.0e-8F) {
        longitude = std::atan2(n.z, n.x);
    }
    const float latitude = std::asin(std::clamp(n.y, -1.0F, 1.0F));
    if (!std::isfinite(longitude) || !std::isfinite(latitude)) {
        return false;
    }
    outUv = Math::Vector2UVE{longitude / (2.0F * Math::kPiUVE) + 0.5F, latitude / Math::kPiUVE + 0.5F};
    return std::isfinite(outUv.x) && std::isfinite(outUv.y);
}

void ApplySunToWorldEnvironmentFrameUVE(WorldEnvironmentFrameUVE& frame, const Math::Vector3UVE& sunDirection,
                                        const Math::Vector3UVE& sunColor, const float sunEnergy) noexcept {
    const bool sunUsable = sunEnergy > 0.0F && Math::IsFiniteUVE(sunDirection) && Math::IsFiniteUVE(sunColor) &&
                           Math::LengthSquaredUVE(sunDirection) >= 1.0e-8F;

    // The Sky's link owns the ambient outright when it is on, and runs before the environment's own
    // tuning below so the two cannot both write it. That tuning is a hand-picked dim-and-warm that
    // approximates a sunset; this resolves the same thing from the bands and the sun's altitude
    // through real air, so letting both run would apply the approximation on top of the answer.
    //
    // `ambientSource == None` still wins over the link: an environment that says "no ambient" has
    // said something more specific than a Sky that offers some. Reflections are not ambient, so a
    // None source does not turn them off - only the sun's altitude does.
    const bool linkActive = (frame.skyAmbientLinked || frame.skyReflectionsLinked) && sunUsable;
    bool reflectionsResolved = false;
    if (linkActive) {
        const Math::Vector3UVE sun = Math::NormalizeUVE(sunDirection);
        const SkyAmbientUVE ambient = ResolveSkyAmbientUVE(frame.skyColor, frame.horizonColor, frame.groundColor,
                                                           frame.skyRayleighDensity, frame.skyAmbientStrength, sun);
        if (frame.skyAmbientLinked && frame.ambientSource != WorldEnvironmentAmbientSourceUVE::None) {
            frame.skyAmbient = ambient.sky;
            frame.groundAmbient = ambient.ground;
        }
        if (frame.skyReflectionsLinked) {
            // Reflections show the sky's own radiance, not its irradiance: a reflective surface is
            // a mirror, so it should show the colour that was drawn rather than the dimmer light
            // that colour throws. Night drives all three to zero, which is what a mirror under a
            // dark sky shows.
            const float daylight = ambient.daylight;
            frame.reflectionSky = Math::Vector3UVE{frame.skyColor.x * daylight, frame.skyColor.y * daylight,
                                                   frame.skyColor.z * daylight};
            frame.reflectionHorizon = Math::Vector3UVE{frame.horizonColor.x * daylight, frame.horizonColor.y * daylight,
                                                       frame.horizonColor.z * daylight};
            frame.reflectionGround = Math::Vector3UVE{frame.groundColor.x * daylight, frame.groundColor.y * daylight,
                                                      frame.groundColor.z * daylight};
            reflectionsResolved = true;
        }
    }

    // With no sun to place the sky against there is no day or night, so the reflected bands would
    // be zero - and a metal with a black environment is worse than one with the old hemisphere
    // approximation. Drop the link instead of asserting a night that was never authored.
    frame.skyReflectionsLinked = reflectionsResolved;

    if (!frame.hasEnvironment ||
        (frame.ambientSource != WorldEnvironmentAmbientSourceUVE::Sky &&
         frame.ambientSource != WorldEnvironmentAmbientSourceUVE::EnvironmentMap) ||
        !(sunEnergy > 0.0F) || !Math::IsFiniteUVE(sunDirection) || !Math::IsFiniteUVE(sunColor) ||
        Math::LengthSquaredUVE(sunDirection) < 1.0e-8F) {
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
