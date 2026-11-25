// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ray_cast_3d.h"

#include <cmath>

namespace UVE::Scene {

bool RayCast3D::IsCasting(const RayCast3DComponent& rayCast) noexcept {
    return rayCast.enabled;
}

std::size_t RayCast3D::ExclusionCount(const RayCast3DComponent& rayCast) noexcept {
    std::size_t count = 0U;
    while (count < rayCast.exclusions.size() && rayCast.exclusions[count] != kInvalidEntity) {
        ++count;
    }
    return count;
}

std::span<const Entity> RayCast3D::ExclusionSpan(const RayCast3DComponent& rayCast) noexcept {
    return std::span<const Entity>(rayCast.exclusions.data(), ExclusionCount(rayCast));
}

bool RayCast3D::AcceptsTarget(const Entity rayEntity, const RayCast3DComponent& rayCast,
                                    const Entity obstacle) noexcept {
    if (obstacle == kInvalidEntity || obstacle == rayEntity) {
        return false;
    }
    const std::size_t exclusionCount = ExclusionCount(rayCast);
    for (std::size_t index = 0U; index < exclusionCount; ++index) {
        if (rayCast.exclusions[index] == obstacle) {
            return false;
        }
    }
    return true;
}

Math::Vector3 RayCast3D::ResolveWorldDirection(const Math::Vector3& localDirection,
                                                         const Math::Quaternion& worldRotation) noexcept {
    if (!IsFinite3DObjectVector(localDirection)) {
        return {};
    }
    Math::Quaternion rotation{};
    if (!Math::TryNormalize(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVector(rotation, localDirection);
}

void RayCast3D::ClearResult(RayCast3DComponent& rayCast) noexcept {
    rayCast.hit = false;
    rayCast.hitPosition = {};
    rayCast.hitNormal = {};
    rayCast.hitEntity = kInvalidEntity;
}

void RayCast3D::RecordHit(RayCast3DComponent& rayCast, const Entity hitEntity,
                                const Math::Vector3& hitPosition, const Math::Vector3& hitNormal) noexcept {
    rayCast.hit = true;
    rayCast.hitEntity = hitEntity;
    rayCast.hitPosition = hitPosition;
    rayCast.hitNormal = hitNormal;
}

std::size_t CountRayCast3DExclusions(const RayCast3DComponent& value) noexcept {
    return RayCast3D::ExclusionCount(value);
}

bool IsRayCast3DObjectComponentValid(const RayCast3DComponent& value) noexcept {
    const float directionLengthSquared = Math::LengthSquared(value.direction);
    if (!IsFinite3DObjectVector(value.direction) || !std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 1.0e-8F || !std::isfinite(value.length) || value.length <= 0.0F) {
        return false;
    }

    const std::size_t exclusionCount = RayCast3D::ExclusionCount(value);
    for (std::size_t index = 0U; index < exclusionCount; ++index) {
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.exclusions[previous] == value.exclusions[index]) {
                return false;
            }
        }
    }
    for (std::size_t index = exclusionCount; index < value.exclusions.size(); ++index) {
        if (value.exclusions[index] != kInvalidEntity) {
            return false;
        }
    }

    return !value.hit || (value.hitEntity != kInvalidEntity && IsFinite3DObjectVector(value.hitPosition) &&
                           IsFinite3DObjectVector(value.hitNormal));
}

} // namespace UVE::Scene
