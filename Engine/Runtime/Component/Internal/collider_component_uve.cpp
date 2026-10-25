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
    case ColliderShapeTypeUVE::Cylinder:
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
                              collider.density > 0.0F;    if (!validCommon) {
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
    case ColliderShapeTypeUVE::Cylinder:
        return std::isfinite(collider.radius) && collider.radius > 0.0F &&
               std::isfinite(collider.height) && collider.height > 0.0F;
    default:
        return false;
    }
}

float MinimumCapsuleHeightUVE(const float radius) noexcept {
    return 2.0F * radius;
}

ColliderComponentUVE FitColliderShapeUVE(const ColliderComponentUVE& collider) noexcept {
    if (collider.shapeType != ColliderShapeTypeUVE::Capsule) {
        return collider;
    }
    const float radius = collider.radius;
    if (!std::isfinite(radius) || radius <= 0.0F) {
        return collider;
    }
    // A barrel counts only if it is strictly positive. Equality is the degenerate case the fit
    // exists for: caps of one radius meeting at a point is a sphere, and a shape that renders as a
    // ball while the Inspector reads "Capsule" is the bug this repairs.
    const float halfHeight = collider.height * 0.5F;
    if (std::isfinite(collider.height) && halfHeight - radius > 0.0F) {
        return collider;
    }
    ColliderComponentUVE fitted = collider;
    // Barrel as long as the capsule is wide: at the field default of radius 0.5 that is height 2,
    // the proportion a capsule is drawn with everywhere, so a fresh Capsule looks like one.
    fitted.height = 4.0F * radius;
    fitted.halfExtents = {radius, fitted.height * 0.5F, radius};
    return fitted;
}

} // namespace UVE::Scene
