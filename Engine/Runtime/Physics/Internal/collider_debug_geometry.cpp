// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "collider_debug_geometry.h"

#include <algorithm>
#include <array>
#include <cmath>

#include "aabb.h"

namespace UVE::Physics {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kMinimumCapsuleBarrelUVE = 0.0F;
constexpr std::size_t kMinimumSegmentsUVE = 8U;
constexpr std::size_t kMaximumSegmentsUVE = 96U;
constexpr std::size_t kSegmentCountUVE = 4U;

/// Writes one segment, or reports that the caller's buffer is full.
[[nodiscard]] bool PushLineUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to,
                               const std::span<ColliderDebugLineUVE> outLines,
                               ColliderDebugBuildResultUVE& result) noexcept {
    if (result.lineCount >= outLines.size()) {
        result.truncated = true;
        return false;
    }
    outLines[result.lineCount] = ColliderDebugLineUVE{from, to};
    ++result.lineCount;
    return true;
}

/// A point on a circle of `radius` in the plane spanned by `axisA`/`axisB`, at `angle` radians.
[[nodiscard]] Math::Vector3UVE CirclePointUVE(const Math::Vector3UVE& center, const Math::Vector3UVE& axisA,
                                              const Math::Vector3UVE& axisB, const float radius,
                                              const float angle) noexcept {
    return center + axisA * (std::cos(angle) * radius) + axisB * (std::sin(angle) * radius);
}

/// Emits `segments` chords closing a circle, in the fixed direction the caller's axes imply.
void PushCircleUVE(const Math::Vector3UVE& center, const Math::Vector3UVE& axisA, const Math::Vector3UVE& axisB,
                   const float radius, const std::size_t segments,
                   const std::span<ColliderDebugLineUVE> outLines,
                   ColliderDebugBuildResultUVE& result) noexcept {
    const float step = 2.0F * kPiUVE / static_cast<float>(segments);
    Math::Vector3UVE previous = CirclePointUVE(center, axisA, axisB, radius, 0.0F);
    for (std::size_t index = 1U; index <= segments; ++index) {
        const Math::Vector3UVE current =
            CirclePointUVE(center, axisA, axisB, radius, step * static_cast<float>(index));
        if (!PushLineUVE(previous, current, outLines, result)) return;
        previous = current;
    }
}

/// One cap's semicircle: from (+radius, capY) over the pole to (-radius, capY).
///
/// `poleSign` is +1 for the top cap and -1 for the bottom, so the arc bulges away from the barrel
/// instead of cutting through it.
void PushCapArcUVE(const Math::Vector3UVE& capCenter, const Math::Vector3UVE& lateralAxis,
                   const Math::Vector3UVE& barrelAxis, const float radius, const float poleSign,
                   const std::size_t halfSegments, const std::span<ColliderDebugLineUVE> outLines,
                   ColliderDebugBuildResultUVE& result) noexcept {
    const float step = kPiUVE / static_cast<float>(halfSegments);
    Math::Vector3UVE previous = CirclePointUVE(capCenter, lateralAxis, barrelAxis * poleSign, radius, 0.0F);
    for (std::size_t index = 1U; index <= halfSegments; ++index) {
        const Math::Vector3UVE current =
            CirclePointUVE(capCenter, lateralAxis, barrelAxis * poleSign, radius, step * static_cast<float>(index));
        if (!PushLineUVE(previous, current, outLines, result)) return;
        previous = current;
    }
}

/// The four barrel lines that join two rings, at the quarter points both rings share.
void PushBarrelLinesUVE(const Math::Vector3UVE& topCenter, const Math::Vector3UVE& bottomCenter,
                        const Math::Vector3UVE& axisA, const Math::Vector3UVE& axisB, const float radius,
                        const std::span<ColliderDebugLineUVE> outLines,
                        ColliderDebugBuildResultUVE& result) noexcept {
    constexpr std::array<float, kSegmentCountUVE> kQuarterAnglesUVE{0.0F, 0.5F * kPiUVE, kPiUVE,
                                                                   1.5F * kPiUVE};
    for (const float angle : kQuarterAnglesUVE) {
        if (!PushLineUVE(CirclePointUVE(topCenter, axisA, axisB, radius, angle),
                         CirclePointUVE(bottomCenter, axisA, axisB, radius, angle), outLines, result)) {
            return;
        }
    }
}

