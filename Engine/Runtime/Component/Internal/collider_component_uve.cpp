// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/collider_component_uve.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace UVE::Scene {

[[nodiscard]] Math::Vector3UVE GetColliderLocalHalfExtentsUVE(
    const ColliderComponentUVE& collider) noexcept {
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Sphere:
        return {collider.radius, collider.radius, collider.radius};
    case ColliderShapeTypeUVE::Capsule:
        return {collider.radius, collider.height * 0.5F, collider.radius};
    case ColliderShapeTypeUVE::Box:
    default:
        return collider.halfExtents;
    }
}

[[nodiscard]] bool IsColliderComponentValidUVE(const ColliderComponentUVE& collider) noexcept {
    const bool validCommon = std::isfinite(collider.halfExtents.x) && std::isfinite(collider.halfExtents.y) &&
                              std::isfinite(collider.halfExtents.z) && collider.halfExtents.x > 0.0F &&
                              collider.halfExtents.y > 0.0F && collider.halfExtents.z > 0.0F &&
                              collider.collisionLayer != 0U && std::isfinite(collider.friction) &&
                              collider.friction >= 0.0F && collider.friction <= 1.0F &&
                              std::isfinite(collider.restitution) && collider.restitution >= 0.0F &&
                              collider.restitution <= 1.0F && std::isfinite(collider.density) &&
                              collider.density > 0.0F;
    if (!validCommon) {
        return false;
    }
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Box:
        return true;
    case ColliderShapeTypeUVE::Sphere:
        return std::isfinite(collider.radius) && collider.radius > 0.0F;
    case ColliderShapeTypeUVE::Capsule:
        return std::isfinite(collider.radius) && collider.radius > 0.0F &&
               std::isfinite(collider.height) && collider.height >= 2.0F * collider.radius;
    default:
        return false;
    }
}

} // namespace UVE::Scene
