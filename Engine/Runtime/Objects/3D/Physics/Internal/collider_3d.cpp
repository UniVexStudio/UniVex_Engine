// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_3d.h"

#include <algorithm>
#include <cmath>
#include <numbers>

#include "i_entity_manager.h"
#include "scalar.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

[[nodiscard]] Math::Quaternion UsableRotation(const Math::Quaternion& rotation) noexcept {
    Math::Quaternion normalized{};
    if (Math::TryNormalize(rotation, normalized)) {
        return normalized;
    }
    return {};
}

[[nodiscard]] bool IsRuntimeColliderValid(const ColliderComponent& collider) noexcept {
    ColliderComponent sanitized = collider;
    sanitized.friction = std::isfinite(sanitized.friction) ? std::clamp(sanitized.friction, 0.0F, 1.0F) : 0.0F;
    sanitized.restitution =
        std::isfinite(sanitized.restitution) ? std::clamp(sanitized.restitution, 0.0F, 1.0F) : 0.0F;
    return IsColliderComponentValid(sanitized);
}

[[nodiscard]] Math::Vector3 ToLocal(const Math::Vector3& center,
                                          const Math::Quaternion& rotation,
                                          const Math::Vector3& point) noexcept {
    Math::Quaternion inverse{};
    if (!Math::TryInverse(rotation, inverse)) {
        inverse = {};
    }
    return Math::RotateVector(inverse, point - center);
}

[[nodiscard]] Math::Vector3 ClosestPointOnSegment(const Math::Vector3& start,
                                                        const Math::Vector3& end,
                                                        const Math::Vector3& point) noexcept {
    const Math::Vector3 delta = end - start;
    const float lengthSquared = Math::LengthSquared(delta);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return start;
    }
    const float t = std::clamp(Math::Dot(point - start, delta) / lengthSquared, 0.0F, 1.0F);
    return start + delta * t;
}

[[nodiscard]] bool CapsuleSegment(const ColliderComponent& collider,
                                     const Math::Vector3& center,
                                     const Math::Quaternion& rotation, Math::Vector3& start,
                                     Math::Vector3& end) noexcept {
    const float half = collider.height * 0.5F - collider.radius;
    if (!std::isfinite(half) || half < 0.0F) {
        return false;
    }
    const Math::Vector3 offset = Math::RotateVector(rotation, Math::Vector3{0.0F, half, 0.0F});
    start = center - offset;
    end = center + offset;
    return Math::IsFinite(start) && Math::IsFinite(end);
}

[[nodiscard]] std::optional<Math::RayHit> RaySphere(const Math::Ray& ray,
                                                          const Math::Vector3& center, const float radius,
                                                          const float maxDistance) noexcept {
    if (!Math::IsFinite(ray.origin) || !Math::IsFinite(ray.direction) || !Math::IsFinite(center) ||
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
        return Math::RayHit{0.0F, {}};
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
    const Math::Vector3 hit = ray.origin + ray.direction * static_cast<float>(t);
    const Math::Vector3 outward = hit - center;
    const float lengthSquared = Math::LengthSquared(outward);
    if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
        return Math::RayHit{static_cast<float>(t), {}};
    }
    return Math::RayHit{static_cast<float>(t), Math::Normalize(outward)};
}

[[nodiscard]] std::optional<Math::RayHit> RayCapsule(const Math::Ray& ray,
                                                           const Math::Vector3& start,
                                                           const Math::Vector3& end, const float radius,
                                                           const float maxDistance) noexcept {
    const Math::Vector3 axis = end - start;
    const float axisLengthSquared = Math::LengthSquared(axis);
    if (!std::isfinite(axisLengthSquared) || axisLengthSquared <= 0.0F) {
        return RaySphere(ray, start, radius, maxDistance);
    }
    const float axisLength = std::sqrt(axisLengthSquared);
    const Math::Vector3 unit = axis * (1.0F / axisLength);
    if (!Math::IsFinite(unit)) {
        return std::nullopt;
    }

    std::optional<Math::RayHit> best = RaySphere(ray, start, radius, maxDistance);
    const auto consider = [&best](const std::optional<Math::RayHit>& hit) {
        if (!hit.has_value()) {
            return;
        }
        if (!best.has_value() || hit->distance < best->distance) {
            best = hit;
        }
    };
    consider(RaySphere(ray, end, radius, maxDistance));

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
                const Math::Vector3 hit = ray.origin + ray.direction * static_cast<float>(t);
                const Math::Vector3 onAxis = start + unit * static_cast<float>(axial);
                const Math::Vector3 outward = hit - onAxis;
                const float lengthSquared = Math::LengthSquared(outward);
                if (!std::isfinite(lengthSquared) || lengthSquared <= 0.0F) {
                    continue;
                }
                consider(Math::RayHit{static_cast<float>(t), Math::Normalize(outward)});
            }
        }
    }
    return best;
}

} // namespace

