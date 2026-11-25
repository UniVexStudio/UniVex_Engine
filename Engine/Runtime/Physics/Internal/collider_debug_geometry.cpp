// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_debug_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "aabb.h"

namespace UVE::Physics {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kMinimumCapsuleBarrel = 0.0F;
constexpr std::size_t kMinimumSegments = 8U;
constexpr std::size_t kMaximumSegments = 96U;
constexpr std::size_t kSegmentCount = 4U;

/// Writes one segment, or reports that the caller's buffer is full.
[[nodiscard]] bool PushLine(const Math::Vector3& from, const Math::Vector3& to,
                               const std::span<ColliderDebugLine> outLines,
                               ColliderDebugBuildResult& result) noexcept {
    if (result.lineCount >= outLines.size()) {
        result.truncated = true;
        return false;
    }
    outLines[result.lineCount] = ColliderDebugLine{from, to};
    ++result.lineCount;
    return true;
}

/// A point on a circle of `radius` in the plane spanned by `axisA`/`axisB`, at `angle` radians.
[[nodiscard]] Math::Vector3 CirclePoint(const Math::Vector3& center, const Math::Vector3& axisA,
                                              const Math::Vector3& axisB, const float radius,
                                              const float angle) noexcept {
    return center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius);
}

/// Emits `segments` chords closing a circle, in the fixed direction the caller's axes imply.
void PushCircle(const Math::Vector3& center, const Math::Vector3& axisA, const Math::Vector3& axisB,
                   const float radius, const std::size_t segments,
                   const std::span<ColliderDebugLine> outLines,
                   ColliderDebugBuildResult& result) noexcept {
    const float step = 2.0F * kPi / static_cast<float>(segments);
    Math::Vector3 previous = CirclePoint(center, axisA, axisB, radius, 0.0F);
    for (std::size_t index = 1U; index <= segments; ++index) {
        const Math::Vector3 current =
            CirclePoint(center, axisA, axisB, radius, step * static_cast<float>(index));
        if (!PushLine(previous, current, outLines, result)) return;
        previous = current;
    }
}

/// One cap's semicircle: from (+radius, capY) over the pole to (-radius, capY).
///
/// `poleSign` is +1 for the top cap and -1 for the bottom, so the arc bulges away from the barrel
/// instead of cutting through it.
void PushCapArc(const Math::Vector3& capCenter, const Math::Vector3& lateralAxis,
                   const Math::Vector3& barrelAxis, const float radius, const float poleSign,
                   const std::size_t halfSegments, const std::span<ColliderDebugLine> outLines,
                   ColliderDebugBuildResult& result) noexcept {
    const float step = kPi / static_cast<float>(halfSegments);
    Math::Vector3 previous = CirclePoint(capCenter, lateralAxis, barrelAxis * poleSign, radius, 0.0F);
    for (std::size_t index = 1U; index <= halfSegments; ++index) {
        const Math::Vector3 current =
            CirclePoint(capCenter, lateralAxis, barrelAxis * poleSign, radius, step * static_cast<float>(index));
        if (!PushLine(previous, current, outLines, result)) return;
        previous = current;
    }
}

/// The four barrel lines that join two rings, at the quarter points both rings share.
void PushBarrelLines(const Math::Vector3& topCenter, const Math::Vector3& bottomCenter,
                        const Math::Vector3& axisA, const Math::Vector3& axisB, const float radius,
                        const std::span<ColliderDebugLine> outLines,
                        ColliderDebugBuildResult& result) noexcept {
    constexpr std::array<float, kSegmentCount> kQuarterAngles{0.0F, 0.5F * kPi, kPi,
                                                                   1.5F * kPi};
    for (const float angle : kQuarterAngles) {
        if (!PushLine(CirclePoint(topCenter, axisA, axisB, radius, angle),
                         CirclePoint(bottomCenter, axisA, axisB, radius, angle), outLines, result)) {
            return;
        }
    }
}

[[nodiscard]] Math::Vector3 ToWorld(const Math::Vector3& local, const Math::Vector3& center,
                                          const Math::Quaternion& rotation) noexcept {
    return center + Math::RotateVector(rotation, local);
}

/// Local half extents of a collider, matching what the narrow phase reads. The caller hands in the
/// shape already scaled by the transform, so this stays one expression and cannot drift from the
/// simulation the way a second scale rule would.
[[nodiscard]] Math::Vector3 LocalHalfExtents(const Scene::ColliderComponent& collider) noexcept {
    return Scene::GetColliderLocalHalfExtents(collider);
}

} // namespace

