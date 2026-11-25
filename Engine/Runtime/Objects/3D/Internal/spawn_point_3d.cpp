// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spawn_point_3d.h"

#include <cmath>

namespace UVE::Scene {

namespace {

constexpr float kMinimumParentScaleAxisMagnitude = 1.0e-6F;

[[nodiscard]] bool IsFiniteVector3(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] float ReciprocalScaleAxis(const float axis) noexcept {
    return 1.0F / axis; // caller guards the magnitude; kept as one place the divide happens
}

} // namespace

bool IsSpawnPoint3DObjectComponentValid(const SpawnPoint3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.spawnTag, false) && IsFinite3DObjectVector(value.localPosition) &&
           IsFinite3DObjectQuaternion(value.localRotation);
}

bool SortsBeforeSpawnPoint(const Entity lhs, const Entity rhs) noexcept {
    return lhs.index < rhs.index || (lhs.index == rhs.index && lhs.generation < rhs.generation);
}

std::optional<Entity> ResolveSpawnPoint3DSelection(
    const std::span<const SpawnPoint3DCandidate> candidates) noexcept {
    std::optional<Entity> best;
    for (const SpawnPoint3DCandidate& candidate : candidates) {
        if (candidate.entity == kInvalidEntity) {
            continue;
        }
        if (!best.has_value() || SortsBeforeSpawnPoint(candidate.entity, *best)) {
            best = candidate.entity;
        }
    }
    return best;
}

std::optional<SpawnPoint3DPose> ComposeSpawnPointPose(
    const Math::Vector3 objectPosition, const Math::Quaternion objectRotation,
    const Math::Vector3 localPosition, const Math::Quaternion localRotation) noexcept {
    if (!IsFiniteVector3(objectPosition) || !IsFiniteVector3(localPosition) ||
        !Math::IsFinite(objectRotation) || !Math::IsFinite(localRotation)) {
        return std::nullopt;
    }
    Math::Quaternion objectRotationNormalized{};
    if (!Math::TryNormalize(objectRotation, objectRotationNormalized)) {
        return std::nullopt;
    }
    SpawnPoint3DPose pose{};
    pose.position =
        objectPosition + Math::RotateVector(objectRotationNormalized, localPosition);
    pose.rotation = Math::Multiply(objectRotationNormalized, localRotation);
    Math::Quaternion rotationNormalized{};
    if (!Math::TryNormalize(pose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    pose.rotation = rotationNormalized;
    return pose;
}

std::optional<SpawnPoint3DPose> ResolveSpawnPointPlayerLocal(
    const SpawnPoint3DPose& spawnPose, const Math::Vector3 parentPosition,
    const Math::Quaternion parentRotation, const Math::Vector3 parentScale) noexcept {
    if (!IsFiniteVector3(spawnPose.position) || !IsFiniteVector3(parentPosition) ||
        !IsFiniteVector3(parentScale) || !Math::IsFinite(spawnPose.rotation) ||
        !Math::IsFinite(parentRotation) ||
        std::fabs(parentScale.x) < kMinimumParentScaleAxisMagnitude ||
        std::fabs(parentScale.y) < kMinimumParentScaleAxisMagnitude ||
        std::fabs(parentScale.z) < kMinimumParentScaleAxisMagnitude) {
        return std::nullopt;
    }
    Math::Quaternion parentRotationNormalized{};
    if (!Math::TryNormalize(parentRotation, parentRotationNormalized)) {
        return std::nullopt;
    }
    Math::Quaternion parentRotationInverse{};
    if (!Math::TryInverse(parentRotationNormalized, parentRotationInverse)) {
        return std::nullopt;
    }
    const Math::Vector3 unrotated =
        Math::RotateVector(parentRotationInverse, spawnPose.position - parentPosition);
    SpawnPoint3DPose localPose{};
    localPose.position = Math::Vector3{
        unrotated.x * ReciprocalScaleAxis(parentScale.x),
        unrotated.y * ReciprocalScaleAxis(parentScale.y),
        unrotated.z * ReciprocalScaleAxis(parentScale.z),
    };
    localPose.rotation = Math::Multiply(parentRotationInverse, spawnPose.rotation);
    Math::Quaternion rotationNormalized{};
    if (!IsFiniteVector3(localPose.position) ||
        !Math::TryNormalize(localPose.rotation, rotationNormalized)) {
        return std::nullopt;
    }
    localPose.rotation = rotationNormalized;
    return localPose;
}

} // namespace UVE::Scene
