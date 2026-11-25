// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "preview_lighting.h"

#include <algorithm>
#include <cmath>

namespace UVE::Scene {
namespace {

constexpr float kDegreesToRadians = 0.01745329251994329577F;
constexpr float kFullTurnDegrees = 360.0F;
constexpr float kMaximumPreviewSunEnergy = 16.0F;
constexpr float kMaximumPreviewAmbientEnergy = 16.0F;

/// Colours are authored through ColorEdit3, so they are display values in 0..1 - the conversion to
/// linear storage happens where a colour is actually written into a component, exactly as the
/// light component's own comment describes.
[[nodiscard]] Math::Vector3 SanitizeColor(const Math::Vector3& color,
                                                const Math::Vector3& fallback) noexcept {
    if (!Math::IsFinite(color)) {
        return fallback;
    }
    return Math::Vector3{std::clamp(color.x, 0.0F, 1.0F), std::clamp(color.y, 0.0F, 1.0F),
                            std::clamp(color.z, 0.0F, 1.0F)};
}

[[nodiscard]] float SanitizeEnergy(const float value, const float fallback, const float maximum) noexcept {
    if (!std::isfinite(value)) {
        return fallback;
    }
    return std::clamp(value, 0.0F, maximum);
}

/// Azimuth wraps: 370 degrees is the same bearing as 10, and refusing it would be a worse answer
/// than turning it into one a user recognises.
[[nodiscard]] float WrapDegrees(const float value) noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    const float wrapped = std::fmod(value, kFullTurnDegrees);
    return wrapped < 0.0F ? wrapped + kFullTurnDegrees : wrapped;
}

} // namespace

Math::Vector3 GetPreviewSunDirection(const float azimuthDegrees, const float altitudeDegrees) noexcept {
    const float azimuth = WrapDegrees(azimuthDegrees) * kDegreesToRadians;
    const float altitude =
        std::clamp(std::isfinite(altitudeDegrees) ? altitudeDegrees : 0.0F, -90.0F, 90.0F) * kDegreesToRadians;
    const float horizontal = std::cos(altitude);
    return Math::Vector3{horizontal * std::sin(azimuth), std::sin(altitude), horizontal * std::cos(azimuth)};
}

std::optional<Math::Quaternion> TryMakePreviewSunRotation(const float azimuthDegrees,
                                                               const float altitudeDegrees) noexcept {
    const Math::Vector3 direction = GetPreviewSunDirection(azimuthDegrees, altitudeDegrees);

    // A light shines along its local -Z (see CameraSystem's matching convention), so pointing
    // +Z at the sun is what makes the entity describe "the sun is over there".
    Math::Quaternion rotation{};
    if (Math::TryMakeLookAt(direction, Math::Vector3{0.0F, 1.0F, 0.0F}, rotation)) {
        return rotation;
    }
    // Straight overhead or straight below: the world up reference is parallel to the direction and
    // no unique orientation exists. +Z is then a valid reference, and keeping the sun active is
    // better than refusing to light the scene because the author dragged it to the pole.
    if (Math::TryMakeLookAt(direction, Math::Vector3{0.0F, 0.0F, 1.0F}, rotation)) {
        return rotation;
    }
    return std::nullopt;
}

PreviewLightingFrame ResolvePreviewLighting(const bool sceneHasDirectionalLight,
                                                  const bool sceneHasWorldEnvironment,
                                                  const PreviewLightingSettings& settings) noexcept {
    PreviewLightingFrame frame{};

    const Math::Vector3 sunColor = SanitizeColor(settings.sunColor, Math::Vector3{1.0F, 1.0F, 1.0F});
    const float sunEnergy = SanitizeEnergy(settings.sunEnergy, 1.0F, kMaximumPreviewSunEnergy);
    const Math::Vector3 sunDirection =
        GetPreviewSunDirection(settings.sunAzimuthDegrees, settings.sunAltitudeDegrees);
    const std::optional<Math::Quaternion> sunRotation =
        TryMakePreviewSunRotation(settings.sunAzimuthDegrees, settings.sunAltitudeDegrees);

    // Authored lighting always wins: the preview exists for the gap, never as an extra layer.
    // Adding a WorldEnvironment3D also steps the preview sun down so an environment without a
    // DirectionalLight3D remains environment-only (ambient/sky) rather than appearing to carry
    // a directional sun of its own.
    const bool authoredLighting = sceneHasDirectionalLight || sceneHasWorldEnvironment;
    frame.sunActive = settings.sunEnabled && !authoredLighting && sunRotation.has_value() && sunEnergy > 0.0F;
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

    const Math::Vector3 skyColor = SanitizeColor(settings.skyColor, Math::Vector3{0.31F, 0.54F, 0.91F});
    const Math::Vector3 horizonColor =
        SanitizeColor(settings.horizonColor, Math::Vector3{0.78F, 0.86F, 0.94F});
    const Math::Vector3 groundColor = SanitizeColor(settings.groundColor, Math::Vector3{0.24F, 0.21F, 0.18F});
    const float ambientEnergy = SanitizeEnergy(settings.ambientEnergy, 1.0F, kMaximumPreviewAmbientEnergy);

    WorldEnvironmentFrame environment{};
    // The renderer's procedural sky pass is gated on this flag, so it must be true for the preview
    // environment to be visible at all. It never reaches the document: the frame is a per-frame
    // value owned by the editor, not a component in the entity manager.
    environment.hasEnvironment = true;
    environment.ambientSource = WorldEnvironmentAmbientSource::Sky;
    environment.skyColor = skyColor;
    environment.horizonColor = horizonColor;
    environment.groundColor = groundColor;
    environment.skyCurve = 0.15F;
    environment.groundCurve = 0.02F;
    environment.skyAmbient = Math::Vector3{skyColor.x * ambientEnergy, skyColor.y * ambientEnergy,
                                              skyColor.z * ambientEnergy};
    environment.groundAmbient = Math::Vector3{groundColor.x * ambientEnergy, groundColor.y * ambientEnergy,
                                                 groundColor.z * ambientEnergy};
    environment.ambientColor = Math::Vector3{horizonColor.x * ambientEnergy, horizonColor.y * ambientEnergy,
                                                horizonColor.z * ambientEnergy};
    // Keep the preview viewport visually aligned with the runtime post-processing pipeline: soft
    // ambient-only SSAO and subtle soft-knee bloom are active by default while fog remains opt-in.
    environment.hasPostProcessingOverride = true;
    environment.postProcessingEnabled = true;
    environment.bloomEnabled = true;
    environment.bloomThreshold = 1.15F;
    environment.bloomIntensity = 0.25F;
    environment.bloomSoftKnee = 0.5F;
    environment.bloomMipCount = 4U;
    environment.ssaoEnabled = true;
    environment.ssaoRadius = 0.5F;
    environment.ssaoIntensity = 0.85F;
    environment.fogEnabled = false;
    environment.exposure = 1.0F;
    environment.fogDensity = 0.0F;

    // The sky darkens and warms as the preview sun drops towards the horizon, using the same
    // resolver an authored WorldEnvironment goes through - so the preview and the environment it
    // turns into behave identically.
    ApplySunToWorldEnvironmentFrame(environment, sunDirection, frame.sunActive ? sunColor : Math::Vector3{},
                                       frame.sunActive ? sunEnergy : 0.0F);

    frame.environment = environment;
    return frame;
}

} // namespace UVE::Scene
