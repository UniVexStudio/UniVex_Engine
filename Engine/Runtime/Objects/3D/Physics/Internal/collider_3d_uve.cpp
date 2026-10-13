// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/collider_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::QuaternionUVE UsableRotationUVE(const Math::QuaternionUVE& rotation) noexcept {
    Math::QuaternionUVE normalized{};
    if (Math::TryNormalizeUVE(rotation, normalized)) {
        return normalized;
    }
    return {};
}

[[nodiscard]] bool IsRuntimeColliderValidUVE(const ColliderComponentUVE& collider) noexcept {
    ColliderComponentUVE sanitized = collider;
    sanitized.friction = std::isfinite(sanitized.friction) ? std::clamp(sanitized.friction, 0.0F, 1.0F) : 0.0F;
    sanitized.restitution =
        std::isfinite(sanitized.restitution) ? std::clamp(sanitized.restitution, 0.0F, 1.0F) : 0.0F;
    return IsColliderComponentValidUVE(sanitized);
}

[[nodiscard]] Math::Vector3UVE ToLocalUVE(const Math::Vector3UVE& center,
                                          const Math::QuaternionUVE& rotation,
                                          const Math::Vector3UVE& point) noexcept {
    Math::QuaternionUVE inverse{};
    if (!Math::TryInverseUVE(rotation, inverse)) {
        inverse = {};
    }
    return Math::RotateVectorUVE(inverse, point - center);
}

[[nodiscard]] Math::Vector3UVE ClosestPointOnSegmentUVE(const Math::Vector3UVE& start,
                                                        const Math::Vector3UVE& end,
                                                        const Math::Vector3UVE& point) noexcept {
    const Math::Vector3UVE delta = end - start;
    const float lengthSquared = Math::LengthSquaredUVE(delta);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return start;
    }
    const float t = std::clamp(Math::DotUVE(point - start, delta) / lengthSquared, 0.0F, 1.0F);
    return start + delta * t;
}

[[nodiscard]] bool CapsuleSegmentUVE(const ColliderComponentUVE& collider,
                                     const Math::Vector3UVE& center,
                                     const Math::QuaternionUVE& rotation, Math::Vector3UVE& start,
                                     Math::Vector3UVE& end) noexcept {
    const float half = collider.height * 0.5F - collider.radius;
    if (!std::isfinite(half) || half < 0.0F) {
        return false;
    }
    const Math::Vector3UVE offset = Math::RotateVectorUVE(rotation, Math::Vector3UVE{0.0F, half, 0.0F});
    start = center - offset;
    end = center + offset;
    return Math::IsFiniteUVE(start) && Math::IsFiniteUVE(end);
}

[[nodiscard]] std::optional<Math::RayHitUVE> RaySphereUVE(const Math::RayUVE& ray,
                                                          const Math::Vector3UVE& center, const float radius,
                                                          const float maxDistance) noexcept {
    if (!Math::IsFiniteUVE(ray.origin) || !Math::IsFiniteUVE(ray.direction) || !Math::IsFiniteUVE(center) ||
        !std::isfinite(radius) || radius <= 0.0F || !std::isfinite(maxDistance) || maxDistance < 0.0F) {
        return std::nullopt;
    }
    const double ox = static_cast<double>(ray.origin.x) - static_cast<double>(center.x);
    const double oy = static_cast<double>(ray.origin.y) - static_cast<double>(center.y);
    const double oz = static_cast<double>(ray.origin.z) - static_cast<double>(center.z);
    const double dx = static_cast<double>(ray.direction.x);
    const double dy = static_cast<double>(ray.direction.y);
    const double dz = static_cast<double>(ray.direction.z);
    const double a = dx * dx + dy * dy + dz * dz;
    const double c = ox * ox + oy * oy + oz * oz - static_cast<double>(radius) * static_cast<double>(radius);
    if (!std::isfinite(a) || a <= 0.0) {
        return std::nullopt;
    }
    if (c <= 0.0) {
        return Math::RayHitUVE{0.0F, {}};
    }
    const double b = 2.0 * (ox * dx + oy * dy + oz * dz);
    const double disc = b * b - 4.0 * a * c;
    if (!std::isfinite(disc) || disc < 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(disc);
    double t = (-b - root) / (2.0 * a);
    if (t < 0.0) {
        t = (-b + root) / (2.0 * a);
    }
    if (!std::isfinite(t) || t < 0.0 || t > static_cast<double>(maxDistance)) {
        return std::nullopt;
    }
    const Math::Vector3UVE hit = ray.origin + ray.direction * static_cast<float>(t);
    const Math::Vector3UVE outward = hit - center;
    const float lengthSquared = Math::LengthSquaredUVE(outward);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return Math::RayHitUVE{static_cast<float>(t), {}};
    }
    return Math::RayHitUVE{static_cast<float>(t), Math::NormalizeUVE(outward)};
}

