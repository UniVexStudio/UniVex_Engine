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

/// Rules every shadow-casting light must satisfy whatever its type.
[[nodiscard]] bool HasUsableShadowStateUVE(const LightDataUVE& light) noexcept {
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
    return Math::IsFiniteUVE(light.position) && Math::IsFiniteUVE(light.rotation);
}

/// The light's reach, as a sphere that CONTAINS whatever it can illuminate. Both light types are
/// bounded by their own `range`, so one radius serves both - and culling a sphere that contains the
/// illuminated volume against the view frustum can only ever reject lights that cannot affect a
/// visible pixel.
[[nodiscard]] bool ReachesViewFrustumUVE(const LightDataUVE& light,
                                         const Math::FrustumUVE& viewFrustum) noexcept {
    const float radius = light.range;
    const Math::AabbUVE influence =
        Math::AabbUVE::FromCenterExtentsUVE(light.position, Math::Vector3UVE{radius, radius, radius});
    return viewFrustum.IntersectsUVE(influence);
}

/// Tiles a single light needs, assuming it already passed eligibility.
[[nodiscard]] constexpr std::size_t TilesForLightUVE(const LightDataUVE& light) noexcept {
    return light.type == Scene::LightTypeUVE::Point
               ? static_cast<std::size_t>(kPointShadowFaceCountUVE)
               : 1U;
}

} // namespace

bool IsSpotLightShadowCandidateUVE(const LightDataUVE& light,
                                   const Math::FrustumUVE& viewFrustum) noexcept {
    if (light.type != Scene::LightTypeUVE::Spot) {
        return false;
    }
    if (!HasUsableShadowStateUVE(light)) {
        return false;
    }
    if (!(light.spotAngleDegrees >= kMinimumSpotAngleDegreesUVE) ||
        !(light.spotAngleDegrees <= kMaximumSpotAngleDegreesUVE)) {
        return false;
    }
    Math::QuaternionUVE normalizedRotation{};
    if (!Math::TryNormalizeUVE(light.rotation, normalizedRotation)) {
        return false;
    }
    return ReachesViewFrustumUVE(light, viewFrustum);
}

bool IsPointLightShadowCandidateUVE(const LightDataUVE& light,
                                    const Math::FrustumUVE& viewFrustum) noexcept {
    if (light.type != Scene::LightTypeUVE::Point) {
        return false;
    }
    if (!HasUsableShadowStateUVE(light)) {
        return false;
    }
    // No cone and no orientation: a point light's six faces come from a fixed table, so unlike the
    // spot path there is nothing here for a degenerate rotation to invalidate. Requiring a unit
    // quaternion anyway would reject lights that can shadow perfectly well, which is the kind of
    // over-strict eligibility rule that shows up as "shadows randomly off on some lights".
    return ReachesViewFrustumUVE(light, viewFrustum);
}

std::size_t CountShadowTilesUVE(const LightListUVE& lights,
                                const Math::FrustumUVE& viewFrustum) noexcept {
    std::size_t tiles = 0U;
    for (std::size_t lightIndex = 0; lightIndex < lights.SizeUVE(); ++lightIndex) {
        const LightDataUVE& light = lights[lightIndex];
        if (IsSpotLightShadowCandidateUVE(light, viewFrustum) ||
            IsPointLightShadowCandidateUVE(light, viewFrustum)) {
            tiles += TilesForLightUVE(light);
        }
    }
    return tiles;
}

std::uint32_t ResolveShadowTileResolutionUVE(const std::uint32_t requestedResolution,
                                             const std::size_t tileDemand) noexcept {
    if (tileDemand == 0U) {
        return 0U;
    }
    const std::size_t demand = std::min(tileDemand, kMaxShadowTilesUVE);
    std::uint32_t tile = RoundTileResolutionUVE(requestedResolution);
    while (tile > kShadowAtlasMinTileResolutionUVE) {
        const std::uint64_t perAxis = TilesPerAxisUVE(tile);
        if (perAxis * perAxis >= static_cast<std::uint64_t>(demand)) {
            break;
        }
        tile /= 2U;
    }
    return tile;
}

float ComputeLightShadowNearPlaneUVE(const float range) noexcept {
    if (!(range > 0.0F) || !std::isfinite(range)) {
        return 0.02F;
    }
    return std::clamp(range * 0.02F, 0.02F, 1.0F);
}

float ShadowLinearDepthUVE(const float nearPlane, const float farPlane,
                               const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float denominator = farValue - (farValue - nearValue) * normalizedDepth;
    return (nearValue * farValue) / std::max(denominator, 1.0e-6F);
}

