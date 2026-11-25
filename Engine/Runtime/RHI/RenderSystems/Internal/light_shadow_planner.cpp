// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "light_shadow_planner.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "aabb.h"
#include "quaternion.h"
#include "vector2.h"
#include "vector3.h"

namespace UVE::Render {

namespace {

/// Inclusive cone limit the shader can represent. Below one degree the projection collapses to a
/// line; at 90 and above it is not a cone any more (a hemisphere or worse), and the shader's
/// `cos(radians(spotAngleDegrees))` falloff band would invert.
constexpr float kMinimumSpotAngleDegrees = 1.0F;
constexpr float kMaximumSpotAngleDegrees = 89.0F;

/// Rounds to the nearest power of two inside the atlas' supported tile range. Anything above the
/// atlas root collapses to the root; anything at or below the minimum collapses to the minimum.
[[nodiscard]] constexpr std::uint32_t RoundTileResolution(const std::uint32_t requested) noexcept {
    if (requested <= kShadowAtlasMinTileResolution) {
        return kShadowAtlasMinTileResolution;
    }
    if (requested >= kShadowAtlasMaxTileResolution) {
        return kShadowAtlasMaxTileResolution;
    }
    std::uint32_t tile = kShadowAtlasMinTileResolution;
    while (tile * 2U <= requested) {
        tile *= 2U;
    }
    return tile;
}

/// Tiles of `tileResolution` that fit in the square atlas, along one axis.
[[nodiscard]] constexpr std::uint64_t TilesPerAxis(const std::uint32_t tileResolution) noexcept {
    return static_cast<std::uint64_t>(kShadowAtlasResolution) /
           static_cast<std::uint64_t>(std::max(tileResolution, 1U));
}

/// Rules every shadow-casting light must satisfy whatever its type.
[[nodiscard]] bool HasUsableShadowState(const LightData& light) noexcept {
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
    return Math::IsFinite(light.position) && Math::IsFinite(light.rotation);
}

/// The light's reach, as a sphere that CONTAINS whatever it can illuminate. Both light types are
/// bounded by their own `range`, so one radius serves both - and culling a sphere that contains the
/// illuminated volume against the view frustum can only ever reject lights that cannot affect a
/// visible pixel.
[[nodiscard]] bool ReachesViewFrustum(const LightData& light,
                                         const Math::Frustum& viewFrustum) noexcept {
    const float radius = light.range;
    const Math::Aabb influence =
        Math::Aabb::FromCenterExtents(light.position, Math::Vector3{radius, radius, radius});
    return viewFrustum.Intersects(influence);
}

/// Tiles a single light needs, assuming it already passed eligibility.
[[nodiscard]] constexpr std::size_t TilesForLight(const LightData& light) noexcept {
    return light.type == Scene::LightType::Point
               ? static_cast<std::size_t>(kPointShadowFaceCount)
               : 1U;
}

} // namespace

bool IsSpotLightShadowCandidate(const LightData& light,
                                   const Math::Frustum& viewFrustum) noexcept {
    if (light.type != Scene::LightType::Spot) {
        return false;
    }
    if (!HasUsableShadowState(light)) {
        return false;
    }
    if (!(light.spotAngleDegrees >= kMinimumSpotAngleDegrees) ||
        !(light.spotAngleDegrees <= kMaximumSpotAngleDegrees)) {
        return false;
    }
    Math::Quaternion normalizedRotation{};
    if (!Math::TryNormalize(light.rotation, normalizedRotation)) {
        return false;
    }
    return ReachesViewFrustum(light, viewFrustum);
}

bool IsPointLightShadowCandidate(const LightData& light,
                                    const Math::Frustum& viewFrustum) noexcept {
    if (light.type != Scene::LightType::Point) {
        return false;
    }
    if (!HasUsableShadowState(light)) {
        return false;
    }
    // No cone and no orientation: a point light's six faces come from a fixed table, so unlike the
    // spot path there is nothing here for a degenerate rotation to invalidate. Requiring a unit
    // quaternion anyway would reject lights that can shadow perfectly well, which is the kind of
    // over-strict eligibility rule that shows up as "shadows randomly off on some lights".
    return ReachesViewFrustum(light, viewFrustum);
}

std::size_t CountShadowTiles(const LightList& lights,
                                const Math::Frustum& viewFrustum) noexcept {
    std::size_t tiles = 0U;
    std::size_t shadowedLights = 0U;
    for (std::size_t lightIndex = 0; lightIndex < lights.Size(); ++lightIndex) {
        if (shadowedLights >= kMaxShadowedLights) {
            break;
        }
        const LightData& light = lights[lightIndex];
        if (IsSpotLightShadowCandidate(light, viewFrustum) ||
            IsPointLightShadowCandidate(light, viewFrustum)) {
            tiles += TilesForLight(light);
            ++shadowedLights;
        }
    }
    return tiles;
}

std::uint32_t ResolveShadowTileResolution(const std::uint32_t requestedResolution,
                                             const std::size_t tileDemand) noexcept {
    if (tileDemand == 0U) {
        return 0U;
    }
    const std::size_t demand = std::min(tileDemand, kMaxShadowTiles);
    std::uint32_t tile = RoundTileResolution(requestedResolution);
    while (tile > kShadowAtlasMinTileResolution) {
        const std::uint64_t perAxis = TilesPerAxis(tile);
        if (perAxis * perAxis >= static_cast<std::uint64_t>(demand)) {
            break;
        }
        tile /= 2U;
    }
    return tile;
}

float ComputeLightShadowNearPlane(const float range) noexcept {
    if (!(range > 0.0F) || !std::isfinite(range)) {
        return 0.02F;
    }
    return std::clamp(range * 0.02F, 0.02F, 1.0F);
}

float ShadowLinearDepth(const float nearPlane, const float farPlane,
                               const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float denominator = farValue - (farValue - nearValue) * normalizedDepth;
    return (nearValue * farValue) / std::max(denominator, 1.0e-6F);
}

float PointShadowProjectionDepth(const float nearPlane, const float farPlane,
                                    const float axisDistance) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    if (!(axisDistance > nearValue)) {
        return 0.0F;
    }
    return farValue * (1.0F - nearValue / axisDistance) / (farValue - nearValue);
}

