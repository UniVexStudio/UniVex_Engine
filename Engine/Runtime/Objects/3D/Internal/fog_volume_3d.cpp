// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "fog_volume_3d.h"

#include <algorithm>
#include <cmath>

#include "editor_internal_entity_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "aabb.h"
#include "quaternion.h"
#include "abstract_objects_3d.h"
#include "occluder_3d.h"
#include "visibility_region_3d.h"
#include "world_partition_3d.h"

namespace UVE::Scene {
namespace {

constexpr float kMinimumScaleAxis = 1.0e-6F;
constexpr float kShapeEpsilon = 1e-5F;

[[nodiscard]] bool IsNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

[[nodiscard]] float Clamp01(const float value) noexcept {
    return std::min(std::max(value, 0.0F), 1.0F);
}

[[nodiscard]] float Smoothstep(const float edge0, const float edge1, const float x) noexcept {
    if (!(edge1 > edge0)) {
        return x >= edge1 ? 1.0F : 0.0F;
    }
    const float t = Clamp01((x - edge0) / (edge1 - edge0));
    return t * t * (3.0F - 2.0F * t);
}

[[nodiscard]] Math::Vector3 HalfExtents(const FogVolume3DFrame& frame) noexcept {
    return Math::Vector3{frame.size.x * 0.5F, frame.size.y * 0.5F, frame.size.z * 0.5F};
}

[[nodiscard]] float ShapeNormalizedDistance(const FogVolumeShape shape, const Math::Vector3& local,
                                               const Math::Vector3& half) noexcept {
    if (!(half.x > 0.0F) || !(half.y > 0.0F) || !(half.z > 0.0F)) {
        return 2.0F;
    }
    switch (shape) {
    case FogVolumeShape::World:
        return 0.0F;
    case FogVolumeShape::Box:
        return std::max(std::abs(local.x) / half.x, std::max(std::abs(local.y) / half.y, std::abs(local.z) / half.z));
    case FogVolumeShape::Ellipsoid: {
        const float nx = local.x / half.x;
        const float ny = local.y / half.y;
        const float nz = local.z / half.z;
        return std::sqrt(nx * nx + ny * ny + nz * nz);
    }
    case FogVolumeShape::Cylinder: {
        const float nx = local.x / half.x;
        const float nz = local.z / half.z;
        return std::max(std::sqrt(nx * nx + nz * nz), std::abs(local.y) / half.y);
    }
    case FogVolumeShape::Cone: {
        if (local.y < -half.y || local.y > half.y) {
            return 2.0F;
        }
        const float along = (local.y + half.y) / (2.0F * half.y);
        const float allowed = 1.0F - along;
        const float nx = local.x / half.x;
        const float nz = local.z / half.z;
        const float radial = std::sqrt(nx * nx + nz * nz);
        const float radialN = allowed > kShapeEpsilon ? radial / allowed : (radial > kShapeEpsilon ? 2.0F : 0.0F);
        return std::max(std::abs(local.y) / half.y, radialN);
    }
    }
    return 2.0F;
}

[[nodiscard]] float Occupancy(const FogVolume3DFrame& frame, const float normalizedDistance) noexcept {
    if (frame.shape == FogVolumeShape::World) {
        return 1.0F;
    }
    if (normalizedDistance > 1.0F) {
        return 0.0F;
    }
    if (!(frame.edgeFade > 0.0F)) {
        return 1.0F;
    }
    return 1.0F - Smoothstep(1.0F - frame.edgeFade, 1.0F, normalizedDistance);
}

[[nodiscard]] float HeightTerm(const FogVolume3DFrame& frame, const Math::Vector3& worldPoint,
                                  const std::optional<Math::Vector3>& local) noexcept {
    if (!(frame.heightFalloff > 0.0F)) {
        return 1.0F;
    }
    float height = 0.0F;
    if (frame.shape == FogVolumeShape::World) {
        height = worldPoint.y - frame.worldPosition.y;
    } else if (local.has_value()) {
        height = local->y + HalfExtents(frame).y;
    }
    return std::exp(-std::max(height, 0.0F) * frame.heightFalloff);
}

[[nodiscard]] bool IntersectLocalAabb(const Math::Vector3& origin, const Math::Vector3& direction,
                                         const Math::Vector3& half, const float rayLength, float& enter,
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
        if (std::abs(directions[axis]) < kShapeEpsilon) {
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

bool IsFogVolume3DObjectComponentValid(const FogVolume3DComponent& value) noexcept {
    return value.shape <= FogVolumeShape::World && IsFinite3DObjectVector(value.size) && value.size.x > 0.0F &&
           value.size.y > 0.0F && value.size.z > 0.0F && std::isfinite(value.density) &&
           IsNonNegative(value.albedo.x) && IsNonNegative(value.albedo.y) && IsNonNegative(value.albedo.z) &&
           IsNonNegative(value.emission.x) && IsNonNegative(value.emission.y) &&
           IsNonNegative(value.emission.z) && IsNonNegative(value.heightFalloff) &&
           std::isfinite(value.edgeFade) && value.edgeFade >= 0.0F && value.edgeFade <= 1.0F &&
           IsBounded3DObjectString(value.materialAssetPath);
}

void ApplyFogVolume3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                       const FogVolume3DObjectDefinition& value) {
    ApplyRenderInstance3DBase(entityManager, entity, FogVolume3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<FogVolume3DComponent>(entity)) {
        entityManager.AddComponent<FogVolume3DComponent>(entity, value.fog);
    }
}

bool TryMakeFogVolume3DFrame(const FogVolume3DComponent& value, const Math::Vector3& worldPosition,
                                const Math::Quaternion& worldRotation, const Math::Vector3& worldScale,
                                FogVolume3DFrame& out) noexcept {
    out = FogVolume3DFrame{};
    if (!IsFogVolume3DObjectComponentValid(value) || !IsFinite3DObjectVector(worldPosition) ||
        !IsFinite3DObjectVector(worldScale) || !IsFinite3DObjectQuaternion(worldRotation)) {
        return false;
    }
    if (std::abs(worldScale.x) < kMinimumScaleAxis || std::abs(worldScale.y) < kMinimumScaleAxis ||
        std::abs(worldScale.z) < kMinimumScaleAxis) {
        return false;
    }
    Math::Quaternion rotation{};
    if (!Math::TryNormalize(worldRotation, rotation)) {
        return false;
    }
    const Math::Vector3 axisX = Math::RotateVector(rotation, Math::Vector3{1.0F, 0.0F, 0.0F});
    const Math::Vector3 axisY = Math::RotateVector(rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 axisZ = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, 1.0F});
    if (!Math::IsFinite(axisX) || !Math::IsFinite(axisY) || !Math::IsFinite(axisZ)) {
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

std::optional<Math::Vector3> FogVolume3DWorldToLocal(const FogVolume3DFrame& frame,
                                                           const Math::Vector3& worldPoint) noexcept {
    if (!Math::IsFinite(worldPoint) || std::abs(frame.worldScale.x) < kMinimumScaleAxis ||
        std::abs(frame.worldScale.y) < kMinimumScaleAxis || std::abs(frame.worldScale.z) < kMinimumScaleAxis) {
        return std::nullopt;
    }
    const Math::Vector3 delta = worldPoint - frame.worldPosition;
    const Math::Vector3 local{Math::Dot(delta, frame.axisX) / frame.worldScale.x,
                                 Math::Dot(delta, frame.axisY) / frame.worldScale.y,
                                 Math::Dot(delta, frame.axisZ) / frame.worldScale.z};
    if (!Math::IsFinite(local)) {
        return std::nullopt;
    }
    return local;
}

float SampleFogVolume3DDensity(const FogVolume3DFrame& frame, const Math::Vector3& worldPoint) noexcept {
    if (frame.shape == FogVolumeShape::World) {
        return frame.density * HeightTerm(frame, worldPoint, std::nullopt);
    }
    const std::optional<Math::Vector3> local = FogVolume3DWorldToLocal(frame, worldPoint);
    if (!local.has_value()) {
        return 0.0F;
    }
    const float distance = ShapeNormalizedDistance(frame.shape, *local, HalfExtents(frame));
    const float occupancy = Occupancy(frame, distance);
    if (!(occupancy > 0.0F)) {
        return 0.0F;
    }
    return frame.density * occupancy * HeightTerm(frame, worldPoint, local);
}

std::optional<FogVolume3DRaySegment> IntersectFogVolume3DRay(const FogVolume3DFrame& frame,
                                                                  const Math::Vector3& rayOrigin,
                                                                  const Math::Vector3& rayDirection,
                                                                  const float rayLength) noexcept {
    if (!Math::IsFinite(rayOrigin) || !Math::IsFinite(rayDirection) || !(rayLength > 0.0F)) {
        return std::nullopt;
    }
    if (frame.shape == FogVolumeShape::World) {
        return FogVolume3DRaySegment{0.0F, rayLength};
    }
    const std::optional<Math::Vector3> localOrigin = FogVolume3DWorldToLocal(frame, rayOrigin);
    if (!localOrigin.has_value() || std::abs(frame.worldScale.x) < kMinimumScaleAxis ||
        std::abs(frame.worldScale.y) < kMinimumScaleAxis || std::abs(frame.worldScale.z) < kMinimumScaleAxis) {
        return std::nullopt;
    }
    const Math::Vector3 localDirection{Math::Dot(rayDirection, frame.axisX) / frame.worldScale.x,
                                          Math::Dot(rayDirection, frame.axisY) / frame.worldScale.y,
                                          Math::Dot(rayDirection, frame.axisZ) / frame.worldScale.z};
    if (!Math::IsFinite(localDirection)) {
        return std::nullopt;
    }
    float enter = 0.0F;
    float exit = 0.0F;
    if (!IntersectLocalAabb(*localOrigin, localDirection, HalfExtents(frame), rayLength, enter, exit)) {
        return std::nullopt;
    }
    return FogVolume3DRaySegment{enter, exit};
}

FogVolume3DRaySample IntegrateFogVolume3DRay(const FogVolume3DFrame& frame, const Math::Vector3& rayOrigin,
                                                   const Math::Vector3& rayDirection, const float rayLength) noexcept {
    FogVolume3DRaySample sample{};
    const std::optional<FogVolume3DRaySegment> segment =
        IntersectFogVolume3DRay(frame, rayOrigin, rayDirection, rayLength);
    if (!segment.has_value()) {
        return sample;
    }
    const float span = segment->exit - segment->enter;
    if (!(span > 0.0F)) {
        return sample;
    }
    const float step = span / static_cast<float>(kFogVolumeRaySamples);
    Math::Vector3 colorMass{};
    float occupancyMass = 0.0F;
    for (std::uint32_t i = 0; i < kFogVolumeRaySamples; ++i) {
        const float t = segment->enter + step * (static_cast<float>(i) + 0.5F);
        const Math::Vector3 point = rayOrigin + rayDirection * t;
        const float density = SampleFogVolume3DDensity(frame, point);
        sample.opticalDepth += density * step;
        float occupancy = 0.0F;
        if (frame.shape == FogVolumeShape::World) {
            occupancy = 1.0F;
        } else if (const std::optional<Math::Vector3> local = FogVolume3DWorldToLocal(frame, point);
                   local.has_value()) {
            occupancy = Occupancy(frame, ShapeNormalizedDistance(frame.shape, *local, HalfExtents(frame)));
        }
        if (occupancy > 0.0F) {
            const float height = HeightTerm(frame, point, FogVolume3DWorldToLocal(frame, point));
            colorMass += (frame.albedo * std::max(frame.density, 0.0F) * height + frame.emission) * (occupancy * step);
            occupancyMass += occupancy * step;
        }
    }
    if (occupancyMass > 0.0F) {
        sample.scatterColor = Math::Vector3{colorMass.x / occupancyMass, colorMass.y / occupancyMass,
                                               colorMass.z / occupancyMass};
        sample.scatterWeight = occupancyMass;
    }
    return sample;
}

std::size_t CollectFogVolume3DFrames(IEntityManager& entityManager, const Math::Vector3& viewPosition,
                                        const std::span<FogVolume3DFrame> out) {
    if (out.empty()) {
        return 0;
    }
    struct Ranked final {
        FogVolume3DFrame frame;
        float distanceSquared = 0.0F;
        bool world = false;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
    std::vector<Ranked> ranked;
    std::vector<Occluder3DSnapshot> occluders;
    CollectOccluder3DSnapshots(entityManager, occluders);
    entityManager.ForEach<WorldTransformComponent, FogVolume3DComponent>(
        [&entityManager, &viewPosition, &ranked, &occluders](const Entity entity,
                                                             const WorldTransformComponent& world,
                                                             const FogVolume3DComponent& fog) {
            if (world.dirty || !IsFogVolume3DObjectComponentValid(fog)) {
                return;
            }
            if (entityManager.HasComponent<VisibilityComponent>(entity) &&
                !entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy) {
                return;
            }
            if (IsWorldPartition3DDrawHidden(entityManager, entity) ||
                IsVisibilityRegion3DDrawHidden(entityManager, entity)) {
                return;
            }
            if (fog.density == 0.0F && Math::LengthSquared(fog.emission) <= 1.0e-12F) {
                return;
            }
            FogVolume3DFrame frame{};
            if (!TryMakeFogVolume3DFrame(fog, world.worldPosition, world.worldRotation, world.worldScale, frame)) {
                return;
            }
            if (fog.shape != FogVolumeShape::World) {
                const Math::Vector3 half{std::abs(frame.worldScale.x) * frame.size.x * 0.5F,
                                            std::abs(frame.worldScale.y) * frame.size.y * 0.5F,
                                            std::abs(frame.worldScale.z) * frame.size.z * 0.5F};
                const Math::Aabb bounds = Math::Aabb::FromCenterExtents(frame.worldPosition, half);
                if (IsOccluder3DAabbDrawHidden(occluders, viewPosition, bounds)) {
                    return;
                }
            }
            const Math::Vector3 offset = world.worldPosition - viewPosition;
            ranked.push_back(Ranked{frame, Math::LengthSquared(offset),
                                       fog.shape == FogVolumeShape::World, entity.index, entity.generation});
        });
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& lhs, const Ranked& rhs) {
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

void CollectFogVolume3DGizmos(IEntityManager& entityManager, std::vector<FogVolume3DGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, FogVolume3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const FogVolume3DComponent& fog) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty) {
                return;
            }
            FogVolume3DFrame frame{};
            if (!TryMakeFogVolume3DFrame(fog, world.worldPosition, world.worldRotation, world.worldScale, frame)) {
                return;
            }
            FogVolume3DGizmo gizmo;
            gizmo.shape = frame.shape;
            gizmo.origin = frame.worldPosition;
            gizmo.axisX = frame.axisX;
            gizmo.axisY = frame.axisY;
            gizmo.axisZ = frame.axisZ;
            gizmo.halfExtents = Math::Vector3{std::abs(frame.worldScale.x) * frame.size.x * 0.5F,
                                                 std::abs(frame.worldScale.y) * frame.size.y * 0.5F,
                                                 std::abs(frame.worldScale.z) * frame.size.z * 0.5F};
            if (frame.shape == FogVolumeShape::World) {
                gizmo.halfExtents = Math::Vector3{1.0F, 1.0F, 1.0F};
            }
            gizmo.color = Math::Vector3{std::clamp(frame.albedo.x * 0.65F + 0.2F, 0.2F, 1.0F),
                                           std::clamp(frame.albedo.y * 0.75F + 0.25F, 0.25F, 1.0F),
                                           std::clamp(frame.albedo.z * 0.85F + 0.35F, 0.35F, 1.0F)};
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
