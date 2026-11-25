// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "shape_narrow_phase.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

#include "vector3.h"

namespace UVE::Physics::Detail {
namespace {

[[nodiscard]] bool IsFiniteAabb(const Math::Aabb& box) noexcept {
    return Math::IsFinite(box.min) && Math::IsFinite(box.max) && box.min.x < box.max.x &&
           box.min.y < box.max.y && box.min.z < box.max.z;
}

[[nodiscard]] bool TryBuildOrientedBoxFrame(
    const Math::Vector3 boxHalfExtents, const Math::Quaternion boxRotation,
    Math::Quaternion& outRotation, Math::Quaternion& outInverse) noexcept {
    if (!Math::IsFinite(boxHalfExtents) || boxHalfExtents.x <= 0.0F || boxHalfExtents.y <= 0.0F ||
        boxHalfExtents.z <= 0.0F || !Math::TryNormalize(boxRotation, outRotation) ||
        !Math::TryInverse(outRotation, outInverse)) {
        return false;
    }
    return true;
}

} // namespace

std::optional<Math::RayHit> IntersectMovingSphereSphere(
    const Math::Ray& ray, const float movingRadius, const Math::Sphere& target,
    const float maxDistance) noexcept {
    // The narrow phase is one step stricter than Sphere::IsValid (which permits a zero
    // radius): a zero-radius TARGET has no surface to hit, so it still fails closed here.
    if (!Math::IsFinite(ray.origin) || !Math::IsFinite(ray.direction) ||
        !target.IsValid() || target.radius <= 0.0F || !std::isfinite(movingRadius) ||
        movingRadius < 0.0F || !std::isfinite(maxDistance) || maxDistance < 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 targetCenter = target.center;
    const float targetRadius = target.radius;

    const double directionX = static_cast<double>(ray.direction.x);
    const double directionY = static_cast<double>(ray.direction.y);
    const double directionZ = static_cast<double>(ray.direction.z);
    const double originDeltaX = static_cast<double>(ray.origin.x) - static_cast<double>(targetCenter.x);
    const double originDeltaY = static_cast<double>(ray.origin.y) - static_cast<double>(targetCenter.y);
    const double originDeltaZ = static_cast<double>(ray.origin.z) - static_cast<double>(targetCenter.z);
    const double combinedRadius = static_cast<double>(movingRadius) + static_cast<double>(targetRadius);
    const double directionSquared = directionX * directionX + directionY * directionY + directionZ * directionZ;
    const double originDistanceSquared = originDeltaX * originDeltaX + originDeltaY * originDeltaY +
                                         originDeltaZ * originDeltaZ;
    const double radiusSquared = combinedRadius * combinedRadius;
    if (!std::isfinite(combinedRadius) || !std::isfinite(directionSquared) || directionSquared <= 0.0 ||
        !std::isfinite(originDistanceSquared) || !std::isfinite(radiusSquared)) {
        return std::nullopt;
    }

    if (originDistanceSquared <= radiusSquared) {
        return Math::RayHit{0.0F, Math::Vector3{}};
    }

    const double linear = originDeltaX * directionX + originDeltaY * directionY + originDeltaZ * directionZ;
    const double constant = originDistanceSquared - radiusSquared;
    const double discriminant = linear * linear - directionSquared * constant;
    if (!std::isfinite(linear) || !std::isfinite(constant) || !std::isfinite(discriminant) || discriminant <= 0.0) {
        return std::nullopt;
    }

    const double root = std::sqrt(discriminant);
    const double nearRoot = (-linear - root) / directionSquared;
    const double farRoot = (-linear + root) / directionSquared;
    double distance = nearRoot >= 0.0 ? nearRoot : farRoot;
    if (!std::isfinite(distance) || distance < 0.0 || distance > static_cast<double>(maxDistance)) {
        return std::nullopt;
    }

    const double contactX = static_cast<double>(ray.origin.x) + directionX * distance;
    const double contactY = static_cast<double>(ray.origin.y) + directionY * distance;
    const double contactZ = static_cast<double>(ray.origin.z) + directionZ * distance;
    const double normalX = contactX - static_cast<double>(targetCenter.x);
    const double normalY = contactY - static_cast<double>(targetCenter.y);
    const double normalZ = contactZ - static_cast<double>(targetCenter.z);
    const double normalLength = std::sqrt(normalX * normalX + normalY * normalY + normalZ * normalZ);
    if (!std::isfinite(contactX) || !std::isfinite(contactY) || !std::isfinite(contactZ) ||
        !std::isfinite(normalLength) || normalLength <= 0.0) {
        return std::nullopt;
    }
    const Math::Vector3 normal{static_cast<float>(normalX / normalLength),
                                  static_cast<float>(normalY / normalLength),
                                  static_cast<float>(normalZ / normalLength)};
    if (!Math::IsFinite(normal)) {
        return std::nullopt;
    }
    return Math::RayHit{static_cast<float>(distance), normal};
}

std::optional<Math::RayHit> IntersectRaySphere(
    const Math::Ray& ray, const Math::Sphere& target, const float maxDistance) noexcept {
    return IntersectMovingSphereSphere(ray, 0.0F, target, maxDistance);
}

std::optional<Math::RayHit> IntersectRayCapsule(
    const Math::Ray& ray, const Math::Vector3 segmentStart, const Math::Vector3 segmentEnd,
    const float capsuleRadius, const float maxDistance) noexcept {
    if (!Math::IsFinite(ray.origin) || !Math::IsFinite(ray.direction) ||
        !Math::IsFinite(segmentStart) || !Math::IsFinite(segmentEnd) ||
        !std::isfinite(capsuleRadius) || capsuleRadius <= 0.0F || !std::isfinite(maxDistance) ||
        maxDistance < 0.0F) {
        return std::nullopt;
    }

    const double segmentX = static_cast<double>(segmentEnd.x) - static_cast<double>(segmentStart.x);
    const double segmentY = static_cast<double>(segmentEnd.y) - static_cast<double>(segmentStart.y);
    const double segmentZ = static_cast<double>(segmentEnd.z) - static_cast<double>(segmentStart.z);
    const double segmentLengthSquared = segmentX * segmentX + segmentY * segmentY + segmentZ * segmentZ;
    if (!std::isfinite(segmentLengthSquared)) {
        return std::nullopt;
    }
    if (segmentLengthSquared <= 0.0) {
        return IntersectRaySphere(ray, Math::Sphere{segmentStart, capsuleRadius}, maxDistance);
    }

    const double segmentLength = std::sqrt(segmentLengthSquared);
    const Math::Vector3 axis{static_cast<float>(segmentX / segmentLength),
                                static_cast<float>(segmentY / segmentLength),
                                static_cast<float>(segmentZ / segmentLength)};
    if (!Math::IsFinite(axis)) {
        return std::nullopt;
    }
    const double originX = static_cast<double>(ray.origin.x) - static_cast<double>(segmentStart.x);
    const double originY = static_cast<double>(ray.origin.y) - static_cast<double>(segmentStart.y);
    const double originZ = static_cast<double>(ray.origin.z) - static_cast<double>(segmentStart.z);
    const double axialOrigin = originX * segmentX / segmentLength + originY * segmentY / segmentLength +
                               originZ * segmentZ / segmentLength;
    const double radialX = originX - axialOrigin * static_cast<double>(axis.x);
    const double radialY = originY - axialOrigin * static_cast<double>(axis.y);
    const double radialZ = originZ - axialOrigin * static_cast<double>(axis.z);
    const double radiusSquared = static_cast<double>(capsuleRadius) * static_cast<double>(capsuleRadius);
    const double closestAxial = std::clamp(axialOrigin, 0.0, segmentLength);
    const double closestDeltaX = originX - closestAxial * static_cast<double>(axis.x);
    const double closestDeltaY = originY - closestAxial * static_cast<double>(axis.y);
    const double closestDeltaZ = originZ - closestAxial * static_cast<double>(axis.z);
    const double closestDistanceSquared = closestDeltaX * closestDeltaX + closestDeltaY * closestDeltaY +
                                         closestDeltaZ * closestDeltaZ;
    if (!std::isfinite(axialOrigin) || !std::isfinite(radiusSquared) || !std::isfinite(closestDistanceSquared)) {
        return std::nullopt;
    }
    if (closestDistanceSquared <= radiusSquared) {
        return Math::RayHit{0.0F, Math::Vector3{}};
    }

    std::optional<Math::RayHit> closestHit;
    const auto ConsiderHit = [&](const std::optional<Math::RayHit>& candidate) {
        if (!candidate.has_value() || candidate->distance < 0.0F ||
            (closestHit.has_value() && candidate->distance >= closestHit->distance)) {
            return;
        }
        closestHit = candidate;
    };

    const double directionX = static_cast<double>(ray.direction.x);
    const double directionY = static_cast<double>(ray.direction.y);
    const double directionZ = static_cast<double>(ray.direction.z);
    const double axialDirection = directionX * static_cast<double>(axis.x) +
                                  directionY * static_cast<double>(axis.y) +
                                  directionZ * static_cast<double>(axis.z);
    const double radialDirectionX = directionX - axialDirection * static_cast<double>(axis.x);
    const double radialDirectionY = directionY - axialDirection * static_cast<double>(axis.y);
    const double radialDirectionZ = directionZ - axialDirection * static_cast<double>(axis.z);
    const double quadraticA = radialDirectionX * radialDirectionX + radialDirectionY * radialDirectionY +
                              radialDirectionZ * radialDirectionZ;
    const double quadraticB = 2.0 * (radialX * radialDirectionX + radialY * radialDirectionY + radialZ * radialDirectionZ);
    const double quadraticC = radialX * radialX + radialY * radialY + radialZ * radialZ - radiusSquared;
    const double discriminant = quadraticB * quadraticB - 4.0 * quadraticA * quadraticC;
    if (std::isfinite(quadraticA) && std::isfinite(quadraticB) && std::isfinite(quadraticC) &&
        std::isfinite(discriminant) && quadraticA > 0.0 && discriminant > 0.0) {
        const double root = std::sqrt(discriminant);
        const double roots[2] = {(-quadraticB - root) / (2.0 * quadraticA),
                                 (-quadraticB + root) / (2.0 * quadraticA)};
        for (const double candidateDistance : roots) {
            if (!std::isfinite(candidateDistance) || candidateDistance < 0.0 ||
                candidateDistance > static_cast<double>(maxDistance)) {
                continue;
            }
            const double axial = axialOrigin + axialDirection * candidateDistance;
            if (axial <= 0.0 || axial >= segmentLength) {
                continue;
            }
            const double contactRadialX = radialX + radialDirectionX * candidateDistance;
            const double contactRadialY = radialY + radialDirectionY * candidateDistance;
            const double contactRadialZ = radialZ + radialDirectionZ * candidateDistance;
            const double normalLength = std::sqrt(contactRadialX * contactRadialX +
                                                  contactRadialY * contactRadialY + contactRadialZ * contactRadialZ);
            if (!std::isfinite(normalLength) || normalLength <= 0.0) {
                continue;
            }
            ConsiderHit(Math::RayHit{
                static_cast<float>(candidateDistance),
                {static_cast<float>(contactRadialX / normalLength), static_cast<float>(contactRadialY / normalLength),
                 static_cast<float>(contactRadialZ / normalLength)}});
        }
    }

    const std::optional<Math::RayHit> startHit =
        IntersectRaySphere(ray, Math::Sphere{segmentStart, capsuleRadius}, maxDistance);
    if (startHit.has_value()) {
        const Math::Vector3 point = ray.origin + ray.direction * startHit->distance;
        const double axial = static_cast<double>(point.x - segmentStart.x) * static_cast<double>(axis.x) +
                             static_cast<double>(point.y - segmentStart.y) * static_cast<double>(axis.y) +
                             static_cast<double>(point.z - segmentStart.z) * static_cast<double>(axis.z);
        if (axial <= 0.0) {
            ConsiderHit(startHit);
        }
    }
    const std::optional<Math::RayHit> endHit =
        IntersectRaySphere(ray, Math::Sphere{segmentEnd, capsuleRadius}, maxDistance);
    if (endHit.has_value()) {
        const Math::Vector3 point = ray.origin + ray.direction * endHit->distance;
        const double axial = static_cast<double>(point.x - segmentStart.x) * static_cast<double>(axis.x) +
                             static_cast<double>(point.y - segmentStart.y) * static_cast<double>(axis.y) +
                             static_cast<double>(point.z - segmentStart.z) * static_cast<double>(axis.z);
        if (axial >= segmentLength) {
            ConsiderHit(endHit);
        }
    }
    return closestHit;
}

std::optional<Math::RayHit> IntersectRayOrientedBox(
    const Math::Ray& ray, const Math::OrientedBox& box, const float maxDistance) noexcept {
    // Deliberately not OrientedBox::IsValid: that requires a unit rotation, but this query
    // normalizes whatever rotation it is handed (a zero rotation still fails below), so only the
    // center/extents/range checks apply here.
    if (!Math::IsFinite(ray.origin) || !Math::IsFinite(ray.direction) ||
        !Math::IsFinite(box.center) || !Math::IsFinite(box.halfExtents) ||
        box.halfExtents.x <= 0.0F || box.halfExtents.y <= 0.0F || box.halfExtents.z <= 0.0F ||
        !std::isfinite(maxDistance) || maxDistance < 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 boxCenter = box.center;
    const Math::Vector3 boxHalfExtents = box.halfExtents;
    const Math::Quaternion boxRotation = box.rotation;
    Math::Quaternion normalizedRotation;
    Math::Quaternion inverseRotation;
    if (!Math::TryNormalize(boxRotation, normalizedRotation) ||
        !Math::TryInverse(normalizedRotation, inverseRotation)) {
        return std::nullopt;
    }
    const Math::Vector3 localOrigin = Math::RotateVector(inverseRotation, ray.origin - boxCenter);
    const Math::Vector3 localDirection = Math::RotateVector(inverseRotation, ray.direction);
    const Math::Ray localRay{localOrigin, localDirection};
    const std::optional<Math::RayHit> localHit = Math::IntersectRay(
        localRay, Math::Aabb::FromCenterExtents({}, boxHalfExtents), maxDistance);
    if (!localHit.has_value()) {
        return std::nullopt;
    }
    const Math::Vector3 worldNormal = Math::RotateVector(normalizedRotation, localHit->normal);
    if (!Math::IsFinite(worldNormal)) {
        return std::nullopt;
    }
    return Math::RayHit{localHit->distance, worldNormal};
}

std::optional<Math::Penetration> ComputeSphereAabbPenetration(
    const Math::Aabb& box, const Math::Sphere& sphere) noexcept {
    // As in IntersectMovingSphereSphere: the narrow phase needs a strictly positive radius,
    // one step stricter than Sphere::IsValid.
    if (!IsFiniteAabb(box) || !sphere.IsValid() || sphere.radius <= 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 sphereCenter = sphere.center;
    const float sphereRadius = sphere.radius;

    const Math::Vector3 closestPoint{
        std::clamp(sphereCenter.x, box.min.x, box.max.x),
        std::clamp(sphereCenter.y, box.min.y, box.max.y),
        std::clamp(sphereCenter.z, box.min.z, box.max.z),
    };
    const Math::Vector3 delta = closestPoint - sphereCenter;
    const double deltaX = static_cast<double>(delta.x);
    const double deltaY = static_cast<double>(delta.y);
    const double deltaZ = static_cast<double>(delta.z);
    const double distanceSquared = deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ;
    const double radiusSquared = static_cast<double>(sphereRadius) * static_cast<double>(sphereRadius);
    if (distanceSquared > 0.0) {
        if (distanceSquared >= radiusSquared) {
            return std::nullopt;
        }
        const float distance = static_cast<float>(std::sqrt(distanceSquared));
        return Math::Penetration{delta * (1.0F / distance), sphereRadius - distance};
    }

    const float distanceToMinX = sphereCenter.x - box.min.x;
    const float distanceToMaxX = box.max.x - sphereCenter.x;
    const float distanceToMinY = sphereCenter.y - box.min.y;
    const float distanceToMaxY = box.max.y - sphereCenter.y;
    const float distanceToMinZ = sphereCenter.z - box.min.z;
    const float distanceToMaxZ = box.max.z - sphereCenter.z;

    float nearestDistance = distanceToMinX;
    Math::Vector3 axis{1.0F, 0.0F, 0.0F};
    if (distanceToMaxX < nearestDistance) {
        nearestDistance = distanceToMaxX;
        axis = {-1.0F, 0.0F, 0.0F};
    }
    if (distanceToMinY < nearestDistance) {
        nearestDistance = distanceToMinY;
        axis = {0.0F, 1.0F, 0.0F};
    }
    if (distanceToMaxY < nearestDistance) {
        nearestDistance = distanceToMaxY;
        axis = {0.0F, -1.0F, 0.0F};
    }
    if (distanceToMinZ < nearestDistance) {
        nearestDistance = distanceToMinZ;
        axis = {0.0F, 0.0F, 1.0F};
    }
    if (distanceToMaxZ < nearestDistance) {
        nearestDistance = distanceToMaxZ;
        axis = {0.0F, 0.0F, -1.0F};
    }
    return Math::Penetration{axis, sphereRadius + nearestDistance};
}

std::optional<Math::Penetration> ComputeCapsuleAabbPenetration(
    const Math::Aabb& box, const Math::Vector3 segmentStart, const Math::Vector3 segmentEnd,
    const float capsuleRadius) noexcept {
    if (!IsFiniteAabb(box) || !std::isfinite(segmentStart.x) || !std::isfinite(segmentStart.y) ||
        !std::isfinite(segmentStart.z) || !std::isfinite(segmentEnd.x) || !std::isfinite(segmentEnd.y) ||
        !std::isfinite(segmentEnd.z) || !std::isfinite(capsuleRadius) || capsuleRadius <= 0.0F) {
        return std::nullopt;
    }

    const double directionX = static_cast<double>(segmentEnd.x) - static_cast<double>(segmentStart.x);
    const double directionY = static_cast<double>(segmentEnd.y) - static_cast<double>(segmentStart.y);
    const double directionZ = static_cast<double>(segmentEnd.z) - static_cast<double>(segmentStart.z);
    if (!std::isfinite(directionX) || !std::isfinite(directionY) || !std::isfinite(directionZ)) {
        return std::nullopt;
    }
    constexpr double kBreakpointEpsilon = 1.0e-6;
    std::array<double, 8U> breakpoints{0.0, 1.0, 0.0, 0.0, 0.0, 0.0, 0.0, 0.0};
    std::size_t breakpointCount = 2U;
    const auto AddBreakpoint = [&](const double value) {
        if (std::isfinite(value) && value > kBreakpointEpsilon && value < 1.0 - kBreakpointEpsilon &&
            breakpointCount < breakpoints.size()) {
            breakpoints[breakpointCount++] = value;
        }
    };
    const auto AddAxisBreakpoints = [&](const double start, const double delta, const float minimum,
                                           const float maximum) {
        if (std::fabs(delta) <= kBreakpointEpsilon) {
            return;
        }
        AddBreakpoint((static_cast<double>(minimum) - start) / delta);
        AddBreakpoint((static_cast<double>(maximum) - start) / delta);
    };
    AddAxisBreakpoints(segmentStart.x, directionX, box.min.x, box.max.x);
    AddAxisBreakpoints(segmentStart.y, directionY, box.min.y, box.max.y);
    AddAxisBreakpoints(segmentStart.z, directionZ, box.min.z, box.max.z);

    for (std::size_t index = 1U; index < breakpointCount; ++index) {
        const double value = breakpoints[index];
        std::size_t insertionIndex = index;
        while (insertionIndex > 0U && breakpoints[insertionIndex - 1U] > value) {
            breakpoints[insertionIndex] = breakpoints[insertionIndex - 1U];
            --insertionIndex;
        }
        breakpoints[insertionIndex] = value;
    }
    std::size_t uniqueCount = 0U;
    for (std::size_t index = 0U; index < breakpointCount; ++index) {
        if (uniqueCount == 0U || std::fabs(breakpoints[index] - breakpoints[uniqueCount - 1U]) >
                                      kBreakpointEpsilon) {
            breakpoints[uniqueCount++] = breakpoints[index];
        }
    }

    double bestDistanceSquared = std::numeric_limits<double>::infinity();
    double bestTime = 0.0;
    double bestDeltaX = 0.0;
    double bestDeltaY = 0.0;
    double bestDeltaZ = 0.0;
    const auto ConsiderTime = [&](const double time) {
        const double pointX = static_cast<double>(segmentStart.x) + directionX * time;
        const double pointY = static_cast<double>(segmentStart.y) + directionY * time;
        const double pointZ = static_cast<double>(segmentStart.z) + directionZ * time;
        const double closestX = std::clamp(pointX, static_cast<double>(box.min.x), static_cast<double>(box.max.x));
        const double closestY = std::clamp(pointY, static_cast<double>(box.min.y), static_cast<double>(box.max.y));
        const double closestZ = std::clamp(pointZ, static_cast<double>(box.min.z), static_cast<double>(box.max.z));
        const double deltaX = closestX - pointX;
        const double deltaY = closestY - pointY;
        const double deltaZ = closestZ - pointZ;
        const double scale = std::max(std::fabs(deltaX), std::max(std::fabs(deltaY), std::fabs(deltaZ)));
        if (!std::isfinite(scale)) {
            return;
        }
        double distanceSquared = 0.0;
        if (scale != 0.0) {
            const double scaledDistanceSquared = (deltaX / scale) * (deltaX / scale) +
                                                 (deltaY / scale) * (deltaY / scale) +
                                                 (deltaZ / scale) * (deltaZ / scale);
            if (!std::isfinite(scaledDistanceSquared)) {
                return;
            }
            distanceSquared = scale * scale * scaledDistanceSquared;
            if (!std::isfinite(distanceSquared)) {
                return;
            }
        }
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestTime = time;
            bestDeltaX = deltaX;
            bestDeltaY = deltaY;
            bestDeltaZ = deltaZ;
        }
    };

    for (std::size_t index = 0U; index < uniqueCount; ++index) {
        ConsiderTime(breakpoints[index]);
    }
    for (std::size_t index = 0U; index + 1U < uniqueCount; ++index) {
        const double intervalStart = breakpoints[index];
        const double intervalEnd = breakpoints[index + 1U];
        if (intervalEnd - intervalStart <= kBreakpointEpsilon) {
            continue;
        }
        const double midpoint = (intervalStart + intervalEnd) * 0.5;
        const double midpointX = static_cast<double>(segmentStart.x) + directionX * midpoint;
        const double midpointY = static_cast<double>(segmentStart.y) + directionY * midpoint;
        const double midpointZ = static_cast<double>(segmentStart.z) + directionZ * midpoint;
        double quadraticA = 0.0;
        double quadraticB = 0.0;
        const auto AccumulateAxis = [&](const double start, const double delta, const float minimum,
                                           const float maximum, const double midpointValue) {
            if (midpointValue < static_cast<double>(minimum)) {
                const double constant = static_cast<double>(minimum) - start;
                quadraticA += delta * delta;
                quadraticB -= 2.0 * constant * delta;
            } else if (midpointValue > static_cast<double>(maximum)) {
                const double constant = start - static_cast<double>(maximum);
                quadraticA += delta * delta;
                quadraticB += 2.0 * constant * delta;
            }
        };
        AccumulateAxis(static_cast<double>(segmentStart.x), directionX, box.min.x, box.max.x, midpointX);
        AccumulateAxis(static_cast<double>(segmentStart.y), directionY, box.min.y, box.max.y, midpointY);
        AccumulateAxis(static_cast<double>(segmentStart.z), directionZ, box.min.z, box.max.z, midpointZ);
        if (quadraticA > 0.0 && std::isfinite(quadraticA) && std::isfinite(quadraticB)) {
            const double stationary = -quadraticB / (2.0 * quadraticA);
            if (std::isfinite(stationary) && stationary > intervalStart && stationary < intervalEnd) {
                ConsiderTime(stationary);
            }
        }
    }

    if (!std::isfinite(bestDistanceSquared)) {
        return std::nullopt;
    }
    const double capsuleRadiusDouble = static_cast<double>(capsuleRadius);
    const double capsuleRadiusSquared = capsuleRadiusDouble * capsuleRadiusDouble;
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(capsuleRadiusSquared) || bestDistanceSquared >= capsuleRadiusSquared) {
        return std::nullopt;
    }
    const double closestX = static_cast<double>(segmentStart.x) + directionX * bestTime;
    const double closestY = static_cast<double>(segmentStart.y) + directionY * bestTime;
    const double closestZ = static_cast<double>(segmentStart.z) + directionZ * bestTime;
    if (bestDistanceSquared <= 0.0) {
        const Math::Vector3 closestSegmentPoint{
            static_cast<float>(closestX), static_cast<float>(closestY), static_cast<float>(closestZ)};
        if (!Math::IsFinite(closestSegmentPoint)) {
            return std::nullopt;
        }
        return ComputeSphereAabbPenetration(box,
                                                   Math::Sphere{closestSegmentPoint, capsuleRadius});
    }

    const double distance = std::sqrt(bestDistanceSquared);
    const double depth = capsuleRadiusDouble - distance;
    if (!std::isfinite(distance) || !std::isfinite(depth) || depth <= 0.0 || depth > maximumFloat) {
        return std::nullopt;
    }
    const double inverseDistance = 1.0 / distance;
    const Math::Vector3 axis{
        static_cast<float>(bestDeltaX * inverseDistance),
        static_cast<float>(bestDeltaY * inverseDistance),
        static_cast<float>(bestDeltaZ * inverseDistance),
    };
    if (!Math::IsFinite(axis)) {
        return std::nullopt;
    }
    return Math::Penetration{axis, static_cast<float>(depth)};
}

std::optional<Math::Penetration> ComputeSphereSpherePenetration(
    const Math::Vector3 firstCenter, const float firstRadius, const Math::Vector3 secondCenter,
    const float secondRadius) noexcept {
    if (!std::isfinite(firstCenter.x) || !std::isfinite(firstCenter.y) || !std::isfinite(firstCenter.z) ||
        !std::isfinite(secondCenter.x) || !std::isfinite(secondCenter.y) ||
        !std::isfinite(secondCenter.z) || !std::isfinite(firstRadius) || !std::isfinite(secondRadius) ||
        firstRadius <= 0.0F || secondRadius <= 0.0F) {
        return std::nullopt;
    }

    const double deltaX = static_cast<double>(secondCenter.x) - static_cast<double>(firstCenter.x);
    const double deltaY = static_cast<double>(secondCenter.y) - static_cast<double>(firstCenter.y);
    const double deltaZ = static_cast<double>(secondCenter.z) - static_cast<double>(firstCenter.z);
    const double scale = std::max(std::fabs(deltaX), std::max(std::fabs(deltaY), std::fabs(deltaZ)));
    if (!std::isfinite(scale)) {
        return std::nullopt;
    }
    double distanceSquared = 0.0;
    if (scale != 0.0) {
        const double scaledDistanceSquared = (deltaX / scale) * (deltaX / scale) +
                                             (deltaY / scale) * (deltaY / scale) +
                                             (deltaZ / scale) * (deltaZ / scale);
        if (!std::isfinite(scaledDistanceSquared)) {
            return std::nullopt;
        }
        distanceSquared = (scale * scale) * scaledDistanceSquared;
        if (!std::isfinite(distanceSquared)) {
            return std::nullopt;
        }
    }

    const double combinedRadius = static_cast<double>(firstRadius) + static_cast<double>(secondRadius);
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(combinedRadius) || combinedRadius <= 0.0 || combinedRadius > maximumFloat) {
        return std::nullopt;
    }
    const double combinedRadiusSquared = combinedRadius * combinedRadius;
    if (!std::isfinite(combinedRadiusSquared) || distanceSquared >= combinedRadiusSquared) {
        return std::nullopt;
    }
    if (distanceSquared == 0.0) {
        return Math::Penetration{{1.0F, 0.0F, 0.0F}, static_cast<float>(combinedRadius)};
    }

    const double distance = std::sqrt(distanceSquared);
    const double depth = combinedRadius - distance;
    if (!std::isfinite(distance) || !std::isfinite(depth) || depth <= 0.0 || depth > maximumFloat) {
        return std::nullopt;
    }
    const double inverseDistance = 1.0 / distance;
    const Math::Vector3 axis{
        static_cast<float>(deltaX * inverseDistance),
        static_cast<float>(deltaY * inverseDistance),
        static_cast<float>(deltaZ * inverseDistance),
    };
    if (!Math::IsFinite(axis)) {
        return std::nullopt;
    }
    return Math::Penetration{axis, static_cast<float>(depth)};
}

std::optional<Math::Penetration> ComputeCapsuleSpherePenetration(
    const Math::Vector3 capsuleSegmentStart, const Math::Vector3 capsuleSegmentEnd,
    const float capsuleRadius, const Math::Vector3 sphereCenter, const float sphereRadius) noexcept {
    if (!std::isfinite(capsuleSegmentStart.x) || !std::isfinite(capsuleSegmentStart.y) ||
        !std::isfinite(capsuleSegmentStart.z) || !std::isfinite(capsuleSegmentEnd.x) ||
        !std::isfinite(capsuleSegmentEnd.y) || !std::isfinite(capsuleSegmentEnd.z) ||
        !std::isfinite(capsuleRadius) || capsuleRadius <= 0.0F || !std::isfinite(sphereCenter.x) ||
        !std::isfinite(sphereCenter.y) || !std::isfinite(sphereCenter.z) || !std::isfinite(sphereRadius) ||
        sphereRadius <= 0.0F) {
        return std::nullopt;
    }

    const double segmentX = static_cast<double>(capsuleSegmentEnd.x) -
                            static_cast<double>(capsuleSegmentStart.x);
    const double segmentY = static_cast<double>(capsuleSegmentEnd.y) -
                            static_cast<double>(capsuleSegmentStart.y);
    const double segmentZ = static_cast<double>(capsuleSegmentEnd.z) -
                            static_cast<double>(capsuleSegmentStart.z);
    const double segmentLengthSquared = segmentX * segmentX + segmentY * segmentY + segmentZ * segmentZ;
    if (!std::isfinite(segmentLengthSquared)) {
        return std::nullopt;
    }
    double segmentTime = 0.0;
    if (segmentLengthSquared > 0.0) {
        const double offsetX = static_cast<double>(sphereCenter.x) -
                               static_cast<double>(capsuleSegmentStart.x);
        const double offsetY = static_cast<double>(sphereCenter.y) -
                               static_cast<double>(capsuleSegmentStart.y);
        const double offsetZ = static_cast<double>(sphereCenter.z) -
                               static_cast<double>(capsuleSegmentStart.z);
        const double projection = (offsetX * segmentX + offsetY * segmentY + offsetZ * segmentZ) /
                                  segmentLengthSquared;
        if (!std::isfinite(projection)) {
            return std::nullopt;
        }
        segmentTime = std::clamp(projection, 0.0, 1.0);
    }

    const double closestX = static_cast<double>(capsuleSegmentStart.x) + segmentX * segmentTime;
    const double closestY = static_cast<double>(capsuleSegmentStart.y) + segmentY * segmentTime;
    const double closestZ = static_cast<double>(capsuleSegmentStart.z) + segmentZ * segmentTime;
    const double deltaX = static_cast<double>(sphereCenter.x) - closestX;
    const double deltaY = static_cast<double>(sphereCenter.y) - closestY;
    const double deltaZ = static_cast<double>(sphereCenter.z) - closestZ;
    const double scale = std::max(std::fabs(deltaX), std::max(std::fabs(deltaY), std::fabs(deltaZ)));
    if (!std::isfinite(scale)) {
        return std::nullopt;
    }
    double distanceSquared = 0.0;
    if (scale != 0.0) {
        const double scaledDistanceSquared = (deltaX / scale) * (deltaX / scale) +
                                             (deltaY / scale) * (deltaY / scale) +
                                             (deltaZ / scale) * (deltaZ / scale);
        if (!std::isfinite(scaledDistanceSquared)) {
            return std::nullopt;
        }
        distanceSquared = (scale * scale) * scaledDistanceSquared;
        if (!std::isfinite(distanceSquared)) {
            return std::nullopt;
        }
    }

    const double combinedRadius = static_cast<double>(capsuleRadius) + static_cast<double>(sphereRadius);
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(combinedRadius) || combinedRadius <= 0.0 || combinedRadius > maximumFloat) {
        return std::nullopt;
    }
    const double combinedRadiusSquared = combinedRadius * combinedRadius;
    if (!std::isfinite(combinedRadiusSquared) || distanceSquared >= combinedRadiusSquared) {
        return std::nullopt;
    }
    if (distanceSquared == 0.0) {
        return Math::Penetration{{1.0F, 0.0F, 0.0F}, static_cast<float>(combinedRadius)};
    }

