// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/reflection_probe_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "uve/component/editor_internal_entity_component_uve.h"
#include "uve/component/visibility_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {
namespace {

constexpr float kCubemapDenomEpsilonUVE = 1.0e-8F;
constexpr float kBoxRayEpsilonUVE = 1.0e-8F;

} // namespace

bool IsReflectionProbe3DObjectComponentValidUVE(const ReflectionProbe3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.size) && value.size.x > 0.0F && value.size.y > 0.0F &&
           value.size.z > 0.0F && value.updateMode <= ReflectionProbeUpdateModeUVE::OnDemand;
}

void ApplyReflectionProbe3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                               const ReflectionProbe3DObjectDefinitionUVE& value) {
    ApplyObject3DRecipeUVE(entityManager, entity, ReflectionProbe3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) &&
        !entityManager.HasComponentUVE<ReflectionProbe3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<ReflectionProbe3DComponentUVE>(entity, value.probe);
    }
}

float ResolveReflectionProbe3DInfluenceWeightUVE(
    const Math::Vector3UVE& probeLocalPoint,
    const Math::Vector3UVE& probeHalfExtents) noexcept {
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

ReflectionProbe3DCaptureActionUVE ResolveReflectionProbe3DCaptureActionUVE(
    const ReflectionProbe3DCaptureFrameUVE& frame) noexcept {
    if (!frame.enabled || frame.updateMode > ReflectionProbeUpdateModeUVE::OnDemand ||
        (frame.hasCameraViewer && !std::isfinite(frame.cameraInfluenceWeight))) {
        return ReflectionProbe3DCaptureActionUVE::None;
    }
    switch (frame.updateMode) {
    case ReflectionProbeUpdateModeUVE::EveryFrame:
        if (frame.hasCameraViewer && frame.cameraInfluenceWeight > 0.0F) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    case ReflectionProbeUpdateModeUVE::OnDemand:
        if (frame.updateRequested) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    case ReflectionProbeUpdateModeUVE::Once:
        if (!frame.capturedOnce) {
            return ReflectionProbe3DCaptureActionUVE::Capture;
        }
        break;
    }
    return ReflectionProbe3DCaptureActionUVE::None;
}

bool TryGetCubemapFaceBasisUVE(const CubemapFaceUVE face, Math::Vector3UVE& outForward,
                               Math::Vector3UVE& outUp) noexcept {
    switch (face) {
    case CubemapFaceUVE::PositiveX:
        outForward = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        outUp = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFaceUVE::NegativeX:
        outForward = Math::Vector3UVE{-1.0F, 0.0F, 0.0F};
        outUp = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFaceUVE::PositiveY:
        outForward = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        outUp = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
        return true;
    case CubemapFaceUVE::NegativeY:
        outForward = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
        outUp = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        return true;
    case CubemapFaceUVE::PositiveZ:
        outForward = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        outUp = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        return true;
    case CubemapFaceUVE::NegativeZ:
        outForward = Math::Vector3UVE{0.0F, 0.0F, -1.0F};
        outUp = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
        return true;
    }
    return false;
}

bool TrySelectCubemapFaceUVE(const Math::Vector3UVE& direction, CubemapFaceUVE& outFace) noexcept {
    if (!Math::IsFiniteUVE(direction)) {
        return false;
    }
    const float absX = std::fabs(direction.x);
    const float absY = std::fabs(direction.y);
    const float absZ = std::fabs(direction.z);
    if (!(absX > 0.0F) && !(absY > 0.0F) && !(absZ > 0.0F)) {
        return false;
    }
    if (absX >= absY && absX >= absZ) {
        outFace = direction.x >= 0.0F ? CubemapFaceUVE::PositiveX : CubemapFaceUVE::NegativeX;
        return true;
    }
    if (absY >= absZ) {
        outFace = direction.y >= 0.0F ? CubemapFaceUVE::PositiveY : CubemapFaceUVE::NegativeY;
        return true;
    }
    outFace = direction.z >= 0.0F ? CubemapFaceUVE::PositiveZ : CubemapFaceUVE::NegativeZ;
    return true;
}

bool TryMakeCubemapFaceCameraRotationUVE(const CubemapFaceUVE face, Math::QuaternionUVE& outRotation) noexcept {
    Math::Vector3UVE forward{};
    Math::Vector3UVE up{};
    if (!TryGetCubemapFaceBasisUVE(face, forward, up)) {
        return false;
    }
    return Math::TryMakeLookAtUVE(Math::Vector3UVE{-forward.x, -forward.y, -forward.z}, up, outRotation);
}

bool TryMakeCubemapFaceUvUVE(const Math::Vector3UVE& direction, const CubemapFaceUVE face,
                             Math::Vector2UVE& outUv) noexcept {
    outUv = Math::Vector2UVE{};
    if (!Math::IsFiniteUVE(direction)) {
        return false;
    }
    Math::QuaternionUVE rotation{};
    if (!TryMakeCubemapFaceCameraRotationUVE(face, rotation)) {
        return false;
    }
    const Math::Vector3UVE right = Math::RotateVectorUVE(rotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    const Math::Vector3UVE up = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE look = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, 0.0F, -1.0F});
    if (!Math::IsFiniteUVE(right) || !Math::IsFiniteUVE(up) || !Math::IsFiniteUVE(look)) {
        return false;
    }
    const float denom = Math::DotUVE(direction, look);
    if (!(denom > kCubemapDenomEpsilonUVE)) {
        return false;
    }
    const float u = Math::DotUVE(direction, right) / denom * 0.5F + 0.5F;
    const float v = Math::DotUVE(direction, up) / denom * 0.5F + 0.5F;
    if (!std::isfinite(u) || !std::isfinite(v)) {
        return false;
    }
    outUv = Math::Vector2UVE{u, v};
    return true;
}

