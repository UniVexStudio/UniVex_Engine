// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "reflection_probe_3d.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "editor_internal_entity_component.h"
#include "visibility_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "quaternion.h"
#include "abstract_objects_3d.h"

namespace UVE::Scene {
namespace {

constexpr float kCubemapDenomEpsilon = 1.0e-8F;
constexpr float kBoxRayEpsilon = 1.0e-8F;

} // namespace

bool IsReflectionProbe3DObjectComponentValid(const ReflectionProbe3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.size) && value.size.x > 0.0F && value.size.y > 0.0F &&
           value.size.z > 0.0F && value.updateMode <= ReflectionProbeUpdateMode::OnDemand &&
           IsReflectionProbeResolutionValid(value.resolution);
}

void ApplyReflectionProbe3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                               const ReflectionProbe3DObjectDefinition& value) {
    ApplyObject3DRecipe(entityManager, entity, ReflectionProbe3DObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) &&
        !entityManager.HasComponent<ReflectionProbe3DComponent>(entity)) {
        entityManager.AddComponent<ReflectionProbe3DComponent>(entity, value.probe);
    }
}

float ResolveReflectionProbe3DInfluenceWeight(
    const Math::Vector3& probeLocalPoint,
    const Math::Vector3& probeHalfExtents) noexcept {
    if (!std::isfinite(probeLocalPoint.x) || !std::isfinite(probeLocalPoint.y) ||
        !std::isfinite(probeLocalPoint.z) || !std::isfinite(probeHalfExtents.x) ||
        !std::isfinite(probeHalfExtents.y) || !std::isfinite(probeHalfExtents.z) ||
        probeHalfExtents.x <= 0.0F || probeHalfExtents.y <= 0.0F || probeHalfExtents.z <= 0.0F) {
        return 0.0F; // degenerate boxes influence nothing - fail-closed, never NaN
    }
    const float normalizedDistance = std::fmax(
        std::fmax(std::fabs(probeLocalPoint.x) / probeHalfExtents.x,
                  std::fabs(probeLocalPoint.y) / probeHalfExtents.y),
        std::fabs(probeLocalPoint.z) / probeHalfExtents.z);
    if (normalizedDistance >= 1.0F) {
        return 0.0F; // at or beyond the face the probe ends - the boundary is not inside
    }
    return 1.0F - normalizedDistance;
}

ReflectionProbe3DCaptureAction ResolveReflectionProbe3DCaptureAction(
    const ReflectionProbe3DCaptureFrame& frame) noexcept {
    if (!frame.enabled || frame.updateMode > ReflectionProbeUpdateMode::OnDemand ||
        (frame.hasCameraViewer && !std::isfinite(frame.cameraInfluenceWeight))) {
        return ReflectionProbe3DCaptureAction::None;
    }
    switch (frame.updateMode) {
    case ReflectionProbeUpdateMode::EveryFrame:
        if (frame.hasCameraViewer && frame.cameraInfluenceWeight > 0.0F) {
            return ReflectionProbe3DCaptureAction::Capture;
        }
        break;
    case ReflectionProbeUpdateMode::OnDemand:
        if (frame.updateRequested) {
            return ReflectionProbe3DCaptureAction::Capture;
        }
        break;
    case ReflectionProbeUpdateMode::Once:
        if (!frame.capturedOnce) {
            return ReflectionProbe3DCaptureAction::Capture;
        }
        break;
    }
    return ReflectionProbe3DCaptureAction::None;
}

