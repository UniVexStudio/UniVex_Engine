// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/sky_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

bool IsSky3DObjectComponentValidUVE(const Sky3DComponentUVE& value) noexcept {
    // Every source a saved document could name, including the reserved one: an authored scene that
    // selected it must still load, and the renderer falls back to the gradient for it.
    const bool sourceKnown = value.source == SkySourceUVE::Gradient || value.source == SkySourceUVE::Panorama ||
                             value.source == SkySourceUVE::Atmosphere;
    const auto colorIsSane = [](const Math::Vector3UVE& color) noexcept {
        return IsFinite3DObjectVectorUVE(color) && color.x >= 0.0F && color.y >= 0.0F && color.z >= 0.0F;
    };
    return sourceKnown && colorIsSane(value.skyColor) && colorIsSane(value.horizonColor) &&
           colorIsSane(value.groundColor) && std::isfinite(value.skyCurve) && value.skyCurve > 0.0F &&
           std::isfinite(value.groundCurve) && value.groundCurve > 0.0F &&
           IsBounded3DObjectStringUVE(value.panoramaPath) && std::isfinite(value.panoramaRotationDegrees) &&
           std::isfinite(value.panoramaExposure) &&
           // Densities are multipliers on real coefficients, so negative would mean negative air.
           // The anisotropy bound is the Henyey-Greenstein singularity: at exactly +/-1 the phase
           // function divides by zero at mu = 1, so one step inside it is the authorable limit.
           std::isfinite(value.atmosphereRayleigh) && value.atmosphereRayleigh >= 0.0F &&
           value.atmosphereRayleigh <= 10.0F && std::isfinite(value.atmosphereMie) &&
           value.atmosphereMie >= 0.0F && value.atmosphereMie <= 10.0F &&
           std::isfinite(value.atmosphereOzone) && value.atmosphereOzone >= 0.0F &&
           value.atmosphereOzone <= 10.0F && std::isfinite(value.atmosphereMieAnisotropy) &&
           value.atmosphereMieAnisotropy > -1.0F && value.atmosphereMieAnisotropy < 1.0F &&
           std::isfinite(value.atmosphereMultiScatter) && value.atmosphereMultiScatter >= 0.0F &&
           value.atmosphereMultiScatter <= 4.0F && std::isfinite(value.atmosphereIntensity) &&
           value.atmosphereIntensity >= 0.0F && value.atmosphereIntensity <= 100.0F &&
           value.atmosphereSamples >= 4U && value.atmosphereSamples <= 64U &&
           std::isfinite(value.starDensity) && value.starDensity >= 0.0F && value.starDensity <= 2000.0F &&
           std::isfinite(value.starBrightness) && value.starBrightness >= 0.0F &&
           value.starBrightness <= 100.0F && std::isfinite(value.starSeed) && std::isfinite(value.milkyWayStrength) &&
           value.milkyWayStrength >= 0.0F && value.milkyWayStrength <= 100.0F &&
           // Coverage and density are fractions of the sky, so a value outside [0,1] has no
           // meaning; softness must stay above zero because it divides the edge gradient.
           std::isfinite(value.cloudCoverage) && value.cloudCoverage >= 0.0F && value.cloudCoverage <= 1.0F &&
           std::isfinite(value.cloudDensity) && value.cloudDensity >= 0.0F && value.cloudDensity <= 1.0F &&
           std::isfinite(value.cloudAltitude) && value.cloudAltitude > 1.0F &&
           value.cloudAltitude <= 100000.0F && std::isfinite(value.cloudWindSpeed) &&
           std::fabs(value.cloudWindSpeed) <= 1000.0F && std::isfinite(value.cloudWindDirectionDegrees) &&
           std::isfinite(value.cloudSoftness) && value.cloudSoftness > 0.0F && value.cloudSoftness <= 1.0F &&
           std::isfinite(value.cloudSilverLining) && value.cloudSilverLining >= 0.0F &&
           value.cloudSilverLining <= 100.0F &&
           // The link strengths are dials over an already-computed answer, so [0,1] is the whole
           // range: zero is exactly "off", one is exactly the sky's own value.
           std::isfinite(value.ambientStrength) && value.ambientStrength >= 0.0F && value.ambientStrength <= 1.0F &&
           std::isfinite(value.fogStrength) && value.fogStrength >= 0.0F && value.fogStrength <= 1.0F &&
           // Bounded rather than merely non-negative: a sky at a thousand is a white screen that
           // also blows out every bloom pass downstream, which is not a brightness an author can
           // use. Eight is three stops, the same ceiling the Inspector offers, so a value written
           // by a script cannot be one the panel refuses to show.
           std::isfinite(value.luminance) && value.luminance >= 0.0F && value.luminance <= 8.0F;
}