bool TryProjectCubemapDirectionUVE(const Math::Vector3UVE& direction, CubemapFaceUVE& outFace,
                                   Math::Vector2UVE& outUv) noexcept {
    if (!TrySelectCubemapFaceUVE(direction, outFace)) {
        return false;
    }
    return TryMakeCubemapFaceUvUVE(direction, outFace, outUv);
}

bool TryMakeReflectionProbe3DFrameUVE(const ReflectionProbe3DComponentUVE& value,
                                      const Math::Vector3UVE& worldPosition,
                                      const Math::QuaternionUVE& worldRotation,
                                      ReflectionProbe3DFrameUVE& out) noexcept {
    out = ReflectionProbe3DFrameUVE{};
    if (!IsReflectionProbe3DObjectComponentValidUVE(value) || !IsFinite3DObjectVectorUVE(worldPosition) ||
        !IsFinite3DObjectQuaternionUVE(worldRotation)) {
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
    out.halfExtents = Math::Vector3UVE{value.size.x * 0.5F, value.size.y * 0.5F, value.size.z * 0.5F};
    out.captureGeneration = value.captureGeneration;
    out.capturedOnce = value.capturedOnce;
    out.enabled = value.enabled;
    return true;
}

std::optional<Math::Vector3UVE> ReflectionProbe3DWorldToLocalUVE(const ReflectionProbe3DFrameUVE& frame,
                                                                const Math::Vector3UVE& worldPoint) noexcept {
    if (!Math::IsFiniteUVE(worldPoint) || !Math::IsFiniteUVE(frame.worldPosition) || !Math::IsFiniteUVE(frame.axisX) ||
        !Math::IsFiniteUVE(frame.axisY) || !Math::IsFiniteUVE(frame.axisZ)) {
        return std::nullopt;
    }
    const Math::Vector3UVE delta = worldPoint - frame.worldPosition;
    const Math::Vector3UVE local{Math::DotUVE(delta, frame.axisX), Math::DotUVE(delta, frame.axisY),
                                 Math::DotUVE(delta, frame.axisZ)};
    if (!Math::IsFiniteUVE(local)) {
        return std::nullopt;
    }
    return local;
}

float SampleReflectionProbe3DInfluenceUVE(const ReflectionProbe3DFrameUVE& frame,
                                          const Math::Vector3UVE& worldPoint) noexcept {
    const std::optional<Math::Vector3UVE> local = ReflectionProbe3DWorldToLocalUVE(frame, worldPoint);
    if (!local.has_value()) {
        return 0.0F;
    }
    return ResolveReflectionProbe3DInfluenceWeightUVE(*local, frame.halfExtents);
}

bool TryBoxProjectReflectionUVE(const ReflectionProbe3DFrameUVE& frame, const Math::Vector3UVE& worldOrigin,
                                const Math::Vector3UVE& worldDirection, Math::Vector3UVE& outWorldPoint) noexcept {
    outWorldPoint = Math::Vector3UVE{};
    const std::optional<Math::Vector3UVE> localOrigin = ReflectionProbe3DWorldToLocalUVE(frame, worldOrigin);
    if (!localOrigin.has_value() || !Math::IsFiniteUVE(worldDirection)) {
        return false;
    }
    if (!(frame.halfExtents.x > 0.0F) || !(frame.halfExtents.y > 0.0F) || !(frame.halfExtents.z > 0.0F)) {
        return false;
    }
    const Math::Vector3UVE localDirection{Math::DotUVE(worldDirection, frame.axisX),
                                          Math::DotUVE(worldDirection, frame.axisY),
                                          Math::DotUVE(worldDirection, frame.axisZ)};
    if (!Math::IsFiniteUVE(localDirection)) {
        return false;
    }
    const float origin[3] = {localOrigin->x, localOrigin->y, localOrigin->z};
    const float direction[3] = {localDirection.x, localDirection.y, localDirection.z};
    const float half[3] = {frame.halfExtents.x, frame.halfExtents.y, frame.halfExtents.z};
    float tFar = std::numeric_limits<float>::infinity();
    for (std::size_t axis = 0; axis < 3U; ++axis) {
        if (std::fabs(direction[axis]) < kBoxRayEpsilonUVE) {
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
    if (!std::isfinite(tFar) || !(tFar > kBoxRayEpsilonUVE)) {
        return false;
    }
    const Math::Vector3UVE localHit{localOrigin->x + localDirection.x * tFar, localOrigin->y + localDirection.y * tFar,
                                    localOrigin->z + localDirection.z * tFar};
    if (!Math::IsFiniteUVE(localHit)) {
        return false;
    }
    outWorldPoint = frame.worldPosition + frame.axisX * localHit.x + frame.axisY * localHit.y + frame.axisZ * localHit.z;
    return Math::IsFiniteUVE(outWorldPoint);
}

std::size_t CollectReflectionProbe3DFramesUVE(IEntityManagerUVE& entityManager, const Math::Vector3UVE& viewPosition,
                                              const std::span<ReflectionProbe3DFrameUVE> out) {
    if (out.empty()) {
        return 0;
    }
    struct RankedUVE final {
        ReflectionProbe3DFrameUVE frame;
        float influence = 0.0F;
        float distanceSquared = 0.0F;
        std::uint32_t index = 0;
        std::uint32_t generation = 0;
    };
    std::vector<RankedUVE> ranked;
    entityManager.ForEachUVE<WorldTransformComponentUVE, ReflectionProbe3DComponentUVE>(
        [&entityManager, &viewPosition, &ranked](const EntityUVE entity, const WorldTransformComponentUVE& world,
                                                 const ReflectionProbe3DComponentUVE& probe) {
            if (world.dirty || !probe.enabled || !IsReflectionProbe3DObjectComponentValidUVE(probe)) {
                return;
            }
            if (entityManager.HasComponentUVE<VisibilityComponentUVE>(entity) &&
                !entityManager.GetComponentUVE<VisibilityComponentUVE>(entity).visibleInHierarchy) {
                return;
            }
            ReflectionProbe3DFrameUVE frame{};
            if (!TryMakeReflectionProbe3DFrameUVE(probe, world.worldPosition, world.worldRotation, frame)) {
                return;
            }
            frame.entity = entity;
            frame.influenceWeight = SampleReflectionProbe3DInfluenceUVE(frame, viewPosition);
            const Math::Vector3UVE offset = world.worldPosition - viewPosition;
            ranked.push_back(RankedUVE{frame, frame.influenceWeight, Math::LengthSquaredUVE(offset), entity.index,
                                       entity.generation});
        });
    std::sort(ranked.begin(), ranked.end(), [](const RankedUVE& lhs, const RankedUVE& rhs) {
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

void CollectReflectionProbe3DGizmosUVE(IEntityManagerUVE& entityManager, std::vector<ReflectionProbe3DGizmoUVE>& out) {
    entityManager.ForEachUVE<WorldTransformComponentUVE, ReflectionProbe3DComponentUVE>(
        [&entityManager, &out](const EntityUVE entity, const WorldTransformComponentUVE& world,
                               const ReflectionProbe3DComponentUVE& probe) {
            if (entityManager.HasComponentUVE<EditorInternalEntityComponentUVE>(entity) || world.dirty) {
                return;
            }
            ReflectionProbe3DFrameUVE frame{};
            if (!TryMakeReflectionProbe3DFrameUVE(probe, world.worldPosition, world.worldRotation, frame) ||
                !frame.enabled) {
                return;
            }
            ReflectionProbe3DGizmoUVE gizmo;
            gizmo.origin = frame.worldPosition;
            gizmo.axisX = frame.axisX;
            gizmo.axisY = frame.axisY;
            gizmo.axisZ = frame.axisZ;
            gizmo.halfExtents = frame.halfExtents;
            out.push_back(gizmo);
        });
}

} // namespace UVE::Scene
