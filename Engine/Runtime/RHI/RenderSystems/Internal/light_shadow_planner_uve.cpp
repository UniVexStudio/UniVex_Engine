// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/light_shadow_planner_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector2_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Render {

namespace {

/// Inclusive cone limit the shader can represent. Below one degree the projection collapses to a
/// line; at 90 and above it is not a cone any more (a hemisphere or worse), and the shader's
/// `cos(radians(spotAngleDegrees))` falloff band would invert.
constexpr float kMinimumSpotAngleDegreesUVE = 1.0F;
constexpr float kMaximumSpotAngleDegreesUVE = 89.0F;

/// Light-space sphere radius as a fraction of a light's range. Culling an influence sphere against
/// the view frustum is only sound when the sphere CONTAINS the cone, and the cone's vertices sit at
/// `range` from the light, so the sphere radius is exactly `range`.
[[nodiscard]] constexpr float InfluenceRadiusUVE(const float range) noexcept { return range; }

/// Rounds to the nearest power of two inside the atlas' supported tile range. Anything above the
/// atlas root collapses to the root; anything at or below the minimum collapses to the minimum.
[[nodiscard]] constexpr std::uint32_t RoundTileResolutionUVE(const std::uint32_t requested) noexcept {
    if (requested <= kShadowAtlasMinTileResolutionUVE) {
        return kShadowAtlasMinTileResolutionUVE;
    }
    if (requested >= kShadowAtlasMaxTileResolutionUVE) {
        return kShadowAtlasMaxTileResolutionUVE;
    }
    std::uint32_t tile = kShadowAtlasMinTileResolutionUVE;
    while (tile * 2U <= requested) {
        tile *= 2U;
    }
    return tile;
}

/// Tiles of `tileResolution` that fit in the square atlas, along one axis.
[[nodiscard]] constexpr std::uint64_t TilesPerAxisUVE(const std::uint32_t tileResolution) noexcept {
    return static_cast<std::uint64_t>(kShadowAtlasResolutionUVE) /
           static_cast<std::uint64_t>(std::max(tileResolution, 1U));
}

} // namespace

bool IsSpotShadowCandidateUVE(const LightDataUVE& light, const Math::FrustumUVE& viewFrustum) noexcept {
    if (light.type != Scene::LightTypeUVE::Spot) {
        return false;
    }
    if (!(light.intensity > 0.0F) || !std::isfinite(light.intensity)) {
        return false;
    }
    // The editor's internal headlight is not part of the authored scene and never casts.
    if (!light.castsShadows || light.isEditorInternal) {
        return false;
    }
    if (!(light.range > 0.0F) || !std::isfinite(light.range)) {
        return false;
    }
    if (!(light.spotAngleDegrees >= kMinimumSpotAngleDegreesUVE) ||
        !(light.spotAngleDegrees <= kMaximumSpotAngleDegreesUVE)) {
        return false;
    }
    if (!Math::IsFiniteUVE(light.position) || !Math::IsFiniteUVE(light.rotation)) {
        return false;
    }
    Math::QuaternionUVE normalizedRotation{};
    if (!Math::TryNormalizeUVE(light.rotation, normalizedRotation)) {
        return false;
    }
    const float radius = InfluenceRadiusUVE(light.range);
    const Math::AabbUVE influence =
        Math::AabbUVE::FromCenterExtentsUVE(light.position, Math::Vector3UVE{radius, radius, radius});
    return viewFrustum.IntersectsUVE(influence);
}

std::size_t CountSpotShadowCandidatesUVE(const LightListUVE& lights,
                                         const Math::FrustumUVE& viewFrustum) noexcept {
    std::size_t count = 0;
    for (std::size_t lightIndex = 0; lightIndex < lights.SizeUVE(); ++lightIndex) {
        if (IsSpotShadowCandidateUVE(lights[lightIndex], viewFrustum)) {
            ++count;
        }
    }
    return count;
}

