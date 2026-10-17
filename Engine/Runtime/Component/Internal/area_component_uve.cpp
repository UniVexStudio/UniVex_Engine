// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/area_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] bool IsAreaSpaceOverrideModeValidUVE(const AreaSpaceOverrideModeUVE mode) noexcept {
    return static_cast<std::uint8_t>(mode) <=
           static_cast<std::uint8_t>(AreaSpaceOverrideModeUVE::ReplaceCombine);
}

[[nodiscard]] bool IsAreaComponentValidUVE(const AreaComponentUVE& area) noexcept {
    return std::isfinite(area.halfExtents.x) && std::isfinite(area.halfExtents.y) &&
           std::isfinite(area.halfExtents.z) && area.halfExtents.x > 0.0F &&
           area.halfExtents.y > 0.0F && area.halfExtents.z > 0.0F && area.collisionLayer != 0U &&
           IsAreaSpaceOverrideModeValidUVE(area.gravityOverride) &&
           IsAreaSpaceOverrideModeValidUVE(area.linearDampOverride) &&
           IsAreaSpaceOverrideModeValidUVE(area.angularDampOverride) &&
           std::isfinite(area.gravityDirection.x) && std::isfinite(area.gravityDirection.y) &&
           std::isfinite(area.gravityDirection.z) && std::isfinite(area.gravityMagnitude) &&
           std::isfinite(area.gravityPointOffset.x) && std::isfinite(area.gravityPointOffset.y) &&
           std::isfinite(area.gravityPointOffset.z) && std::isfinite(area.gravityPointUnitDistance) &&
           area.gravityPointUnitDistance >= 0.0F && std::isfinite(area.linearDamp) &&
           area.linearDamp >= 0.0F && std::isfinite(area.angularDamp) && area.angularDamp >= 0.0F;
}

AreaComponentUVE SanitizeAreaComponentUVE(const AreaComponentUVE& area) noexcept {
    AreaComponentUVE out = area;
    if (!Math::IsFiniteUVE(out.halfExtents) || out.halfExtents.x <= 0.0F ||
        out.halfExtents.y <= 0.0F || out.halfExtents.z <= 0.0F) {
        out.halfExtents = Math::Vector3UVE{0.5F, 0.5F, 0.5F};
    }
    if (out.collisionLayer == 0U) {
        out.collisionLayer = 1U;
    }
    if (!IsAreaSpaceOverrideModeValidUVE(out.gravityOverride)) {
        out.gravityOverride = AreaSpaceOverrideModeUVE::Disabled;
    }
    if (!IsAreaSpaceOverrideModeValidUVE(out.linearDampOverride)) {
        out.linearDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    }
    if (!IsAreaSpaceOverrideModeValidUVE(out.angularDampOverride)) {
        out.angularDampOverride = AreaSpaceOverrideModeUVE::Disabled;
    }
    if (!Math::IsFiniteUVE(out.gravityDirection) ||
        Math::LengthSquaredUVE(out.gravityDirection) <= 1e-6F) {
        out.gravityDirection = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.gravityMagnitude)) {
        out.gravityMagnitude = kDefaultAreaGravityMagnitudeUVE;
    }
    if (!Math::IsFiniteUVE(out.gravityPointOffset)) {
        out.gravityPointOffset = Math::Vector3UVE{};
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

bool ContainsAreaOverlappingBodyUVE(const AreaComponentUVE& area,
                                    const EntityUVE entity) noexcept {
    if (entity == kInvalidEntityUVE) {
        return false;
    }
    const std::size_t count =
        std::min<std::size_t>(area.overlappingBodyCount, kMaximumArea3DOverlapsUVE);
    for (std::size_t i = 0U; i < count; ++i) {
        if (area.overlappingBodies[i] == entity) {
            return true;
        }
    }
    return false;
}

bool ContainsAreaOverlappingAreaUVE(const AreaComponentUVE& area,
                                    const EntityUVE entity) noexcept {
    if (entity == kInvalidEntityUVE) {
        return false;
    }
    const std::size_t count =
        std::min<std::size_t>(area.overlappingAreaCount, kMaximumArea3DOverlapsUVE);
    for (std::size_t i = 0U; i < count; ++i) {
        if (area.overlappingAreas[i] == entity) {
            return true;
        }
    }
    return false;
}

Math::Vector3UVE EvaluateAreaGravityVectorUVE(
    const AreaComponentUVE& area, const Math::Vector3UVE& areaWorldPosition,
    const Math::Vector3UVE& bodyWorldPosition) noexcept {
    const AreaComponentUVE safe = SanitizeAreaComponentUVE(area);
    if (safe.gravityMagnitude == 0.0F) {
        return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (safe.gravityPoint && Math::IsFiniteUVE(areaWorldPosition) &&
        Math::IsFiniteUVE(bodyWorldPosition)) {
        const Math::Vector3UVE attractor = areaWorldPosition + safe.gravityPointOffset;
        const Math::Vector3UVE toCenter = attractor - bodyWorldPosition;
        const float distSq = Math::LengthSquaredUVE(toCenter);
        if (distSq <= 1e-6F) {
            return Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
        const Math::Vector3UVE unitDir = Math::NormalizeUVE(toCenter);
        if (safe.gravityPointUnitDistance > 0.0F) {
            const float refSq = safe.gravityPointUnitDistance * safe.gravityPointUnitDistance;
            const float scale = refSq / distSq;
            return unitDir * (safe.gravityMagnitude * scale);
        }
        return unitDir * safe.gravityMagnitude;
    }
    const Math::Vector3UVE unitDir = Math::NormalizeUVE(safe.gravityDirection);
    return unitDir * safe.gravityMagnitude;
}

void ClearAreaRuntimeOverlapsUVE(AreaComponentUVE& area) noexcept {
    area.overlappingBodies.fill(kInvalidEntityUVE);
    area.overlappingBodyCount = 0U;
    area.overlappingBodiesTruncated = false;
    area.overlappingAreas.fill(kInvalidEntityUVE);
    area.overlappingAreaCount = 0U;
    area.overlappingAreasTruncated = false;
}

} // namespace UVE::Scene