namespace {

/// Vertical Rayleigh optical depth per channel: the coefficient times the scale height, which is
/// what a column of air presents looking straight up. The same coefficients the sky shader
/// integrates, so ambient and the drawn sky cannot disagree about what colour the air is.
constexpr float kRayleighPerMetreRedUVE = 5.8e-6F;
constexpr float kRayleighPerMetreGreenUVE = 13.5e-6F;
constexpr float kRayleighPerMetreBlueUVE = 33.1e-6F;
constexpr float kRayleighScaleHeightMetresUVE = 8000.0F;
constexpr float kPiUVE = 3.14159265358979323846F;

/// Calibration for the ambient the sky throws: chosen so a noon sky lands a little above the
/// environment's authored default triple rather than several times above it. Not a physical
/// constant - the irradiance integral it stands in for is not evaluated here - so it is named for
/// what it is and kept in one place.
constexpr float kSkyAmbientCalibrationUVE = 0.35F;
/// How much of the sky's own tint the ground takes. The ground sees the sky above it, so it is lit
/// by the same colour, but from a hemisphere dimmed by its own albedo.
constexpr float kSkyAmbientGroundShareUVE = 0.75F;

[[nodiscard]] float LuminanceUVE(const Math::Vector3UVE& color) noexcept {
    return 0.2126F * color.x + 0.7152F * color.y + 0.0722F * color.z;
}

[[nodiscard]] float SmoothStepUVE(const float edge0, const float edge1, const float value) noexcept {
    if (edge1 - edge0 <= 0.0F) {
        return value < edge0 ? 0.0F : 1.0F;
    }
    const float t = std::clamp((value - edge0) / (edge1 - edge0), 0.0F, 1.0F);
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Math::Vector3UVE MixUVE(const Math::Vector3UVE& a, const Math::Vector3UVE& b,
                                     const float t) noexcept {
    return Math::Vector3UVE{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t};
}

/// Air mass: how much thicker the atmosphere is along a line to the sun than straight up. The
/// Kasten-Young form is used rather than the naive 1/sin because it stays finite at the horizon,
/// where 1/sin does not - and the horizon is exactly where this matters most.
[[nodiscard]] float AirMassUVE(const float altitudeDegrees) noexcept {
    const float altitude = std::max(altitudeDegrees, 0.0F);
    const float denominator = std::sin(altitude * kPiUVE / 180.0F) +
                              0.50572F * std::pow(altitude + 6.07995F, -1.6364F);
    return denominator > 1.0e-4F ? 1.0F / denominator : 1.0e4F;
}

} // namespace

Math::Vector3UVE SkySunTransmittanceUVE(const float sunAltitudeRadians, const float rayleighDensity) noexcept {
    const float altitudeDegrees =
        std::isfinite(sunAltitudeRadians) ? sunAltitudeRadians * 180.0F / kPiUVE : 90.0F;
    const float density = std::isfinite(rayleighDensity) ? std::max(rayleighDensity, 0.0F) : 1.0F;
    const float path = AirMassUVE(altitudeDegrees) * density;
    const auto survive = [path](const float perMetre) {
        return std::exp(-perMetre * kRayleighScaleHeightMetresUVE * path);
    };
    return Math::Vector3UVE{survive(kRayleighPerMetreRedUVE), survive(kRayleighPerMetreGreenUVE),
                            survive(kRayleighPerMetreBlueUVE)};
}

SkyAmbientUVE ResolveSkyAmbientUVE(const Math::Vector3UVE& skyColor, const Math::Vector3UVE& horizonColor,
                                   const Math::Vector3UVE& groundColor, const float rayleighDensity,
                                   const float strengthScale, const Math::Vector3UVE& sunDirection) noexcept {
    SkyAmbientUVE result;
    const bool directionUsable =
        Math::IsFiniteUVE(sunDirection) && Math::LengthSquaredUVE(sunDirection) > 1.0e-12F;
    const Math::Vector3UVE up =
        directionUsable ? Math::NormalizeUVE(sunDirection) : Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    if (!Math::IsFiniteUVE(up)) {
        return result;
    }
    const float altitudeDegrees = std::asin(std::clamp(up.y, -1.0F, 1.0F)) * 180.0F / kPiUVE;

    // Two curves, and they are not the same curve. `daylight` is how much light there is at all: it
    // starts at astronomical twilight, -18 degrees, where the sky is still faintly lit, and reaches
    // full by four degrees, where the sun has cleared the haze. `lowness` is how red that light is:
    // it only begins at the horizon, because a high sun reddens nothing.
    const float daylight = SmoothStepUVE(-18.0F, 4.0F, altitudeDegrees);
    const float lowness = 1.0F - SmoothStepUVE(0.0F, 30.0F, altitudeDegrees);
    result.daylight = daylight;
    if (daylight <= 0.0F) {
        return result;
    }

    // How warm the light is: the transmittance normalised by its own brightness, so what is left is
    // a colour. Floored and clamped because at the horizon the ratio runs away - red passes, blue
    // does not - and an unclamped ratio would turn a sunset's ambient into one blown-out channel.
    const Math::Vector3UVE transmittance =
        SkySunTransmittanceUVE(altitudeDegrees * kPiUVE / 180.0F, rayleighDensity);
    const float brightness = std::max(LuminanceUVE(transmittance), 0.05F);
    const Math::Vector3UVE tint{std::clamp(transmittance.x / brightness, 0.25F, 3.0F),
                                std::clamp(transmittance.y / brightness, 0.25F, 3.0F),
                                std::clamp(transmittance.z / brightness, 0.25F, 3.0F)};

    // Which band lights the upper hemisphere. At a high sun it is the zenith band; as the sun drops
    // more of it is the horizon band, which is why a sunset's fill is orange and not blue.
    const Math::Vector3UVE above = MixUVE(skyColor, horizonColor, 0.75F * lowness);
    const Math::Vector3UVE skyTint = MixUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}, tint, 0.45F);
    const Math::Vector3UVE groundTint = MixUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}, tint, 0.30F);

    const float strength = kSkyAmbientCalibrationUVE * std::max(strengthScale, 0.0F) * daylight;
    result.sky = Math::Vector3UVE{above.x * strength * skyTint.x, above.y * strength * skyTint.y,
                                  above.z * strength * skyTint.z};
    const float groundStrength = strength * kSkyAmbientGroundShareUVE;
    result.ground = Math::Vector3UVE{groundColor.x * groundStrength * groundTint.x,
                                     groundColor.y * groundStrength * groundTint.y,
                                     groundColor.z * groundStrength * groundTint.z};
    return result;
}

SkyAmbientUVE ResolveSkyAmbientUVE(const Sky3DComponentUVE& sky, const Math::Vector3UVE& sunDirection) noexcept {
    return ResolveSkyAmbientUVE(sky.skyColor, sky.horizonColor, sky.groundColor, sky.atmosphereRayleigh,
                                sky.ambientStrength, sunDirection);
}

Math::Vector3UVE ResolveSkyFogColorUVE(const Math::Vector3UVE& groundColor,
                                       const Math::Vector3UVE& horizonColor) noexcept {
    // Fog is aerial perspective: distant geometry fades into the light it is seen THROUGH, which is
    // the horizon band, with some of the ground beneath it mixed in for a low, thick haze. Nothing
    // here reads the sun, because the sky's bands already carry the hour.
    return MixUVE(groundColor, horizonColor, 0.65F);
}

void ApplySky3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                   const Sky3DObjectDefinitionUVE& value) {
    EnsureObjectBaselineUVE(entityManager, entity, Sky3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Sky3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Sky3DComponentUVE>(entity, value.sky);
    }
}

} // namespace UVE::Scene