bool TryGetCubemapFaceBasis(const CubemapFace face, Math::Vector3& outForward,
                               Math::Vector3& outUp) noexcept {
    switch (face) {
    case CubemapFace::PositiveX:
        outForward = Math::Vector3{1.0F, 0.0F, 0.0F};
        outUp = Math::Vector3{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFace::NegativeX:
        outForward = Math::Vector3{-1.0F, 0.0F, 0.0F};
        outUp = Math::Vector3{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFace::PositiveY:
        outForward = Math::Vector3{0.0F, 1.0F, 0.0F};
        outUp = Math::Vector3{0.0F, 0.0F, -1.0F};
        return true;
    case CubemapFace::NegativeY:
        outForward = Math::Vector3{0.0F, -1.0F, 0.0F};
        outUp = Math::Vector3{0.0F, 0.0F, 1.0F};
        return true;
    case CubemapFace::PositiveZ:
        outForward = Math::Vector3{0.0F, 0.0F, 1.0F};
        outUp = Math::Vector3{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFace::NegativeZ:
        outForward = Math::Vector3{0.0F, 0.0F, -1.0F};
        outUp = Math::Vector3{0.0F, 1.0F, 0.0F};
        return true;
    }
    return false;
}

bool TrySelectCubemapFace(const Math::Vector3& direction, CubemapFace& outFace) noexcept {
    if (!Math::IsFinite(direction)) {
        return false;
    }
    const float absX = std::fabs(direction.x);
    const float absY = std::fabs(direction.y);
    const float absZ = std::fabs(direction.z);
    if (!(absX > 0.0F) && !(absY > 0.0F) && !(absZ > 0.0F)) {
        return false;
    }
    if (absX >= absY && absX >= absZ) {
        outFace = direction.x >= 0.0F ? CubemapFace::PositiveX : CubemapFace::NegativeX;
        return true;
    }
    if (absY >= absZ) {
        outFace = direction.y >= 0.0F ? CubemapFace::PositiveY : CubemapFace::NegativeY;
        return true;
    }
    outFace = direction.z >= 0.0F ? CubemapFace::PositiveZ : CubemapFace::NegativeZ;
    return true;
}

bool TryMakeCubemapFaceCameraRotation(const CubemapFace face, Math::Quaternion& outRotation) noexcept {
    Math::Vector3 forward{};
    Math::Vector3 up{};
    if (!TryGetCubemapFaceBasis(face, forward, up)) {
        return false;
    }
    return Math::TryMakeLookAt(Math::Vector3{-forward.x, -forward.y, -forward.z}, up, outRotation);
}

bool TryMakeCubemapFaceUv(const Math::Vector3& direction, const CubemapFace face,
                             Math::Vector2& outUv) noexcept {
    outUv = Math::Vector2{};
    if (!Math::IsFinite(direction)) {
        return false;
    }
    Math::Quaternion rotation{};
    if (!TryMakeCubemapFaceCameraRotation(face, rotation)) {
        return false;
    }
    const Math::Vector3 right = Math::RotateVector(rotation, Math::Vector3{1.0F, 0.0F, 0.0F});
    const Math::Vector3 up = Math::RotateVector(rotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 look = Math::RotateVector(rotation, Math::Vector3{0.0F, 0.0F, -1.0F});
    if (!Math::IsFinite(right) || !Math::IsFinite(up) || !Math::IsFinite(look)) {
        return false;
    }
    const float denom = Math::Dot(direction, look);
    if (!(denom > kCubemapDenomEpsilon)) {
        return false;
    }
    const float u = Math::Dot(direction, right) / denom * 0.5F + 0.5F;
    const float v = Math::Dot(direction, up) / denom * 0.5F + 0.5F;
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return false;
    }
    outUv = Math::Vector2{u, v};
    return true;
}

bool TryProjectCubemapDirection(const Math::Vector3& direction, CubemapFace& outFace,
                                   Math::Vector2& outUv) noexcept {
    if (!TrySelectCubemapFace(direction, outFace)) {
        return false;
    }
    return TryMakeCubemapFaceUv(direction, outFace, outUv);
}

bool TryMakeReflectionProbe3DFrame(const ReflectionProbe3DComponent& value,
                                      const Math::Vector3& worldPosition,
                                      const Math::Quaternion& worldRotation,
                                      ReflectionProbe3DFrame& out) noexcept {
    out = ReflectionProbe3DFrame{};
    if (!IsReflectionProbe3DObjectComponentValid(value) || !IsFinite3DObjectVector(worldPosition) ||
        !IsFinite3DObjectQuaternion(worldRotation)) {
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
    out.halfExtents = Math::Vector3{value.size.x * 0.5F, value.size.y * 0.5F, value.size.z * 0.5F};
    out.visibilityLayers = value.visibilityLayers;
    out.captureResolution = GetReflectionProbeResolutionPixels(value.resolution);
    out.captureGeneration = value.captureGeneration;
    out.capturedOnce = value.capturedOnce;
    out.enabled = value.enabled;
    return true;
}

std::optional<Math::Vector3> ReflectionProbe3DWorldToLocal(const ReflectionProbe3DFrame& frame,
                                                                const Math::Vector3& worldPoint) noexcept {
    if (!Math::IsFinite(worldPoint) || !Math::IsFinite(frame.worldPosition) || !Math::IsFinite(frame.axisX) ||
        !Math::IsFinite(frame.axisY) || !Math::IsFinite(frame.axisZ)) {
        return std::nullopt;
    }
    const Math::Vector3 delta = worldPoint - frame.worldPosition;
    const Math::Vector3 local{Math::Dot(delta, frame.axisX), Math::Dot(delta, frame.axisY),
                                 Math::Dot(delta, frame.axisZ)};
    if (!Math::IsFinite(local)) {
        return std::nullopt;
    }
    return local;
}

float SampleReflectionProbe3DInfluence(const ReflectionProbe3DFrame& frame,
                                          const Math::Vector3& worldPoint) noexcept {
    const std::optional<Math::Vector3> local = ReflectionProbe3DWorldToLocal(frame, worldPoint);
    if (!local.has_value()) {
        return 0.0F;
    }
    return ResolveReflectionProbe3DInfluenceWeight(*local, frame.halfExtents);
}

bool TryBoxProjectReflection(const ReflectionProbe3DFrame& frame, const Math::Vector3& worldOrigin,
                                const Math::Vector3& worldDirection, Math::Vector3& outWorldPoint) noexcept {
    outWorldPoint = Math::Vector3{};
    const std::optional<Math::Vector3> localOrigin = ReflectionProbe3DWorldToLocal(frame, worldOrigin);
    if (!localOrigin.has_value() || !Math::IsFinite(worldDirection)) {
        return false;
    }
    if (!(frame.halfExtents.x > 0.0F) || !(frame.halfExtents.y > 0.0F) || !(frame.halfExtents.z > 0.0F)) {
        return false;
    }
    const Math::Vector3 localDirection{Math::Dot(worldDirection, frame.axisX),
                                          Math::Dot(worldDirection, frame.axisY),
                                          Math::Dot(worldDirection, frame.axisZ)};
    if (!Math::IsFinite(localDirection)) {
        return false;
    }
    const float origin[3] = {localOrigin->x, localOrigin->y, localOrigin->z};
    const float direction[3] = {localDirection.x, localDirection.y, localDirection.z};
    const float half[3] = {frame.halfExtents.x, frame.halfExtents.y, frame.halfExtents.z};
    float tFar = std::numeric_limits<float>::infinity();
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (std::fabs(direction[axis]) < kBoxRayEpsilon) {
            if (origin[axis] < -half[axis] || origin[axis] > half[axis]) {
                return false;
            }
            continue;
        }
        const float inv = 1.0F / direction[axis];
        const float t1 = (-half[axis] - origin[axis]) * inv;
        const float t2 = (half[axis] - origin[axis]) * inv;
        tFar = std::fmin(tFar, std::fmax(t1, t2));
    }
    if (!std::isfinite(tFar) || !(tFar > kBoxRayEpsilon)) {
        return false;
    }
    const Math::Vector3 localHit{localOrigin->x + localDirection.x * tFar, localOrigin->y + localDirection.y * tFar,
                                    localOrigin->z + localDirection.z * tFar};
    if (!Math::IsFinite(localHit)) {
        return false;
    }
    outWorldPoint = frame.worldPosition + frame.axisX * localHit.x + frame.axisY * localHit.y + frame.axisZ * localHit.z;
    return Math::IsFinite(outWorldPoint);
}

std::size_t CollectReflectionProbe3DFrames(IEntityManager& entityManager, const Math::Vector3& viewPosition,
                                              const std::span<ReflectionProbe3DFrame> out) {
    if (out.empty()) {
        return 0;
    }
    struct Ranked final {
        ReflectionProbe3DFrame frame;
        float influence = 0.0F;
        float distanceSquared = 0.0F;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
    std::vector<Ranked> ranked;
    entityManager.ForEach<WorldTransformComponent, ReflectionProbe3DComponent>(
        [&entityManager, &viewPosition, &ranked](const Entity entity, const WorldTransformComponent& world,
                                                 const ReflectionProbe3DComponent& probe) {
            if (world.dirty || !probe.enabled || !IsReflectionProbe3DObjectComponentValid(probe)) {
                return;
            }
            if (entityManager.HasComponent<VisibilityComponent>(entity) &&
                !entityManager.GetComponent<VisibilityComponent>(entity).visibleInHierarchy) {
                return;
            }
            ReflectionProbe3DFrame frame{};
            if (!TryMakeReflectionProbe3DFrame(probe, world.worldPosition, world.worldRotation, frame)) {
                return;
            }
            frame.entity = entity;
            frame.influenceWeight = SampleReflectionProbe3DInfluence(frame, viewPosition);
            const Math::Vector3 offset = world.worldPosition - viewPosition;
            ranked.push_back(Ranked{frame, frame.influenceWeight, Math::LengthSquared(offset), entity.index,
                                       entity.generation});
        });
    std::sort(ranked.begin(), ranked.end(), [](const Ranked& lhs, const Ranked& rhs) {
        if (lhs.influence != rhs.influence) {
            return lhs.influence > rhs.influence;
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

void CollectReflectionProbe3DGizmos(IEntityManager& entityManager, std::vector<ReflectionProbe3DGizmo>& out) {
    entityManager.ForEach<WorldTransformComponent, ReflectionProbe3DComponent>(
        [&entityManager, &out](const Entity entity, const WorldTransformComponent& world,
                               const ReflectionProbe3DComponent& probe) {
            if (entityManager.HasComponent<EditorInternalEntityComponent>(entity) || world.dirty) {
                return;
            }
            ReflectionProbe3DFrame frame{};
            if (!TryMakeReflectionProbe3DFrame(probe, world.worldPosition, world.worldRotation, frame) ||
                !frame.enabled) {
                return;
            }
            ReflectionProbe3DGizmo gizmo;
            gizmo.origin = frame.worldPosition;
            gizmo.axisX = frame.axisX;
            gizmo.axisY = frame.axisY;
            gizmo.axisZ = frame.axisZ;
            gizmo.halfExtents = frame.halfExtents;
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
