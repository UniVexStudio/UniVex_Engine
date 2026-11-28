// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ragdoll_world.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>

#include "shape_narrow_phase.h"
#include "physics_material.h"

namespace UVE::Physics {
namespace {

constexpr float kMinimumBodyRadius = 1.0e-4F;
constexpr float kMovingColliderWakeSpeedSq = 0.04F * 0.04F;
constexpr float kFloorSupportNormalY = 0.35F;

[[nodiscard]] Math::Vector3 Negate(const Math::Vector3& value) noexcept {
    return Math::Vector3{-value.x, -value.y, -value.z};
}

[[nodiscard]] bool AabbOverlaps(const Math::Vector3& minA, const Math::Vector3& maxA,
                                   const Math::Aabb& b) noexcept {
    return !(maxA.x < b.min.x || minA.x > b.max.x ||
             maxA.y < b.min.y || minA.y > b.max.y ||
             maxA.z < b.min.z || minA.z > b.max.z);
}

[[nodiscard]] Math::Vector3 ClosestPointOnSegment(const Math::Vector3& segmentStart,
                                                        const Math::Vector3& segmentEnd,
                                                        const Math::Vector3& target) noexcept {
    const Math::Vector3 direction = segmentEnd - segmentStart;
    const float lengthSquared = Math::LengthSquared(direction);
    if (!(lengthSquared > 1.0e-12F)) return segmentStart;
    const float projection = Math::Dot(target - segmentStart, direction) / lengthSquared;
    return segmentStart + direction * std::clamp(projection, 0.0F, 1.0F);
}

/// Closest points between two finite segments (Ericson, Real-Time Collision Detection §5.1.9).
void ClosestPointsBetweenSegments(const Math::Vector3& p1, const Math::Vector3& q1,
                                     const Math::Vector3& p2, const Math::Vector3& q2,
                                     Math::Vector3& outC1, Math::Vector3& outC2) noexcept {
    const Math::Vector3 d1 = q1 - p1;
    const Math::Vector3 d2 = q2 - p2;
    const Math::Vector3 r = p1 - p2;
    const float a = Math::Dot(d1, d1);
    const float e = Math::Dot(d2, d2);
    const float f = Math::Dot(d2, r);
    float s = 0.0F;
    float t = 0.0F;

    if (a <= 1.0e-12F && e <= 1.0e-12F) {
        outC1 = p1;
        outC2 = p2;
        return;
    }
    if (a <= 1.0e-12F) {
        s = 0.0F;
        t = std::clamp(f / e, 0.0F, 1.0F);
    } else {
        const float c = Math::Dot(d1, r);
        if (e <= 1.0e-12F) {
            t = 0.0F;
            s = std::clamp(-c / a, 0.0F, 1.0F);
        } else {
            const float b = Math::Dot(d1, d2);
            const float denom = a * e - b * b;
            if (denom > 1.0e-12F) {
                s = std::clamp((b * f - c * e) / denom, 0.0F, 1.0F);
            } else {
                s = 0.0F;
            }
            t = (b * s + f) / e;
            if (t < 0.0F) {
                t = 0.0F;
                s = std::clamp(-c / a, 0.0F, 1.0F);
            } else if (t > 1.0F) {
                t = 1.0F;
                s = std::clamp((b - c) / a, 0.0F, 1.0F);
            }
        }
    }
    outC1 = p1 + d1 * s;
    outC2 = p2 + d2 * t;
}

/// Signed penetration depth of a sphere (center `p`, radius `radius`) against a centered AABB `[-h, +h]`.
/// Positive means penetrating; negative means separated by `-depth`.
[[nodiscard]] float SphereDepthInLocalBox(const Math::Vector3& p, const Math::Vector3& halfExtents,
                                             const float radius) noexcept {
    const Math::Vector3 clamped{
        std::clamp(p.x, -halfExtents.x, halfExtents.x),
        std::clamp(p.y, -halfExtents.y, halfExtents.y),
        std::clamp(p.z, -halfExtents.z, halfExtents.z),
    };
    const Math::Vector3 diff = p - clamped;
    const float distSq = Math::LengthSquared(diff);
    if (distSq > 0.0F) {
        return radius - std::sqrt(distSq);
    }
    // Sphere center is strictly inside the box: distance to nearest face plus radius.
    const float dx = halfExtents.x - std::fabs(p.x);
    const float dy = halfExtents.y - std::fabs(p.y);
    const float dz = halfExtents.z - std::fabs(p.z);
    return radius + std::min({dx, dy, dz});
}

/// Finds the exact deepest point(s) along a capsule segment against a box in box-local space.
/// When both endpoints penetrate (e.g. a horizontal limb resting on a floor or ramp), returns a
/// 2-point manifold (`outCount = 2`) to prevent single-point capsule rocking and speed up sleeping.
void ComputeCapsuleLocalBoxContactPoints(const Math::Vector3& localStart, const Math::Vector3& localEnd,
                                            const Math::Vector3& halfExtents, const float radius,
                                            const float narrowPhaseDepth,
                                            std::array<float, 2U>& outT, std::array<float, 2U>& outDepth,
                                            std::size_t& outCount) noexcept {
    outCount = 0U;
    const float depthStart = SphereDepthInLocalBox(localStart, halfExtents, radius);
    const float depthEnd = SphereDepthInLocalBox(localEnd, halfExtents, radius);
    const float segLenSq = Math::LengthSquared(localEnd - localStart);

    // 2-point flat/shallow-angle manifold when both capsule endpoints penetrate the box.
    if (segLenSq > 1.0e-6F && depthStart > 0.0F && depthEnd > 0.0F) {
        outT[0] = 0.0F;
        outDepth[0] = depthStart;
        outT[1] = 1.0F;
        outDepth[1] = depthEnd;
        outCount = 2U;
        return;
    }

    // Golden-section maximization of signed depth along the convex distance function on [0, 1].
    float lo = 0.0F;
    float hi = 1.0F;
    constexpr float kInvPhi = 0.61803398875F;
    constexpr float kInvPhi2 = 0.38196601125F;
    float t1 = lo + kInvPhi2 * (hi - lo);
    float t2 = lo + kInvPhi * (hi - lo);
    const Math::Vector3 delta = localEnd - localStart;
    float f1 = SphereDepthInLocalBox(localStart + delta * t1, halfExtents, radius);
    float f2 = SphereDepthInLocalBox(localStart + delta * t2, halfExtents, radius);
    for (int iter = 0; iter < 12; ++iter) {
        if (f1 < f2) {
            lo = t1;
            t1 = t2;
            f1 = f2;
            t2 = lo + kInvPhi * (hi - lo);
            f2 = SphereDepthInLocalBox(localStart + delta * t2, halfExtents, radius);
        } else {
            hi = t2;
            t2 = t1;
            f2 = f1;
            t1 = lo + kInvPhi2 * (hi - lo);
            f1 = SphereDepthInLocalBox(localStart + delta * t1, halfExtents, radius);
        }
    }

    float bestT = 0.5F * (lo + hi);
    float bestDepth = SphereDepthInLocalBox(localStart + delta * bestT, halfExtents, radius);
    if (depthStart >= bestDepth && depthStart >= depthEnd) {
        bestT = 0.0F;
        bestDepth = depthStart;
    } else if (depthEnd >= bestDepth) {
        bestT = 1.0F;
        bestDepth = depthEnd;
    }

    outT[0] = bestT;
    outDepth[0] = std::max(bestDepth, narrowPhaseDepth);
    outCount = 1U;
}

} // namespace

void BuildRagdollBroadphaseCandidates(const RagdollSolver& solver,
                                         const std::span<const Detail::ColliderWorldAabb> colliders,
                                         const RagdollCollisionFilter& filter, const float expandMargin,
                                         std::vector<std::size_t>& outCandidateIndices) {
    outCandidateIndices.clear();
    const std::size_t bodyCount = solver.GetBodyCount();
    if (bodyCount == 0U || colliders.empty()) return;

    const float margin = std::isfinite(expandMargin) && expandMargin > 0.0F ? expandMargin : 0.0F;
    Math::Vector3 ragdollMin{std::numeric_limits<float>::infinity(), std::numeric_limits<float>::infinity(),
                                std::numeric_limits<float>::infinity()};
    Math::Vector3 ragdollMax{-std::numeric_limits<float>::infinity(), -std::numeric_limits<float>::infinity(),
                                -std::numeric_limits<float>::infinity()};

    bool hasValidBody = false;
    for (std::size_t i = 0U; i < bodyCount; ++i) {
        const RagdollBody& body = solver.GetBody(i);
        if (!std::isfinite(body.radius) || body.radius < kMinimumBodyRadius) continue;
        Math::Vector3 segStart{};
        Math::Vector3 segEnd{};
        ComputeRagdollCapsuleWorldSegment(body, segStart, segEnd);
        if (!Math::IsFinite(segStart) || !Math::IsFinite(segEnd)) continue;
        const float r = body.radius + margin;
        ragdollMin.x = std::min({ragdollMin.x, segStart.x - r, segEnd.x - r});
        ragdollMin.y = std::min({ragdollMin.y, segStart.y - r, segEnd.y - r});
        ragdollMin.z = std::min({ragdollMin.z, segStart.z - r, segEnd.z - r});
        ragdollMax.x = std::max({ragdollMax.x, segStart.x + r, segEnd.x + r});
        ragdollMax.y = std::max({ragdollMax.y, segStart.y + r, segEnd.y + r});
        ragdollMax.z = std::max({ragdollMax.z, segStart.z + r, segEnd.z + r});
        hasValidBody = true;
    }
    if (!hasValidBody) return;

    for (std::size_t idx = 0U; idx < colliders.size(); ++idx) {
        const Detail::ColliderWorldAabb& collider = colliders[idx];
        if (collider.entity == filter.ignoreEntity || collider.entity == filter.alsoIgnoreEntity) continue;
        if ((filter.layerMask & collider.collisionMask) == 0U) continue;
        if (!AabbOverlaps(ragdollMin, ragdollMax, collider.worldAabb)) continue;
        outCandidateIndices.push_back(idx);
    }
}

RagdollContactStats BuildRagdollContacts(const RagdollSolver& solver,
                                               const std::span<const Detail::ColliderWorldAabb> colliders,
                                               const RagdollCollisionFilter& filter,
                                               const std::span<RagdollContact> outContacts,
                                               const std::span<const RagdollColliderMotion> colliderMotions,
                                               const std::span<const std::size_t> candidateIndices,
                                               const std::optional<float> floorPlaneY) noexcept {
    RagdollContactStats stats;
    const std::size_t bodyCount = solver.GetBodyCount();
    const bool hasFloorPlane = floorPlaneY.has_value() && std::isfinite(*floorPlaneY);
    if (outContacts.empty() || bodyCount == 0U || (colliders.empty() && !hasFloorPlane)) return stats;

    const bool useCandidates = !candidateIndices.empty();
    const std::size_t loopCount = useCandidates ? candidateIndices.size() : colliders.size();

    for (std::size_t bodyIndex = 0U; bodyIndex < bodyCount; ++bodyIndex) {
        const RagdollBody& body = solver.GetBody(bodyIndex);
        if (!std::isfinite(body.radius) || body.radius < kMinimumBodyRadius) continue;
        Math::Vector3 segmentStart{};
        Math::Vector3 segmentEnd{};
        ComputeRagdollCapsuleWorldSegment(body, segmentStart, segmentEnd);
        if (!Math::IsFinite(segmentStart) || !Math::IsFinite(segmentEnd)) continue;

        if (hasFloorPlane) {
            const float planeY = *floorPlaneY;
            const auto emitFloorPoint = [&](const Math::Vector3& pt, const float depth) -> bool {
                RagdollContact contact;
                contact.body = static_cast<std::uint32_t>(bodyIndex);
                contact.point = Math::Vector3{pt.x, planeY, pt.z};
                contact.normal = Math::Vector3{0.0F, 1.0F, 0.0F};
                contact.depth = depth;
                contact.friction = std::clamp(body.friction, 0.0F, 1.0F);
                contact.restitution = std::clamp(body.restitution, 0.0F, 1.0F);
                contact.otherBody = kInvalidRagdollBody;
                stats.hasFloorSupport = true;
                outContacts[stats.generatedContactCount++] = contact;
                if (stats.generatedContactCount >= outContacts.size()) {
                    stats.truncated = true;
                    return false;
                }
                return true;
            };
            const float depthStart = planeY - (segmentStart.y - body.radius);
            const float depthEnd = planeY - (segmentEnd.y - body.radius);
            if (depthStart > 0.0F && !emitFloorPoint(segmentStart, depthStart)) {
                return stats;
            }
            if (depthEnd > 0.0F && Math::LengthSquared(segmentEnd - segmentStart) > 1.0e-6F &&
                !emitFloorPoint(segmentEnd, depthEnd)) {
                return stats;
            }
        }

        if (colliders.empty()) continue;

        const Math::Vector3 bodyMin{
            std::min(segmentStart.x, segmentEnd.x) - body.radius,
            std::min(segmentStart.y, segmentEnd.y) - body.radius,
            std::min(segmentStart.z, segmentEnd.z) - body.radius,
        };
        const Math::Vector3 bodyMax{
            std::max(segmentStart.x, segmentEnd.x) + body.radius,
            std::max(segmentStart.y, segmentEnd.y) + body.radius,
            std::max(segmentStart.z, segmentEnd.z) + body.radius,
        };

        for (std::size_t listIndex = 0U; listIndex < loopCount; ++listIndex) {
            const std::size_t colliderIndex = useCandidates ? candidateIndices[listIndex] : listIndex;
            if (colliderIndex >= colliders.size()) continue;
            const Detail::ColliderWorldAabb& collider = colliders[colliderIndex];
            if (collider.entity == filter.ignoreEntity || collider.entity == filter.alsoIgnoreEntity) continue;
            if ((filter.layerMask & collider.collisionMask) == 0U) continue;

            // Per-body AABB broadphase pre-rejection.
            if (!AabbOverlaps(bodyMin, bodyMax, collider.worldAabb)) {
                ++stats.broadphaseRejectedCount;
                continue;
            }
            ++stats.testedColliderCount;

            const Math::Vector3 colliderCenter = collider.worldAabb.GetCenter();
            std::optional<Math::Penetration> penetration;
            switch (collider.shapeType) {
            case Scene::ColliderShapeType::Sphere:
                penetration = Detail::ComputeCapsuleSpherePenetration(segmentStart, segmentEnd, body.radius,
                                                                         colliderCenter, collider.shapeRadius);
                break;
            case Scene::ColliderShapeType::Capsule:
                penetration = Detail::ComputeCapsuleCapsulePenetration(segmentStart, segmentEnd, body.radius,
                                                                          collider.shapeSegmentStart,
                                                                          collider.shapeSegmentEnd,
                                                                          collider.shapeRadius);
                break;
            case Scene::ColliderShapeType::Box:
                penetration = Detail::ComputeCapsuleOrientedBoxPenetration(
                    colliderCenter, collider.shapeHalfExtents, collider.shapeRotation, segmentStart, segmentEnd,
                    body.radius);
                break;
            case Scene::ColliderShapeType::Cylinder:
                penetration = Detail::ComputeCapsuleAabbPenetration(collider.worldAabb, segmentStart, segmentEnd,
                                                                       body.radius);
                break;
            }
            if (!penetration.has_value() || !(penetration->depth > 0.0F)) continue;
            if (!Math::IsFinite(penetration->axis)) continue;

            // The narrow phase reports "capsule -> other"; the solver needs "push the body out".
            const Math::Vector3 normal = Negate(penetration->axis);
            const PhysicsMaterial combined =
                CombineMaterials(PhysicsMaterial{body.friction, body.restitution, 1.0F}, collider.material);

            // Exact contact point(s) on the capsule axis (fixes fake world-origin torque on large boxes!).
            std::array<Math::Vector3, 2U> axisPoints{};
            std::array<float, 2U> contactDepths{};
            std::size_t manifoldCount = 0U;

            switch (collider.shapeType) {
            case Scene::ColliderShapeType::Sphere: {
                axisPoints[0] = ClosestPointOnSegment(segmentStart, segmentEnd, colliderCenter);
                contactDepths[0] = penetration->depth;
                manifoldCount = 1U;
                break;
            }
            case Scene::ColliderShapeType::Capsule: {
                Math::Vector3 cBody{};
                Math::Vector3 cOther{};
                ClosestPointsBetweenSegments(segmentStart, segmentEnd, collider.shapeSegmentStart,
                                                collider.shapeSegmentEnd, cBody, cOther);
                axisPoints[0] = cBody;
                contactDepths[0] = penetration->depth;
                manifoldCount = 1U;
                break;
            }
            case Scene::ColliderShapeType::Box: {
                const Math::Quaternion invBoxRot = Math::Conjugate(collider.shapeRotation);
                const Math::Vector3 localStart = Math::RotateVector(invBoxRot, segmentStart - colliderCenter);
                const Math::Vector3 localEnd = Math::RotateVector(invBoxRot, segmentEnd - colliderCenter);
                std::array<float, 2U> ts{};
                ComputeCapsuleLocalBoxContactPoints(localStart, localEnd, collider.shapeHalfExtents, body.radius,
                                                       penetration->depth, ts, contactDepths, manifoldCount);
                const Math::Vector3 segDelta = segmentEnd - segmentStart;
                for (std::size_t m = 0U; m < manifoldCount; ++m) {
                    axisPoints[m] = segmentStart + segDelta * ts[m];
                }
                break;
            }
            case Scene::ColliderShapeType::Cylinder: {
                const Math::Vector3 aabbHalfExtents = (collider.worldAabb.max - collider.worldAabb.min) * 0.5F;
                const Math::Vector3 localStart = segmentStart - colliderCenter;
                const Math::Vector3 localEnd = segmentEnd - colliderCenter;
                std::array<float, 2U> ts{};
                ComputeCapsuleLocalBoxContactPoints(localStart, localEnd, aabbHalfExtents, body.radius,
                                                       penetration->depth, ts, contactDepths, manifoldCount);
                const Math::Vector3 segDelta = segmentEnd - segmentStart;
                for (std::size_t m = 0U; m < manifoldCount; ++m) {
                    axisPoints[m] = segmentStart + segDelta * ts[m];
                }
                break;
            }
            }

            const bool hasMotion = colliderIndex < colliderMotions.size();
            const RagdollColliderMotion motion =
                hasMotion ? colliderMotions[colliderIndex] : RagdollColliderMotion{};

            if (normal.y > kFloorSupportNormalY) {
                stats.hasFloorSupport = true;
            }

            for (std::size_t m = 0U; m < manifoldCount; ++m) {
                const Math::Vector3 contactPoint = axisPoints[m] - normal * body.radius;

                RagdollContact contact;
                contact.body = static_cast<std::uint32_t>(bodyIndex);
                contact.point = contactPoint;
                contact.normal = normal;
                contact.depth = contactDepths[m];
                contact.friction = combined.friction;
                contact.restitution = combined.restitution;
                contact.otherBody = kInvalidRagdollBody;

                if (hasMotion) {
                    const Math::Vector3 otherOffset = contactPoint - motion.worldCenter;
                    const Math::Vector3 surfaceVel =
                        motion.linearVelocity + Math::Cross(motion.angularVelocity, otherOffset);
                    if (Math::IsFinite(surfaceVel)) {
                        contact.otherVelocity = surfaceVel;
                        if (Math::LengthSquared(surfaceVel) > kMovingColliderWakeSpeedSq) {
                            stats.hasMovingColliderContact = true;
                        }
                    }
                    if (motion.isDynamic && std::isfinite(motion.inverseMass) && motion.inverseMass > 0.0F) {
                        contact.colliderIndex = static_cast<std::uint32_t>(colliderIndex);
                        contact.otherInverseMass = motion.inverseMass;
                        ++stats.dynamicContactCount;
                    }
                }

                outContacts[stats.generatedContactCount++] = contact;
                if (stats.generatedContactCount >= outContacts.size()) {
                    stats.truncated = true;
                    return stats;
                }
            }
        }
    }
    return stats;
}

} // namespace UVE::Physics