std::uint32_t ResolveSpotShadowTileResolutionUVE(const std::uint32_t requestedResolution,
                                                 const std::size_t casterCount) noexcept {
    if (casterCount == 0U) {
        return 0U;
    }
    const std::size_t casters = std::min(casterCount, kMaxSpotShadowLightsUVE);
    std::uint32_t tile = RoundTileResolutionUVE(requestedResolution);
    while (tile > kShadowAtlasMinTileResolutionUVE) {
        const std::uint64_t perAxis = TilesPerAxisUVE(tile);
        if (perAxis * perAxis >= static_cast<std::uint64_t>(casters)) {
            break;
        }
        tile /= 2U;
    }
    return tile;
}

float ComputeSpotShadowNearPlaneUVE(const float range) noexcept {
    if (!(range > 0.0F) || !std::isfinite(range)) {
        return 0.02F;
    }
    return std::clamp(range * 0.02F, 0.02F, 1.0F);
}

float SpotShadowLinearDepthUVE(const float nearPlane, const float farPlane,
                               const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float denominator = farValue - (farValue - nearValue) * normalizedDepth;
    return (nearValue * farValue) / std::max(denominator, 1.0e-6F);
}

float SpotShadowDepthSlopeUVE(const float nearPlane, const float farPlane,
                              const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float linear = SpotShadowLinearDepthUVE(nearValue, farValue, normalizedDepth);
    return (nearValue * farValue) / std::max((farValue - nearValue) * linear * linear, 1.0e-6F);
}

SpotShadowPlanUVE BuildSpotShadowPlanUVE(const LightListUVE& lights,
                                         const Math::FrustumUVE& viewFrustum,
                                         ShadowAtlasManagerUVE& atlas,
                                         const std::uint32_t requestedResolution) noexcept {
    SpotShadowPlanUVE plan{};
    atlas.ResetUVE();

    // Counted first, not accumulated on the fly: the tile resolution depends on how many lights
    // need a tile, and every light must be allocated at the SAME resolution for the layout to be
    // a single power-of-two tiling of the atlas.
    const std::size_t casterCount = CountSpotShadowCandidatesUVE(lights, viewFrustum);
    const std::uint32_t tileResolution =
        ResolveSpotShadowTileResolutionUVE(requestedResolution, casterCount);
    if (tileResolution == 0U) {
        return plan;
    }

    for (std::size_t lightIndex = 0; lightIndex < lights.SizeUVE(); ++lightIndex) {
        if (plan.count >= kMaxSpotShadowLightsUVE) {
            break;
        }
        const LightDataUVE& light = lights[lightIndex];
        if (!IsSpotShadowCandidateUVE(light, viewFrustum)) {
            continue;
        }
        const std::optional<TileAllocUVE> tile = atlas.AllocateTileUVE(tileResolution);
        if (!tile.has_value()) {
            // Unreachable by construction - ResolveSpotShadowTileResolutionUVE fits
            // kMaxSpotShadowLightsUVE tiles of at least 128x128 into 4096x4096 - but a planner
            // that silently produced a plan the atlas could not satisfy would be worse than one
            // that reports fewer lights, so the shortfall is reported through `count`.
            break;
        }
        Math::QuaternionUVE rotation{};
        if (!Math::TryNormalizeUVE(light.rotation, rotation)) {
            atlas.FreeTileUVE(*tile);
            continue;
        }
        const float nearPlane = ComputeSpotShadowNearPlaneUVE(light.range);
        const float farPlane = std::max(light.range, nearPlane + 0.1F);
        SpotShadowAssignmentUVE& assignment = plan.assignments[plan.count];
        assignment.lightIndex = lightIndex;
        assignment.tile = *tile;
        assignment.nearPlane = nearPlane;
        assignment.farPlane = farPlane;
        assignment.lightViewProjection = ShadowAtlasManagerUVE::ComputeSpotLightMatrixUVE(
            light.position, rotation, light.spotAngleDegrees, farPlane, nearPlane);
        ++plan.count;
    }

    if (plan.count == 0U) {
        return plan;
    }
    plan.tileResolution = tileResolution;
    return plan;
}

} // namespace UVE::Render