[[nodiscard]] std::optional<Math::RayHitUVE> RayCapsuleUVE(const Math::RayUVE& ray,
                                                           const Math::Vector3UVE& start,
                                                           const Math::Vector3UVE& end, const float radius,
                                                           const float maxDistance) noexcept {
    const Math::Vector3UVE axis = end - start;
    const float axisLengthSquared = Math::LengthSquaredUVE(axis);
    if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 0.0F) {
        return RaySphereUVE(ray, start, radius, maxDistance);
    }
    const float axisLength = std::sqrt(axisLengthSquared);
    const Math::Vector3UVE unit = axis * (1.0F / axisLength);
    if (!Math::IsFiniteUVE(unit)) {
        return std::nullopt;
    }

    std::optional<Math::RayHitUVE> best = RaySphereUVE(ray, start, radius, maxDistance);
    const auto consider = [&best](const std::optional<Math::RayHitUVE>& hit) {
        if (!hit.has_value()) {
            return;
        }
        if (!best.has_value() || hit->distance < best->distance) {
            best = hit;
        }
    };
    consider(RaySphereUVE(ray, end, radius, maxDistance));

    const double ux = static_cast<double>(unit.x);
    const double uy = static_cast<double>(unit.y);
    const double uz = static_cast<double>(unit.z);
    const double px = static_cast<double>(ray.origin.x) - static_cast<double>(start.x);
    const double py = static_cast<double>(ray.origin.y) - static_cast<double>(start.y);
    const double pz = static_cast<double>(ray.origin.z) - static_cast<double>(start.z);
    const double dx = static_cast<double>(ray.direction.x);
    const double dy = static_cast<double>(ray.direction.y);
    const double dz = static_cast<double>(ray.direction.z);
    const double dAxial = dx * ux + dy * uy + dz * uz;
    const double pAxial = px * ux + py * uy + pz * uz;
    const double vx = dx - dAxial * ux;
    const double vy = dy - dAxial * uy;
    const double vz = dz - dAxial * uz;
    const double wx = px - pAxial * ux;
    const double wy = py - pAxial * uy;
    const double wz = pz - pAxial * uz;
    const double a = vx * vx + vy * vy + vz * vz;
    const double c = wx * wx + wy * wy + wz * wz -
                     static_cast<double>(radius) * static_cast<double>(radius);
    if (std::isfinite(a) && a > 0.0) {
        const double b = 2.0 * (vx * wx + vy * wy + vz * wz);
        const double disc = b * b - 4.0 * a * c;
        if (std::isfinite(disc) && disc >= 0.0) {
            const double root = std::sqrt(disc);
            const double candidates[2]{(-b - root) / (2.0 * a), (-b + root) / (2.0 * a)};
            for (const double t : candidates) {
                if (!std::isfinite(t) || t < 0.0 || t > static_cast<double>(maxDistance)) {
                    continue;
                }
                const double axial = pAxial + t * dAxial;
                if (axial < 0.0 || axial > static_cast<double>(axisLength)) {
                    continue;
                }
                const Math::Vector3UVE hit = ray.origin + ray.direction * static_cast<float>(t);
                const Math::Vector3UVE onAxis = start + unit * static_cast<float>(axial);
                const Math::Vector3UVE outward = hit - onAxis;
                const float lengthSquared = Math::LengthSquaredUVE(outward);
                if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
                    continue;
                }
                consider(Math::RayHitUVE{static_cast<float>(t), Math::NormalizeUVE(outward)});
            }
        }
    }
    return best;
}

} // namespace

bool IsCollider3DObjectDefinitionValidUVE(const Collider3DObjectDefinitionUVE& value) noexcept {
    return IsColliderComponentValidUVE(value.collider);
}

void ApplyCollider3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                        const Collider3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity, Collider3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<ColliderComponentUVE>(entity, value.collider);
}

