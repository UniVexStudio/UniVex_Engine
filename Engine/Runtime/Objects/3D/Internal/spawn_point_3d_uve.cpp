// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/spawn_point_3d_uve.h"

#include <cmath>

namespace UVE::Scene {

namespace {

constexpr float kMinimumParentScaleAxisMagnitudeUVE = 1.0e-6F;

[[nodiscard]] bool IsFiniteVector3UVE(const Math::Vector3UVE& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float ReciprocalScaleAxisUVE(const float axis) noexcept {
    return 1.0F / axis; // caller guards the magnitude; kept as one place the divide happens
}

} // namespace

bool IsSpawnPoint3DNodeComponentValidUVE(const SpawnPoint3DNodeComponentUVE& value) noexcept {
    return IsBounded3DNodeStringUVE(value.spawnTag, false) && IsFinite3DNodeVectorUVE(value.localPosition) &&
           IsFinite3DNodeQuaternionUVE(value.localRotation);
}

std::optional<EntityUVE> ResolveSpawnPoint3DSelectionUVE(
    const std::span<const SpawnPoint3DCandidateUVE> candidates) noexcept {
    std::optional<EntityUVE> best;
    for (const SpawnPoint3DCandidateUVE& candidate : candidates) {
        if (candidate.entity == kInvalidEntityUVE) {
            continue;
        }
        if (!best.has_value() || candidate.entity.index < best->index ||
            (candidate.entity.index == best->index && candidate.entity.generation < best->generation)) {
            best = candidate.entity;
        }
    }
    return best;
}

std::optional<SpawnPoint3DPoseUVE> ComposeSpawnPointPoseUVE(
    const Math::Vector3UVE nodePosition, const Math::QuaternionUVE nodeRotation,
    const Math::Vector3UVE localPosition, const Math::QuaternionUVE localRotation) noexcept {
    if (!IsFiniteVector3UVE(nodePosition) || !IsFiniteVector3UVE(localPosition) ||
        !Math::IsFiniteUVE(nodeRotation) || !Math::IsFiniteUVE(localRotation)) {
        return std::nullopt;
    }
    Math::QuaternionUVE nodeRotationNormalized{};
    if (!Math::TryNormalizeUVE(nodeRotation, nodeRotationNormalized)) {
        return std::nullopt;
    }
    SpawnPoint3DPoseUVE pose{};
    pose.position =
        nodePosition + Math::RotateVectorUVE(nodeRotationNormalized, localPosition);
    pose.rotation = Math::MultiplyUVE(nodeRotationNormalized, localRotation);
    Math::QuaternionUVE rotationNormalized{};
    if (!Math::TryNormalizeUVE(pose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    pose.rotation = rotationNormalized;
    return pose;
}

std::optional<SpawnPoint3DPoseUVE> ResolveSpawnPointPlayerLocalUVE(
    const SpawnPoint3DPoseUVE& spawnPose, const Math::Vector3UVE parentPosition,
    const Math::QuaternionUVE parentRotation, const Math::Vector3UVE parentScale) noexcept {
    if (!IsFiniteVector3UVE(spawnPose.position) || !IsFiniteVector3UVE(parentPosition) ||
        !IsFiniteVector3UVE(parentScale) || !Math::IsFiniteUVE(spawnPose.rotation) ||
        !Math::IsFiniteUVE(parentRotation) ||
        std::fabs(parentScale.x) < kMinimumParentScaleAxisMagnitudeUVE ||
        std::fabs(parentScale.y) < kMinimumParentScaleAxisMagnitudeUVE ||
        std::fabs(parentScale.z) < kMinimumParentScaleAxisMagnitudeUVE) {
        return std::nullopt;
    }
    Math::QuaternionUVE parentRotationNormalized{};
    if (!Math::TryNormalizeUVE(parentRotation, parentRotationNormalized)) {
        return std::nullopt;
    }
    Math::QuaternionUVE parentRotationInverse{};
    if (!Math::TryInverseUVE(parentRotationNormalized, parentRotationInverse)) {
        return std::nullopt;
    }
    const Math::Vector3UVE unrotated =
        Math::RotateVectorUVE(parentRotationInverse, spawnPose.position - parentPosition);
    SpawnPoint3DPoseUVE localPose{};
    localPose.position = Math::Vector3UVE{
        unrotated.x * ReciprocalScaleAxisUVE(parentScale.x),
        unrotated.y * ReciprocalScaleAxisUVE(parentScale.y),
        unrotated.z * ReciprocalScaleAxisUVE(parentScale.z),
    };
    localPose.rotation = Math::MultiplyUVE(parentRotationInverse, spawnPose.rotation);
    Math::QuaternionUVE rotationNormalized{};
    if (!IsFiniteVector3UVE(localPose.position) ||
        !Math::TryNormalizeUVE(localPose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    localPose.rotation = rotationNormalized;
    return localPose;
}

} // namespace UVE::Scene