bool IsCollider3DObjectDefinitionValid(const Collider3DObjectDefinition& value) noexcept {
    return IsColliderComponentValid(value.collider);
}

void ApplyCollider3DObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                        const Collider3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity, Collider3DObjectDefinition::defaultName);
    entityManager.AddComponent<ColliderComponent>(entity, value.collider);
}

ColliderComponent Collider3D::MakeBox(const Math::Vector3& halfExtents) noexcept {
    ColliderComponent collider{};
    collider.shapeType = ColliderShapeType::Box;
    collider.halfExtents = halfExtents;
    return collider;
}

ColliderComponent Collider3D::MakeSphere(const float radius) noexcept {
    ColliderComponent collider{};
    collider.shapeType = ColliderShapeType::Sphere;
    collider.radius = radius;
    collider.halfExtents = {radius, radius, radius};
    return collider;
}

ColliderComponent Collider3D::MakeCapsule(const float radius, const float height) noexcept {
    ColliderComponent collider{};
    collider.shapeType = ColliderShapeType::Capsule;
    collider.radius = radius;
    collider.height = height;
    collider.halfExtents = {radius, height * 0.5F, radius};
    return collider;
}

bool Collider3D::IsParticipating(const ColliderComponent& collider) noexcept {
    return IsRuntimeColliderValid(collider) && !collider.disabled;
}

Math::Aabb Collider3D::GetLocalAabb(const ColliderComponent& collider) noexcept {
    return Math::Aabb::FromCenterExtents({}, GetColliderLocalHalfExtents(collider));
}

Math::Aabb Collider3D::GetWorldAabb(const ColliderComponent& collider,
                                            const Math::Vector3& center,
                                            const Math::Quaternion& rotation,
                                            const Math::Vector3& worldScale) noexcept {
    const Math::Quaternion usable = UsableRotation(rotation);
    // Scale first, then rotate the already-scaled extents. The other order scales a rotated
    // bounding box, which is neither what the shape is nor what the mesh looks like.
    const ColliderComponent scaled = ScaleCollider(collider, worldScale);
    const Math::Vector3 local = GetColliderLocalHalfExtents(scaled);
    Math::Vector3 worldHalf = local;
    if (scaled.shapeType == ColliderShapeType::Capsule) {
        Math::Vector3 start{};
        Math::Vector3 end{};
        if (CapsuleSegment(scaled, center, usable, start, end)) {
            const Math::Vector3 offset = end - center;
            worldHalf = {scaled.radius + std::fabs(offset.x), scaled.radius + std::fabs(offset.y),
                         scaled.radius + std::fabs(offset.z)};
        }
    } else if (scaled.shapeType == ColliderShapeType::Box) {
        const Math::Vector3 x = Math::RotateVector(usable, Math::Vector3{local.x, 0.0F, 0.0F});
        const Math::Vector3 y = Math::RotateVector(usable, Math::Vector3{0.0F, local.y, 0.0F});
        const Math::Vector3 z = Math::RotateVector(usable, Math::Vector3{0.0F, 0.0F, local.z});
        worldHalf = {std::fabs(x.x) + std::fabs(y.x) + std::fabs(z.x),
                     std::fabs(x.y) + std::fabs(y.y) + std::fabs(z.y),
                     std::fabs(x.z) + std::fabs(y.z) + std::fabs(z.z)};
    }
    return Math::Aabb::FromCenterExtents(center, worldHalf);
}

float Collider3D::GetVolume(const ColliderComponent& collider) noexcept {
    if (!IsRuntimeColliderValid(collider)) {
        return 0.0F;
    }
    switch (collider.shapeType) {
    case ColliderShapeType::Sphere: {
        const float r = collider.radius;
        return (4.0F / 3.0F) * Math::kPi * r * r * r;
    }
    case ColliderShapeType::Capsule: {
        const float r = collider.radius;
        const float cylinder = collider.height - 2.0F * r;
        return Math::kPi * r * r * cylinder +
               (4.0F / 3.0F) * Math::kPi * r * r * r;
    }
    case ColliderShapeType::Box:
    default:
        return 8.0F * collider.halfExtents.x * collider.halfExtents.y * collider.halfExtents.z;
    }
}