ColliderComponentUVE Collider3DUVE::MakeBoxUVE(const Math::Vector3UVE& halfExtents) noexcept {
    ColliderComponentUVE collider{};
    collider.shapeType = ColliderShapeTypeUVE::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

ColliderComponentUVE Collider3DUVE::MakeSphereUVE(const float radius) noexcept {
    ColliderComponentUVE collider{};
    collider.shapeType = ColliderShapeTypeUVE::Sphere;
    collider.radius = radius;
    collider.halfExtents = {radius, radius, radius};
    return collider;
}

ColliderComponentUVE Collider3DUVE::MakeCapsuleUVE(const float radius, const float height) noexcept {
    ColliderComponentUVE collider{};
    collider.shapeType = ColliderShapeTypeUVE::Capsule;
    collider.radius = radius;
    collider.height = height;
    collider.halfExtents = {radius, height * 0.5F, radius};
    return collider;
}

bool Collider3DUVE::IsParticipatingUVE(const ColliderComponentUVE& collider) noexcept {
    return IsRuntimeColliderValidUVE(collider) && !collider.disabled;
}

Math::AabbUVE Collider3DUVE::GetLocalAabbUVE(const ColliderComponentUVE& collider) noexcept {
    return Math::AabbUVE::FromCenterExtentsUVE({}, GetColliderLocalHalfExtentsUVE(collider));
}

Math::AabbUVE Collider3DUVE::GetWorldAabbUVE(const ColliderComponentUVE& collider,
                                            const Math::Vector3UVE& center,
                                            const Math::QuaternionUVE& rotation) noexcept {
    const Math::QuaternionUVE usable = UsableRotationUVE(rotation);
    const Math::Vector3UVE local = GetColliderLocalHalfExtentsUVE(collider);
    Math::Vector3UVE worldHalf = local;
    if (collider.shapeType == ColliderShapeTypeUVE::Capsule) {
        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        if (CapsuleSegmentUVE(collider, center, usable, start, end)) {
            const Math::Vector3UVE offset = end - center;
            worldHalf = {collider.radius + std::fabs(offset.x), collider.radius + std::fabs(offset.y),
                         collider.radius + std::fabs(offset.z)};
        }
    } else if (collider.shapeType == ColliderShapeTypeUVE::Box) {
        const Math::Vector3UVE x = Math::RotateVectorUVE(usable, Math::Vector3UVE{local.x, 0.0F, 0.0F});
        const Math::Vector3UVE y = Math::RotateVectorUVE(usable, Math::Vector3UVE{0.0F, local.y, 0.0F});
        const Math::Vector3UVE z = Math::RotateVectorUVE(usable, Math::Vector3UVE{0.0F, 0.0F, local.z});
        worldHalf = {std::fabs(x.x) + std::fabs(y.x) + std::fabs(z.x),
                     std::fabs(x.y) + std::fabs(y.y) + std::fabs(z.y),
                     std::fabs(x.z) + std::fabs(y.z) + std::fabs(z.z)};
    }
    return Math::AabbUVE::FromCenterExtentsUVE(center, worldHalf);
}

float Collider3DUVE::GetVolumeUVE(const ColliderComponentUVE& collider) noexcept {
    if (!IsRuntimeColliderValidUVE(collider)) {
        return 0.0F;
    }
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Sphere: {
        const float r = collider.radius;
        return (4.0F / 3.0F) * Math::kPiUVE * r * r * r;
    }
    case ColliderShapeTypeUVE::Capsule: {
        const float r = collider.radius;
        const float cylinder = collider.height - 2.0F * r;
        return Math::kPiUVE * r * r * cylinder +
               (4.0F / 3.0F) * Math::kPiUVE * r * r * r;
    }
    case ColliderShapeTypeUVE::Box:
    default:
        return 8.0F * collider.halfExtents.x * collider.halfExtents.y * collider.halfExtents.z;
    }
}

bool Collider3DUVE::ContainsPointUVE(const ColliderComponentUVE& collider, const Math::Vector3UVE& center,
                                     const Math::QuaternionUVE& rotation,
                                     const Math::Vector3UVE& point) noexcept {
    if (!IsParticipatingUVE(collider) || !Math::IsFiniteUVE(center) || !Math::IsFiniteUVE(point)) {
        return false;
    }
    const Math::QuaternionUVE usable = UsableRotationUVE(rotation);
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Sphere: {
        const float distanceSquared = Math::LengthSquaredUVE(point - center);
        return std::isfinite(distanceSquared) &&
               distanceSquared <= collider.radius * collider.radius;
    }
    case ColliderShapeTypeUVE::Capsule: {
        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        if (!CapsuleSegmentUVE(collider, center, usable, start, end)) {
            return false;
        }
        const Math::Vector3UVE closest = ClosestPointOnSegmentUVE(start, end, point);
        const float distanceSquared = Math::LengthSquaredUVE(point - closest);
        return std::isfinite(distanceSquared) &&
               distanceSquared <= collider.radius * collider.radius;
    }
    case ColliderShapeTypeUVE::Box:
    default: {
        const Math::Vector3UVE local = ToLocalUVE(center, usable, point);
        return std::fabs(local.x) <= collider.halfExtents.x &&
               std::fabs(local.y) <= collider.halfExtents.y &&
               std::fabs(local.z) <= collider.halfExtents.z;
    }
    }
}

