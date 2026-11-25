// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "aabb.h"
#include "point_transform_batch.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace UVE::Math {

Aabb Aabb::Transform(const Matrix4x4& matrix) const noexcept {
    const std::array<Vector3, 8> corners = {
        Vector3{min.x, min.y, min.z}, Vector3{max.x, min.y, min.z}, Vector3{min.x, max.y, min.z},
        Vector3{max.x, max.y, min.z}, Vector3{min.x, min.y, max.z}, Vector3{max.x, min.y, max.z},
        Vector3{min.x, max.y, max.z}, Vector3{max.x, max.y, max.z},
    };

    std::array<Vector3, 8> transformedCorners{};
    if (!TryTransformPoints(matrix, corners, transformedCorners)) {
        // Matching disjoint arrays satisfy the current batch contract; preserve the scalar
        // result even if a future kernel introduces additional validation requirements.
        for (std::size_t index = 0U; index < corners.size(); ++index) {
            transformedCorners[index] = TransformPoint(matrix, corners[index]);
        }
    }
    Vector3 newMin = transformedCorners[0];
    Vector3 newMax = newMin;
    for (std::size_t index = 1; index < corners.size(); ++index) {
        const Vector3 transformed = transformedCorners[index];
        newMin.x = std::min(newMin.x, transformed.x);
        newMin.y = std::min(newMin.y, transformed.y);
        newMin.z = std::min(newMin.z, transformed.z);
        newMax.x = std::max(newMax.x, transformed.x);
        newMax.y = std::max(newMax.y, transformed.y);
        newMax.z = std::max(newMax.z, transformed.z);
    }
    return Aabb{newMin, newMax};
}

std::string ToString(const Aabb& box) {
    return "[" + ToString(box.min) + " .. " + ToString(box.max) + "]";
}

std::optional<Penetration> ComputePenetration(const Aabb& a, const Aabb& b) noexcept {
    if (!IsFinite(a.min) || !IsFinite(a.max) || !IsFinite(b.min) ||
        !IsFinite(b.max) || a.min.x > a.max.x || a.min.y > a.max.y || a.min.z > a.max.z ||
        b.min.x > b.max.x || b.min.y > b.max.y || b.min.z > b.max.z || !a.Intersects(b)) {
        return std::nullopt;
    }

    const float overlapX = std::min(a.max.x, b.max.x) - std::max(a.min.x, b.min.x);
    const float overlapY = std::min(a.max.y, b.max.y) - std::max(a.min.y, b.min.y);
    const float overlapZ = std::min(a.max.z, b.max.z) - std::max(a.min.z, b.min.z);
    const Vector3 centerA = a.GetCenter();
    const Vector3 centerB = b.GetCenter();
    if (!std::isfinite(overlapX) || !std::isfinite(overlapY) || !std::isfinite(overlapZ) ||
        overlapX <= 0.0F || overlapY <= 0.0F || overlapZ <= 0.0F || !IsFinite(centerA) ||
        !IsFinite(centerB)) {
        return std::nullopt;
    }

    if (overlapX <= overlapY && overlapX <= overlapZ) {
        return Penetration{Vector3{centerB.x >= centerA.x ? 1.0F : -1.0F, 0.0F, 0.0F}, overlapX};
    }
    if (overlapY <= overlapZ) {
        return Penetration{Vector3{0.0F, centerB.y >= centerA.y ? 1.0F : -1.0F, 0.0F}, overlapY};
    }
    return Penetration{Vector3{0.0F, 0.0F, centerB.z >= centerA.z ? 1.0F : -1.0F}, overlapZ};
}