bool Collider3D::ContainsPoint(const ColliderComponent& collider, const Math::Vector3& center,
                                     const Math::Quaternion& rotation,
                                     const Math::Vector3& point) noexcept {
    if (!IsParticipating(collider) || !Math::IsFinite(center) || !Math::IsFinite(point)) {
        return false;
    }
    const Math::Quaternion usable = UsableRotation(rotation);
    switch (collider.shapeType) {
    case ColliderShapeType::Sphere: {
        const float distanceSquared = Math::LengthSquared(point - center);
        return std::isfinite(distanceSquared) &&
               distanceSquared <= collider.radius * collider.radius;
    }
    case ColliderShapeType::Capsule: {
        Math::Vector3 start{};
        Math::Vector3 end{};
        if (!CapsuleSegment(collider, center, usable, start, end)) {
            return false;
        }
        const Math::Vector3 closest = ClosestPointOnSegment(start, end, point);
        const float distanceSquared = Math::LengthSquared(point - closest);
        return std::isfinite(distanceSquared) &&
               distanceSquared <= collider.radius * collider.radius;
    }
    case ColliderShapeType::Box:
    default: {
        const Math::Vector3 local = ToLocal(center, usable, point);
        return std::fabs(local.x) <= collider.halfExtents.x &&
               std::fabs(local.y) <= collider.halfExtents.y &&
               std::fabs(local.z) <= collider.halfExtents.z;
    }
    }
}

std::optional<Math::Vector3> Collider3D::ClosestPoint(const ColliderComponent& collider,
                                                              const Math::Vector3& center,
                                                              const Math::Quaternion& rotation,
                                                              const Math::Vector3& point) noexcept {
    if (!IsParticipating(collider) || !Math::IsFinite(center) || !Math::IsFinite(point)) {
        return std::nullopt;
    }
    const Math::Quaternion usable = UsableRotation(rotation);
    switch (collider.shapeType) {
    case ColliderShapeType::Sphere: {
        const Math::Vector3 offset = point - center;
        const float lengthSquared = Math::LengthSquared(offset);
        if (!std::isfinite(lengthSquared)) {
            return std::nullopt;
        }
        if (lengthSquared <= collider.radius * collider.radius) {
            return point;
        }
        if (lengthSquared <= 0.0F) {
            return center;
        }
        return center + Math::Normalize(offset) * collider.radius;
    }
    case ColliderShapeType::Capsule: {
        Math::Vector3 start{};
        Math::Vector3 end{};
        if (!CapsuleSegment(collider, center, usable, start, end)) {
            return std::nullopt;
        }
        const Math::Vector3 onAxis = ClosestPointOnSegment(start, end, point);
        const Math::Vector3 offset = point - onAxis;
        const float lengthSquared = Math::LengthSquared(offset);
        if (!std::isfinite(lengthSquared)) {
            return std::nullopt;
        }
        if (lengthSquared <= collider.radius * collider.radius) {
            return point;
        }
        if (lengthSquared <= 0.0F) {
            return onAxis;
        }
        return onAxis + Math::Normalize(offset) * collider.radius;
    }
    case ColliderShapeType::Box:
    default: {
        const Math::Vector3 local = ToLocal(center, usable, point);
        const Math::Vector3 clamped{
            std::clamp(local.x, -collider.halfExtents.x, collider.halfExtents.x),
            std::clamp(local.y, -collider.halfExtents.y, collider.halfExtents.y),
            std::clamp(local.z, -collider.halfExtents.z, collider.halfExtents.z),
        };
        return center + Math::RotateVector(usable, clamped);
    }
    }
}

std::optional<Math::RayHit> Collider3D::IntersectRay(const ColliderComponent& collider,
                                                             const Math::Vector3& center,
                                                             const Math::Quaternion& rotation,
                                                             const Math::Ray& ray,
                                                             const float maxDistance) noexcept {
    if (!IsParticipating(collider) || !Math::IsFinite(center) || !Math::IsFinite(ray.origin) ||
        !Math::IsFinite(ray.direction) || !std::isfinite(maxDistance) || maxDistance < 0.0F) {
        return std::nullopt;
    }
    const Math::Quaternion usable = UsableRotation(rotation);
    switch (collider.shapeType) {
    case ColliderShapeType::Sphere:
        return RaySphere(ray, center, collider.radius, maxDistance);
    case ColliderShapeType::Capsule: {
        Math::Vector3 start{};
        Math::Vector3 end{};
        if (!CapsuleSegment(collider, center, usable, start, end)) {
            return std::nullopt;
        }
        if (ContainsPoint(collider, center, usable, ray.origin)) {
            return Math::RayHit{0.0F, {}};
        }
        return RayCapsule(ray, start, end, collider.radius, maxDistance);
    }
    case ColliderShapeType::Box:
    default: {
        Math::Quaternion inverse{};
        if (!Math::TryInverse(usable, inverse)) {
            inverse = {};
        }
        const Math::Ray localRay{ToLocal(center, usable, ray.origin),
                                    Math::RotateVector(inverse, ray.direction)};
        const std::optional<Math::RayHit> localHit = Math::IntersectRay(
            localRay, Math::Aabb::FromCenterExtents({}, collider.halfExtents), maxDistance);
        if (!localHit.has_value()) {
            return std::nullopt;
        }
        return Math::RayHit{localHit->distance, Math::RotateVector(usable, localHit->normal)};
    }
    }
}

} // namespace UVE::Scene
