// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "continuous_collision.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Physics {

namespace {

constexpr float kEpsilon = 1.0e-6F;
/// Below this the sweep is treated as stationary and no ray/slab solution is attempted: a zero
/// displacement has no direction, and dividing by its length would produce an infinite impact time.
constexpr float kMinimumSweepLength = 1.0e-9F;

[[nodiscard]] bool IsFiniteVector(const Math::Vector3& value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
}

[[nodiscard]] Math::Vector3 NormalizeOr(const Math::Vector3& value,
                                              const Math::Vector3& fallback) noexcept {
    const float lengthSquared = value.x * value.x + value.y * value.y + value.z * value.z;
    if (!(lengthSquared > kEpsilon) || !std::isfinite(lengthSquared)) {
        return fallback;
    }
    const float inverseLength = 1.0F / std::sqrt(lengthSquared);
    return Math::Vector3{value.x * inverseLength, value.y * inverseLength, value.z * inverseLength};
}

/// Unit axis for an index, and its sign.
[[nodiscard]] Math::Vector3 AxisNormal(const int axis, const float sign) noexcept {
    Math::Vector3 normal{};
    if (axis == 0) {
        normal.x = sign;
    } else if (axis == 1) {
        normal.y = sign;
    } else {
        normal.z = sign;
    }
    return normal;
}

/// The axis along which a point inside `box` is closest to leaving it, with the outward sign.
/// Used for the "already overlapping at t == 0" case, where a slab test has no entry face to report.
[[nodiscard]] Math::Vector3 LeastPenetrationAxisNormal(const Math::Vector3& point,
                                                             const Math::Aabb& box) noexcept {
    const float distances[3] = {point.x - box.min.x, point.y - box.min.y, point.z - box.min.z};
    const float exits[3] = {box.max.x - point.x, box.max.y - point.y, box.max.z - point.z};
    int bestAxis = 0;
    float bestDistance = distances[0] < exits[0] ? distances[0] : exits[0];
    float bestSign = distances[0] < exits[0] ? -1.0F : 1.0F;
    for (int axis = 1; axis < 3; ++axis) {
        const bool towardsMin = distances[axis] < exits[axis];
        const float distance = towardsMin ? distances[axis] : exits[axis];
        if (distance < bestDistance) {
            bestDistance = distance;
            bestAxis = axis;
            bestSign = towardsMin ? -1.0F : 1.0F;
        }
    }
    return AxisNormal(bestAxis, bestSign);
}

} // namespace

std::optional<SweptContact> SweepSphereAgainstSphere(const SweptSphere& mover,
                                                           const Math::Vector3& targetCenter,
                                                           const float targetRadius) noexcept {
    if (!IsFiniteVector(mover.center) || !IsFiniteVector(mover.displacement) ||
        !IsFiniteVector(targetCenter) || !std::isfinite(mover.radius) ||
        !std::isfinite(targetRadius) || mover.radius < 0.0F || targetRadius < 0.0F) {
        return std::nullopt;
    }
    const float combinedRadius = mover.radius + targetRadius;
    const Math::Vector3 offset{mover.center.x - targetCenter.x, mover.center.y - targetCenter.y,
                                  mover.center.z - targetCenter.z};

    // Starting overlapped: report an immediate contact and let the discrete resolver separate the
    // pair. Reporting "no contact" here would let a body that spawned inside another one stay there.
    const float startDistanceSquared =
        offset.x * offset.x + offset.y * offset.y + offset.z * offset.z;
    if (startDistanceSquared <= combinedRadius * combinedRadius) {
        const Math::Vector3 away = NormalizeOr(offset, Math::Vector3{0.0F, 1.0F, 0.0F});
        return SweptContact{0.0F, away, mover.center};
    }

    const float a = mover.displacement.x * mover.displacement.x +
                    mover.displacement.y * mover.displacement.y +
                    mover.displacement.z * mover.displacement.z;
    if (!(a > kMinimumSweepLength)) {
        return std::nullopt;
    }
    const float b = 2.0F * (offset.x * mover.displacement.x + offset.y * mover.displacement.y +
                            offset.z * mover.displacement.z);
    const float c = startDistanceSquared - combinedRadius * combinedRadius;
    const float discriminant = b * b - 4.0F * a * c;
    if (!(discriminant >= 0.0F)) {
        return std::nullopt;
    }
    const float root = std::sqrt(discriminant);
    // The smaller root is first contact; the larger one is where the sweep leaves the target.
    const float timeOfImpact = (-b - root) / (2.0F * a);
    if (!(timeOfImpact >= 0.0F) || !(timeOfImpact <= 1.0F)) {
        return std::nullopt;
    }
    const Math::Vector3 position{mover.center.x + mover.displacement.x * timeOfImpact,
                                    mover.center.y + mover.displacement.y * timeOfImpact,
                                    mover.center.z + mover.displacement.z * timeOfImpact};
    const Math::Vector3 normal = NormalizeOr(
        Math::Vector3{position.x - targetCenter.x, position.y - targetCenter.y,
                         position.z - targetCenter.z},
        Math::Vector3{0.0F, 1.0F, 0.0F});
    return SweptContact{timeOfImpact, normal, position};
}

