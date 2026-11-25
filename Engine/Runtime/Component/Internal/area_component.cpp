// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "area_component.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsAreaSpaceOverrideModeValid(const AreaSpaceOverrideMode mode) noexcept {
    return static_cast<std::uint8_t>(mode) <=
           static_cast<std::uint8_t>(AreaSpaceOverrideMode::ReplaceCombine);
}

[[nodiscard]] bool IsAreaComponentValid(const AreaComponent& area) noexcept {
    return std::isfinite(area.halfExtents.x) && std::isfinite(area.halfExtents.y) &&
           std::isfinite(area.halfExtents.z) && area.halfExtents.x > 0.0F &&
           area.halfExtents.y > 0.0F && area.halfExtents.z > 0.0F && area.collisionLayer != 0U &&
           IsAreaSpaceOverrideModeValid(area.gravityOverride) &&
           IsAreaSpaceOverrideModeValid(area.linearDampOverride) &&
           IsAreaSpaceOverrideModeValid(area.angularDampOverride) &&
           std::isfinite(area.gravityDirection.x) && std::isfinite(area.gravityDirection.y) &&
           std::isfinite(area.gravityDirection.z) && std::isfinite(area.gravityMagnitude) &&
           std::isfinite(area.gravityPointOffset.x) && std::isfinite(area.gravityPointOffset.y) &&
           std::isfinite(area.gravityPointOffset.z) && std::isfinite(area.gravityPointUnitDistance) &&
           area.gravityPointUnitDistance >= 0.0F && std::isfinite(area.linearDamp) &&
           area.linearDamp >= 0.0F && std::isfinite(area.angularDamp) && area.angularDamp >= 0.0F;
}

AreaComponent SanitizeAreaComponent(const AreaComponent& area) noexcept {
    AreaComponent out = area;
    if (!Math::IsFinite(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3{0.5F, 0.5F, 0.5F};
    }
    if (out.collisionLayer == 0U) {
        out.collisionLayer = 1U;
    }
    if (!IsAreaSpaceOverrideModeValid(out.gravityOverride)) {
        out.gravityOverride = AreaSpaceOverrideMode::Disabled;
    }
    if (!IsAreaSpaceOverrideModeValid(out.linearDampOverride)) {
        out.linearDampOverride = AreaSpaceOverrideMode::Disabled;
    }
    if (!IsAreaSpaceOverrideModeValid(out.angularDampOverride)) {
        out.angularDampOverride = AreaSpaceOverrideMode::Disabled;
    }
    if (!Math::IsFinite(out.gravityDirection) ||
        Math::LengthSquared(out.gravityDirection) <= 1e-6F) {
        out.gravityDirection = Math::Vector3{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.gravityMagnitude)) {
        out.gravityMagnitude = kDefaultAreaGravityMagnitude;
    }
    if (!Math::IsFinite(out.gravityPointOffset)) {
        out.gravityPointOffset = Math::Vector3{};
    }
    out.gravityPointUnitDistance =
        (std::isfinite(out.gravityPointUnitDistance) && out.gravityPointUnitDistance >= 0.0F)
            ? out.gravityPointUnitDistance
            : 0.0F;
    out.linearDamp =
        (std::isfinite(out.linearDamp) && out.linearDamp >= 0.0F) ? out.linearDamp : 0.1F;
    out.angularDamp =
        (std::isfinite(out.angularDamp) && out.angularDamp >= 0.0F) ? out.angularDamp : 0.1F;
    return out;
}

bool ContainsAreaOverlappingBody(const AreaComponent& area,
                                    const Entity entity) noexcept {
    if (entity == kInvalidEntity) {
        return false;
    }
    const std::size_t count =
        std::min<std::size_t>(area.overlappingBodyCount, kMaximumArea3DOverlaps);
    for (std::size_t i = 0U; i < count; ++i) {
        if (area.overlappingBodies[i] == entity) {
            return true;
        }
    }
    return false;
}

bool ContainsAreaOverlappingArea(const AreaComponent& area,
                                    const Entity entity) noexcept {
    if (entity == kInvalidEntity) {
        return false;
    }
    const std::size_t count =
        std::min<std::size_t>(area.overlappingAreaCount, kMaximumArea3DOverlaps);
    for (std::size_t i = 0U; i < count; ++i) {
        if (area.overlappingAreas[i] == entity) {
            return true;
        }
    }
    return false;
}

Math::Vector3 EvaluateAreaGravityVector(
    const AreaComponent& area, const Math::Vector3& areaWorldPosition,
    const Math::Vector3& bodyWorldPosition) noexcept {
    const AreaComponent safe = SanitizeAreaComponent(area);
    if (safe.gravityMagnitude == 0.0F) {
        return Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (safe.gravityPoint && Math::IsFinite(areaWorldPosition) &&
        Math::IsFinite(bodyWorldPosition)) {
        const Math::Vector3 attractor = areaWorldPosition + safe.gravityPointOffset;
        const Math::Vector3 toCenter = attractor - bodyWorldPosition;
        const float distSq = Math::LengthSquared(toCenter);
        if (distSq <= 1e-6F) {
            return Math::Vector3{0.0F, 0.0F, 0.0F};
        }
        const Math::Vector3 unitDir = Math::Normalize(toCenter);
        if (safe.gravityPointUnitDistance > 0.0F) {
            const float refSq = safe.gravityPointUnitDistance * safe.gravityPointUnitDistance;
            const float scale = refSq / distSq;
            return unitDir * (safe.gravityMagnitude * scale);
        }
        return unitDir * safe.gravityMagnitude;
    }
    const Math::Vector3 unitDir = Math::Normalize(safe.gravityDirection);
    return unitDir * safe.gravityMagnitude;
}

void ClearAreaRuntimeOverlaps(AreaComponent& area) noexcept {
    area.overlappingBodies.fill(kInvalidEntity);
    area.overlappingBodyCount = 0U;
    area.overlappingBodiesTruncated = false;
    area.overlappingAreas.fill(kInvalidEntity);
    area.overlappingAreaCount = 0U;
    area.overlappingAreasTruncated = false;
}

} // namespace UVE::Scene