float PointShadowProjectionDepthUVE(const float nearPlane, const float farPlane,
                                    const float axisDistance) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    if (!(axisDistance > nearValue)) {
        return 0.0F;
    }
    return farValue * (1.0F - nearValue / axisDistance) / (farValue - nearValue);
}

float ShadowDepthSlopeUVE(const float nearPlane, const float farPlane,
                              const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float linear = ShadowLinearDepthUVE(nearValue, farValue, normalizedDepth);
    return (nearValue * farValue) / std::max((farValue - nearValue) * linear * linear, 1.0e-6F);
}

LightShadowPlanUVE BuildLightShadowPlanUVE(const LightListUVE& lights,
                                           const Math::FrustumUVE& viewFrustum,
                                           ShadowAtlasManagerUVE& atlas,
                                           const std::uint32_t requestedResolution) noexcept {
    LightShadowPlanUVE plan{};
    atlas.ResetUVE();

    // Counted first, not accumulated on the fly: the tile resolution depends on how many tiles the
    // frame needs, and every tile must be allocated at the SAME resolution for the layout to be a
    // single power-of-two tiling of the atlas.
    const std::size_t tileDemand = CountShadowTilesUVE(lights, viewFrustum);
    const std::uint32_t tileResolution =
        ResolveShadowTileResolutionUVE(requestedResolution, tileDemand);
    if (tileResolution == 0U) {
        return plan;
    }

    std::size_t shadowedLights = 0U;
    for (std::size_t lightIndex = 0; lightIndex < lights.SizeUVE(); ++lightIndex) {
        if (shadowedLights >= kMaxShadowedLightsUVE) {
            break;
        }
        const LightDataUVE& light = lights[lightIndex];
        const bool isSpot = IsSpotLightShadowCandidateUVE(light, viewFrustum);
        const bool isPoint = !isSpot && IsPointLightShadowCandidateUVE(light, viewFrustum);
        if (!isSpot && !isPoint) {
            continue;
        }
        const float nearPlane = ComputeLightShadowNearPlaneUVE(light.range);
        const float farPlane = std::max(light.range, nearPlane + 0.1F);

        // A spot light is one orientation; a point light is six. Both are expressed as the same
        // struct so the renderer, the atlas layout and the shader contract have one shape.
        const std::uint32_t faceCount = isPoint ? kPointShadowFaceCountUVE : 1U;
        Math::QuaternionUVE rotation{};
        if (isSpot && !Math::TryNormalizeUVE(light.rotation, rotation)) {
            continue;
        }

        const std::size_t firstTile = plan.count;
        bool complete = true;
        for (std::uint32_t face = 0U; face < faceCount; ++face) {
            const std::optional<TileAllocUVE> tile = atlas.AllocateTileUVE(tileResolution);
            if (!tile.has_value()) {
                // Unreachable by construction - ResolveShadowTileResolutionUVE fits
                // kMaxShadowTilesUVE tiles of at least 128x128 into 4096x4096 - but a planner that
                // silently produced a plan the atlas could not satisfy would be worse than one that
                // reports fewer lights, so the shortfall is reported through `count`.
                complete = false;
                break;
            }
            LightShadowAssignmentUVE& assignment = plan.assignments[plan.count++];
            assignment.lightIndex = lightIndex;
            assignment.faceIndex = face;
            assignment.isPoint = isPoint;
            assignment.tile = *tile;
            assignment.nearPlane = nearPlane;
            assignment.farPlane = farPlane;
            assignment.lightViewProjection =
                isPoint ? ShadowAtlasManagerUVE::ComputePointLightFaceMatrixUVE(
                              light.position, face, farPlane, nearPlane)
                        : ShadowAtlasManagerUVE::ComputeSpotLightMatrixUVE(
                              light.position, rotation, light.spotAngleDegrees, farPlane, nearPlane);
        }
        if (!complete) {
            // Roll the partial light back: a point light with three of its six faces allocated
            // would sample a face that was never rendered, which reads as shadow acne across half
            // the scene rather than as a missing light.
            while (plan.count > firstTile) {
                --plan.count;
                atlas.FreeTileUVE(plan.assignments[plan.count].tile);
            }
            break;
        }
        ++shadowedLights;
    }

    if (plan.count == 0U) {
        return plan;
    }
    plan.tileResolution = tileResolution;
    return plan;
}

} // namespace UVE::Render