std::optional<SweptContact> SweepSphereAgainstAabb(const SweptSphere& mover,
                                                         const Math::Aabb& target) noexcept {
    if (!IsFiniteVector(mover.center) || !IsFiniteVector(mover.displacement) ||
        !std::isfinite(mover.radius) || mover.radius < 0.0F) {
        return std::nullopt;
    }
    if (!(target.max.x >= target.min.x) || !(target.max.y >= target.min.y) ||
        !(target.max.z >= target.min.z)) {
        return std::nullopt;
    }
    // Minkowski sum: inflate the box by the mover's radius so the sphere becomes a point.
    const Math::Vector3 expandedMin{target.min.x - mover.radius, target.min.y - mover.radius,
                                       target.min.z - mover.radius};
    const Math::Vector3 expandedMax{target.max.x + mover.radius, target.max.y + mover.radius,
                                       target.max.z + mover.radius};

    const float center[3] = {mover.center.x, mover.center.y, mover.center.z};
    const float delta[3] = {mover.displacement.x, mover.displacement.y, mover.displacement.z};
    const float low[3] = {expandedMin.x, expandedMin.y, expandedMin.z};
    const float high[3] = {expandedMax.x, expandedMax.y, expandedMax.z};

    // Inside the inflated box means "already touching", which a slab test cannot express (its entry
    // time would be <= 0). Report the shallowest direction out instead.
    if (center[0] >= low[0] && center[0] <= high[0] && center[1] >= low[1] && center[1] <= high[1] &&
        center[2] >= low[2] && center[2] <= high[2]) {
        return SweptContact{0.0F, LeastPenetrationAxisNormal(mover.center, target),
                               mover.center};
    }

    float entryTime = 0.0F;
    float exitTime = 1.0F;
    int entryAxis = 0;
    float entrySign = -1.0F;
    for (int axis = 0; axis < 3; ++axis) {
        if (std::abs(delta[axis]) <= kEpsilon) {
            // Parallel to this slab: outside it the sweep can never enter the box.
            if (center[axis] < low[axis] || center[axis] > high[axis]) {
                return std::nullopt;
            }
            continue;
        }
        const float inverse = 1.0F / delta[axis];
        float nearTime = (low[axis] - center[axis]) * inverse;
        float farTime = (high[axis] - center[axis]) * inverse;
        float sign = -1.0F;
        if (nearTime > farTime) {
            std::swap(nearTime, farTime);
            sign = 1.0F;
        }
        if (nearTime > entryTime) {
            entryTime = nearTime;
            entryAxis = axis;
            entrySign = sign;
        }
        exitTime = std::min(exitTime, farTime);
        if (entryTime > exitTime) {
            return std::nullopt;
        }
    }
    if (!(entryTime <= 1.0F) || !(exitTime >= 0.0F)) {
        return std::nullopt;
    }
    const float timeOfImpact = std::max(entryTime, 0.0F);
    const Math::Vector3 position{mover.center.x + mover.displacement.x * timeOfImpact,
                                    mover.center.y + mover.displacement.y * timeOfImpact,
                                    mover.center.z + mover.displacement.z * timeOfImpact};
    return SweptContact{timeOfImpact, AxisNormal(entryAxis, entrySign), position};
}

std::optional<SweptContact> SweepSphereAgainstAabbs(
    const SweptSphere& mover, const std::span<const Math::Aabb> targets) noexcept {
    std::optional<SweptContact> earliest;
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const std::optional<SweptContact> contact = SweepSphereAgainstAabb(mover, targets[index]);
        if (!contact.has_value()) {
            continue;
        }
        // Strictly earlier, so a tie keeps the first target in enumeration order. Two surfaces at
        // the same distance is a corner, and picking by index is what makes that reproducible.
        if (!earliest.has_value() || contact->timeOfImpact < earliest->timeOfImpact) {
            earliest = contact;
        }
    }
    return earliest;
}

ContinuousMotionResult ResolveContinuousMotion(
    const SweptSphere& mover, const std::span<const Math::Aabb> targets,
    const ContinuousCollisionSettings& settings) noexcept {
    ContinuousMotionResult result{mover.displacement, std::nullopt};
    if (!settings.enabled || !IsFiniteVector(mover.displacement)) {
        return result;
    }
    const float travel = std::sqrt(mover.displacement.x * mover.displacement.x +
                                   mover.displacement.y * mover.displacement.y +
                                   mover.displacement.z * mover.displacement.z);
    if (!(travel > std::max(settings.motionThreshold, kMinimumSweepLength))) {
        return result;
    }
    const std::optional<SweptContact> contact = SweepSphereAgainstAabbs(mover, targets);
    if (!contact.has_value()) {
        return result;
    }
    if (contact->timeOfImpact <= 0.0F) {
        // Already touching, so there is nothing ahead to cut. Clamping to zero here would freeze
        // every body resting on the floor - sliding is exactly the case where the mover is in
        // contact for its whole step. The contact is still reported so the caller can drop the
        // velocity component pressed into the surface; the discrete resolver owns the overlap.
        result.contact = contact;
        return result;
    }

    // Pull the cut back by the contact offset, expressed in the same [0, 1] fraction the sweep
    // reports. Clamped at zero so a contact inside the offset still stops the body rather than
    // leaving it to travel backwards.
    const float offsetFraction = std::max(settings.contactOffset, 0.0F) / travel;
    const float clampedTime = std::max(0.0F, contact->timeOfImpact - offsetFraction);
    SweptContact resolved = *contact;
    resolved.timeOfImpact = clampedTime;
    resolved.position = Math::Vector3{mover.center.x + mover.displacement.x * clampedTime,
                                         mover.center.y + mover.displacement.y * clampedTime,
                                         mover.center.z + mover.displacement.z * clampedTime};
    result.displacement = Math::Vector3{mover.displacement.x * clampedTime,
                                           mover.displacement.y * clampedTime,
                                           mover.displacement.z * clampedTime};
    result.contact = resolved;
    return result;
}

} // namespace UVE::Physics