    const double distance = std::sqrt(distanceSquared);
    const double depth = combinedRadius - distance;
    if (!std::isfinite(distance) || !std::isfinite(depth) || depth <= 0.0 || depth > maximumFloat) {
        return std::nullopt;
    }
    const double inverseDistance = 1.0 / distance;
    const Math::Vector3 axis{
        static_cast<float>(deltaX * inverseDistance),
        static_cast<float>(deltaY * inverseDistance),
        static_cast<float>(deltaZ * inverseDistance),
    };
    if (!Math::IsFinite(axis)) {
        return std::nullopt;
    }
    return Math::Penetration{axis, static_cast<float>(depth)};
}

std::optional<Math::Penetration> ComputeCapsuleCapsulePenetration(
    const Math::Vector3 firstSegmentStart, const Math::Vector3 firstSegmentEnd, const float firstRadius,
    const Math::Vector3 secondSegmentStart, const Math::Vector3 secondSegmentEnd,
    const float secondRadius) noexcept {
    if (!Math::IsFinite(firstSegmentStart) || !Math::IsFinite(firstSegmentEnd) ||
        !Math::IsFinite(secondSegmentStart) || !Math::IsFinite(secondSegmentEnd) ||
        !std::isfinite(firstRadius) || firstRadius <= 0.0F || !std::isfinite(secondRadius) || secondRadius <= 0.0F) {
        return std::nullopt;
    }

    const double firstDirectionX = static_cast<double>(firstSegmentEnd.x) - static_cast<double>(firstSegmentStart.x);
    const double firstDirectionY = static_cast<double>(firstSegmentEnd.y) - static_cast<double>(firstSegmentStart.y);
    const double firstDirectionZ = static_cast<double>(firstSegmentEnd.z) - static_cast<double>(firstSegmentStart.z);
    const double secondDirectionX = static_cast<double>(secondSegmentEnd.x) - static_cast<double>(secondSegmentStart.x);
    const double secondDirectionY = static_cast<double>(secondSegmentEnd.y) - static_cast<double>(secondSegmentStart.y);
    const double secondDirectionZ = static_cast<double>(secondSegmentEnd.z) - static_cast<double>(secondSegmentStart.z);
    const double firstLengthSquared = firstDirectionX * firstDirectionX + firstDirectionY * firstDirectionY +
                                      firstDirectionZ * firstDirectionZ;
    const double secondLengthSquared = secondDirectionX * secondDirectionX + secondDirectionY * secondDirectionY +
                                       secondDirectionZ * secondDirectionZ;
    const double directionDot = firstDirectionX * secondDirectionX + firstDirectionY * secondDirectionY +
                                firstDirectionZ * secondDirectionZ;
    const double offsetX = static_cast<double>(secondSegmentStart.x) - static_cast<double>(firstSegmentStart.x);
    const double offsetY = static_cast<double>(secondSegmentStart.y) - static_cast<double>(firstSegmentStart.y);
    const double offsetZ = static_cast<double>(secondSegmentStart.z) - static_cast<double>(firstSegmentStart.z);
    const double firstOffsetDot = firstDirectionX * offsetX + firstDirectionY * offsetY + firstDirectionZ * offsetZ;
    const double secondOffsetDot = secondDirectionX * offsetX + secondDirectionY * offsetY + secondDirectionZ * offsetZ;
    if (!std::isfinite(firstLengthSquared) || !std::isfinite(secondLengthSquared) || !std::isfinite(directionDot) ||
        !std::isfinite(firstOffsetDot) || !std::isfinite(secondOffsetDot)) {
        return std::nullopt;
    }

    double bestFirstTime = 0.0;
    double bestSecondTime = 0.0;
    double bestDistanceSquared = std::numeric_limits<double>::infinity();
    double bestDeltaX = 0.0;
    double bestDeltaY = 0.0;
    double bestDeltaZ = 0.0;
    const auto ConsiderPair = [&](const double firstTime, const double secondTime) {
        const double firstPointX = static_cast<double>(firstSegmentStart.x) + firstDirectionX * firstTime;
        const double firstPointY = static_cast<double>(firstSegmentStart.y) + firstDirectionY * firstTime;
        const double firstPointZ = static_cast<double>(firstSegmentStart.z) + firstDirectionZ * firstTime;
        const double secondPointX = static_cast<double>(secondSegmentStart.x) + secondDirectionX * secondTime;
        const double secondPointY = static_cast<double>(secondSegmentStart.y) + secondDirectionY * secondTime;
        const double secondPointZ = static_cast<double>(secondSegmentStart.z) + secondDirectionZ * secondTime;
        const double deltaX = secondPointX - firstPointX;
        const double deltaY = secondPointY - firstPointY;
        const double deltaZ = secondPointZ - firstPointZ;
        const double scale = std::max(std::fabs(deltaX), std::max(std::fabs(deltaY), std::fabs(deltaZ)));
        if (!std::isfinite(scale)) {
            return;
        }
        double distanceSquared = 0.0;
        if (scale != 0.0) {
            const double scaledDistanceSquared = (deltaX / scale) * (deltaX / scale) +
                                                 (deltaY / scale) * (deltaY / scale) +
                                                 (deltaZ / scale) * (deltaZ / scale);
            if (!std::isfinite(scaledDistanceSquared)) {
                return;
            }
            distanceSquared = (scale * scale) * scaledDistanceSquared;
            if (!std::isfinite(distanceSquared)) {
                return;
            }
        }
        if (distanceSquared < bestDistanceSquared) {
            bestDistanceSquared = distanceSquared;
            bestFirstTime = firstTime;
            bestSecondTime = secondTime;
            bestDeltaX = deltaX;
            bestDeltaY = deltaY;
            bestDeltaZ = deltaZ;
        }
    };
    const auto ClosestSecondTime = [&](const double pointX, const double pointY, const double pointZ) {
        if (secondLengthSquared <= 0.0) {
            return 0.0;
        }
        const double relativeX = pointX - static_cast<double>(secondSegmentStart.x);
        const double relativeY = pointY - static_cast<double>(secondSegmentStart.y);
        const double relativeZ = pointZ - static_cast<double>(secondSegmentStart.z);
        return std::clamp((relativeX * secondDirectionX + relativeY * secondDirectionY +
                           relativeZ * secondDirectionZ) /
                              secondLengthSquared,
                          0.0, 1.0);
    };
    const auto ClosestFirstTime = [&](const double pointX, const double pointY, const double pointZ) {
        if (firstLengthSquared <= 0.0) {
            return 0.0;
        }
        const double relativeX = pointX - static_cast<double>(firstSegmentStart.x);
        const double relativeY = pointY - static_cast<double>(firstSegmentStart.y);
        const double relativeZ = pointZ - static_cast<double>(firstSegmentStart.z);
        return std::clamp((relativeX * firstDirectionX + relativeY * firstDirectionY +
                           relativeZ * firstDirectionZ) /
                              firstLengthSquared,
                          0.0, 1.0);
    };

    ConsiderPair(0.0, ClosestSecondTime(firstSegmentStart.x, firstSegmentStart.y, firstSegmentStart.z));
    ConsiderPair(1.0, ClosestSecondTime(firstSegmentEnd.x, firstSegmentEnd.y, firstSegmentEnd.z));
    ConsiderPair(ClosestFirstTime(secondSegmentStart.x, secondSegmentStart.y, secondSegmentStart.z), 0.0);
    ConsiderPair(ClosestFirstTime(secondSegmentEnd.x, secondSegmentEnd.y, secondSegmentEnd.z), 1.0);

    const double denominator = firstLengthSquared * secondLengthSquared - directionDot * directionDot;
    constexpr double kParallelEpsilon = 1.0e-12;
    if (denominator > kParallelEpsilon) {
        const double firstTime = (directionDot * secondOffsetDot - secondLengthSquared * firstOffsetDot) /
                                 denominator;
        const double secondTime = (firstLengthSquared * secondOffsetDot - directionDot * firstOffsetDot) /
                                  denominator;
        if (std::isfinite(firstTime) && std::isfinite(secondTime) && firstTime >= 0.0 && firstTime <= 1.0 &&
            secondTime >= 0.0 && secondTime <= 1.0) {
            ConsiderPair(firstTime, secondTime);
        }
    }
    if (!std::isfinite(bestDistanceSquared)) {
        return std::nullopt;
    }

    const double combinedRadius = static_cast<double>(firstRadius) + static_cast<double>(secondRadius);
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(combinedRadius) || combinedRadius <= 0.0 || combinedRadius > maximumFloat) {
        return std::nullopt;
    }
    const double combinedRadiusSquared = combinedRadius * combinedRadius;
    if (!std::isfinite(combinedRadiusSquared) || bestDistanceSquared >= combinedRadiusSquared) {
        return std::nullopt;
    }
    if (bestDistanceSquared == 0.0) {
        return Math::Penetration{{1.0F, 0.0F, 0.0F}, static_cast<float>(combinedRadius)};
    }

    const double distance = std::sqrt(bestDistanceSquared);
    const double depth = combinedRadius - distance;
    if (!std::isfinite(distance) || !std::isfinite(depth) || depth <= 0.0 || depth > maximumFloat) {
        return std::nullopt;
    }
    const double inverseDistance = 1.0 / distance;
    const Math::Vector3 axis{
        static_cast<float>(bestDeltaX * inverseDistance),
        static_cast<float>(bestDeltaY * inverseDistance),
        static_cast<float>(bestDeltaZ * inverseDistance),
    };
    if (!Math::IsFinite(axis)) {
        return std::nullopt;
    }
    return Math::Penetration{axis, static_cast<float>(depth)};
}

