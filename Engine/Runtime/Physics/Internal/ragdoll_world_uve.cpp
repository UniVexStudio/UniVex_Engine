// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/ragdoll_world_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>

#include "uve/physics/detail/shape_narrow_phase_uve.h"
#include "uve/physics/physics_material_uve.h"

namespace UVE::Physics {
namespace {

constexpr float kMinimumBodyRadiusUVE = 1.0e-4F;

/// Where on the body's own capsule axis the other shape is closest. Used for the contact point:
/// the solver needs an offset from the body's centre to turn a normal impulse into a torque, and the
/// closest point on the axis is the standard answer for a capsule against anything convex.
[[nodiscard]] Math::Vector3UVE ClosestPointOnSegmentUVE(const Math::Vector3UVE& segmentStart,
                                                        const Math::Vector3UVE& segmentEnd,
                                                        const Math::Vector3UVE& target) noexcept {
    const Math::Vector3UVE direction = segmentEnd - segmentStart;
    const float lengthSquared = Math::LengthSquaredUVE(direction);
    if (!(lengthSquared > 1.0e-12F)) return segmentStart;
    const float projection = Math::DotUVE(target - segmentStart, direction) / lengthSquared;
    return segmentStart + direction * std::clamp(projection, 0.0F, 1.0F);
}

[[nodiscard]] Math::Vector3UVE NegateUVE(const Math::Vector3UVE& value) noexcept {
    return Math::Vector3UVE{-value.x, -value.y, -value.z};
}

} // namespace

RagdollContactStatsUVE BuildRagdollContactsUVE(const RagdollSolverUVE& solver,
                                               const std::span<const Detail::ColliderWorldAabbUVE> colliders,
                                               const RagdollCollisionFilterUVE& filter,
                                               const std::span<RagdollContactUVE> outContacts) noexcept {
    RagdollContactStatsUVE stats;
    if (outContacts.empty() || solver.GetBodyCountUVE() == 0U) return stats;

    for (std::size_t bodyIndex = 0U; bodyIndex < solver.GetBodyCountUVE(); ++bodyIndex) {
        const RagdollBodyUVE& body = solver.GetBodyUVE(bodyIndex);
        if (!std::isfinite(body.radius) || body.radius < kMinimumBodyRadiusUVE) continue;
        Math::Vector3UVE segmentStart{};
        Math::Vector3UVE segmentEnd{};
        ComputeRagdollCapsuleWorldSegmentUVE(body, segmentStart, segmentEnd);
        if (!Math::IsFiniteUVE(segmentStart) || !Math::IsFiniteUVE(segmentEnd)) continue;

        for (const Detail::ColliderWorldAabbUVE& collider : colliders) {
            if (collider.entity == filter.ignoreEntity || collider.entity == filter.alsoIgnoreEntity) continue;
            if ((filter.layerMask & collider.collisionMask) == 0U) continue;
            ++stats.testedColliderCount;

            const Math::Vector3UVE colliderCenter = collider.worldAabb.GetCenterUVE();
            std::optional<Math::PenetrationUVE> penetration;
            switch (collider.shapeType) {
            case Scene::ColliderShapeTypeUVE::Sphere:
                penetration = Detail::ComputeCapsuleSpherePenetrationUVE(segmentStart, segmentEnd, body.radius,
                                                                         colliderCenter, collider.shapeRadius);
                break;
            case Scene::ColliderShapeTypeUVE::Capsule:
                penetration = Detail::ComputeCapsuleCapsulePenetrationUVE(segmentStart, segmentEnd, body.radius,
                                                                          collider.shapeSegmentStart,
                                                                          collider.shapeSegmentEnd,
                                                                          collider.shapeRadius);
                break;
            case Scene::ColliderShapeTypeUVE::Box:
                penetration = Detail::ComputeCapsuleOrientedBoxPenetrationUVE(
                    colliderCenter, collider.shapeHalfExtents, collider.shapeRotation, segmentStart, segmentEnd,
                    body.radius);
                break;
            case Scene::ColliderShapeTypeUVE::Cylinder:
                penetration = Detail::ComputeCapsuleAabbPenetrationUVE(collider.worldAabb, segmentStart, segmentEnd,
                                                                       body.radius);
                break;
            }
            if (!penetration.has_value() || !(penetration->depth > 0.0F)) continue;
            if (!Math::IsFiniteUVE(penetration->axis)) continue;

            // The narrow phase reports "capsule -> other"; the solver needs "push the body out".
            const Math::Vector3UVE normal = NegateUVE(penetration->axis);
            const Math::Vector3UVE axisPoint = ClosestPointOnSegmentUVE(segmentStart, segmentEnd, colliderCenter);

            const PhysicsMaterialUVE combined =
                CombineMaterialsUVE(PhysicsMaterialUVE{body.friction, body.restitution, 1.0F}, collider.material);

            RagdollContactUVE contact;
            contact.body = static_cast<std::uint32_t>(bodyIndex);
            contact.point = axisPoint - normal * body.radius;
            contact.normal = normal;
            contact.depth = penetration->depth;
            contact.friction = combined.friction;
            contact.restitution = combined.restitution;

            outContacts[stats.generatedContactCount++] = contact;
            if (stats.generatedContactCount >= outContacts.size()) {
                // Full: keep the buffer consistent and tell the caller. Dropping the rest is the
                // graceful failure - the alternatives are overrunning the buffer or making the
                // caller pre-size it from an unbounded count.
                stats.truncated = true;
                return stats;
            }
        }
    }
    return stats;
}

} // namespace UVE::Physics
