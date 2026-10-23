// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/preview_lighting_uve.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

constexpr float kDegreesToRadiansUVE = 0.01745329251994329577F;
constexpr float kFullTurnDegreesUVE = 360.0F;
constexpr float kMaximumPreviewSunEnergyUVE = 16.0F;
constexpr float kMaximumPreviewAmbientEnergyUVE = 16.0F;

/// Colours are authored through ColorEdit3, so they are display values in 0..1 - the conversion to
/// linear storage happens where a colour is actually written into a component, exactly as the
/// light component's own comment describes.
[[nodiscard]] Math::Vector3UVE SanitizeColorUVE(const Math::Vector3UVE& color,
                                                const Math::Vector3UVE& fallback) noexcept {
    if (!Math::IsFiniteUVE(color)) {
        return fallback;
    }
    return Math::Vector3UVE{std::clamp(color.x, 0.0F, 1.0F), std::clamp(color.y, 0.0F, 1.0F),
                            std::clamp(color.z, 0.0F, 1.0F)};
}

[[nodiscard]] float SanitizeEnergyUVE(const float value, const float fallback, const float maximum) noexcept {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, 0.0F, maximum);
}

/// Azimuth wraps: 370 degrees is the same bearing as 10, and refusing it would be a worse answer
/// than turning it into one a user recognises.
[[nodiscard]] float WrapDegreesUVE(const float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    const float wrapped = std::fmod(value, kFullTurnDegreesUVE);
    return wrapped < 0.0F ? wrapped + kFullTurnDegreesUVE : wrapped;
}

} // namespace

Math::Vector3UVE GetPreviewSunDirectionUVE(const float azimuthDegrees, const float altitudeDegrees) noexcept {
    const float azimuth = WrapDegreesUVE(azimuthDegrees) * kDegreesToRadiansUVE;
    const float altitude =
        std::clamp(std::isfinite(altitudeDegrees) ? altitudeDegrees : 0.0F, -90.0F, 90.0F) * kDegreesToRadiansUVE;
    const float horizontal = std::cos(altitude);
    return Math::Vector3UVE{horizontal * std::sin(azimuth), std::sin(altitude), horizontal * std::cos(azimuth)};
}

std::optional<Math::QuaternionUVE> TryMakePreviewSunRotationUVE(const float azimuthDegrees,
                                                               const float altitudeDegrees) noexcept {
    const Math::Vector3UVE direction = GetPreviewSunDirectionUVE(azimuthDegrees, altitudeDegrees);

    // A light shines along its local -Z (see CameraSystemUVE's matching convention), so pointing
    // +Z at the sun is what makes the entity describe "the sun is over there".
    Math::QuaternionUVE rotation{};
    if (Math::TryMakeLookAtUVE(direction, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, rotation)) {
        return rotation;
    }
    // Straight overhead or straight below: the world up reference is parallel to the direction and
    // no unique orientation exists. +Z is then a valid reference, and keeping the sun active is
    // better than refusing to light the scene because the author dragged it to the pole.
    if (Math::TryMakeLookAtUVE(direction, Math::Vector3UVE{0.0F, 0.0F, 1.0F}, rotation)) {
        return rotation;
    }
    return std::nullopt;
}

PreviewLightingFrameUVE ResolvePreviewLightingUVE(const bool sceneHasDirectionalLight,
                                                  const bool sceneHasWorldEnvironment,
                                                  const PreviewLightingSettingsUVE& settings) noexcept {
    PreviewLightingFrameUVE frame{};

    const Math::Vector3UVE sunColor = SanitizeColorUVE(settings.sunColor, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    const float sunEnergy = SanitizeEnergyUVE(settings.sunEnergy, 1.0F, kMaximumPreviewSunEnergyUVE);
    const Math::Vector3UVE sunDirection =
        GetPreviewSunDirectionUVE(settings.sunAzimuthDegrees, settings.sunAltitudeDegrees);
    const std::optional<Math::QuaternionUVE> sunRotation =
        TryMakePreviewSunRotationUVE(settings.sunAzimuthDegrees, settings.sunAltitudeDegrees);

    // Authored lighting always wins: the preview exists for the gap, never as an extra layer.
    frame.sunActive = settings.sunEnabled && !sceneHasDirectionalLight && sunRotation.has_value() && sunEnergy > 0.0F;
    if (frame.sunActive) {
        frame.sunDirection = sunDirection;
        frame.sunColor = sunColor;
        frame.sunEnergy = sunEnergy;
        frame.sunRotation = *sunRotation;
    }

    frame.environmentActive = settings.environmentEnabled && !sceneHasWorldEnvironment;
    if (!frame.environmentActive) {
        return frame;
    }

    const Math::Vector3UVE skyColor = SanitizeColorUVE(settings.skyColor, Math::Vector3UVE{0.31F, 0.54F, 0.91F});
    const Math::Vector3UVE horizonColor =
        SanitizeColorUVE(settings.horizonColor, Math::Vector3UVE{0.78F, 0.86F, 0.94F});
    const Math::Vector3UVE groundColor = SanitizeColorUVE(settings.groundColor, Math::Vector3UVE{0.24F, 0.21F, 0.18F});
    const float ambientEnergy = SanitizeEnergyUVE(settings.ambientEnergy, 1.0F, kMaximumPreviewAmbientEnergyUVE);

    WorldEnvironmentFrameUVE environment{};
    // The renderer's procedural sky pass is gated on this flag, so it must be true for the preview
    // environment to be visible at all. It never reaches the document: the frame is a per-frame
    // value owned by the editor, not a component in the entity manager.
    environment.hasEnvironment = true;
    environment.ambientSource = WorldEnvironmentAmbientSourceUVE::Sky;
    environment.skyColor = skyColor;
    environment.horizonColor = horizonColor;
    environment.groundColor = groundColor;
    environment.skyCurve = 0.15F;
    environment.groundCurve = 0.02F;
    environment.skyAmbient = Math::Vector3UVE{skyColor.x * ambientEnergy, skyColor.y * ambientEnergy,
                                              skyColor.z * ambientEnergy};
    environment.groundAmbient = Math::Vector3UVE{groundColor.x * ambientEnergy, groundColor.y * ambientEnergy,
                                                 groundColor.z * ambientEnergy};
    environment.ambientColor = Math::Vector3UVE{horizonColor.x * ambientEnergy, horizonColor.y * ambientEnergy,
                                                horizonColor.z * ambientEnergy};
    // A preview is a stand-in for the missing environment, not a hidden effects stack: the author's
    // post-processing decisions belong to the WorldEnvironment they will add themselves.
    environment.postProcessingEnabled = false;
    environment.bloomEnabled = false;
    environment.ssaoEnabled = false;
    environment.fogEnabled = false;
    environment.exposure = 1.0F;
    environment.fogDensity = 0.0F;

    // The sky darkens and warms as the preview sun drops towards the horizon, using the same
    // resolver an authored WorldEnvironment goes through - so the preview and the environment it
    // turns into behave identically.
    ApplySunToWorldEnvironmentFrameUVE(environment, sunDirection, frame.sunActive ? sunColor : Math::Vector3UVE{},
                                       frame.sunActive ? sunEnergy : 0.0F);

    frame.environment = environment;
    return frame;
}

} // namespace UVE::Scene