std::optional<Math::Penetration> ComputeSphereOrientedBoxPenetration(
    const Math::Vector3 boxCenter, const Math::Vector3 boxHalfExtents,
    const Math::Quaternion boxRotation, const Math::Vector3 sphereCenter,
    const float sphereRadius) noexcept {
    if (!Math::IsFinite(boxCenter) || !Math::IsFinite(sphereCenter) || !std::isfinite(sphereRadius) ||
        sphereRadius <= 0.0F) {
        return std::nullopt;
    }
    Math::Quaternion normalizedRotation;
    Math::Quaternion inverseRotation;
    if (!TryBuildOrientedBoxFrame(boxHalfExtents, boxRotation, normalizedRotation, inverseRotation)) {
        return std::nullopt;
    }

    const double centerDeltaX = static_cast<double>(sphereCenter.x) - static_cast<double>(boxCenter.x);
    const double centerDeltaY = static_cast<double>(sphereCenter.y) - static_cast<double>(boxCenter.y);
    const double centerDeltaZ = static_cast<double>(sphereCenter.z) - static_cast<double>(boxCenter.z);
    const double centerScale = std::max(1.0, std::max(std::fabs(centerDeltaX),
                                                      std::max(std::fabs(centerDeltaY), std::fabs(centerDeltaZ))));
    if (!std::isfinite(centerScale)) {
        return std::nullopt;
    }
    const Math::Vector3 scaledSphereOffset{
        static_cast<float>(centerDeltaX / centerScale),
        static_cast<float>(centerDeltaY / centerScale),
        static_cast<float>(centerDeltaZ / centerScale),
    };
    const Math::Vector3 scaledHalfExtents{
        static_cast<float>(static_cast<double>(boxHalfExtents.x) / centerScale),
        static_cast<float>(static_cast<double>(boxHalfExtents.y) / centerScale),
        static_cast<float>(static_cast<double>(boxHalfExtents.z) / centerScale),
    };
    const float scaledSphereRadius = static_cast<float>(static_cast<double>(sphereRadius) / centerScale);
    if (!Math::IsFinite(scaledSphereOffset) || !Math::IsFinite(scaledHalfExtents) ||
        !std::isfinite(scaledSphereRadius) || scaledSphereRadius <= 0.0F ||
        scaledHalfExtents.x <= 0.0F || scaledHalfExtents.y <= 0.0F || scaledHalfExtents.z <= 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 localSphereCenter = Math::RotateVector(inverseRotation, scaledSphereOffset);
    const Math::Aabb localBox =
        Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, scaledHalfExtents);
    const std::optional<Math::Penetration> localPenetration =
        ComputeSphereAabbPenetration(localBox,
                                           Math::Sphere{localSphereCenter, scaledSphereRadius});
    if (!localPenetration.has_value()) {
        return std::nullopt;
    }
    const double worldDepth = static_cast<double>(localPenetration->depth) * centerScale;
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    const Math::Vector3 worldAxis = Math::RotateVector(normalizedRotation, localPenetration->axis);
    if (!std::isfinite(worldDepth) || worldDepth <= 0.0 || worldDepth > maximumFloat ||
        !Math::IsFinite(worldAxis)) {
        return std::nullopt;
    }
    return Math::Penetration{worldAxis, static_cast<float>(worldDepth)};
}

