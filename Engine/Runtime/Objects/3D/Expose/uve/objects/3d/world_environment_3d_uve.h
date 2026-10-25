// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"

#include "uve/math/vector2_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

/// Ambient light source selected by a WorldEnvironment. Sky is the backward-compatible default;
/// EnvironmentMap samples the equirectangular sky asset for diffuse and specular ambient lighting.
enum class WorldEnvironmentAmbientSourceUVE : std::uint8_t {
    None = 0U,
    FlatColor = 1U,
    Sky = 2U,
    EnvironmentMap = 3U,
};

enum class WorldEnvironmentFogModeUVE : std::uint8_t {
    Linear = 0U,
    Exponential = 1U,
    Height = 2U,
};

enum class WorldEnvironmentDepthOfFieldFocusModeUVE : std::uint8_t {
    Manual = 0U,
    ScreenCenter = 1U,
};

enum class WorldEnvironmentDepthOfFieldBokehShapeUVE : std::uint8_t {
    Circular = 0U,
    Hexagonal = 1U,
};

inline constexpr std::uint32_t kMaximumWorldEnvironmentBloomMipCountUVE = 6U;

struct WorldEnvironment3DComponentUVE final {
    std::string skyAssetPath;
    WorldEnvironmentAmbientSourceUVE ambientSource = WorldEnvironmentAmbientSourceUVE::Sky;
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    float ambientEnergy = 1.0F;
    float exposure = 1.0F;
    float fogDensity = 0.0F;
    bool fogEnabled = false;
    WorldEnvironmentFogModeUVE fogMode = WorldEnvironmentFogModeUVE::Height;
    float fogStart = 0.0F;
    float fogEnd = 1000.0F;
    /// Off by default, so adding an environment to a level does not change what the level looks
    /// like: the editor's preview environment - what an author sees before there is one - draws no
    /// effects stack, and a default environment that turned bloom, vignette, grain and the rest on
    /// the moment it was created made "I only added an environment" look like a colour change.
    bool postProcessingEnabled = false;
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};
    float skyCurve = 0.15F;
    float groundCurve = 0.02F;
    float fogSkyAffect = 0.35F;
    bool bloomEnabled = true;
    float bloomIntensity = 0.45F;
    float bloomThreshold = 1.0F;
    /// Normalized soft-threshold width as a fraction of bloomThreshold; zero preserves hard clipping.
    float bloomSoftKnee = 0.0F;
    /// Number of half-resolution bloom scales to blur and combine; one preserves the legacy single scale.
    std::uint32_t bloomMipCount = 1U;
    bool ssaoEnabled = true;
    float ssaoIntensity = 1.0F;
    float ssaoRadius = 0.5F;
    float brightness = 0.0F;
    float contrast = 1.0F;
    float saturation = 1.0F;
    Math::Vector3UVE colorFilter{1.0F, 1.0F, 1.0F};
    float vignetteIntensity = 0.0F;
    /// Normalized radial position where edge darkening begins; 1 keeps the vignette limited to the corners.
    float vignetteRadius = 0.65F;
    /// 0..1 scales red/blue channel separation to at most four render-target texels at the corners.
    float chromaticAberrationIntensity = 0.0F;
    float filmGrainIntensity = 0.0F;
    /// 0..1 controls aspect-correct barrel distortion, up to 12% radial contraction at the corners.
    float lensDistortionIntensity = 0.0F;
    bool depthOfFieldEnabled = false;
    WorldEnvironmentDepthOfFieldFocusModeUVE depthOfFieldFocusMode = WorldEnvironmentDepthOfFieldFocusModeUVE::Manual;
    WorldEnvironmentDepthOfFieldBokehShapeUVE depthOfFieldBokehShape =
        WorldEnvironmentDepthOfFieldBokehShapeUVE::Circular;
    /// Manual focus distance along the camera ray, in world units.
    float depthOfFieldFocusDistance = 10.0F;
    /// Normalized aperture strength; 1.0 reaches the shader's eight-pixel blur-radius limit.
    float depthOfFieldAperture = 0.5F;
    /// Blur tap tier: 0 (4 taps), 1 (8 taps), or 2 (12 taps).
    std::uint32_t depthOfFieldQuality = 1U;
    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.5F;
    std::uint32_t motionBlurSampleCount = 8U;
    float fogHeight = 0.0F;
    float fogHeightFalloff = 1000.0F;
    float fogSunScatter = 0.55F;
};