std::size_t ResolveColliderDebugSegments(const ColliderDebugStyle& style) noexcept {
    std::size_t segments = std::clamp(style.circleSegments, kMinimumSegments, kMaximumSegments);
    // Even, so a capsule's half-circle fan lands exactly on a ring vertex instead of beside it.
    segments += segments % 2U;
    return segments;
}

std::size_t ComputeColliderDebugLineCapacity(const Scene::ColliderComponent& collider,
                                                const ColliderDebugStyle& style) noexcept {
    if (!Scene::IsColliderComponentValid(collider) || style.drawBoundsOnly) return 12U;
    const std::size_t segments = ResolveColliderDebugSegments(style);
    switch (collider.shapeType) {
    case Scene::ColliderShapeType::Box:
        return 12U;
    case Scene::ColliderShapeType::Sphere:
        return 3U * segments;
    case Scene::ColliderShapeType::Capsule:
        // Two rings, four barrel lines, and two planes of semicircle over each cap.
        return 4U * segments + 4U;
    case Scene::ColliderShapeType::Cylinder:
        return 2U * segments + 4U;
    }
    return 12U;
}

void BuildOrientedBoxDebugLines(const Math::Vector3& center, const Math::Vector3& halfExtents,
                                   const Math::Quaternion& rotation,
                                   const std::span<ColliderDebugLine> outLines,
                                   ColliderDebugBuildResult& outResult) noexcept {
    outResult = ColliderDebugBuildResult{};
    if (outLines.empty()) return;
    if (!Math::IsFinite(center) || !Math::IsFinite(halfExtents) || !Math::IsFinite(rotation)) return;
    if (!(halfExtents.x > 0.0F) || !(halfExtents.y > 0.0F) || !(halfExtents.z > 0.0F)) return;

    // Corner order is the binary count from (-,-,-) to (+,+,+), so the edge list below is a fixed
    // enumeration of the 12 differences that differ in exactly one bit.
    std::array<Math::Vector3, 8U> corners{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const Math::Vector3 local{((index & 1U) != 0U ? halfExtents.x : -halfExtents.x),
                                     ((index & 2U) != 0U ? halfExtents.y : -halfExtents.y),
                                     ((index & 4U) != 0U ? halfExtents.z : -halfExtents.z)};
        corners[index] = ToWorld(local, center, rotation);
    }
    constexpr std::array<std::array<std::size_t, 2U>, 12U> kEdges{{
        {0U, 1U}, {2U, 3U}, {4U, 5U}, {6U, 7U}, // along X
        {0U, 2U}, {1U, 3U}, {4U, 6U}, {5U, 7U}, // along Y
        {0U, 4U}, {1U, 5U}, {2U, 6U}, {3U, 7U}, // along Z
    }};
    for (const std::array<std::size_t, 2U>& edge : kEdges) {
        if (!PushLine(corners[edge[0]], corners[edge[1]], outLines, outResult)) return;
    }
}