std::optional<Math::Penetration> ComputeCapsuleOrientedBoxPenetration(
    const Math::Vector3 boxCenter, const Math::Vector3 boxHalfExtents,
    const Math::Quaternion boxRotation, const Math::Vector3 capsuleSegmentStart,
    const Math::Vector3 capsuleSegmentEnd, const float capsuleRadius) noexcept {
    if (!Math::IsFinite(boxCenter) || !Math::IsFinite(capsuleSegmentStart) ||
        !Math::IsFinite(capsuleSegmentEnd) || !std::isfinite(capsuleRadius) || capsuleRadius <= 0.0F) {
        return std::nullopt;
    }
    Math::Quaternion normalizedRotation;
    Math::Quaternion inverseRotation;
    if (!TryBuildOrientedBoxFrame(boxHalfExtents, boxRotation, normalizedRotation, inverseRotation)) {
        return std::nullopt;
    }

    const double startOffsetX = static_cast<double>(capsuleSegmentStart.x) - static_cast<double>(boxCenter.x);
    const double startOffsetY = static_cast<double>(capsuleSegmentStart.y) - static_cast<double>(boxCenter.y);
    const double startOffsetZ = static_cast<double>(capsuleSegmentStart.z) - static_cast<double>(boxCenter.z);
    const double endOffsetX = static_cast<double>(capsuleSegmentEnd.x) - static_cast<double>(boxCenter.x);
    const double endOffsetY = static_cast<double>(capsuleSegmentEnd.y) - static_cast<double>(boxCenter.y);
    const double endOffsetZ = static_cast<double>(capsuleSegmentEnd.z) - static_cast<double>(boxCenter.z);
    const double geometryScale = std::max(
        1.0,
        std::max(std::fabs(startOffsetX),
                 std::max(std::fabs(startOffsetY),
                          std::max(std::fabs(startOffsetZ),
                                   std::max(std::fabs(endOffsetX),
                                            std::max(std::fabs(endOffsetY),
                                                     std::max(std::fabs(endOffsetZ),
                                                              std::max(static_cast<double>(boxHalfExtents.x),
                                                                       std::max(static_cast<double>(boxHalfExtents.y),
                                                                                std::max(static_cast<double>(boxHalfExtents.z),
                                                                                         static_cast<double>(capsuleRadius)))))))))));
    if (!std::isfinite(geometryScale)) {
        return std::nullopt;
    }
    const Math::Vector3 scaledSegmentStart{
        static_cast<float>(startOffsetX / geometryScale),
        static_cast<float>(startOffsetY / geometryScale),
        static_cast<float>(startOffsetZ / geometryScale),
    };
    const Math::Vector3 scaledSegmentEnd{
        static_cast<float>(endOffsetX / geometryScale),
        static_cast<float>(endOffsetY / geometryScale),
        static_cast<float>(endOffsetZ / geometryScale),
    };
    const Math::Vector3 scaledHalfExtents{
        static_cast<float>(static_cast<double>(boxHalfExtents.x) / geometryScale),
        static_cast<float>(static_cast<double>(boxHalfExtents.y) / geometryScale),
        static_cast<float>(static_cast<double>(boxHalfExtents.z) / geometryScale),
    };
    const float scaledCapsuleRadius = static_cast<float>(static_cast<double>(capsuleRadius) / geometryScale);
    if (!Math::IsFinite(scaledSegmentStart) || !Math::IsFinite(scaledSegmentEnd) ||
        !Math::IsFinite(scaledHalfExtents) || !std::isfinite(scaledCapsuleRadius) ||
        scaledCapsuleRadius <= 0.0F || scaledHalfExtents.x <= 0.0F || scaledHalfExtents.y <= 0.0F ||
        scaledHalfExtents.z <= 0.0F) {
        return std::nullopt;
    }
    const Math::Vector3 localSegmentStart = Math::RotateVector(
        inverseRotation, scaledSegmentStart);
    const Math::Vector3 localSegmentEnd = Math::RotateVector(inverseRotation, scaledSegmentEnd);
    const Math::Aabb localBox =
        Math::Aabb::FromCenterExtents({0.0F, 0.0F, 0.0F}, scaledHalfExtents);
    const std::optional<Math::Penetration> localPenetration = ComputeCapsuleAabbPenetration(
        localBox, localSegmentStart, localSegmentEnd, scaledCapsuleRadius);
    if (!localPenetration.has_value()) {
        return std::nullopt;
    }
    const double worldDepth = static_cast<double>(localPenetration->depth) * geometryScale;
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    const Math::Vector3 worldAxis = Math::RotateVector(normalizedRotation, localPenetration->axis);
    if (!std::isfinite(worldDepth) || worldDepth <= 0.0 || worldDepth > maximumFloat ||
        !Math::IsFinite(worldAxis)) {
        return std::nullopt;
    }
    return Math::Penetration{worldAxis, static_cast<float>(worldDepth)};
}

