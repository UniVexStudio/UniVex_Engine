// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/fog_volume_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/visibility_region_3d_uve.h"
#include "uve/objects/3d/world_partition_3d_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kMinimumScaleAxisUVE = 1.0e-6F;
constexpr float kShapeEpsilonUVE = 1e-5F;

[[nodiscard]] bool IsNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float Clamp01UVE(const float value) noexcept {
    return std::min(std::max(value, 0.0F), 1.0F);
}

[[nodiscard]] float SmoothstepUVE(const float edge0, const float edge1, const float x) noexcept {
    if (!(edge1 > edge0)) {
        return x >= edge1 ? 1.0F : 0.0F;
    }
    const float t = Clamp01UVE((x - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Math::Vector3UVE HalfExtentsUVE(const FogVolume3DFrameUVE& frame) noexcept {
    return Math::Vector3UVE{frame.size.x * 0.5F, frame.size.y * 0.5F, frame.size.z * 0.5F};
}

[[nodiscard]] float ShapeNormalizedDistanceUVE(const FogVolumeShapeUVE shape, const Math::Vector3UVE& local,
                                               const Math::Vector3UVE& half) noexcept {
    if (!(half.x > 0.0F) || !(half.y > 0.0F) || !(half.z > 0.0F)) {
        return 2.0F;
    }
    switch (shape) {
    case FogVolumeShapeUVE::World:
        return 0.0F;
    case FogVolumeShapeUVE::Box:
        return std::max(std::abs(local.x) / half.x, std::max(std::abs(local.y) / half.y, std::abs(local.z) / half.z));
    case FogVolumeShapeUVE::Ellipsoid: {
        const float nx = local.x / half.x;
        const float ny = local.y / half.y;
        const float nz = local.z / half.z;
        return std::sqrt(nx * nx + ny * ny + nz * nz);
    }
    case FogVolumeShapeUVE::Cylinder: {
        const float nx = local.x / half.x;
        const float nz = local.z / half.z;
        return std::max(std::sqrt(nx * nx + nz * nz), std::abs(local.y) / half.y);
    }
    case FogVolumeShapeUVE::Cone: {
        if (local.y < -half.y || local.y > half.y) {
            return 2.0F;
        }
        const float along = (local.y + half.y) / (2.0F * half.y);
        const float allowed = 1.0F - along;
        const float nx = local.x / half.x;
        const float nz = local.z / half.z;
        const float radial = std::sqrt(nx * nx + nz * nz);
        const float radialN = allowed > kShapeEpsilonUVE ? radial / allowed : (radial > kShapeEpsilonUVE ? 2.0F : 0.0F);
        return std::max(std::abs(local.y) / half.y, radialN);
    }
    }
    return 2.0F;
}

[[nodiscard]] float OccupancyUVE(const FogVolume3DFrameUVE& frame, const float normalizedDistance) noexcept {
    if (frame.shape == FogVolumeShapeUVE::World) {
        return 1.0F;
    }
    if (normalizedDistance > 1.0F) {
        return 0.0F;
    }
    if (!(frame.edgeFade > 0.0F)) {
        return 1.0F;
    }
    return 1.0F - SmoothstepUVE(1.0F - frame.edgeFade, 1.0F, normalizedDistance);
}

[[nodiscard]] float HeightTermUVE(const FogVolume3DFrameUVE& frame, const Math::Vector3UVE& worldPoint,
                                  const std::optional<Math::Vector3UVE>& local) noexcept {
    if (!(frame.heightFalloff > 0.0F)) {
        return 1.0F;
    }
    float height = 0.0F;
    if (frame.shape == FogVolumeShapeUVE::World) {
        height = worldPoint.y - frame.worldPosition.y;
    } else if (local.has_value()) {
        height = local->y + HalfExtentsUVE(frame).y;
    }
    return std::exp(-std::max(height, 0.0F) * frame.heightFalloff);
}

[[nodiscard]] bool IntersectLocalAabbUVE(const Math::Vector3UVE& origin, const Math::Vector3UVE& direction,
                                         const Math::Vector3UVE& half, const float rayLength, float& enter,
                                         float& exit) noexcept {
    if (!(rayLength > 0.0F) || !std::isfinite(rayLength)) {
        return false;
    }
    enter = 0.0F;
    exit = rayLength;
    const float origins[3] = {origin.x, origin.y, origin.z};
    const float directions[3] = {direction.x, direction.y, direction.z};
    const float halves[3] = {half.x, half.y, half.z};
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (!(halves[axis] > 0.0F)) {
            return false;
        }
        if (std::abs(directions[axis]) < kShapeEpsilonUVE) {
            if (origins[axis] < -halves[axis] || origins[axis] > halves[axis]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0F / directions[axis];
        float nearT = (-halves[axis] - origins[axis]) * inv;
        float farT = (halves[axis] - origins[axis]) * inv;
        if (nearT > farT) {
            std::swap(nearT, farT);
        }
        enter = std::max(enter, nearT);
        exit = std::min(exit, farT);
        if (enter >= exit) {
            return false;
        }
    }
    enter = std::max(enter, 0.0F);
    exit = std::min(exit, rayLength);
    return exit > enter;
}

} // namespace

bool IsFogVolume3DObjectComponentValidUVE(const FogVolume3DComponentUVE& value) noexcept {
    return value.shape <= FogVolumeShapeUVE::World && IsFinite3DObjectVectorUVE(value.size) && value.size.x > 0.0F &&
           value.size.y > 0.0F && value.size.z > 0.0F && std::isfinite(value.density) &&
           IsNonNegativeUVE(value.albedo.x) && IsNonNegativeUVE(value.albedo.y) && IsNonNegativeUVE(value.albedo.z) &&
           IsNonNegativeUVE(value.emission.x) && IsNonNegativeUVE(value.emission.y) &&
           IsNonNegativeUVE(value.emission.z) && IsNonNegativeUVE(value.heightFalloff) &&
           std::isfinite(value.edgeFade) && value.edgeFade >= 0.0F && value.edgeFade <= 1.0F &&
           IsBounded3DObjectStringUVE(value.materialAssetPath);
}

void ApplyFogVolume3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                       const FogVolume3DObjectDefinitionUVE& value) {
    ApplyRenderInstance3DBaseUVE(entityManager, entity, FogVolume3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<FogVolume3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<FogVolume3DComponentUVE>(entity, value.fog);
    }
}

bool TryMakeFogVolume3DFrameUVE(const FogVolume3DComponentUVE& value, const Math::Vector3UVE& worldPosition,
                                const Math::QuaternionUVE& worldRotation, const Math::Vector3UVE& worldScale,
                                FogVolume3DFrameUVE& out) noexcept {
    out = FogVolume3DFrameUVE{};
    if (!IsFogVolume3DObjectComponentValidUVE(value) || !IsFinite3DObjectVectorUVE(worldPosition) ||
        !IsFinite3DObjectVectorUVE(worldScale) || !IsFinite3DObjectQuaternionUVE(worldRotation)) {
        return false;
    }
    if (std::abs(worldScale.x) < kMinimumScaleAxisUVE || std::abs(worldScale.y) < kMinimumScaleAxisUVE ||
        std::abs(worldScale.z) < kMinimumScaleAxisUVE) {
        return false;
    }
    Math::QuaternionUVE rotation{};
    if (!Math::TryNormalizeUVE(worldRotation, rotation)) {
        return false;
    }
    const Math::Vector3UVE axisX = Math::RotateVectorUVE(rotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    const Math::Vector3UVE axisY = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE axisZ = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});
    if (!Math::IsFiniteUVE(axisX) || !Math::IsFiniteUVE(axisY) || !Math::IsFiniteUVE(axisZ)) {
        return false;
    }
    out.worldPosition = worldPosition;
    out.axisX = axisX;
    out.axisY = axisY;
    out.axisZ = axisZ;
    out.worldScale = worldScale;
    out.size = value.size;
    out.albedo = value.albedo;
    out.emission = value.emission;
    out.density = value.density;
    out.heightFalloff = value.heightFalloff;
    out.edgeFade = value.edgeFade;
    out.shape = value.shape;
    return true;
}

std::optional<Math::Vector3UVE> FogVolume3DWorldToLocalUVE(const FogVolume3DFrameUVE& frame,
                                                           const Math::Vector3UVE& worldPoint) noexcept {
    if (!Math::IsFiniteUVE(worldPoint) || std::abs(frame.worldScale.x) < kMinimumScaleAxisUVE ||
        std::abs(frame.worldScale.y) < kMinimumScaleAxisUVE || std::abs(frame.worldScale.z) < kMinimumScaleAxisUVE) {
        return std::nullopt;
    }
    const Math::Vector3UVE delta = worldPoint - frame.worldPosition;
    const Math::Vector3UVE local{Math::DotUVE(delta, frame.axisX) / frame.worldScale.x,
                                 Math::DotUVE(delta, frame.axisY) / frame.worldScale.y,
                                 Math::DotUVE(delta, frame.axisZ) / frame.worldScale.z};
    if (!Math::IsFiniteUVE(local)) {
        return std::nullopt;
    }
    return local;
}

float SampleFogVolume3DDensityUVE(const FogVolume3DFrameUVE& frame, const Math::Vector3UVE& worldPoint) noexcept {
    if (frame.shape == FogVolumeShapeUVE::World) {
        return frame.density * HeightTermUVE(frame, worldPoint, std::nullopt);
    }
    const std::optional<Math::Vector3UVE> local = FogVolume3DWorldToLocalUVE(frame, worldPoint);
    if (!local.has_value()) {
        return 0.0F;
    }
    const float distance = ShapeNormalizedDistanceUVE(frame.shape, *local, HalfExtentsUVE(frame));
    const float occupancy = OccupancyUVE(frame, distance);
    if (!(occupancy > 0.0F)) {
        return 0.0F;
    }
    return frame.density * occupancy * HeightTermUVE(frame, worldPoint, local);
}

std::optional<FogVolume3DRaySegmentUVE> IntersectFogVolume3DRayUVE(const FogVolume3DFrameUVE& frame,
                                                                  const Math::Vector3UVE& rayOrigin,
                                                                  const Math::Vector3UVE& rayDirection,
                                                                  const float rayLength) noexcept {
    if (!Math::IsFiniteUVE(rayOrigin) || !Math::IsFiniteUVE(rayDirection) || !(rayLength > 0.0F)) {
        return std::nullopt;
    }
    if (frame.shape == FogVolumeShapeUVE::World) {
        return FogVolume3DRaySegmentUVE{0.0F, rayLength};
    }
    const std::optional<Math::Vector3UVE> localOrigin = FogVolume3DWorldToLocalUVE(frame, rayOrigin);
    if (!localOrigin.has_value() || std::abs(frame.worldScale.x) < kMinimumScaleAxisUVE ||
        std::abs(frame.worldScale.y) < kMinimumScaleAxisUVE || std::abs(frame.worldScale.z) < kMinimumScaleAxisUVE) {
        return std::nullopt;
    }
    const Math::Vector3UVE localDirection{Math::DotUVE(rayDirection, frame.axisX) / frame.worldScale.x,
                                          Math::DotUVE(rayDirection, frame.axisY) / frame.worldScale.y,
                                          Math::DotUVE(rayDirection, frame.axisZ) / frame.worldScale.z};
    if (!Math::IsFiniteUVE(localDirection)) {
        return std::nullopt;
    }
    float enter = 0.0F;
    float exit = 0.0F;
    if (!IntersectLocalAabbUVE(*localOrigin, localDirection, HalfExtentsUVE(frame), rayLength, enter, exit)) {
        return std::nullopt;
    }
    return FogVolume3DRaySegmentUVE{enter, exit};
}

FogVolume3DRaySampleUVE IntegrateFogVolume3DRayUVE(const FogVolume3DFrameUVE& frame, const Math::Vector3UVE& rayOrigin,
                                                   const Math::Vector3UVE& rayDirection, const float rayLength) noexcept {
    FogVolume3DRaySampleUVE sample{};
    const std::optional<FogVolume3DRaySegmentUVE> segment =
        IntersectFogVolume3DRayUVE(frame, rayOrigin, rayDirection, rayLength);
    if (!segment.has_value()) {
        return sample;
    }
    const float span = segment->exit - segment->enter;
    if (!(span > 0.0F)) {
        return sample;
    }
    const float step = span / static_cast<float>(kFogVolumeRaySamplesUVE);
    Math::Vector3UVE colorMass{};
    float occupancyMass = 0.0F;
    for (std::uint32_t i = 0; i < kFogVolumeRaySamplesUVE; ++i) {
        const float t = segment->enter + step * (static_cast<float>(i) + 0.5F);
        const Math::Vector3UVE point = rayOrigin + rayDirection * t;
        const float density = SampleFogVolume3DDensityUVE(frame, point);
        sample.opticalDepth += density * step;
        float occupancy = 0.0F;
        if (frame.shape == FogVolumeShapeUVE::World) {
            occupancy = 1.0F;
        } else if (const std::optional<Math::Vector3UVE> local = FogVolume3DWorldToLocalUVE(frame, point);
                   local.has_value()) {
            occupancy = OccupancyUVE(frame, ShapeNormalizedDistanceUVE(frame.shape, *local, HalfExtentsUVE(frame)));
        }
        if (occupancy > 0.0F) {
            const float height = HeightTermUVE(frame, point, FogVolume3DWorldToLocalUVE(frame, point));
            colorMass += (frame.albedo * std::max(frame.density, 0.0F) * height + frame.emission) * (occupancy * step);
            occupancyMass += occupancy * step;
        }
    }
    if (occupancyMass > 0.0F) {
        sample.scatterColor = Math::Vector3UVE{colorMass.x / occupancyMass, colorMass.y / occupancyMass,
                                               colorMass.z / occupancyMass};
        sample.scatterWeight = occupancyMass;
    }
    return sample;
}

std::size_t CollectFogVolume3DFramesUVE(IEntityManagerUVE& entityManager, const Math::Vector3UVE& viewPosition,
                                        const std::span<FogVolume3DFrameUVE> out) {
    if (out.empty()) {
        return 0;
    }
    struct RankedUVE final {
        FogVolume3DFrameUVE frame;
        float distanceSquared = 0.0F;
        bool world = false;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
    std::vector<RankedUVE> ranked;
    entityManager.ForEachUVE<WorldTransformComponentUVE, FogVolume3DComponentUVE>(
        [&entityManager, &viewPosition, &ranked](const EntityUVE entity, const WorldTransformComponentUVE& world,
                                                 const FogVolume3DComponentUVE& fog) {
            if (world.dirty || !IsFogVolume3DObjectComponentValidUVE(fog)) {
                return;
            }
            if (entityManager.HasComponentUVE<VisibilityComponentUVE>(entity) &&
                !entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy) {
                return;
            }
            if (IsWorldPartition3DDrawHiddenUVE(entityManager, entity) ||
                IsVisibilityRegion3DDrawHiddenUVE(entityManager, entity)) {
                return;
            }
            if (fog.density == 0.0F && Math::LengthSquaredUVE(fog.emission) <= 1.0e-12F) {
                return;
            }
            FogVolume3DFrameUVE frame{};
            if (!TryMakeFogVolume3DFrameUVE(fog, world.worldPosition, world.worldRotation, world.worldScale, frame)) {
                return;
            }
            const Math::Vector3UVE offset = world.worldPosition - viewPosition;
            ranked.push_back(RankedUVE{frame, Math::LengthSquaredUVE(offset),
                                       fog.shape == FogVolumeShapeUVE::World, entity.index, entity.generation});
        });
    std::sort(ranked.begin(), ranked.end(), [](const RankedUVE& lhs, const RankedUVE& rhs) {
        if (lhs.world != rhs.world) {
            return lhs.world && !rhs.world;
        }
        if (lhs.distanceSquared != rhs.distanceSquared) {
            return lhs.distanceSquared < rhs.distanceSquared;
        }
        if (lhs.index != rhs.index) {
            return lhs.index < rhs.index;
        }
        return lhs.generation < rhs.generation;
    });
    const std::size_t count = std::min(ranked.size(), out.size());
    for (std::size_t i = 0; i < count; ++i) {
        out[i] = ranked[i].frame;
    }
    return count;
}

void CollectFogVolume3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<FogVolume3DGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, FogVolume3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const FogVolume3DComponentUVE& fog) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty) {
                return;
            }
            FogVolume3DFrameUVE frame{};
            if (!TryMakeFogVolume3DFrameUVE(fog, world.worldPosition, world.worldRotation, world.worldScale, frame)) {
                return;
            }
            FogVolume3DGizmoUVE gizmo;
            gizmo.shape = frame.shape;
            gizmo.origin = frame.worldPosition;
            gizmo.axisX = frame.axisX;
            gizmo.axisY = frame.axisY;
            gizmo.axisZ = frame.axisZ;
            gizmo.halfExtents = Math::Vector3UVE{std::abs(frame.worldScale.x) * frame.size.x * 0.5F,
                                                 std::abs(frame.worldScale.y) * frame.size.y * 0.5F,
                                                 std::abs(frame.worldScale.z) * frame.size.z * 0.5F};
            if (frame.shape == FogVolumeShapeUVE::World) {
                gizmo.halfExtents = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
            }
            gizmo.color = Math::Vector3UVE{std::clamp(frame.albedo.x * 0.65F + 0.2F, 0.2F, 1.0F),
                                           std::clamp(frame.albedo.y * 0.75F + 0.25F, 0.25F, 1.0F),
                                           std::clamp(frame.albedo.z * 0.85F + 0.35F, 0.35F, 1.0F)};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