[[nodiscard]] bool IsWorldEnvironment3DObjectComponentValidUVE(const WorldEnvironment3DComponentUVE& value) noexcept;

struct WorldEnvironmentFrameUVE final {
    WorldEnvironmentAmbientSourceUVE ambientSource = WorldEnvironmentAmbientSourceUVE::Sky;
    Math::Vector3UVE ambientColor{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE skyAmbient{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE groundAmbient{0.2F, 0.2F, 0.2F};
    Math::Vector3UVE skyColor{0.31F, 0.54F, 0.91F};
    Math::Vector3UVE horizonColor{0.78F, 0.86F, 0.94F};
    Math::Vector3UVE groundColor{0.24F, 0.21F, 0.18F};
    Math::Vector3UVE fogColor{0.5F, 0.6F, 0.7F};
    Math::Vector3UVE backgroundColor{0.050F, 0.050F, 0.050F};
    Math::Vector3UVE colorFilter{1.0F, 1.0F, 1.0F};
    float skyCurve = 0.15F;
    float groundCurve = 0.02F;
    float fogDensity = 0.0F;
    WorldEnvironmentFogModeUVE fogMode = WorldEnvironmentFogModeUVE::Height;
    float fogStart = 0.0F;
    float fogEnd = 1000.0F;
    float fogSkyAffect = 0.35F;
    float fogHeight = 0.0F;
    float fogHeightFalloff = 1000.0F;
    float fogSunScatter = 0.55F;
    float exposure = 1.0F;
    float bloomIntensity = 0.45F;
    float bloomThreshold = 1.0F;
    float bloomSoftKnee = 0.0F;
    std::uint32_t bloomMipCount = 1U;
    float ssaoIntensity = 1.0F;
    float ssaoRadius = 0.5F;
    float brightness = 0.0F;
    float contrast = 1.0F;
    float saturation = 1.0F;
    float vignetteIntensity = 0.0F;
    float vignetteRadius = 0.65F;
    float chromaticAberrationIntensity = 0.0F;
    float filmGrainIntensity = 0.0F;
    float lensDistortionIntensity = 0.0F;
    bool depthOfFieldEnabled = false;
    WorldEnvironmentDepthOfFieldFocusModeUVE depthOfFieldFocusMode = WorldEnvironmentDepthOfFieldFocusModeUVE::Manual;
    WorldEnvironmentDepthOfFieldBokehShapeUVE depthOfFieldBokehShape =
        WorldEnvironmentDepthOfFieldBokehShapeUVE::Circular;
    float depthOfFieldFocusDistance = 10.0F;
    float depthOfFieldAperture = 0.5F;
    std::uint32_t depthOfFieldQuality = 1U;
    bool motionBlurEnabled = false;
    float motionBlurStrength = 0.5F;
    std::uint32_t motionBlurSampleCount = 8U;
    bool fogEnabled = false;
    bool postProcessingEnabled = false;
    bool bloomEnabled = true;
    bool ssaoEnabled = true;
    bool hasEnvironment = false;
    std::string skyAssetPath;

    /// True when the sky for this frame comes from a Sky object's scattering model instead of the
    /// three-band gradient. The bands above are still filled - the model uses the ground colour
    /// below the horizon - but they are no longer what paints the sky.
    bool atmosphericSky = false;
    float atmosphereRayleigh = 1.0F;
    float atmosphereMie = 1.0F;
    float atmosphereOzone = 1.0F;
    float atmosphereMieAnisotropy = 0.76F;
    float atmosphereMultiScatter = 1.0F;
    float atmosphereIntensity = 1.0F;
    std::uint32_t atmosphereSamples = 16U;

    /// Night overlay: the stars behind everything and the band across them.
    float starDensity = 420.0F;
    float starBrightness = 0.0F;
    float starSeed = 0.0F;
    float milkyWayStrength = 0.0F;

    /// Cloud deck over the sky. `cloudWind` is already resolved to metres per second in world XZ,
    /// so the renderer never has to convert an angle while building its uniforms.
    bool cloudsEnabled = false;
    float cloudCoverage = 0.45F;
    float cloudDensity = 1.0F;
    float cloudAltitude = 1500.0F;
    Math::Vector2UVE cloudWind{};
    float cloudSoftness = 0.35F;
    float cloudSilverLining = 1.0F;

    /// Lighting link from the Sky object. The ambient is applied later, by
    /// ApplySunToWorldEnvironmentFrameUVE, because it depends on the sun's altitude and the sun is
    /// not resolved until after this frame is built.
    bool skyAmbientLinked = false;
    float skyAmbientStrength = 1.0F;
    float skyRayleighDensity = 1.0F;
    /// Fog is applied where it is resolved: the horizon band does not depend on the sun.
    bool skyFogLinked = false;
    /// Output of the sky as a whole, straight from the component: the renderer multiplies the sky
    /// pass by this and gates the sky clock on the second.
    float skyLuminance = 1.0F;
    bool skyAnimated = true;
    /// What a reflective surface shows, resolved alongside the ambient for the same reason.
    bool skyReflectionsLinked = false;
    Math::Vector3UVE reflectionSky{};
    Math::Vector3UVE reflectionHorizon{};
    Math::Vector3UVE reflectionGround{};
};

[[nodiscard]] bool TryMakeSkyEquirectUvUVE(const Math::Vector3UVE& direction, Math::Vector2UVE& outUv) noexcept;

[[nodiscard]] WorldEnvironmentFrameUVE ResolveWorldEnvironmentFrameUVE(
    IEntityManagerUVE& entityManager, const Math::Vector3UVE& fallbackAmbient) noexcept;

void ApplySunToWorldEnvironmentFrameUVE(WorldEnvironmentFrameUVE& frame, const Math::Vector3UVE& sunDirection,
                                        const Math::Vector3UVE& sunColor, float sunEnergy) noexcept;

struct WorldEnvironmentObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "WorldEnvironment";
    WorldEnvironment3DComponentUVE environment = [] {
        WorldEnvironment3DComponentUVE env{};
        env.ambientSource = WorldEnvironmentAmbientSourceUVE::None;
        return env;
    }();
};

void ApplyWorldEnvironmentObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                            const WorldEnvironmentObjectDefinitionUVE& value);