ColliderDebugBuildResult BuildColliderDebugLines(const Scene::ColliderComponent& collider,
                                                       const Math::Vector3& worldCenter,
                                                       const Math::Quaternion& worldRotation,
                                                       const std::span<ColliderDebugLine> outLines,
                                                       const ColliderDebugStyle& style,
                                                       const Math::Vector3& worldScale) noexcept {
    ColliderDebugBuildResult result;
    if (outLines.empty()) return result;
    if (!Math::IsFinite(worldCenter) || !Math::IsFinite(worldRotation) ||
        !Math::IsFinite(worldScale)) {
        return result;
    }
    // Everything below is written against the shape the simulation collides with, at the size the
    // mesh is drawn at, so the wireframe can never disagree with either one.
    const Scene::ColliderComponent scaled = Scene::ScaleCollider(collider, worldScale);

    if (style.drawBoundsOnly) {
        const Math::Vector3 halfExtents = LocalHalfExtents(collider);
        if (!Scene::IsColliderComponentValid(scaled)) return result;
        BuildOrientedBoxDebugLines(worldCenter, halfExtents, worldRotation, outLines, result);
        return result;
    }
    if (!Scene::IsColliderComponentValid(scaled)) return result;

    const std::size_t segments = ResolveColliderDebugSegments(style);
    // The collider's own local axes, rotated into world space once. Capsules and cylinders stand
    // along local +Y (the engine's collider convention), so `barrel` is that axis and the two
    // laterals are the rings' plane.
    const Math::Vector3 barrelAxis = Math::RotateVector(worldRotation, Math::Vector3{0.0F, 1.0F, 0.0F});
    const Math::Vector3 lateralX = Math::RotateVector(worldRotation, Math::Vector3{1.0F, 0.0F, 0.0F});
    const Math::Vector3 lateralZ = Math::RotateVector(worldRotation, Math::Vector3{0.0F, 0.0F, 1.0F});

    switch (scaled.shapeType) {
    case Scene::ColliderShapeType::Box: {
        BuildOrientedBoxDebugLines(worldCenter, scaled.halfExtents, worldRotation, outLines, result);
        return result;
    }
    case Scene::ColliderShapeType::Sphere: {
        const float radius = scaled.radius;
        PushCircle(worldCenter, lateralX, barrelAxis, radius, segments, outLines, result);
        PushCircle(worldCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircle(worldCenter, lateralZ, barrelAxis, radius, segments, outLines, result);
        return result;
    }
    case Scene::ColliderShapeType::Capsule: {
        const float radius = scaled.radius;
        const float barrelHalfLength =
            std::max(kMinimumCapsuleBarrel, scaled.height * 0.5F - radius);
        const Math::Vector3 topCenter = worldCenter + barrelAxis * barrelHalfLength;
        const Math::Vector3 bottomCenter = worldCenter - barrelAxis * barrelHalfLength;
        PushCircle(topCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircle(bottomCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushBarrelLines(topCenter, bottomCenter, lateralX, lateralZ, radius, outLines, result);
        // A capsule with no barrel is a sphere: the two caps' arcs then close the whole shape, so
        // the four semicircles still describe it correctly instead of drawing flat ends.
        PushCapArc(topCenter, lateralX, barrelAxis, radius, 1.0F, segments / 2U, outLines, result);
        PushCapArc(topCenter, lateralZ, barrelAxis, radius, 1.0F, segments / 2U, outLines, result);
        PushCapArc(bottomCenter, lateralX, barrelAxis, radius, -1.0F, segments / 2U, outLines, result);
        PushCapArc(bottomCenter, lateralZ, barrelAxis, radius, -1.0F, segments / 2U, outLines, result);
        return result;
    }
    case Scene::ColliderShapeType::Cylinder: {
        const float radius = scaled.radius;
        const float halfHeight = scaled.height * 0.5F;
        const Math::Vector3 topCenter = worldCenter + barrelAxis * halfHeight;
        const Math::Vector3 bottomCenter = worldCenter - barrelAxis * halfHeight;
        PushCircle(topCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircle(bottomCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushBarrelLines(topCenter, bottomCenter, lateralX, lateralZ, radius, outLines, result);
        return result;
    }
    }
    return result;
}

ColliderDebugBuildResult BuildRagdollDebugLines(const RagdollSolver& solver,
                                                      const std::span<ColliderDebugLine> outLines,
                                                      const RagdollDebugDrawOptions& options) noexcept {
    ColliderDebugBuildResult result;
    const std::size_t bodyCount = solver.GetBodyCount();
    if (outLines.empty() || bodyCount == 0U) return result;

    if (options.drawBodies) {
        for (std::size_t index = 0U; index < bodyCount; ++index) {
            if (result.lineCount >= outLines.size()) {
                result.truncated = true;
                return result;
            }
            const RagdollBody& body = solver.GetBody(index);
            if (!std::isfinite(body.radius) || !(body.radius > 0.0F)) continue;
            Scene::ColliderComponent capsule;
            capsule.shapeType = Scene::ColliderShapeType::Capsule;
            capsule.radius = body.radius;
            capsule.height = 2.0F * (std::max(0.0F, body.halfHeight) + body.radius);
            const ColliderDebugBuildResult sub = BuildColliderDebugLines(
                capsule, body.position, body.rotation,
                outLines.subspan(result.lineCount), options.style);
            result.lineCount += sub.lineCount;
            if (sub.truncated) {
                result.truncated = true;
                return result;
            }
        }
    }

    if (!options.drawJointCones) return result;

    const float coneLen =
        std::isfinite(options.jointConeLength) && options.jointConeLength > 0.01F ? options.jointConeLength : 0.14F;
    const float degToRad = kPi / 180.0F;
    constexpr std::size_t kConeSegments = 16U;

    const std::size_t jointCount = solver.GetJointCount();
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        const RagdollJoint& joint = solver.GetJoint(jointIndex);
        if (joint.firstBody >= bodyCount || joint.secondBody >= bodyCount) continue;
        const RagdollBody& first = solver.GetBody(joint.firstBody);
        const RagdollBody& second = solver.GetBody(joint.secondBody);
        if (!Math::IsFinite(first.position) || !Math::IsFinite(first.rotation) ||
            !Math::IsFinite(second.position) || !Math::IsFinite(second.rotation)) {
            continue;
        }

        const Math::Vector3 pivot = first.position + Math::RotateVector(first.rotation, joint.firstAnchor);
        const Math::Quaternion worldFrame = Math::Multiply(first.rotation, joint.firstFrame);
        const Math::Vector3 twistAxis = Math::RotateVector(worldFrame, Math::Vector3{1.0F, 0.0F, 0.0F});
        const Math::Vector3 swing2Axis = Math::RotateVector(worldFrame, Math::Vector3{0.0F, 1.0F, 0.0F});
        const Math::Vector3 swing1Axis = Math::RotateVector(worldFrame, Math::Vector3{0.0F, 0.0F, 1.0F});

        const bool isHinge = joint.swing1Motion == RagdollMotion::Locked &&
                             joint.swing2Motion == RagdollMotion::Locked &&
                             joint.twistMotion == RagdollMotion::Limited;

        if (isHinge) {
            // Hinge axis pin bar.
            if (!PushLine(pivot - twistAxis * (coneLen * 0.45F), pivot + twistAxis * (coneLen * 0.45F),
                             outLines, result)) {
                return result;
            }
            // Twist limit arc fan in the plane orthogonal to `twistAxis`.
            const float lowerRad =
                (joint.twistAsymmetric ? joint.twistLowerLimitDegrees : -joint.twistLimitDegrees) * degToRad;
            const float upperRad =
                (joint.twistAsymmetric ? joint.twistUpperLimitDegrees : joint.twistLimitDegrees) * degToRad;
            const Math::Vector3 refDir = swing2Axis;
            const Math::Vector3 orthoDir = swing1Axis;
            const auto arcPoint = [&](const float angle) noexcept -> Math::Vector3 {
                return pivot + (refDir * std::cos(angle) + orthoDir * std::sin(angle)) * coneLen;
            };
            const Math::Vector3 startPt = arcPoint(lowerRad);
            const Math::Vector3 endPt = arcPoint(upperRad);
            if (!PushLine(pivot, startPt, outLines, result)) return result;
            if (!PushLine(pivot, endPt, outLines, result)) return result;

            Math::Vector3 prev = startPt;
            const float span = upperRad - lowerRad;
            for (std::size_t s = 1U; s <= kConeSegments; ++s) {
                const float angle = lowerRad + span * (static_cast<float>(s) / static_cast<float>(kConeSegments));
                const Math::Vector3 cur = arcPoint(angle);
                if (!PushLine(prev, cur, outLines, result)) return result;
                prev = cur;
            }
            continue;
        }

        // Elliptical swing limit cone around `twistAxis` (bone rest axis).
        const float lim1 = joint.swing1Motion == RagdollMotion::Limited
                               ? std::clamp(joint.swing1LimitDegrees, 1.0F, 175.0F) * degToRad
                               : 0.0F;
        const float lim2 = joint.swing2Motion == RagdollMotion::Limited
                               ? std::clamp(joint.swing2LimitDegrees, 1.0F, 175.0F) * degToRad
                               : 0.0F;
        if (lim1 > 0.0F || lim2 > 0.0F) {
            const auto conePoint = [&](const float phi) noexcept -> Math::Vector3 {
                const float s1 = lim1 * std::cos(phi);
                const float s2 = lim2 * std::sin(phi);
                const float theta = std::hypot(s1, s2);
                Math::Vector3 dir = twistAxis;
                if (theta > 1.0e-5F) {
                    const Math::Vector3 rotAxis = (swing1Axis * (s1 / theta)) + (swing2Axis * (s2 / theta));
                    Math::Quaternion q;
                    if (Math::TryMakeAxisAngle(rotAxis, theta, q)) {
                        dir = Math::RotateVector(q, twistAxis);
                    }
                }
                return pivot + dir * coneLen;
            };
            const float step = (2.0F * kPi) / static_cast<float>(kConeSegments);
            Math::Vector3 prev = conePoint(0.0F);
            for (std::size_t s = 1U; s <= kConeSegments; ++s) {
                const Math::Vector3 cur = conePoint(step * static_cast<float>(s));
                if (!PushLine(prev, cur, outLines, result)) return result;
                prev = cur;
            }
            // Four cardinal meridian rays from joint pivot to cone rim.
            for (std::size_t q = 0U; q < 4U; ++q) {
                if (!PushLine(pivot, conePoint(0.5F * kPi * static_cast<float>(q)), outLines, result)) {
                    return result;
                }
            }
        }
        // Current child frame axis ray so the author sees live deviation inside the limit cone.
        const Math::Quaternion secondWorldFrame = Math::Multiply(second.rotation, joint.secondFrame);
        const Math::Vector3 currentAxis =
            Math::RotateVector(secondWorldFrame, Math::Vector3{1.0F, 0.0F, 0.0F});
        if (!PushLine(pivot, pivot + currentAxis * (coneLen * 1.15F), outLines, result)) {
            return result;
        }
    }

    return result;
}

} // namespace UVE::Physics