std::optional<SweptAabbHit> SweepAabb(const Aabb& moving, const Vector3& displacement,
                                                   const Aabb& target) noexcept {
    if (!IsFinite(moving.min) || !IsFinite(moving.max) ||
        !IsFinite(displacement) || !IsFinite(target.min) ||
        !IsFinite(target.max) || moving.Intersects(target)) {
        return std::nullopt;
    }

    const Vector3 movingExtents = moving.GetExtents();
    if (!IsFinite(movingExtents) || movingExtents.x < 0.0F || movingExtents.y < 0.0F ||
        movingExtents.z < 0.0F) {
        return std::nullopt;
    }
    const Aabb expandedTarget{
        Vector3{target.min.x - movingExtents.x, target.min.y - movingExtents.y,
                   target.min.z - movingExtents.z},
        Vector3{target.max.x + movingExtents.x, target.max.y + movingExtents.y,
                   target.max.z + movingExtents.z}};
    const Vector3 origin = moving.GetCenter();
    const std::array<float, 3> origins{origin.x, origin.y, origin.z};
    const std::array<float, 3> directions{displacement.x, displacement.y, displacement.z};
    const std::array<float, 3> boxMin{expandedTarget.min.x, expandedTarget.min.y, expandedTarget.min.z};
    const std::array<float, 3> boxMax{expandedTarget.max.x, expandedTarget.max.y, expandedTarget.max.z};

    float entryTime = 0.0F;
    float exitTime = 1.0F;
    Vector3 normal{};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        if (directions[axis] == 0.0F) {
            if (origins[axis] < boxMin[axis] || origins[axis] > boxMax[axis]) {
                return std::nullopt;
            }
            continue;
        }
        float nearTime = (boxMin[axis] - origins[axis]) / directions[axis];
        float farTime = (boxMax[axis] - origins[axis]) / directions[axis];
        if (nearTime > farTime) {
            std::swap(nearTime, farTime);
        }
        if (nearTime > entryTime || (nearTime == entryTime && normal == Vector3{})) {
            entryTime = nearTime;
            normal = Vector3{};
            const float directionSign = directions[axis] > 0.0F ? 1.0F : -1.0F;
            if (axis == 0U) {
                normal.x = directionSign;
            } else if (axis == 1U) {
                normal.y = directionSign;
            } else {
                normal.z = directionSign;
            }
        }
        exitTime = std::min(exitTime, farTime);
        if (entryTime > exitTime) {
            return std::nullopt;
        }
    }
    if (entryTime < 0.0F || entryTime > 1.0F || normal == Vector3{}) {
        return std::nullopt;
    }
    return SweptAabbHit{entryTime, normal};
}

std::optional<RayHit> IntersectRay(const Ray& ray, const Aabb& aabb, float maxDistance) noexcept {
    if (!IsFinite(ray.origin) || !IsFinite(ray.direction) ||
        !IsFinite(aabb.min) || !IsFinite(aabb.max) || !std::isfinite(maxDistance) ||
        maxDistance < 0.0F || aabb.min.x > aabb.max.x || aabb.min.y > aabb.max.y || aabb.min.z > aabb.max.z) {
        return std::nullopt;
    }

    const std::array<float, 3> origin{ray.origin.x, ray.origin.y, ray.origin.z};
    const std::array<float, 3> direction{ray.direction.x, ray.direction.y, ray.direction.z};
    const std::array<float, 3> boxMin{aabb.min.x, aabb.min.y, aabb.min.z};
    const std::array<float, 3> boxMax{aabb.max.x, aabb.max.y, aabb.max.z};

    float tmin = 0.0F;
    float tmax = maxDistance;
    Vector3 normal{0.0F, 0.0F, 0.0F};

    for (std::size_t axis = 0; axis < 3; ++axis) {
        if (direction[axis] == 0.0F) {
            if (origin[axis] < boxMin[axis] || origin[axis] > boxMax[axis]) {
                return std::nullopt;
            }
            continue;
        }

        const float nearNumerator = boxMin[axis] - origin[axis];
        const float farNumerator = boxMax[axis] - origin[axis];
        if (!std::isfinite(nearNumerator) || !std::isfinite(farNumerator)) {
            return std::nullopt;
        }
        float tNear = nearNumerator / direction[axis];
        float tFar = farNumerator / direction[axis];
        if (!std::isfinite(tNear) || !std::isfinite(tFar)) {
            return std::nullopt;
        }
        float nearSign = -1.0F;
        if (tNear > tFar) {
            std::swap(tNear, tFar);
            nearSign = 1.0F;
        }

        if (tNear > tmin) {
            tmin = tNear;
            normal = Vector3{0.0F, 0.0F, 0.0F};
            if (axis == 0) {
                normal.x = nearSign;
            } else if (axis == 1) {
                normal.y = nearSign;
            } else {
                normal.z = nearSign;
            }
        }
        tmax = std::min(tmax, tFar);
        if (tmin > tmax) {
            return std::nullopt;
        }
    }

    if (!std::isfinite(tmin) || !IsFinite(normal)) {
        return std::nullopt;
    }
    return RayHit{tmin, normal};
}

} // namespace UVE::Math