std::optional<Math::Penetration> ComputeOrientedBoxOrientedBoxPenetration(
    const Math::Vector3 firstCenter, const Math::Vector3 firstHalfExtents,
    const Math::Quaternion firstRotation, const Math::Vector3 secondCenter,
    const Math::Vector3 secondHalfExtents, const Math::Quaternion secondRotation) noexcept {
    if (!Math::IsFinite(firstCenter) || !Math::IsFinite(secondCenter)) {
        return std::nullopt;
    }
    Math::Quaternion normalizedFirstRotation;
    Math::Quaternion inverseFirstRotation;
    Math::Quaternion normalizedSecondRotation;
    Math::Quaternion inverseSecondRotation;
    if (!TryBuildOrientedBoxFrame(firstHalfExtents, firstRotation, normalizedFirstRotation,
                                     inverseFirstRotation) ||
        !TryBuildOrientedBoxFrame(secondHalfExtents, secondRotation, normalizedSecondRotation,
                                     inverseSecondRotation)) {
        return std::nullopt;
    }

    const std::array<Math::Vector3, 3> firstAxes{
        Math::RotateVector(normalizedFirstRotation, {1.0F, 0.0F, 0.0F}),
        Math::RotateVector(normalizedFirstRotation, {0.0F, 1.0F, 0.0F}),
        Math::RotateVector(normalizedFirstRotation, {0.0F, 0.0F, 1.0F}),
    };
    const std::array<Math::Vector3, 3> secondAxes{
        Math::RotateVector(normalizedSecondRotation, {1.0F, 0.0F, 0.0F}),
        Math::RotateVector(normalizedSecondRotation, {0.0F, 1.0F, 0.0F}),
        Math::RotateVector(normalizedSecondRotation, {0.0F, 0.0F, 1.0F}),
    };
    const double centerDeltaX = static_cast<double>(secondCenter.x) - static_cast<double>(firstCenter.x);
    const double centerDeltaY = static_cast<double>(secondCenter.y) - static_cast<double>(firstCenter.y);
    const double centerDeltaZ = static_cast<double>(secondCenter.z) - static_cast<double>(firstCenter.z);
    if (!std::isfinite(centerDeltaX) || !std::isfinite(centerDeltaY) || !std::isfinite(centerDeltaZ)) {
        return std::nullopt;
    }
    constexpr double kAxisEpsilon = 1.0e-6;
    (void)inverseFirstRotation;
    (void)inverseSecondRotation;

    double minimumOverlap = std::numeric_limits<double>::infinity();
    Math::Vector3 minimumAxis{1.0F, 0.0F, 0.0F};
    const auto TestAxis = [&](Math::Vector3 axis) noexcept {
        const double axisLengthSquared = static_cast<double>(axis.x) * static_cast<double>(axis.x) +
                                         static_cast<double>(axis.y) * static_cast<double>(axis.y) +
                                         static_cast<double>(axis.z) * static_cast<double>(axis.z);
        if (!std::isfinite(axisLengthSquared)) {
            return false;
        }
        if (axisLengthSquared <= kAxisEpsilon * kAxisEpsilon) {
            return true;
        }
        const double inverseAxisLength = 1.0 / std::sqrt(axisLengthSquared);
        axis = {
            static_cast<float>(static_cast<double>(axis.x) * inverseAxisLength),
            static_cast<float>(static_cast<double>(axis.y) * inverseAxisLength),
            static_cast<float>(static_cast<double>(axis.z) * inverseAxisLength),
        };
        const double firstRadius =
            std::fabs(static_cast<double>(Math::Dot(axis, firstAxes[0]))) * static_cast<double>(firstHalfExtents.x) +
            std::fabs(static_cast<double>(Math::Dot(axis, firstAxes[1]))) * static_cast<double>(firstHalfExtents.y) +
            std::fabs(static_cast<double>(Math::Dot(axis, firstAxes[2]))) * static_cast<double>(firstHalfExtents.z);
        const double secondRadius =
            std::fabs(static_cast<double>(Math::Dot(axis, secondAxes[0]))) * static_cast<double>(secondHalfExtents.x) +
            std::fabs(static_cast<double>(Math::Dot(axis, secondAxes[1]))) * static_cast<double>(secondHalfExtents.y) +
            std::fabs(static_cast<double>(Math::Dot(axis, secondAxes[2]))) * static_cast<double>(secondHalfExtents.z);
        const double centerProjection = centerDeltaX * static_cast<double>(axis.x) +
                                        centerDeltaY * static_cast<double>(axis.y) +
                                        centerDeltaZ * static_cast<double>(axis.z);
        const double overlap = firstRadius + secondRadius - std::fabs(centerProjection);
        if (!std::isfinite(firstRadius) || !std::isfinite(secondRadius) || !std::isfinite(overlap) || overlap <= 0.0) {
            return false;
        }
        if (overlap < minimumOverlap) {
            minimumOverlap = overlap;
            minimumAxis = centerProjection < 0.0 ? -axis : axis;
        }
        return true;
    };

    for (const Math::Vector3 axis : firstAxes) {
        if (!TestAxis(axis)) {
            return std::nullopt;
        }
    }
    for (const Math::Vector3 axis : secondAxes) {
        if (!TestAxis(axis)) {
            return std::nullopt;
        }
    }
    for (const Math::Vector3 firstAxis : firstAxes) {
        for (const Math::Vector3 secondAxis : secondAxes) {
            if (!TestAxis(Math::Cross(firstAxis, secondAxis))) {
                return std::nullopt;
            }
        }
    }
    const double maximumFloat = static_cast<double>(std::numeric_limits<float>::max());
    if (!std::isfinite(minimumOverlap) || minimumOverlap > maximumFloat || !Math::IsFinite(minimumAxis)) {
        return std::nullopt;
    }
    return Math::Penetration{minimumAxis, static_cast<float>(minimumOverlap)};
}

} // namespace UVE::Physics::Detail

// EOF