std::optional<Math::Vector3UVE> Collider3DUVE::ClosestPointUVE(const ColliderComponentUVE& collider,
                                                              const Math::Vector3UVE& center,
                                                              const Math::QuaternionUVE& rotation,
                                                              const Math::Vector3UVE& point) noexcept {
    if (!IsParticipatingUVE(collider) || !Math::IsFiniteUVE(center) || !Math::IsFiniteUVE(point)) {
        return std::nullopt;
    }
    const Math::QuaternionUVE usable = UsableRotationUVE(rotation);
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Sphere: {
        const Math::Vector3UVE offset = point - center;
        const float lengthSquared = Math::LengthSquaredUVE(offset);
        if (!std::isfinite(lengthSquared)) {
            return std::nullopt;
        }
        if (lengthSquared <= collider.radius * collider.radius) {
            return point;
        }
        if (lengthSquared <= 0.0F) {
            return center;
        }
        return center + Math::NormalizeUVE(offset) * collider.radius;
    }
    case ColliderShapeTypeUVE::Capsule: {
        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        if (!CapsuleSegmentUVE(collider, center, usable, start, end)) {
            return std::nullopt;
        }
        const Math::Vector3UVE onAxis = ClosestPointOnSegmentUVE(start, end, point);
        const Math::Vector3UVE offset = point - onAxis;
        const float lengthSquared = Math::LengthSquaredUVE(offset);
        if (!std::isfinite(lengthSquared)) {
            return std::nullopt;
        }
        if (lengthSquared <= collider.radius * collider.radius) {
            return point;
        }
        if (lengthSquared <= 0.0F) {
            return onAxis;
        }
        return onAxis + Math::NormalizeUVE(offset) * collider.radius;
    }
    case ColliderShapeTypeUVE::Box:
    default: {
        const Math::Vector3UVE local = ToLocalUVE(center, usable, point);
        const Math::Vector3UVE clamped{
            std::clamp(local.x, -collider.halfExtents.x, collider.halfExtents.x),
            std::clamp(local.y, -collider.halfExtents.y, collider.halfExtents.y),
            std::clamp(local.z, -collider.halfExtents.z, collider.halfExtents.z),
        };
        return center + Math::RotateVectorUVE(usable, clamped);
    }
    }
}

std::optional<Math::RayHitUVE> Collider3DUVE::IntersectRayUVE(const ColliderComponentUVE& collider,
                                                             const Math::Vector3UVE& center,
                                                             const Math::QuaternionUVE& rotation,
                                                             const Math::RayUVE& ray,
                                                             const float maxDistance) noexcept {
    if (!IsParticipatingUVE(collider) || !Math::IsFiniteUVE(center) || !Math::IsFiniteUVE(ray.origin) ||
        !Math::IsFiniteUVE(ray.direction) || !std::isfinite(maxDistance) || maxDistance < 0.0F) {
        return std::nullopt;
    }
    const Math::QuaternionUVE usable = UsableRotationUVE(rotation);
    switch (collider.shapeType) {
    case ColliderShapeTypeUVE::Sphere:
        return RaySphereUVE(ray, center, collider.radius, maxDistance);
    case ColliderShapeTypeUVE::Capsule: {
        Math::Vector3UVE start{};
        Math::Vector3UVE end{};
        if (!CapsuleSegmentUVE(collider, center, usable, start, end)) {
            return std::nullopt;
        }
        if (ContainsPointUVE(collider, center, usable, ray.origin)) {
            return Math::RayHitUVE{0.0F, {}};
        }
        return RayCapsuleUVE(ray, start, end, collider.radius, maxDistance);
    }
    case ColliderShapeTypeUVE::Box:
    default: {
        Math::QuaternionUVE inverse{};
        if (!Math::TryInverseUVE(usable, inverse)) {
            inverse = {};
        }
        const Math::RayUVE localRay{ToLocalUVE(center, usable, ray.origin),
                                    Math::RotateVectorUVE(inverse, ray.direction)};
        const std::optional<Math::RayHitUVE> localHit = Math::IntersectRayUVE(
            localRay, Math::AabbUVE::FromCenterExtentsUVE({}, collider.halfExtents), maxDistance);
        if (!localHit.has_value()) {
            return std::nullopt;
        }
        return Math::RayHitUVE{localHit->distance, Math::RotateVectorUVE(usable, localHit->normal)};
    }
    }
}

} // namespace UVE::Scene