[[nodiscard]] Math::Vector3UVE ToWorldUVE(const Math::Vector3UVE& local, const Math::Vector3UVE& center,
                                          const Math::QuaternionUVE& rotation) noexcept {
    return center + Math::RotateVectorUVE(rotation, local);
}

/// Local half extents of a collider, matching what the narrow phase reads. The caller hands in the
/// shape already scaled by the transform, so this stays one expression and cannot drift from the
/// simulation the way a second scale rule would.
[[nodiscard]] Math::Vector3UVE LocalHalfExtentsUVE(const Scene::ColliderComponentUVE& collider) noexcept {
    return Scene::GetColliderLocalHalfExtentsUVE(collider);
}

} // namespace

std::size_t ResolveColliderDebugSegmentsUVE(const ColliderDebugStyleUVE& style) noexcept {
    std::size_t segments = std::clamp(style.circleSegments, kMinimumSegmentsUVE, kMaximumSegmentsUVE);
    // Even, so a capsule's half-circle fan lands exactly on a ring vertex instead of beside it.
    segments += segments % 2U;
    return segments;
}

std::size_t ComputeColliderDebugLineCapacityUVE(const Scene::ColliderComponentUVE& collider,
                                                const ColliderDebugStyleUVE& style) noexcept {
    if (!Scene::IsColliderComponentValidUVE(collider) || style.drawBoundsOnly) return 12U;
    const std::size_t segments = ResolveColliderDebugSegmentsUVE(style);
    switch (collider.shapeType) {
    case Scene::ColliderShapeTypeUVE::Box:
        return 12U;
    case Scene::ColliderShapeTypeUVE::Sphere:
        return 3U * segments;
    case Scene::ColliderShapeTypeUVE::Capsule:
        // Two rings, four barrel lines, and two planes of semicircle over each cap.
        return 4U * segments + 4U;
    case Scene::ColliderShapeTypeUVE::Cylinder:
        return 2U * segments + 4U;
    }
    return 12U;
}

void BuildOrientedBoxDebugLinesUVE(const Math::Vector3UVE& center, const Math::Vector3UVE& halfExtents,
                                   const Math::QuaternionUVE& rotation,
                                   const std::span<ColliderDebugLineUVE> outLines,
                                   ColliderDebugBuildResultUVE& outResult) noexcept {
    outResult = ColliderDebugBuildResultUVE{};
    if (outLines.empty()) return;
    if (!Math::IsFiniteUVE(center) || !Math::IsFiniteUVE(halfExtents) || !Math::IsFiniteUVE(rotation)) return;
    if (!(halfExtents.x > 0.0F) || !(halfExtents.y > 0.0F) || !(halfExtents.z > 0.0F)) return;

    // Corner order is the binary count from (-,-,-) to (+,+,+), so the edge list below is a fixed
    // enumeration of the 12 differences that differ in exactly one bit.
    std::array<Math::Vector3UVE, 8U> corners{};
    for (std::size_t index = 0U; index < corners.size(); ++index) {
        const Math::Vector3UVE local{((index & 1U) != 0U ? halfExtents.x : -halfExtents.x),
                                     ((index & 2U) != 0U ? halfExtents.y : -halfExtents.y),
                                     ((index & 4U) != 0U ? halfExtents.z : -halfExtents.z)};
        corners[index] = ToWorldUVE(local, center, rotation);
    }
    constexpr std::array<std::array<std::size_t, 2U>, 12U> kEdgesUVE{{
        {0U, 1U}, {2U, 3U}, {4U, 5U}, {6U, 7U}, // along X
        {0U, 2U}, {1U, 3U}, {4U, 6U}, {5U, 7U}, // along Y
        {0U, 4U}, {1U, 5U}, {2U, 6U}, {3U, 7U}, // along Z
    }};
    for (const std::array<std::size_t, 2U>& edge : kEdgesUVE) {
        if (!PushLineUVE(corners[edge[0]], corners[edge[1]], outLines, outResult)) return;
    }
}