float ShadowDepthSlope(const float nearPlane, const float farPlane,
                              const float normalizedDepth) noexcept {
    const float nearValue = std::max(nearPlane, 0.01F);
    const float farValue = std::max(farPlane, nearValue + 0.1F);
    const float linear = ShadowLinearDepth(nearValue, farValue, normalizedDepth);
    return (nearValue * farValue) / std::max((farValue - nearValue) * linear * linear, 1.0e-6F);
}

LightShadowPlan BuildLightShadowPlan(const LightList& lights,
                                           const Math::Frustum& viewFrustum,
                                           ShadowAtlasManager& atlas,
                                           const std::uint32_t requestedResolution) noexcept {
    LightShadowPlan plan{};
    plan.lightToShadowSlot.fill(-1);
    atlas.Reset();

    // Counted first, not accumulated on the fly: the tile resolution depends on how many tiles the
    // frame needs, and every tile must be allocated at the SAME resolution for the layout to be a
    // single power-of-two tiling of the atlas.
    const std::size_t tileDemand = CountShadowTiles(lights, viewFrustum);
    const std::uint32_t tileResolution =
        ResolveShadowTileResolution(requestedResolution, tileDemand);
    if (tileResolution == 0U) {
        return plan;
    }

    std::size_t shadowedLights = 0U;
    for (std::size_t lightIndex = 0; lightIndex < lights.Size(); ++lightIndex) {
        if (shadowedLights >= kMaxShadowedLights) {
            break;
        }
        const LightData& light = lights[lightIndex];
        const bool isSpot = IsSpotLightShadowCandidate(light, viewFrustum);
        const bool isPoint = !isSpot && IsPointLightShadowCandidate(light, viewFrustum);
        if (!isSpot && !isPoint) {
            continue;
        }
        const float nearPlane = ComputeLightShadowNearPlane(light.range);
        const float farPlane = std::max(light.range, nearPlane + 0.1F);

        // A spot light is one orientation; a point light is six. Both are expressed as the same
        // struct so the renderer, the atlas layout and the shader contract have one shape.
        const std::uint32_t faceCount = isPoint ? kPointShadowFaceCount : 1U;
        Math::Quaternion rotation{};
        if (isSpot && !Math::TryNormalize(light.rotation, rotation)) {
            continue;
        }

        const std::size_t firstTile = plan.count;
        bool complete = true;
        for (std::uint32_t face = 0U; face < faceCount; ++face) {
            const std::optional<TileAlloc> tile = atlas.AllocateTile(tileResolution);
            if (!tile.has_value()) {
                // Unreachable by construction - ResolveShadowTileResolution fits
                // kMaxShadowTiles tiles of at least 128x128 into 4096x4096 - but a planner that
                // silently produced a plan the atlas could not satisfy would be worse than one that
                // reports fewer lights, so the shortfall is reported through `count`.
                complete = false;
                break;
            }
            LightShadowAssignment& assignment = plan.assignments[plan.count++];
            assignment.lightIndex = lightIndex;
            assignment.shadowSlot = shadowedLights;
            assignment.faceIndex = face;
            assignment.isPoint = isPoint;
            assignment.tile = *tile;
            assignment.nearPlane = nearPlane;
            assignment.farPlane = farPlane;
            assignment.lightViewProjection =
                isPoint ? ShadowAtlasManager::ComputePointLightFaceMatrix(
                              light.position, face, farPlane, nearPlane)
                        : ShadowAtlasManager::ComputeSpotLightMatrix(
                              light.position, rotation, light.spotAngleDegrees, farPlane, nearPlane);
        }
        if (!complete) {
            // Roll the partial light back: a point light with three of its six faces allocated
            // would sample a face that was never rendered, which reads as shadow acne across half
            // the scene rather than as a missing light.
            while (plan.count > firstTile) {
                --plan.count;
                atlas.FreeTile(plan.assignments[plan.count].tile);
            }
            break;
        }
        plan.lightToShadowSlot[lightIndex] = static_cast<std::int32_t>(shadowedLights);
        ++shadowedLights;
    }

    if (plan.count == 0U) {
        return plan;
    }
    plan.tileResolution = tileResolution;
    return plan;
}

} // namespace UVE::Render
