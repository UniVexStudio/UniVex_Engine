// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/ray_cast_3d_uve.h"

#include <cmath>

namespace UVE::Scene {

bool RayCast3DUVE::IsCastingUVE(const RayCast3DComponentUVE& rayCast) noexcept {
    return rayCast.enabled;
}

std::size_t RayCast3DUVE::ExclusionCountUVE(const RayCast3DComponentUVE& rayCast) noexcept {
    std::size_t count = 0U;
    while (count < rayCast.exclusions.size() && rayCast.exclusions[count] != kInvalidEntityUVE) {
        ++count;
    }
    return count;
}

std::span<const EntityUVE> RayCast3DUVE::ExclusionSpanUVE(const RayCast3DComponentUVE& rayCast) noexcept {
    return std::span<const EntityUVE>(rayCast.exclusions.data(), ExclusionCountUVE(rayCast));
}

bool RayCast3DUVE::AcceptsTargetUVE(const EntityUVE rayEntity, const RayCast3DComponentUVE& rayCast,
                                    const EntityUVE obstacle) noexcept {
    if (obstacle == kInvalidEntityUVE || obstacle == rayEntity) {
        return false;
    }
    const std::size_t exclusionCount = ExclusionCountUVE(rayCast);
    for (std::size_t index = 0U; index < exclusionCount; ++index) {
        if (rayCast.exclusions[index] == obstacle) {
            return false;
        }
    }
    return true;
}

Math::Vector3UVE RayCast3DUVE::ResolveWorldDirectionUVE(const Math::Vector3UVE& localDirection,
                                                         const Math::QuaternionUVE& worldRotation) noexcept {
    if (!IsFinite3DObjectVectorUVE(localDirection)) {
        return {};
    }
    Math::QuaternionUVE rotation{};
    if (!Math::TryNormalizeUVE(worldRotation, rotation)) {
        rotation = {};
    }
    return Math::RotateVectorUVE(rotation, localDirection);
}

void RayCast3DUVE::ClearResultUVE(RayCast3DComponentUVE& rayCast) noexcept {
    rayCast.hit = false;
    rayCast.hitPosition = {};
    rayCast.hitNormal = {};
    rayCast.hitEntity = kInvalidEntityUVE;
}

void RayCast3DUVE::RecordHitUVE(RayCast3DComponentUVE& rayCast, const EntityUVE hitEntity,
                                const Math::Vector3UVE& hitPosition, const Math::Vector3UVE& hitNormal) noexcept {
    rayCast.hit = true;
    rayCast.hitEntity = hitEntity;
    rayCast.hitPosition = hitPosition;
    rayCast.hitNormal = hitNormal;
}

std::size_t CountRayCast3DExclusionsUVE(const RayCast3DComponentUVE& value) noexcept {
    return RayCast3DUVE::ExclusionCountUVE(value);
}

bool IsRayCast3DObjectComponentValidUVE(const RayCast3DComponentUVE& value) noexcept {
    const float directionLengthSquared = Math::LengthSquaredUVE(value.direction);
    if (!IsFinite3DObjectVectorUVE(value.direction) || !std::isfinite(directionLengthSquared) ||
        directionLengthSquared <= 1.0e-8F || !std::isfinite(value.length) || value.length <= 0.0F) {
        return false;
    }

    const std::size_t exclusionCount = RayCast3DUVE::ExclusionCountUVE(value);
    for (std::size_t index = 0U; index < exclusionCount; ++index) {
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.exclusions[previous] == value.exclusions[index]) {
                return false;
            }
        }
    }
    for (std::size_t index = exclusionCount; index < value.exclusions.size(); ++index) {
        if (value.exclusions[index] != kInvalidEntityUVE) {
            return false;
        }
    }

    return !value.hit || (value.hitEntity != kInvalidEntityUVE && IsFinite3DObjectVectorUVE(value.hitPosition) &&
                           IsFinite3DObjectVectorUVE(value.hitNormal));
}

} // namespace UVE::Scene