/// Blends two environment profiles by `weightB` (clamped to 0..1) for overlapping environment volumes.
[[nodiscard]] inline WorldEnvironment3DComponentUVE BlendWorldEnvironment3DComponentsUVE(
    const WorldEnvironment3DComponentUVE& a, const WorldEnvironment3DComponentUVE& b,
    const float weightB) noexcept {
    const float t = weightB < 0.0F ? 0.0F : (weightB > 1.0F ? 1.0F : weightB);
    const float s = 1.0F - t;
    WorldEnvironment3DComponentUVE result = t >= 0.5F ? b : a;
    result.ambientColor = Math::Vector3UVE{a.ambientColor.x * s + b.ambientColor.x * t,
                                           a.ambientColor.y * s + b.ambientColor.y * t,
                                           a.ambientColor.z * s + b.ambientColor.z * t};
    result.fogColor = Math::Vector3UVE{a.fogColor.x * s + b.fogColor.x * t,
                                       a.fogColor.y * s + b.fogColor.y * t,
                                       a.fogColor.z * s + b.fogColor.z * t};
    result.ambientEnergy = a.ambientEnergy * s + b.ambientEnergy * t;
    result.exposure = a.exposure * s + b.exposure * t;
    result.fogDensity = a.fogDensity * s + b.fogDensity * t;
    result.bloomIntensity = a.bloomIntensity * s + b.bloomIntensity * t;
    result.bloomThreshold = a.bloomThreshold * s + b.bloomThreshold * t;
    result.ssaoIntensity = a.ssaoIntensity * s + b.ssaoIntensity * t;
    result.ssaoRadius = a.ssaoRadius * s + b.ssaoRadius * t;
    result.vignetteIntensity = a.vignetteIntensity * s + b.vignetteIntensity * t;
    return result;
}

/// Resolves the active environment resource with precedence:
/// per-camera override > scene volume > project default > editor preview fallback.
[[nodiscard]] inline WorldEnvironment3DComponentUVE ResolveEffectiveWorldEnvironment3DUVE(
    const WorldEnvironment3DComponentUVE* const cameraOverride,
    const WorldEnvironment3DComponentUVE* const sceneVolume,
    const WorldEnvironment3DComponentUVE* const projectDefault,
    const WorldEnvironment3DComponentUVE* const editorPreviewFallback) {
    if (cameraOverride != nullptr) {
        return *cameraOverride;
    }
    if (sceneVolume != nullptr) {
        return *sceneVolume;
    }
    if (projectDefault != nullptr) {
        return *projectDefault;
    }
    if (editorPreviewFallback != nullptr) {
        return *editorPreviewFallback;
    }
    return WorldEnvironment3DComponentUVE{};
}

} // namespace UVE::Scene