ColliderDebugBuildResultUVE BuildColliderDebugLinesUVE(const Scene::ColliderComponentUVE& collider,
                                                       const Math::Vector3UVE& worldCenter,
                                                       const Math::QuaternionUVE& worldRotation,
                                                       const std::span<ColliderDebugLineUVE> outLines,
                                                       const ColliderDebugStyleUVE& style,
                                                       const Math::Vector3UVE& worldScale) noexcept {
    ColliderDebugBuildResultUVE result;
    if (outLines.empty()) return result;
    if (!Math::IsFiniteUVE(worldCenter) || !Math::IsFiniteUVE(worldRotation) ||
        !Math::IsFiniteUVE(worldScale)) {
        return result;
    }
    // Everything below is written against the shape the simulation collides with, at the size the
    // mesh is drawn at, so the wireframe can never disagree with either one.
    const Scene::ColliderComponentUVE scaled = Scene::ScaleColliderUVE(collider, worldScale);

    if (style.drawBoundsOnly) {
        const Math::Vector3UVE halfExtents = LocalHalfExtentsUVE(collider);
        if (!Scene::IsColliderComponentValidUVE(scaled)) return result;
        BuildOrientedBoxDebugLinesUVE(worldCenter, halfExtents, worldRotation, outLines, result);
        return result;
    }
    if (!Scene::IsColliderComponentValidUVE(scaled)) return result;

    const std::size_t segments = ResolveColliderDebugSegmentsUVE(style);
    // The collider's own local axes, rotated into world space once. Capsules and cylinders stand
    // along local +Y (the engine's collider convention), so `barrel` is that axis and the two
    // laterals are the rings' plane.
    const Math::Vector3UVE barrelAxis = Math::RotateVectorUVE(worldRotation, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
    const Math::Vector3UVE lateralX = Math::RotateVectorUVE(worldRotation, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
    const Math::Vector3UVE lateralZ = Math::RotateVectorUVE(worldRotation, Math::Vector3UVE{0.0F, 0.0F, 1.0F});

    switch (scaled.shapeType) {
    case Scene::ColliderShapeTypeUVE::Box: {
        BuildOrientedBoxDebugLinesUVE(worldCenter, scaled.halfExtents, worldRotation, outLines, result);
        return result;
    }
    case Scene::ColliderShapeTypeUVE::Sphere: {
        const float radius = scaled.radius;
        PushCircleUVE(worldCenter, lateralX, barrelAxis, radius, segments, outLines, result);
        PushCircleUVE(worldCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircleUVE(worldCenter, lateralZ, barrelAxis, radius, segments, outLines, result);
        return result;
    }
    case Scene::ColliderShapeTypeUVE::Capsule: {
        const float radius = scaled.radius;
        const float barrelHalfLength =
            std::max(kMinimumCapsuleBarrelUVE, scaled.height * 0.5F - radius);
        const Math::Vector3UVE topCenter = worldCenter + barrelAxis * barrelHalfLength;
        const Math::Vector3UVE bottomCenter = worldCenter - barrelAxis * barrelHalfLength;
        PushCircleUVE(topCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircleUVE(bottomCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushBarrelLinesUVE(topCenter, bottomCenter, lateralX, lateralZ, radius, outLines, result);
        // A capsule with no barrel is a sphere: the two caps' arcs then close the whole shape, so
        // the four semicircles still describe it correctly instead of drawing flat ends.
        PushCapArcUVE(topCenter, lateralX, barrelAxis, radius, 1.0F, segments / 2U, outLines, result);
        PushCapArcUVE(topCenter, lateralZ, barrelAxis, radius, 1.0F, segments / 2U, outLines, result);
        PushCapArcUVE(bottomCenter, lateralX, barrelAxis, radius, -1.0F, segments / 2U, outLines, result);
        PushCapArcUVE(bottomCenter, lateralZ, barrelAxis, radius, -1.0F, segments / 2U, outLines, result);
        return result;
    }
    case Scene::ColliderShapeTypeUVE::Cylinder: {
        const float radius = scaled.radius;
        const float halfHeight = scaled.height * 0.5F;
        const Math::Vector3UVE topCenter = worldCenter + barrelAxis * halfHeight;
        const Math::Vector3UVE bottomCenter = worldCenter - barrelAxis * halfHeight;
        PushCircleUVE(topCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushCircleUVE(bottomCenter, lateralX, lateralZ, radius, segments, outLines, result);
        PushBarrelLinesUVE(topCenter, bottomCenter, lateralX, lateralZ, radius, outLines, result);
        return result;
    }
    }
    return result;
}

ColliderDebugBuildResultUVE BuildRagdollDebugLinesUVE(const RagdollSolverUVE& solver,
                                                      const std::span<ColliderDebugLineUVE> outLines,
                                                      const RagdollDebugDrawOptionsUVE& options) noexcept {
    ColliderDebugBuildResultUVE result;
    const std::size_t bodyCount = solver.GetBodyCountUVE();
    if (outLines.empty() || bodyCount == 0U) return result;

    if (options.drawBodies) {
        for (std::size_t index = 0U; index < bodyCount; ++index) {
            if (result.lineCount >= outLines.size()) {
                result.truncated = true;
                return result;
            }
            const RagdollBodyUVE& body = solver.GetBodyUVE(index);
            if (!std::isfinite(body.radius) || !(body.radius > 0.0F)) continue;
            Scene::ColliderComponentUVE capsule;
            capsule.shapeType = Scene::ColliderShapeTypeUVE::Capsule;
            capsule.radius = body.radius;
            capsule.height = 2.0F * (std::max(0.0F, body.halfHeight) + body.radius);
            const ColliderDebugBuildResultUVE sub = BuildColliderDebugLinesUVE(
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
    const float degToRad = kPiUVE / 180.0F;
    constexpr std::size_t kConeSegments = 16U;

    const std::size_t jointCount = solver.GetJointCountUVE();
    for (std::size_t jointIndex = 0U; jointIndex < jointCount; ++jointIndex) {
        const RagdollJointUVE& joint = solver.GetJointUVE(jointIndex);
        if (joint.firstBody >= bodyCount || joint.secondBody >= bodyCount) continue;
        const RagdollBodyUVE& first = solver.GetBodyUVE(joint.firstBody);
        const RagdollBodyUVE& second = solver.GetBodyUVE(joint.secondBody);
        if (!Math::IsFiniteUVE(first.position) || !Math::IsFiniteUVE(first.rotation) ||
            !Math::IsFiniteUVE(second.position) || !Math::IsFiniteUVE(second.rotation)) {
            continue;
        }

        const Math::Vector3UVE pivot = first.position + Math::RotateVectorUVE(first.rotation, joint.firstAnchor);
        const Math::QuaternionUVE worldFrame = Math::MultiplyUVE(first.rotation, joint.firstFrame);
        const Math::Vector3UVE twistAxis = Math::RotateVectorUVE(worldFrame, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        const Math::Vector3UVE swing2Axis = Math::RotateVectorUVE(worldFrame, Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        const Math::Vector3UVE swing1Axis = Math::RotateVectorUVE(worldFrame, Math::Vector3UVE{0.0F, 0.0F, 1.0F});

        const bool isHinge = joint.swing1Motion == RagdollMotionUVE::Locked &&
                             joint.swing2Motion == RagdollMotionUVE::Locked &&
                             joint.twistMotion == RagdollMotionUVE::Limited;

        if (isHinge) {
            // Hinge axis pin bar.
            if (!PushLineUVE(pivot - twistAxis * (coneLen * 0.45F), pivot + twistAxis * (coneLen * 0.45F),
                             outLines, result)) {
                return result;
            }
            // Twist limit arc fan in the plane orthogonal to `twistAxis`.
            const float lowerRad =
                (joint.twistAsymmetric ? joint.twistLowerLimitDegrees : -joint.twistLimitDegrees) * degToRad;
            const float upperRad =
                (joint.twistAsymmetric ? joint.twistUpperLimitDegrees : joint.twistLimitDegrees) * degToRad;
            const Math::Vector3UVE refDir = swing2Axis;
            const Math::Vector3UVE orthoDir = swing1Axis;
            const auto arcPoint = [&](const float angle) noexcept -> Math::Vector3UVE {
                return pivot + (refDir * std::cos(angle) + orthoDir * std::sin(angle)) * coneLen;
            };
            const Math::Vector3UVE startPt = arcPoint(lowerRad);
            const Math::Vector3UVE endPt = arcPoint(upperRad);
            if (!PushLineUVE(pivot, startPt, outLines, result)) return result;
            if (!PushLineUVE(pivot, endPt, outLines, result)) return result;

            Math::Vector3UVE prev = startPt;
            const float span = upperRad - lowerRad;
            for (std::size_t s = 1U; s <= kConeSegments; ++s) {
                const float angle = lowerRad + span * (static_cast<float>(s) / static_cast<float>(kConeSegments));
                const Math::Vector3UVE cur = arcPoint(angle);
                if (!PushLineUVE(prev, cur, outLines, result)) return result;
                prev = cur;
            }
            continue;
        }

        // Elliptical swing limit cone around `twistAxis` (bone rest axis).
        const float lim1 = joint.swing1Motion == RagdollMotionUVE::Limited
                               ? std::clamp(joint.swing1LimitDegrees, 1.0F, 175.0F) * degToRad
                               : 0.0F;
        const float lim2 = joint.swing2Motion == RagdollMotionUVE::Limited
                               ? std::clamp(joint.swing2LimitDegrees, 1.0F, 175.0F) * degToRad
                               : 0.0F;
        if (lim1 > 0.0F || lim2 > 0.0F) {
            const auto conePoint = [&](const float phi) noexcept -> Math::Vector3UVE {
                const float s1 = lim1 * std::cos(phi);
                const float s2 = lim2 * std::sin(phi);
                const float theta = std::hypot(s1, s2);
                Math::Vector3UVE dir = twistAxis;
                if (theta > 1.0e-5F) {
                    const Math::Vector3UVE rotAxis = (swing1Axis * (s1 / theta)) + (swing2Axis * (s2 / theta));
                    Math::QuaternionUVE q;
                    if (Math::TryMakeAxisAngleUVE(rotAxis, theta, q)) {
                        dir = Math::RotateVectorUVE(q, twistAxis);
                    }
                }
                return pivot + dir * coneLen;
            };
            const float step = (2.0F * kPiUVE) / static_cast<float>(kConeSegments);
            Math::Vector3UVE prev = conePoint(0.0F);
            for (std::size_t s = 1U; s <= kConeSegments; ++s) {
                const Math::Vector3UVE cur = conePoint(step * static_cast<float>(s));
                if (!PushLineUVE(prev, cur, outLines, result)) return result;
                prev = cur;
            }
            // Four cardinal meridian rays from joint pivot to cone rim.
            for (std::size_t q = 0U; q < 4U; ++q) {
                if (!PushLineUVE(pivot, conePoint(0.5F * kPiUVE * static_cast<float>(q)), outLines, result)) {
                    return result;
                }
            }
        }
        // Current child frame axis ray so the author sees live deviation inside the limit cone.
        const Math::QuaternionUVE secondWorldFrame = Math::MultiplyUVE(second.rotation, joint.secondFrame);
        const Math::Vector3UVE currentAxis =
            Math::RotateVectorUVE(secondWorldFrame, Math::Vector3UVE{1.0F, 0.0F, 0.0F});
        if (!PushLineUVE(pivot, pivot + currentAxis * (coneLen * 1.15F), outLines, result)) {
            return result;
        }
    }

    return result;
}

} // namespace UVE::Physics
