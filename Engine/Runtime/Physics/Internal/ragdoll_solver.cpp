// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ragdoll_solver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Physics {
namespace {

constexpr float kPiUVE = 3.14159265358979323846F;
constexpr float kTwoPiUVE = 6.28318530717958647692F;
/// Below this a direction or an angle is noise, and dividing by it produces the kind of large number
/// that turns a solver into a fountain.
constexpr float kDirectionEpsilonUVE = 1.0e-6F;
/// A bias that can drive an angular velocity of thousands of rad/s is a bug, not a correction. Ten
/// rad/s is already far past any joint that is about to be seen.
constexpr float kMaximumJointBiasUVE = 10.0F;
/// Restitution only means something above this approach speed. Below it a resting body would bounce
/// on the last millimetre of every contact and never settle.
constexpr float kRestitutionThresholdUVE = 1.0F;
/// One position correction is capped so a single bad frame cannot teleport a limb across the map.
constexpr float kMaximumContactCorrectionUVE = 0.25F;
/// Anchor error below this is float noise in a nineteen-body chain, not drift worth reporting.
constexpr float kJointDriftToleranceUVE = 0.002F;
/// A rotation below this many radians is not worth a quaternion multiply in the projection pass.
constexpr float kMinimumProjectionAngleUVE = 1.0e-4F;
/// Two radians of swing is past any human joint, so a decomposition that reports more than this is
/// reading a degenerate quaternion rather than a pose.
constexpr float kMaximumSwingAngleUVE = kPiUVE + 0.01F;
/// Small bind-pose overlap margin (1 cm) when building Jolt-style self-collision pair masks.
constexpr float kBindOverlapMarginUVE = 0.01F;

[[nodiscard]] float WrapAngleUVE(const float radians) noexcept {
    if (!std::isfinite(radians)) return 0.0F;
    float wrapped = std::fmod(radians + kPiUVE, kTwoPiUVE);
    if (wrapped < 0.0F) wrapped += kTwoPiUVE;
    return wrapped - kPiUVE;
}

[[nodiscard]] Math::Matrix3x3UVE SkewUVE(const Math::Vector3UVE& value) noexcept {
    Math::Matrix3x3UVE result;
    result.m[0][0] = 0.0F;
    result.m[0][1] = -value.z;
    result.m[0][2] = value.y;
    result.m[1][0] = value.z;
    result.m[1][1] = 0.0F;
    result.m[1][2] = -value.x;
    result.m[2][0] = -value.y;
    result.m[2][1] = value.x;
    result.m[2][2] = 0.0F;
    return result;
}

[[nodiscard]] Math::Vector3UVE MultiplyUVE(const Math::Matrix3x3UVE& matrix,
                                           const Math::Vector3UVE& vector) noexcept {
    return Math::Vector3UVE{
        matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z,
        matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z,
        matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z,
    };
}

/// Rotation matrix of a unit quaternion, row-major.
[[nodiscard]] Math::Matrix3x3UVE RotationMatrixUVE(const Math::QuaternionUVE& rotation) noexcept {
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Math::Matrix3x3UVE result;
    result.m[0][0] = 1.0F - 2.0F * (yy + zz);
    result.m[0][1] = 2.0F * (xy - wz);
    result.m[0][2] = 2.0F * (xz + wy);
    result.m[1][0] = 2.0F * (xy + wz);
    result.m[1][1] = 1.0F - 2.0F * (xx + zz);
    result.m[1][2] = 2.0F * (yz - wx);
    result.m[2][0] = 2.0F * (xz - wy);
    result.m[2][1] = 2.0F * (yz + wx);
    result.m[2][2] = 1.0F - 2.0F * (xx + yy);
    return result;
}

/// `K = inverseMassSum * I - sum(skew(r) * I^-1 * skew(r))`, the effective inverse mass a unit
/// impulse applied at `r` sees along its own direction.
[[nodiscard]] Math::Matrix3x3UVE ComputePointEffectiveMassUVE(
    const std::array<Math::Vector3UVE, 2U>& offsets,
    const std::array<Math::Matrix3x3UVE, 2U>& worldInverseInertia, const float inverseMassSum) noexcept {
    Math::Matrix3x3UVE result;
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            result.m[row][column] = row == column ? inverseMassSum : 0.0F;
        }
    }
    for (std::size_t body = 0U; body < 2U; ++body) {
        const Math::Matrix3x3UVE skew = SkewUVE(offsets[body]);
        const Math::Matrix3x3UVE term = skew * worldInverseInertia[body] * skew;
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                result.m[row][column] -= term.m[row][column];
            }
        }
    }
    return result;
}

/// Two orthonormal tangents for a unit normal, chosen deterministically from the normal itself.
void BuildTangentsUVE(const Math::Vector3UVE& normal, Math::Vector3UVE& outTangent1,
                      Math::Vector3UVE& outTangent2) noexcept {
    const Math::Vector3UVE reference =
        std::fabs(normal.y) < 0.9F ? Math::Vector3UVE{0.0F, 1.0F, 0.0F} : Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    const Math::Vector3UVE first = Math::CrossUVE(normal, reference);
    const float lengthSquared = Math::LengthSquaredUVE(first);
    if (!(lengthSquared > kDirectionEpsilonUVE)) {
        outTangent1 = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
        outTangent2 = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
        return;
    }
    outTangent1 = first * (1.0F / std::sqrt(lengthSquared));
    outTangent2 = Math::CrossUVE(normal, outTangent1);
}

/// Closest points between two finite line segments (Ericson, Real-Time Collision Detection §5.1.9).
void ClosestPointsBetweenSegmentsUVE(const Math::Vector3UVE& p1, const Math::Vector3UVE& q1,
                                     const Math::Vector3UVE& p2, const Math::Vector3UVE& q2,
                                     Math::Vector3UVE& outC1, Math::Vector3UVE& outC2) noexcept {
    const Math::Vector3UVE d1 = q1 - p1;
    const Math::Vector3UVE d2 = q2 - p2;
    const Math::Vector3UVE r = p1 - p2;
    const float a = Math::DotUVE(d1, d1);
    const float e = Math::DotUVE(d2, d2);
    const float f = Math::DotUVE(d2, r);
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
        const float c = Math::DotUVE(d1, r);
        if (e <= 1.0e-12F) {
            t = 0.0F;
            s = std::clamp(-c / a, 0.0F, 1.0F);
        } else {
            const float b = Math::DotUVE(d1, d2);
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

/// Shortest-arc axis-angle error vector representing `target * conjugate(current)`.
[[nodiscard]] Math::Vector3UVE QuaternionRotationErrorVectorUVE(const Math::QuaternionUVE& current,
                                                                const Math::QuaternionUVE& target) noexcept {
    Math::QuaternionUVE error = Math::MultiplyUVE(target, Math::ConjugateUVE(current));
    if (!Math::IsFiniteUVE(error)) return Math::Vector3UVE{};
    if (error.w < 0.0F) {
        error.x = -error.x;
        error.y = -error.y;
        error.z = -error.z;
        error.w = -error.w;
    }
    const Math::Vector3UVE imag{error.x, error.y, error.z};
    const float sinHalf = std::sqrt(Math::LengthSquaredUVE(imag));
    if (sinHalf > 1.0e-6F) {
        const float angle = 2.0F * std::atan2(sinHalf, std::clamp(error.w, -1.0F, 1.0F));
        return imag * (angle / sinHalf);
    }
    return imag * 2.0F;
}

/// Jolt/Havok-style conditioning on a single diagonal inverse-inertia vector: caps the ratio
/// between largest and smallest positive inverse-inertia components (lifting tiny axial inertia).
[[nodiscard]] Math::Vector3UVE ConditionSingleInverseInertiaUVE(const Math::Vector3UVE& invI,
                                                                const float maxPrincipalRatio) noexcept {
    if (!Math::IsFiniteUVE(invI)) return Math::Vector3UVE{};
    const float ratio = std::max(1.0F, std::isfinite(maxPrincipalRatio) ? maxPrincipalRatio : 4.0F);
    float minPositive = std::numeric_limits<float>::infinity();
    if (invI.x > 0.0F) minPositive = std::min(minPositive, invI.x);
    if (invI.y > 0.0F) minPositive = std::min(minPositive, invI.y);
    if (invI.z > 0.0F) minPositive = std::min(minPositive, invI.z);
    if (!std::isfinite(minPositive) || !(minPositive > 0.0F)) return invI;
    const float cap = minPositive * ratio;
    return Math::Vector3UVE{
        invI.x > 0.0F ? std::min(invI.x, cap) : 0.0F,
        invI.y > 0.0F ? std::min(invI.y, cap) : 0.0F,
        invI.z > 0.0F ? std::min(invI.z, cap) : 0.0F,
    };
}

/// Swing and twist of a joint's relative rotation, about the frame's local X, Y and Z.
struct JointAnglesUVE final {
    float twist = 0.0F;
    float swing1 = 0.0F;
    float swing2 = 0.0F;
    Math::Vector3UVE swingAxis{};
};

[[nodiscard]] JointAnglesUVE DecomposeJointAnglesUVE(const Math::QuaternionUVE& relative) noexcept {
    JointAnglesUVE angles;
    if (!Math::IsFiniteUVE(relative)) return angles;

    // Twist: the component of the quaternion about the frame's X axis.
    const float twistLength = std::sqrt(relative.x * relative.x + relative.w * relative.w);
    Math::QuaternionUVE twist{};
    if (twistLength > kDirectionEpsilonUVE) {
        twist = Math::QuaternionUVE{relative.x / twistLength, 0.0F, 0.0F, relative.w / twistLength};
    }
    angles.twist = WrapAngleUVE(2.0F * std::atan2(twist.x, twist.w));

    // Swing: whatever is left once the twist is divided out.
    const Math::QuaternionUVE swing = Math::MultiplyUVE(relative, Math::ConjugateUVE(twist));
    const float swingW = std::clamp(swing.w, -1.0F, 1.0F);
    const float swingVectorLength = std::sqrt(std::max(0.0F, 1.0F - swingW * swingW));
    float swingAngle = 0.0F;
    Math::Vector3UVE swingAxis{};
    if (swingVectorLength > kDirectionEpsilonUVE) {
        swingAngle = std::min(2.0F * std::acos(swingW), kMaximumSwingAngleUVE);
        swingAxis = Math::Vector3UVE{swing.x, swing.y, swing.z} * (1.0F / swingVectorLength);
    }
    // Swing1 is rotation about the frame's Z, swing2 about its Y: twist took X, so the three always
    // describe the whole rotation with no leftovers.
    angles.swing1 = swingAxis.z * swingAngle;
    angles.swing2 = swingAxis.y * swingAngle;
    angles.swingAxis = swingAxis * swingAngle;
    return angles;
}

/// The interval one angular axis is allowed to occupy, given its authored motion and limit.
struct AxisRangeUVE final {
    bool constrained = false;
    bool bilateral = false;
    float lower = 0.0F;
    float upper = 0.0F;
};

[[nodiscard]] AxisRangeUVE ResolveAxisRangeUVE(const RagdollMotionUVE motion, const float angle,
                                               const float lowerRadians, const float upperRadians) noexcept {
    AxisRangeUVE range;
    if (motion == RagdollMotionUVE::Free) return range;
    if (motion == RagdollMotionUVE::Locked) {
        range.constrained = true;
        range.bilateral = true;
        range.lower = 0.0F;
        range.upper = 0.0F;
        return range;
    }
    range.lower = lowerRadians;
    range.upper = upperRadians;
    range.constrained = angle < lowerRadians || angle > upperRadians;
    return range;
}

} // namespace

Math::Vector3UVE ComputeCapsuleInverseInertiaUVE(const float mass, const float radius,
                                                 const float halfHeight) noexcept {
    const float safeRadius = std::isfinite(radius) && radius > 0.0F ? radius : 0.0F;
    const float safeHalfHeight = std::isfinite(halfHeight) && halfHeight > 0.0F ? halfHeight : 0.0F;
    if (!(safeRadius > 0.0F) || !std::isfinite(mass) || mass <= 0.0F) {
        return Math::Vector3UVE{};
    }
    const double massValue = static_cast<double>(mass);
    const double radiusValue = static_cast<double>(safeRadius);
    double axial = 0.4 * massValue * radiusValue * radiusValue;
    double lateral = axial;
    if (safeHalfHeight > 0.0F) {
        const double lengthValue = 2.0 * (static_cast<double>(safeHalfHeight) + radiusValue);
        axial = 0.5 * massValue * radiusValue * radiusValue;
        lateral = massValue * (3.0 * radiusValue * radiusValue + lengthValue * lengthValue) / 12.0;
    }
    if (!(axial > 0.0) || !(lateral > 0.0)) return Math::Vector3UVE{};
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    const double inverseAxial = 1.0 / axial;
    const double inverseLateral = 1.0 / lateral;
    if (!std::isfinite(inverseAxial) || !std::isfinite(inverseLateral) || inverseAxial > maximum ||
        inverseLateral > maximum) {
        return Math::Vector3UVE{};
    }
    // Local +Y is the capsule's long axis.
    return Math::Vector3UVE{static_cast<float>(inverseLateral), static_cast<float>(inverseAxial),
                            static_cast<float>(inverseLateral)};
}

void ComputeRagdollCapsuleWorldSegmentUVE(const RagdollBodyUVE& body, Math::Vector3UVE& outStart,
                                          Math::Vector3UVE& outEnd) noexcept {
    const float halfHeight = std::isfinite(body.halfHeight) && body.halfHeight > 0.0F ? body.halfHeight : 0.0F;
    const Math::Vector3UVE offset = Math::RotateVectorUVE(body.rotation, Math::Vector3UVE{0.0F, halfHeight, 0.0F});
    outStart = body.position - offset;
    outEnd = body.position + offset;
}

Math::QuaternionUVE MakeJointFrameFromTwistAxisUVE(const Math::Vector3UVE& axis) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(axis);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > kDirectionEpsilonUVE)) return Math::QuaternionUVE{};
    const Math::Vector3UVE normalised = axis * (1.0F / std::sqrt(lengthSquared));
    const Math::Vector3UVE from{1.0F, 0.0F, 0.0F};
    const float cosine = Math::DotUVE(from, normalised);
    if (cosine >= 1.0F - 1.0e-6F) return Math::QuaternionUVE{};

    Math::Vector3UVE rotationAxis = Math::CrossUVE(from, normalised);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    const float rotationLengthSquared = Math::LengthSquaredUVE(rotationAxis);
    if (!(rotationLengthSquared > kDirectionEpsilonUVE)) {
        rotationAxis = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    } else {
        rotationAxis = rotationAxis * (1.0F / std::sqrt(rotationLengthSquared));
    }

    Math::QuaternionUVE rotation;
    if (!Math::TryMakeAxisAngleUVE(rotationAxis, std::atan2(sine, cosine), rotation)) {
        return Math::QuaternionUVE{};
    }
    return rotation;
}

Math::QuaternionUVE ComputeJointSecondFrameUVE(const Math::QuaternionUVE& firstBindRotation,
                                               const Math::QuaternionUVE& secondBindRotation,
                                               const Math::QuaternionUVE& firstFrame) noexcept {
    Math::QuaternionUVE inverseSecond;
    if (!Math::TryInverseUVE(secondBindRotation, inverseSecond)) return Math::QuaternionUVE{};
    const Math::QuaternionUVE candidate =
        Math::MultiplyUVE(Math::MultiplyUVE(inverseSecond, firstBindRotation), firstFrame);
    Math::QuaternionUVE normalised;
    return Math::TryNormalizeUVE(candidate, normalised) ? normalised : Math::QuaternionUVE{};
}

RagdollJointUVE MakeBallSocketJointUVE(const std::uint32_t firstBody, const std::uint32_t secondBody,
                                       const Math::Vector3UVE& firstAnchor,
                                       const Math::Vector3UVE& secondAnchor,
                                       const float swingLimitDegrees) noexcept {
    RagdollJointUVE joint;
    joint.firstBody = firstBody;
    joint.secondBody = secondBody;
    joint.firstAnchor = firstAnchor;
    joint.secondAnchor = secondAnchor;
    joint.swing1Motion = RagdollMotionUVE::Limited;
    joint.swing2Motion = RagdollMotionUVE::Limited;
    joint.twistMotion = RagdollMotionUVE::Limited;
    joint.swing1LimitDegrees = swingLimitDegrees;
    joint.swing2LimitDegrees = swingLimitDegrees;
    joint.twistLimitDegrees = swingLimitDegrees;
    return joint;
}

RagdollJointUVE MakeHingeJointUVE(const std::uint32_t firstBody, const std::uint32_t secondBody,
                                  const Math::Vector3UVE& firstAnchor, const Math::Vector3UVE& secondAnchor,
                                  const float twistLimitDegrees) noexcept {
    RagdollJointUVE joint = MakeBallSocketJointUVE(firstBody, secondBody, firstAnchor, secondAnchor);
    joint.swing1Motion = RagdollMotionUVE::Locked;
    joint.swing2Motion = RagdollMotionUVE::Locked;
    joint.twistMotion = RagdollMotionUVE::Limited;
    joint.twistLimitDegrees = twistLimitDegrees;
    return joint;
}

bool RagdollSolverUVE::ResetUVE(const std::span<const RagdollBodyUVE> bodies) {
    if (bodies.size() > kMaximumRagdollBodiesUVE) return false;
    m_bodies = {};
    m_effectiveInverseInertiaLocal = {};
    m_selfCollisionMask = {};
    m_bodyCount = 0U;
    m_joints = {};
    m_jointCount = 0U;
    m_lastContactFeedbackCount = 0U;
    m_sleepTimer = 0.0F;
    m_sleeping = false;
    m_lastStepSeconds = kRagdollFixedStepSecondsUVE;
    for (const RagdollBodyUVE& authored : bodies) {
        RagdollBodyUVE body = authored;
        body.inverseInertiaLocal = ComputeCapsuleInverseInertiaUVE(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
        m_bodies[m_bodyCount] = body;
        m_effectiveInverseInertiaLocal[m_bodyCount] = body.kinematic ? Math::Vector3UVE{} : body.inverseInertiaLocal;
        ++m_bodyCount;
    }
    // Enable all non-self pairs by default; AddJointUVE automatically clears direct parent-child pairs.
    const std::uint32_t allBodiesMask =
        m_bodyCount >= 32U ? 0xFFFFFFFFU : ((std::uint32_t{1U} << m_bodyCount) - 1U);
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_selfCollisionMask[index] = allBodiesMask & ~(std::uint32_t{1U} << index);
    }
    return true;
}

bool RagdollSolverUVE::AddJointUVE(const RagdollJointUVE& joint) {
    if (joint.firstBody >= m_bodyCount || joint.secondBody >= m_bodyCount ||
        joint.firstBody == joint.secondBody || m_jointCount >= kMaximumRagdollJointsUVE) {
        return false;
    }
    m_joints[m_jointCount++] = joint;
    // Direct parent-child bodies overlap at the joint pivot; always disable their self-collision.
    SetSelfCollisionPairEnabledUVE(joint.firstBody, joint.secondBody, false);
    return true;
}

const RagdollBodyUVE& RagdollSolverUVE::GetBodyUVE(const std::size_t index) const noexcept {
    return m_bodies[std::min(index, kMaximumRagdollBodiesUVE - 1U)];
}

RagdollBodyUVE& RagdollSolverUVE::GetBodyUVE(const std::size_t index) noexcept {
    return m_bodies[std::min(index, kMaximumRagdollBodiesUVE - 1U)];
}

const RagdollJointUVE& RagdollSolverUVE::GetJointUVE(const std::size_t index) const noexcept {
    return m_joints[std::min(index, kMaximumRagdollJointsUVE - 1U)];
}

RagdollJointUVE& RagdollSolverUVE::GetJointUVE(const std::size_t index) noexcept {
    return m_joints[std::min(index, kMaximumRagdollJointsUVE - 1U)];
}

void RagdollSolverUVE::RebuildInertiaUVE() noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        body.inverseInertiaLocal = ComputeCapsuleInverseInertiaUVE(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
        m_effectiveInverseInertiaLocal[index] = body.kinematic ? Math::Vector3UVE{} : body.inverseInertiaLocal;
    }
}

void RagdollSolverUVE::StabilizeInertiaUVE(const float maxPrincipalRatio, const float maxChainRatio) noexcept {
    const float principalRatio = std::max(1.0F, std::isfinite(maxPrincipalRatio) ? maxPrincipalRatio : 4.0F);
    const float chainRatio = std::max(1.0F, std::isfinite(maxChainRatio) ? maxChainRatio : 16.0F);

    float minBodyMaxInvI = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        body.inverseInertiaLocal = ConditionSingleInverseInertiaUVE(body.inverseInertiaLocal, principalRatio);
        const float maxComp =
            std::max({body.inverseInertiaLocal.x, body.inverseInertiaLocal.y, body.inverseInertiaLocal.z});
        if (!body.kinematic && maxComp > 0.0F) {
            minBodyMaxInvI = std::min(minBodyMaxInvI, maxComp);
        }
    }
    if (std::isfinite(minBodyMaxInvI) && minBodyMaxInvI > 0.0F) {
        const float chainCap = minBodyMaxInvI * chainRatio;
        for (std::size_t index = 0U; index < m_bodyCount; ++index) {
            RagdollBodyUVE& body = m_bodies[index];
            body.inverseInertiaLocal.x = std::min(body.inverseInertiaLocal.x, chainCap);
            body.inverseInertiaLocal.y = std::min(body.inverseInertiaLocal.y, chainCap);
            body.inverseInertiaLocal.z = std::min(body.inverseInertiaLocal.z, chainCap);
            m_effectiveInverseInertiaLocal[index] =
                body.kinematic ? Math::Vector3UVE{} : body.inverseInertiaLocal;
        }
    }
}

void RagdollSolverUVE::SetSelfCollisionPairEnabledUVE(const std::uint32_t firstBody,
                                                      const std::uint32_t secondBody,
                                                      const bool enabled) noexcept {
    if (firstBody >= m_bodyCount || secondBody >= m_bodyCount || firstBody == secondBody) return;
    const std::uint32_t bitFirst = std::uint32_t{1U} << firstBody;
    const std::uint32_t bitSecond = std::uint32_t{1U} << secondBody;
    if (enabled) {
        m_selfCollisionMask[firstBody] |= bitSecond;
        m_selfCollisionMask[secondBody] |= bitFirst;
    } else {
        m_selfCollisionMask[firstBody] &= ~bitSecond;
        m_selfCollisionMask[secondBody] &= ~bitFirst;
    }
}

bool RagdollSolverUVE::CanSelfCollideUVE(const std::uint32_t firstBody,
                                         const std::uint32_t secondBody) const noexcept {
    if (firstBody >= m_bodyCount || secondBody >= m_bodyCount || firstBody == secondBody) return false;
    return (m_selfCollisionMask[firstBody] & (std::uint32_t{1U} << secondBody)) != 0U;
}

void RagdollSolverUVE::RebuildSelfCollisionFilterUVE(const bool disableCurrentPoseOverlaps,
                                                     const std::uint32_t minimumJointHops) noexcept {
    const std::uint32_t allBodiesMask =
        m_bodyCount >= 32U ? 0xFFFFFFFFU : ((std::uint32_t{1U} << m_bodyCount) - 1U);
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_selfCollisionMask[index] = allBodiesMask & ~(std::uint32_t{1U} << index);
    }

    // All-pairs shortest hop distance along the joint graph (bounded at <= 24 bodies).
    constexpr std::uint32_t kInfHops = 64U;
    std::array<std::array<std::uint32_t, kMaximumRagdollBodiesUVE>, kMaximumRagdollBodiesUVE> dist{};
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        for (std::size_t j = 0U; j < m_bodyCount; ++j) {
            dist[i][j] = (i == j) ? 0U : kInfHops;
        }
    }
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJointUVE& joint = m_joints[jointIndex];
        dist[joint.firstBody][joint.secondBody] = 1U;
        dist[joint.secondBody][joint.firstBody] = 1U;
    }
    for (std::size_t k = 0U; k < m_bodyCount; ++k) {
        for (std::size_t i = 0U; i < m_bodyCount; ++i) {
            if (dist[i][k] == kInfHops) continue;
            for (std::size_t j = 0U; j < m_bodyCount; ++j) {
                const std::uint32_t via = dist[i][k] + dist[k][j];
                if (via < dist[i][j]) dist[i][j] = via;
            }
        }
    }

    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> segStarts{};
    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> segEnds{};
    if (disableCurrentPoseOverlaps) {
        for (std::size_t i = 0U; i < m_bodyCount; ++i) {
            ComputeRagdollCapsuleWorldSegmentUVE(m_bodies[i], segStarts[i], segEnds[i]);
        }
    }

    const std::uint32_t minHops = std::max<std::uint32_t>(1U, minimumJointHops);
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        for (std::size_t j = i + 1U; j < m_bodyCount; ++j) {
            if (dist[i][j] < minHops) {
                SetSelfCollisionPairEnabledUVE(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j), false);
                continue;
            }
            if (disableCurrentPoseOverlaps) {
                Math::Vector3UVE c1{};
                Math::Vector3UVE c2{};
                ClosestPointsBetweenSegmentsUVE(segStarts[i], segEnds[i], segStarts[j], segEnds[j], c1, c2);
                const float combinedRadius =
                    std::max(0.0F, m_bodies[i].radius) + std::max(0.0F, m_bodies[j].radius) + kBindOverlapMarginUVE;
                if (Math::LengthSquaredUVE(c2 - c1) < combinedRadius * combinedRadius) {
                    SetSelfCollisionPairEnabledUVE(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j),
                                                   false);
                }
            }
        }
    }
}

float RagdollSolverUVE::GetTotalMassUVE() const noexcept {
    float total = 0.0F;
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        const float mass = m_bodies[index].mass;
        if (std::isfinite(mass) && mass > 0.0F) total += mass;
    }
    return total;
}

void RagdollSolverUVE::SleepUVE() noexcept {
    m_sleeping = true;
    m_sleepTimer = 0.0F;
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_bodies[index].linearVelocity = Math::Vector3UVE{};
        m_bodies[index].angularVelocity = Math::Vector3UVE{};
    }
}

void RagdollSolverUVE::WakeUVE() noexcept {
    m_sleeping = false;
    m_sleepTimer = 0.0F;
}

float RagdollSolverUVE::EffectiveInverseMassUVE(const std::size_t bodyIndex) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    if (body.kinematic || !std::isfinite(body.inverseMass) || !(body.inverseMass > 0.0F)) return 0.0F;
    return body.inverseMass;
}

void RagdollSolverUVE::PrepareEffectiveInertiasUVE(const RagdollStepSettingsUVE& settings) noexcept {
    float minBodyMaxInvI = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        const RagdollBodyUVE& body = m_bodies[index];
        if (body.kinematic) {
            m_effectiveInverseInertiaLocal[index] = Math::Vector3UVE{};
            continue;
        }
        Math::Vector3UVE invI = body.inverseInertiaLocal;
        if (settings.stabilizeInertia) {
            invI = ConditionSingleInverseInertiaUVE(invI, settings.maxPrincipalInertiaRatio);
        }
        m_effectiveInverseInertiaLocal[index] = invI;
        const float maxComp = std::max({invI.x, invI.y, invI.z});
        if (maxComp > 0.0F) {
            minBodyMaxInvI = std::min(minBodyMaxInvI, maxComp);
        }
    }
    if (settings.stabilizeInertia && m_jointCount > 0U && std::isfinite(minBodyMaxInvI) && minBodyMaxInvI > 0.0F) {
        const float chainRatio =
            std::max(1.0F, std::isfinite(settings.maxChainInertiaRatio) ? settings.maxChainInertiaRatio : 16.0F);
        const float chainCap = minBodyMaxInvI * chainRatio;
        for (std::size_t index = 0U; index < m_bodyCount; ++index) {
            if (m_bodies[index].kinematic) continue;
            m_effectiveInverseInertiaLocal[index].x = std::min(m_effectiveInverseInertiaLocal[index].x, chainCap);
            m_effectiveInverseInertiaLocal[index].y = std::min(m_effectiveInverseInertiaLocal[index].y, chainCap);
            m_effectiveInverseInertiaLocal[index].z = std::min(m_effectiveInverseInertiaLocal[index].z, chainCap);
        }
    }
}

std::size_t RagdollSolverUVE::GenerateSelfContactsUVE(const std::span<RagdollContactUVE> outSelfContacts) const noexcept {
    if (outSelfContacts.empty() || m_bodyCount < 2U) return 0U;
    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> segStarts{};
    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> segEnds{};
    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> aabbMin{};
    std::array<Math::Vector3UVE, kMaximumRagdollBodiesUVE> aabbMax{};

    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        ComputeRagdollCapsuleWorldSegmentUVE(m_bodies[i], segStarts[i], segEnds[i]);
        const float r = std::max(0.0F, m_bodies[i].radius);
        aabbMin[i] = Math::Vector3UVE{std::min(segStarts[i].x, segEnds[i].x) - r,
                                      std::min(segStarts[i].y, segEnds[i].y) - r,
                                      std::min(segStarts[i].z, segEnds[i].z) - r};
        aabbMax[i] = Math::Vector3UVE{std::max(segStarts[i].x, segEnds[i].x) + r,
                                      std::max(segStarts[i].y, segEnds[i].y) + r,
                                      std::max(segStarts[i].z, segEnds[i].z) + r};
    }

    std::size_t count = 0U;
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        const float ri = m_bodies[i].radius;
        if (!std::isfinite(ri) || !(ri > 1.0e-4F)) continue;
        const std::uint32_t mask = m_selfCollisionMask[i];
        for (std::size_t j = i + 1U; j < m_bodyCount; ++j) {
            if ((mask & (std::uint32_t{1U} << j)) == 0U) continue;
            if (m_bodies[i].kinematic && m_bodies[j].kinematic) continue;
            const float rj = m_bodies[j].radius;
            if (!std::isfinite(rj) || !(rj > 1.0e-4F)) continue;

            // Fast AABB rejection before segment-segment distance.
            if (aabbMax[i].x < aabbMin[j].x || aabbMin[i].x > aabbMax[j].x ||
                aabbMax[i].y < aabbMin[j].y || aabbMin[i].y > aabbMax[j].y ||
                aabbMax[i].z < aabbMin[j].z || aabbMin[i].z > aabbMax[j].z) {
                continue;
            }

            Math::Vector3UVE c1{};
            Math::Vector3UVE c2{};
            ClosestPointsBetweenSegmentsUVE(segStarts[i], segEnds[i], segStarts[j], segEnds[j], c1, c2);
            const Math::Vector3UVE delta = c1 - c2; // Points from j -> i (pushes body i out of j).
            const float distSq = Math::LengthSquaredUVE(delta);
            const float radiusSum = ri + rj;
            if (!(distSq < radiusSum * radiusSum)) continue;

            Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
            float dist = 0.0F;
            if (distSq > 1.0e-12F) {
                dist = std::sqrt(distSq);
                normal = delta * (1.0F / dist);
            } else {
                const Math::Vector3UVE fallback = m_bodies[i].position - m_bodies[j].position;
                const float fbSq = Math::LengthSquaredUVE(fallback);
                if (fbSq > 1.0e-12F) {
                    normal = fallback * (1.0F / std::sqrt(fbSq));
                }
            }
            const float depth = radiusSum - dist;
            if (!(depth > 0.0F)) continue;

            RagdollContactUVE contact;
            contact.body = static_cast<std::uint32_t>(i);
            contact.otherBody = static_cast<std::uint32_t>(j);
            contact.point = (c1 - normal * ri + c2 + normal * rj) * 0.5F;
            contact.normal = normal;
            contact.depth = depth;
            contact.friction = std::sqrt(std::max(0.0F, m_bodies[i].friction * m_bodies[j].friction));
            contact.restitution = 0.0F;
            outSelfContacts[count++] = contact;
            if (count >= outSelfContacts.size()) return count;
        }
    }
    return count;
}

Math::Vector3UVE RagdollSolverUVE::TransformInverseInertiaUVE(const std::size_t bodyIndex,
                                                              const Math::Vector3UVE& worldVector) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    const Math::Vector3UVE& invI = m_effectiveInverseInertiaLocal[bodyIndex];
    if (body.kinematic || (invI.x == 0.0F && invI.y == 0.0F && invI.z == 0.0F)) {
        return Math::Vector3UVE{};
    }
    const Math::Vector3UVE local = Math::RotateVectorUVE(Math::ConjugateUVE(body.rotation), worldVector);
    const Math::Vector3UVE scaled{local.x * invI.x, local.y * invI.y, local.z * invI.z};
    return Math::RotateVectorUVE(body.rotation, scaled);
}

Math::Matrix3x3UVE RagdollSolverUVE::WorldInverseInertiaUVE(const std::size_t bodyIndex) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    const Math::Vector3UVE& invI = m_effectiveInverseInertiaLocal[bodyIndex];
    if (body.kinematic || (invI.x == 0.0F && invI.y == 0.0F && invI.z == 0.0F)) {
        return Math::Matrix3x3UVE{};
    }
    const Math::Matrix3x3UVE rotation = RotationMatrixUVE(body.rotation);
    Math::Matrix3x3UVE diagonal;
    diagonal.m[0][0] = invI.x;
    diagonal.m[1][1] = invI.y;
    diagonal.m[2][2] = invI.z;
    return rotation * diagonal * Math::TransposeUVE(rotation);
}

Math::Vector3UVE RagdollSolverUVE::AnchorVelocityUVE(const std::size_t bodyIndex,
                                                     const Math::Vector3UVE& worldOffset) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    return body.linearVelocity + Math::CrossUVE(body.angularVelocity, worldOffset);
}

void RagdollSolverUVE::IntegrateVelocitiesUVE(const Math::Vector3UVE& gravity,
                                              const float deltaTimeSeconds) noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        if (body.kinematic) continue;
        const float invMass = EffectiveInverseMassUVE(index);
        if (!(invMass > 0.0F) && Math::LengthSquaredUVE(m_effectiveInverseInertiaLocal[index]) <= 0.0F) continue;
        if (body.gravityEnabled && invMass > 0.0F) body.linearVelocity += gravity * deltaTimeSeconds;
        const float linearFactor = 1.0F / (1.0F + std::max(0.0F, body.linearDamping) * deltaTimeSeconds);
        const float angularFactor = 1.0F / (1.0F + std::max(0.0F, body.angularDamping) * deltaTimeSeconds);
        body.linearVelocity = body.linearVelocity * linearFactor;
        body.angularVelocity = body.angularVelocity * angularFactor;
    }
}

void RagdollSolverUVE::SolveJointVelocityUVE(const RagdollJointUVE& joint) noexcept {
    RagdollBodyUVE& first = m_bodies[joint.firstBody];
    RagdollBodyUVE& second = m_bodies[joint.secondBody];
    const float firstInvMass = EffectiveInverseMassUVE(joint.firstBody);
    const float secondInvMass = EffectiveInverseMassUVE(joint.secondBody);
    const float inverseMassSum = firstInvMass + secondInvMass;
    if (!(inverseMassSum > 0.0F)) return;

    const Math::Vector3UVE firstOffset = Math::RotateVectorUVE(first.rotation, joint.firstAnchor);
    const Math::Vector3UVE secondOffset = Math::RotateVectorUVE(second.rotation, joint.secondAnchor);
    const Math::Vector3UVE relativeVelocity =
        AnchorVelocityUVE(joint.secondBody, secondOffset) - AnchorVelocityUVE(joint.firstBody, firstOffset);
    if (!Math::IsFiniteUVE(relativeVelocity)) return;

    const Math::Matrix3x3UVE effectiveMass =
        ComputePointEffectiveMassUVE({firstOffset, secondOffset},
                                     {WorldInverseInertiaUVE(joint.firstBody), WorldInverseInertiaUVE(joint.secondBody)},
                                     inverseMassSum);
    Math::Matrix3x3UVE inverseEffectiveMass;
    if (!Math::TryInverseUVE(effectiveMass, inverseEffectiveMass)) return;

    const Math::Vector3UVE impulse = MultiplyUVE(inverseEffectiveMass, relativeVelocity) * -1.0F;
    if (!Math::IsFiniteUVE(impulse)) return;

    first.linearVelocity -= impulse * firstInvMass;
    first.angularVelocity -= TransformInverseInertiaUVE(joint.firstBody, Math::CrossUVE(firstOffset, impulse));
    second.linearVelocity += impulse * secondInvMass;
    second.angularVelocity += TransformInverseInertiaUVE(joint.secondBody, Math::CrossUVE(secondOffset, impulse));
}

void RagdollSolverUVE::SolveJointMotorUVE(const RagdollJointUVE& joint, JointAccumulatorsUVE& accum,
                                          const float deltaTimeSeconds) noexcept {
    const RagdollJointMotorUVE& motor = joint.motor;
    if (!motor.enabled) return;
    const float scale = std::clamp(motor.strengthScale, 0.0F, 1.0F);
    if (!(scale > 0.0F)) return;
    const float stiffness = std::max(0.0F, std::isfinite(motor.angularStiffness) ? motor.angularStiffness : 0.0F) * scale;
    const float damping = std::max(0.0F, std::isfinite(motor.angularDamping) ? motor.angularDamping : 0.0F) * scale;
    const float maxTorque = std::max(0.0F, std::isfinite(motor.maxTorque) ? motor.maxTorque : 0.0F) * scale;
    if (!(maxTorque > 0.0F) || (!(stiffness > 0.0F) && !(damping > 0.0F))) return;

    RagdollBodyUVE& first = m_bodies[joint.firstBody];
    RagdollBodyUVE& second = m_bodies[joint.secondBody];
    const Math::QuaternionUVE firstFrame = Math::MultiplyUVE(first.rotation, joint.firstFrame);
    const Math::QuaternionUVE secondFrame = Math::MultiplyUVE(second.rotation, joint.secondFrame);
    Math::QuaternionUVE inverseFirstFrame;
    if (!Math::TryInverseUVE(firstFrame, inverseFirstFrame)) return;

    const Math::QuaternionUVE currentRelative = Math::MultiplyUVE(inverseFirstFrame, secondFrame);
    Math::QuaternionUVE targetRelative = motor.targetRelativeRotation;
    if (!Math::TryNormalizeUVE(targetRelative, targetRelative)) {
        targetRelative = Math::QuaternionUVE{};
    }
    const Math::Vector3UVE localAngleError = QuaternionRotationErrorVectorUVE(currentRelative, targetRelative);
    if (!Math::IsFiniteUVE(localAngleError)) return;

    const Math::Vector3UVE targetAngVel =
        Math::IsFiniteUVE(motor.targetAngularVelocity) ? motor.targetAngularVelocity : Math::Vector3UVE{};

    // Unconditionally stable implicit spring-damper denominator: 1 + c*dt + k*dt^2.
    const float denom = 1.0F + damping * deltaTimeSeconds + stiffness * deltaTimeSeconds * deltaTimeSeconds;
    const float maxImpulse = maxTorque * deltaTimeSeconds;

    const std::array<Math::Vector3UVE, 3U> localAxes{
        Math::Vector3UVE{1.0F, 0.0F, 0.0F},
        Math::Vector3UVE{0.0F, 1.0F, 0.0F},
        Math::Vector3UVE{0.0F, 0.0F, 1.0F},
    };
    const std::array<float, 3U> errors{localAngleError.x, localAngleError.y, localAngleError.z};
    const std::array<float, 3U> targetVels{targetAngVel.x, targetAngVel.y, targetAngVel.z};
    std::array<float*, 3U> accumSlots{&accum.motorAngular.x, &accum.motorAngular.y, &accum.motorAngular.z};

    for (std::size_t axisIndex = 0U; axisIndex < 3U; ++axisIndex) {
        const Math::Vector3UVE worldAxis = Math::RotateVectorUVE(firstFrame, localAxes[axisIndex]);
        const float axisLenSq = Math::LengthSquaredUVE(worldAxis);
        if (!(axisLenSq > kDirectionEpsilonUVE)) continue;
        const Math::Vector3UVE unitAxis = worldAxis * (1.0F / std::sqrt(axisLenSq));

        const float effInv =
            Math::DotUVE(unitAxis, TransformInverseInertiaUVE(joint.firstBody, unitAxis)) +
            Math::DotUVE(unitAxis, TransformInverseInertiaUVE(joint.secondBody, unitAxis));
        if (!(effInv > kDirectionEpsilonUVE)) continue;

        const float relOmega = Math::DotUVE(second.angularVelocity - first.angularVelocity, unitAxis);
        const float velError = targetVels[axisIndex] - relOmega;
        const float desiredDeltaOmega =
            (stiffness * errors[axisIndex] + damping * velError) * deltaTimeSeconds / denom;
        const float deltaImpulse = desiredDeltaOmega / effInv;
        if (!std::isfinite(deltaImpulse)) continue;

        const float oldAccum = *accumSlots[axisIndex];
        const float newAccum = std::clamp(oldAccum + deltaImpulse, -maxImpulse, maxImpulse);
        const float applied = newAccum - oldAccum;
        *accumSlots[axisIndex] = newAccum;
        if (std::fabs(applied) <= 0.0F) continue;

        const Math::Vector3UVE angularImpulse = unitAxis * applied;
        first.angularVelocity -= TransformInverseInertiaUVE(joint.firstBody, angularImpulse);
        second.angularVelocity += TransformInverseInertiaUVE(joint.secondBody, angularImpulse);
    }
}

void RagdollSolverUVE::SolveBodyDriveUVE(const std::size_t bodyIndex, BodyDriveAccumulatorUVE& accum,
                                         const float deltaTimeSeconds) noexcept {
    RagdollBodyUVE& body = m_bodies[bodyIndex];
    if (body.kinematic || !body.worldDrive.enabled) return;
    const RagdollBodyDriveUVE& drive = body.worldDrive;

    // World-space linear PD drive.
    const float invMass = EffectiveInverseMassUVE(bodyIndex);
    const float linK = std::max(0.0F, std::isfinite(drive.linearStiffness) ? drive.linearStiffness : 0.0F);
    const float linC = std::max(0.0F, std::isfinite(drive.linearDamping) ? drive.linearDamping : 0.0F);
    const float maxLinForce = std::max(0.0F, std::isfinite(drive.maxLinearForce) ? drive.maxLinearForce : 0.0F);
    if (invMass > 0.0F && maxLinForce > 0.0F && (linK > 0.0F || linC > 0.0F) &&
        Math::IsFiniteUVE(drive.targetPosition)) {
        const Math::Vector3UVE targetVel =
            Math::IsFiniteUVE(drive.targetLinearVelocity) ? drive.targetLinearVelocity : Math::Vector3UVE{};
        const Math::Vector3UVE posErr = drive.targetPosition - body.position;
        const Math::Vector3UVE velErr = targetVel - body.linearVelocity;
        const float denom = 1.0F + linC * deltaTimeSeconds + linK * deltaTimeSeconds * deltaTimeSeconds;
        const Math::Vector3UVE desiredDeltaV = (posErr * linK + velErr * linC) * (deltaTimeSeconds / denom);
        const Math::Vector3UVE deltaImpulse = desiredDeltaV * (1.0F / invMass);
        if (Math::IsFiniteUVE(deltaImpulse)) {
            Math::Vector3UVE cand = accum.linearImpulse + deltaImpulse;
            const float maxImpulse = maxLinForce * deltaTimeSeconds;
            const float magSq = Math::LengthSquaredUVE(cand);
            if (magSq > maxImpulse * maxImpulse && magSq > 1.0e-12F) {
                cand = cand * (maxImpulse / std::sqrt(magSq));
            }
            const Math::Vector3UVE applied = cand - accum.linearImpulse;
            accum.linearImpulse = cand;
            body.linearVelocity += applied * invMass;
        }
    }

    // World-space angular PD drive.
    const float angK = std::max(0.0F, std::isfinite(drive.angularStiffness) ? drive.angularStiffness : 0.0F);
    const float angC = std::max(0.0F, std::isfinite(drive.angularDamping) ? drive.angularDamping : 0.0F);
    const float maxAngTorque = std::max(0.0F, std::isfinite(drive.maxAngularTorque) ? drive.maxAngularTorque : 0.0F);
    if (maxAngTorque > 0.0F && (angK > 0.0F || angC > 0.0F)) {
        Math::QuaternionUVE targetRot = drive.targetRotation;
        if (!Math::TryNormalizeUVE(targetRot, targetRot)) targetRot = Math::QuaternionUVE{};
        const Math::Vector3UVE rotErr = QuaternionRotationErrorVectorUVE(body.rotation, targetRot);
        const Math::Vector3UVE targetOmega =
            Math::IsFiniteUVE(drive.targetAngularVelocity) ? drive.targetAngularVelocity : Math::Vector3UVE{};
        const float denom = 1.0F + angC * deltaTimeSeconds + angK * deltaTimeSeconds * deltaTimeSeconds;
        const float maxImpulse = maxAngTorque * deltaTimeSeconds;

        const std::array<Math::Vector3UVE, 3U> worldAxes{
            Math::Vector3UVE{1.0F, 0.0F, 0.0F},
            Math::Vector3UVE{0.0F, 1.0F, 0.0F},
            Math::Vector3UVE{0.0F, 0.0F, 1.0F},
        };
        const std::array<float, 3U> errComps{rotErr.x, rotErr.y, rotErr.z};
        const std::array<float, 3U> omegaTargets{targetOmega.x, targetOmega.y, targetOmega.z};
        std::array<float*, 3U> accumSlots{&accum.angularImpulse.x, &accum.angularImpulse.y, &accum.angularImpulse.z};

        for (std::size_t k = 0U; k < 3U; ++k) {
            const Math::Vector3UVE& axis = worldAxes[k];
            const float effInv = Math::DotUVE(axis, TransformInverseInertiaUVE(bodyIndex, axis));
            if (!(effInv > kDirectionEpsilonUVE)) continue;
            const float velErr = omegaTargets[k] - Math::DotUVE(body.angularVelocity, axis);
            const float desiredDeltaOmega = (angK * errComps[k] + angC * velErr) * deltaTimeSeconds / denom;
            const float deltaImpulse = desiredDeltaOmega / effInv;
            if (!std::isfinite(deltaImpulse)) continue;

            const float oldVal = *accumSlots[k];
            const float newVal = std::clamp(oldVal + deltaImpulse, -maxImpulse, maxImpulse);
            const float applied = newVal - oldVal;
            *accumSlots[k] = newVal;
            body.angularVelocity += TransformInverseInertiaUVE(bodyIndex, axis * applied);
        }
    }
}

void RagdollSolverUVE::SolveJointLimitsUVE(const RagdollJointUVE& joint, JointAccumulatorsUVE& accum) noexcept {
    RagdollBodyUVE& first = m_bodies[joint.firstBody];
    RagdollBodyUVE& second = m_bodies[joint.secondBody];
    if (joint.twistMotion == RagdollMotionUVE::Free && joint.swing1Motion == RagdollMotionUVE::Free &&
        joint.swing2Motion == RagdollMotionUVE::Free) {
        return;
    }

    const Math::QuaternionUVE firstFrame = Math::MultiplyUVE(first.rotation, joint.firstFrame);
    const Math::QuaternionUVE secondFrame = Math::MultiplyUVE(second.rotation, joint.secondFrame);
    Math::QuaternionUVE inverseFirstFrame;
    if (!Math::TryInverseUVE(firstFrame, inverseFirstFrame)) return;
    const JointAnglesUVE angles = DecomposeJointAnglesUVE(Math::MultiplyUVE(inverseFirstFrame, secondFrame));

    const float degreesToRadians = kPiUVE / 180.0F;
    const float swing1Limit = std::max(0.0F, joint.swing1LimitDegrees) * degreesToRadians;
    const float swing2Limit = std::max(0.0F, joint.swing2LimitDegrees) * degreesToRadians;
    const float twistLimit = std::max(0.0F, joint.twistLimitDegrees) * degreesToRadians;
    const float twistLower = joint.twistAsymmetric ? joint.twistLowerLimitDegrees * degreesToRadians : -twistLimit;
    const float twistUpper = joint.twistAsymmetric ? joint.twistUpperLimitDegrees * degreesToRadians : twistLimit;

    const float stiffness = std::clamp(joint.limitStiffness, 0.0F, 1.0F);
    const float damping = std::clamp(joint.limitDamping, 0.0F, 2.0F);

    const auto solveAxis = [&](const Math::Vector3UVE& localAxis, const float violation,
                               const bool bilateral, const bool upperBound, float& accumulated) {
        const Math::Vector3UVE worldAxis = Math::RotateVectorUVE(firstFrame, localAxis);
        const float axisLengthSquared = Math::LengthSquaredUVE(worldAxis);
        if (!(axisLengthSquared > kDirectionEpsilonUVE)) return;
        const Math::Vector3UVE normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));

        const float effectiveMass =
            Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.firstBody, normalisedAxis)) +
            Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.secondBody, normalisedAxis));
        if (!(effectiveMass > kDirectionEpsilonUVE)) return;

        const float relativeAngular = Math::DotUVE(second.angularVelocity - first.angularVelocity, normalisedAxis);
        const float bias = std::clamp(violation * stiffness / m_lastStepSeconds, -kMaximumJointBiasUVE,
                                      kMaximumJointBiasUVE);
        const float dampTerm = bilateral ? (1.0F + damping) : (1.0F + damping * 0.5F);
        const float deltaImpulse = -(relativeAngular * dampTerm + bias) / effectiveMass;
        if (!std::isfinite(deltaImpulse)) return;

        float applied = deltaImpulse;
        if (!bilateral) {
            const float oldAccum = accumulated;
            // Strictly one-sided accumulated limit impulse (eliminates sticky joint limits!).
            accumulated = upperBound ? std::min(0.0F, oldAccum + deltaImpulse)
                                     : std::max(0.0F, oldAccum + deltaImpulse);
            applied = accumulated - oldAccum;
        }
        if (std::fabs(applied) <= 0.0F) return;

        const Math::Vector3UVE angularImpulse = normalisedAxis * applied;
        first.angularVelocity -= TransformInverseInertiaUVE(joint.firstBody, angularImpulse);
        second.angularVelocity += TransformInverseInertiaUVE(joint.secondBody, angularImpulse);
    };

    // 1. Twist limit (X axis).
    {
        const AxisRangeUVE twistRange = ResolveAxisRangeUVE(joint.twistMotion, angles.twist, twistLower, twistUpper);
        if (twistRange.constrained) {
            const float clamped = std::clamp(angles.twist, twistRange.lower, twistRange.upper);
            const float violation = angles.twist - clamped;
            solveAxis(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, violation, twistRange.bilateral, violation > 0.0F,
                      accum.twistLimit);
        }
    }

    // 2. Swing limits: true elliptical cone when both axes are Limited, else per-axis.
    if (joint.swing1Motion == RagdollMotionUVE::Limited && joint.swing2Motion == RagdollMotionUVE::Limited &&
        swing1Limit > kDirectionEpsilonUVE && swing2Limit > kDirectionEpsilonUVE) {
        const float r1 = angles.swing1 / swing1Limit;
        const float r2 = angles.swing2 / swing2Limit;
        const float magSq = r1 * r1 + r2 * r2;
        if (magSq > 1.0F) {
            const float mag = std::sqrt(magSq);
            const float target1 = angles.swing1 / mag;
            const float target2 = angles.swing2 / mag;
            // Outward normal to the ellipse (s1/a)^2 + (s2/b)^2 = 1.
            const float g1 = angles.swing1 / (swing1Limit * swing1Limit);
            const float g2 = angles.swing2 / (swing2Limit * swing2Limit);
            const float gLen = std::hypot(g1, g2);
            if (gLen > kDirectionEpsilonUVE) {
                const float n1 = g1 / gLen; // Along swing1 (local +Z)
                const float n2 = g2 / gLen; // Along swing2 (local +Y)
                const float violation = (angles.swing1 - target1) * n1 + (angles.swing2 - target2) * n2;
                solveAxis(Math::Vector3UVE{0.0F, n2, n1}, violation, /*bilateral=*/false, /*upperBound=*/true,
                          accum.swingConeLimit);
            }
        }
    } else {
        const AxisRangeUVE s1Range =
            ResolveAxisRangeUVE(joint.swing1Motion, angles.swing1, -swing1Limit, swing1Limit);
        if (s1Range.constrained) {
            const float violation = angles.swing1 - std::clamp(angles.swing1, s1Range.lower, s1Range.upper);
            solveAxis(Math::Vector3UVE{0.0F, 0.0F, 1.0F}, violation, s1Range.bilateral, violation > 0.0F,
                      accum.swing1Limit);
        }
        const AxisRangeUVE s2Range =
            ResolveAxisRangeUVE(joint.swing2Motion, angles.swing2, -swing2Limit, swing2Limit);
        if (s2Range.constrained) {
            const float violation = angles.swing2 - std::clamp(angles.swing2, s2Range.lower, s2Range.upper);
            solveAxis(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, violation, s2Range.bilateral, violation > 0.0F,
                      accum.swing2Limit);
        }
    }
}

float RagdollSolverUVE::ComputeContactEffectiveMassUVE(const std::size_t bodyIndex,
                                                       const Math::Vector3UVE& worldOffset,
                                                       const Math::Vector3UVE& direction) const noexcept {
    const float invMass = EffectiveInverseMassUVE(bodyIndex);
    const Math::Vector3UVE lever = Math::CrossUVE(worldOffset, direction);
    const float result = invMass + Math::DotUVE(TransformInverseInertiaUVE(bodyIndex, lever), lever);
    return std::isfinite(result) ? result : 0.0F;
}

void RagdollSolverUVE::ApplyImpulseAtPointUVE(const std::size_t bodyIndex, const Math::Vector3UVE& impulse,
                                              const Math::Vector3UVE& worldOffset) noexcept {
    RagdollBodyUVE& body = m_bodies[bodyIndex];
    if (body.kinematic) return;
    body.linearVelocity += impulse * EffectiveInverseMassUVE(bodyIndex);
    body.angularVelocity += TransformInverseInertiaUVE(bodyIndex, Math::CrossUVE(worldOffset, impulse));
}

void RagdollSolverUVE::PrepareContactSolveStateUVE(const RagdollContactUVE& contact,
                                                   ContactSolveStateUVE& outState) const noexcept {
    outState = {};
    if (contact.body >= m_bodyCount) return;
    const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
    const bool bodyActive = EffectiveInverseMassUVE(contact.body) > 0.0F ||
                            Math::LengthSquaredUVE(m_effectiveInverseInertiaLocal[contact.body]) > 0.0F;
    const bool otherActive = hasOtherBody &&
                             (EffectiveInverseMassUVE(contact.otherBody) > 0.0F ||
                              Math::LengthSquaredUVE(m_effectiveInverseInertiaLocal[contact.otherBody]) > 0.0F);
    if (!bodyActive && !otherActive) return;

    const float normalLengthSquared = Math::LengthSquaredUVE(contact.normal);
    if (!std::isfinite(normalLengthSquared) || !(normalLengthSquared > kDirectionEpsilonUVE)) return;
    outState.normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));
    BuildTangentsUVE(outState.normal, outState.tangent1, outState.tangent2);

    const Math::Vector3UVE offsetA = contact.point - m_bodies[contact.body].position;
    Math::Vector3UVE otherVel = Math::IsFiniteUVE(contact.otherVelocity) ? contact.otherVelocity : Math::Vector3UVE{};
    if (hasOtherBody) {
        otherVel = AnchorVelocityUVE(contact.otherBody, contact.point - m_bodies[contact.otherBody].position);
    }
    const float initialNormalVel = Math::DotUVE(AnchorVelocityUVE(contact.body, offsetA) - otherVel, outState.normal);
    if (!std::isfinite(initialNormalVel)) return;

    const float restitution = std::clamp(contact.restitution, 0.0F, 1.0F);
    outState.restitutionTarget =
        initialNormalVel < -kRestitutionThresholdUVE ? -restitution * initialNormalVel : 0.0F;
    outState.valid = true;
}

void RagdollSolverUVE::SolveContactVelocityUVE(const RagdollContactUVE& contact,
                                               ContactSolveStateUVE& state) noexcept {
    if (!state.valid) return;
    const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
    const Math::Vector3UVE offsetA = contact.point - m_bodies[contact.body].position;
    const Math::Vector3UVE offsetB =
        hasOtherBody ? (contact.point - m_bodies[contact.otherBody].position) : Math::Vector3UVE{};
    const float externalInvMass =
        (!hasOtherBody && std::isfinite(contact.otherInverseMass) && contact.otherInverseMass > 0.0F)
            ? contact.otherInverseMass
            : 0.0F;

    const auto relativePointVelocity = [&]() noexcept -> Math::Vector3UVE {
        const Math::Vector3UVE velA = AnchorVelocityUVE(contact.body, offsetA);
        if (hasOtherBody) return velA - AnchorVelocityUVE(contact.otherBody, offsetB);
        const Math::Vector3UVE extVel =
            Math::IsFiniteUVE(contact.otherVelocity) ? contact.otherVelocity : Math::Vector3UVE{};
        return velA - extVel;
    };

    const auto combinedEffectiveInvMass = [&](const Math::Vector3UVE& dir) noexcept -> float {
        float eff = ComputeContactEffectiveMassUVE(contact.body, offsetA, dir);
        if (hasOtherBody) {
            eff += ComputeContactEffectiveMassUVE(contact.otherBody, offsetB, dir);
        } else {
            eff += externalInvMass;
        }
        return eff;
    };

    // 1. Accumulated clamped normal impulse (Havok / Box2D Sequential Impulses).
    const float normalVelocity = Math::DotUVE(relativePointVelocity(), state.normal);
    const float normalEffInvMass = combinedEffectiveInvMass(state.normal);
    if (std::isfinite(normalVelocity) && normalEffInvMass > kDirectionEpsilonUVE) {
        const float deltaNormal = (state.restitutionTarget - normalVelocity) / normalEffInvMass;
        if (std::isfinite(deltaNormal)) {
            const float oldNormal = state.accumulatedNormal;
            state.accumulatedNormal = std::max(0.0F, oldNormal + deltaNormal);
            const float appliedNormal = state.accumulatedNormal - oldNormal;
            if (std::fabs(appliedNormal) > 0.0F) {
                const Math::Vector3UVE impulse = state.normal * appliedNormal;
                ApplyImpulseAtPointUVE(contact.body, impulse, offsetA);
                if (hasOtherBody) {
                    ApplyImpulseAtPointUVE(contact.otherBody, impulse * -1.0F, offsetB);
                }
            }
        }
    }

    // 2. Isotropic circular Coulomb friction cone clamped on accumulated normal impulse.
    const float friction = std::clamp(contact.friction, 0.0F, 4.0F);
    if (!(friction > 0.0F) || !(state.accumulatedNormal > 0.0F)) return;

    const Math::Vector3UVE relVel = relativePointVelocity();
    const float massT1 = combinedEffectiveInvMass(state.tangent1);
    const float massT2 = combinedEffectiveInvMass(state.tangent2);
    if (!(massT1 > kDirectionEpsilonUVE) || !(massT2 > kDirectionEpsilonUVE)) return;

    const float deltaT1 = -Math::DotUVE(relVel, state.tangent1) / massT1;
    const float deltaT2 = -Math::DotUVE(relVel, state.tangent2) / massT2;
    if (!std::isfinite(deltaT1) || !std::isfinite(deltaT2)) return;

    float candT1 = state.accumulatedTangent1 + deltaT1;
    float candT2 = state.accumulatedTangent2 + deltaT2;
    const float maxFriction = friction * state.accumulatedNormal;
    const float mag = std::hypot(candT1, candT2);
    if (mag > maxFriction && mag > 1.0e-9F) {
        const float scale = maxFriction / mag;
        candT1 *= scale;
        candT2 *= scale;
    }

    const float appliedT1 = candT1 - state.accumulatedTangent1;
    const float appliedT2 = candT2 - state.accumulatedTangent2;
    state.accumulatedTangent1 = candT1;
    state.accumulatedTangent2 = candT2;

    const Math::Vector3UVE frictionImpulse = state.tangent1 * appliedT1 + state.tangent2 * appliedT2;
    if (Math::IsFiniteUVE(frictionImpulse) && Math::LengthSquaredUVE(frictionImpulse) > 0.0F) {
        ApplyImpulseAtPointUVE(contact.body, frictionImpulse, offsetA);
        if (hasOtherBody) {
            ApplyImpulseAtPointUVE(contact.otherBody, frictionImpulse * -1.0F, offsetB);
        }
    }
}

void RagdollSolverUVE::IntegratePositionsUVE(const float deltaTimeSeconds) noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        if (body.kinematic) continue;
        if (!Math::IsFiniteUVE(body.linearVelocity) || !Math::IsFiniteUVE(body.angularVelocity)) {
            body.linearVelocity = Math::Vector3UVE{};
            body.angularVelocity = Math::Vector3UVE{};
            continue;
        }
        body.position += body.linearVelocity * deltaTimeSeconds;

        const Math::Vector3UVE spin = body.angularVelocity;
        if (Math::LengthSquaredUVE(spin) > 1.0e-12F) {
            const Math::QuaternionUVE spinQuaternion{spin.x, spin.y, spin.z, 0.0F};
            const Math::QuaternionUVE delta = Math::MultiplyUVE(spinQuaternion, body.rotation);
            const Math::QuaternionUVE candidate{body.rotation.x + delta.x * 0.5F * deltaTimeSeconds,
                                                body.rotation.y + delta.y * 0.5F * deltaTimeSeconds,
                                                body.rotation.z + delta.z * 0.5F * deltaTimeSeconds,
                                                body.rotation.w + delta.w * 0.5F * deltaTimeSeconds};
            Math::QuaternionUVE normalised;
            if (Math::TryNormalizeUVE(candidate, normalised)) body.rotation = normalised;
        }
    }
}

void RagdollSolverUVE::ApplyRotationUVE(const std::size_t bodyIndex, const Math::Vector3UVE& axis,
                                        const float radians) noexcept {
    if (m_bodies[bodyIndex].kinematic) return;
    if (!Math::IsFiniteUVE(axis) || !std::isfinite(radians) || std::fabs(radians) <= 0.0F) return;
    Math::QuaternionUVE delta;
    if (!Math::TryMakeAxisAngleUVE(axis, radians, delta)) return;
    const Math::QuaternionUVE candidate = Math::MultiplyUVE(delta, m_bodies[bodyIndex].rotation);
    Math::QuaternionUVE normalised;
    if (Math::TryNormalizeUVE(candidate, normalised)) m_bodies[bodyIndex].rotation = normalised;
}

void RagdollSolverUVE::ProjectJointsUVE() noexcept {
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJointUVE& joint = m_joints[jointIndex];
        if (!joint.projectionEnabled) continue;
        RagdollBodyUVE& first = m_bodies[joint.firstBody];
        RagdollBodyUVE& second = m_bodies[joint.secondBody];
        const float firstInvMass = EffectiveInverseMassUVE(joint.firstBody);
        const float secondInvMass = EffectiveInverseMassUVE(joint.secondBody);
        const float inverseMassSum = firstInvMass + secondInvMass;
        if (!(inverseMassSum > 0.0F)) continue;

        const auto projectLinearAnchors = [&]() noexcept {
            const Math::Vector3UVE firstOffset = Math::RotateVectorUVE(first.rotation, joint.firstAnchor);
            const Math::Vector3UVE secondOffset = Math::RotateVectorUVE(second.rotation, joint.secondAnchor);
            const Math::Vector3UVE error = (second.position + secondOffset) - (first.position + firstOffset);
            if (Math::IsFiniteUVE(error)) {
                first.position += error * (firstInvMass / inverseMassSum);
                second.position -= error * (secondInvMass / inverseMassSum);
            }
        };

        // --- Linear: move both bodies until the two anchors coincide. ---
        projectLinearAnchors();

        // --- Angular: rotate both bodies back inside their limits (true elliptical cone). ---
        if (joint.twistMotion == RagdollMotionUVE::Free && joint.swing1Motion == RagdollMotionUVE::Free &&
            joint.swing2Motion == RagdollMotionUVE::Free) {
            continue;
        }
        const Math::QuaternionUVE firstFrame = Math::MultiplyUVE(first.rotation, joint.firstFrame);
        const Math::QuaternionUVE secondFrame = Math::MultiplyUVE(second.rotation, joint.secondFrame);
        Math::QuaternionUVE inverseFirstFrame;
        if (!Math::TryInverseUVE(firstFrame, inverseFirstFrame)) continue;
        const JointAnglesUVE angles = DecomposeJointAnglesUVE(Math::MultiplyUVE(inverseFirstFrame, secondFrame));

        const float degreesToRadians = kPiUVE / 180.0F;
        const float swing1Limit = std::max(0.0F, joint.swing1LimitDegrees) * degreesToRadians;
        const float swing2Limit = std::max(0.0F, joint.swing2LimitDegrees) * degreesToRadians;
        const float twistLimit = std::max(0.0F, joint.twistLimitDegrees) * degreesToRadians;
        const float twistLower = joint.twistAsymmetric ? joint.twistLowerLimitDegrees * degreesToRadians : -twistLimit;
        const float twistUpper = joint.twistAsymmetric ? joint.twistUpperLimitDegrees * degreesToRadians : twistLimit;

        bool rotated = false;
        const auto projectAxis = [&](const Math::Vector3UVE& localAxis, const float excess) noexcept {
            if (std::fabs(excess) <= kMinimumProjectionAngleUVE) return;
            const Math::Vector3UVE worldAxis = Math::RotateVectorUVE(firstFrame, localAxis);
            const float axisLengthSquared = Math::LengthSquaredUVE(worldAxis);
            if (!(axisLengthSquared > kDirectionEpsilonUVE)) return;
            const Math::Vector3UVE normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));
            const float firstResponse =
                Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.firstBody, normalisedAxis));
            const float secondResponse =
                Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.secondBody, normalisedAxis));
            const float responseSum = firstResponse + secondResponse;
            if (!(responseSum > kDirectionEpsilonUVE)) return;
            ApplyRotationUVE(joint.secondBody, normalisedAxis, -excess * (secondResponse / responseSum));
            ApplyRotationUVE(joint.firstBody, normalisedAxis, excess * (firstResponse / responseSum));
            rotated = true;
        };

        // Twist projection.
        const AxisRangeUVE twistRange = ResolveAxisRangeUVE(joint.twistMotion, angles.twist, twistLower, twistUpper);
        if (twistRange.constrained) {
            const float excess = angles.twist - std::clamp(angles.twist, twistRange.lower, twistRange.upper);
            projectAxis(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, excess);
        }

        // Elliptical swing-cone projection (matching SolveJointLimitsUVE).
        if (joint.swing1Motion == RagdollMotionUVE::Limited && joint.swing2Motion == RagdollMotionUVE::Limited &&
            swing1Limit > kDirectionEpsilonUVE && swing2Limit > kDirectionEpsilonUVE) {
            const float r1 = angles.swing1 / swing1Limit;
            const float r2 = angles.swing2 / swing2Limit;
            const float magSq = r1 * r1 + r2 * r2;
            if (magSq > 1.0F) {
                const float scale = 1.0F / std::sqrt(magSq);
                const float excess1 = angles.swing1 - angles.swing1 * scale;
                const float excess2 = angles.swing2 - angles.swing2 * scale;
                const float excessMag = std::hypot(excess1, excess2);
                if (excessMag > kMinimumProjectionAngleUVE) {
                    projectAxis(Math::Vector3UVE{0.0F, excess2 / excessMag, excess1 / excessMag}, excessMag);
                }
            }
        } else {
            const AxisRangeUVE s1Range =
                ResolveAxisRangeUVE(joint.swing1Motion, angles.swing1, -swing1Limit, swing1Limit);
            if (s1Range.constrained) {
                projectAxis(Math::Vector3UVE{0.0F, 0.0F, 1.0F},
                            angles.swing1 - std::clamp(angles.swing1, s1Range.lower, s1Range.upper));
            }
            const AxisRangeUVE s2Range =
                ResolveAxisRangeUVE(joint.swing2Motion, angles.swing2, -swing2Limit, swing2Limit);
            if (s2Range.constrained) {
                projectAxis(Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                            angles.swing2 - std::clamp(angles.swing2, s2Range.lower, s2Range.upper));
            }
        }

        // Re-project linear anchors after angular rotation so clamping never pulls joints apart!
        if (rotated) {
            projectLinearAnchors();
        }
    }
}

void RagdollSolverUVE::ProjectContactsUVE(const std::span<const RagdollContactUVE> contacts,
                                          const float contactSlop, const float alpha) noexcept {
    const float clampedAlpha = std::clamp(alpha, 0.0F, 1.0F);
    for (const RagdollContactUVE& contact : contacts) {
        if (contact.body >= m_bodyCount) continue;
        const float depth = contact.depth - contactSlop;
        if (!(depth > 0.0F)) continue;
        const float normalLengthSquared = Math::LengthSquaredUVE(contact.normal);
        if (!(normalLengthSquared > kDirectionEpsilonUVE)) continue;
        const Math::Vector3UVE normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));
        const float correction = std::min(depth * clampedAlpha, kMaximumContactCorrectionUVE);

        const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
        if (hasOtherBody) {
            const float invA = EffectiveInverseMassUVE(contact.body);
            const float invB = EffectiveInverseMassUVE(contact.otherBody);
            const float invSum = invA + invB;
            if (!(invSum > 0.0F)) continue;
            m_bodies[contact.body].position += normal * (correction * (invA / invSum));
            m_bodies[contact.otherBody].position -= normal * (correction * (invB / invSum));
        } else if (!m_bodies[contact.body].kinematic) {
            m_bodies[contact.body].position += normal * correction;
        }
    }
}

void RagdollSolverUVE::UpdateSleepUVE(const float deltaTimeSeconds,
                                      const RagdollStepSettingsUVE& settings) noexcept {
    if (!(settings.sleepDelaySeconds > 0.0F) || !std::isfinite(settings.sleepDelaySeconds)) {
        m_sleepTimer = 0.0F;
        return;
    }
    float maximumLinearSquared = 0.0F;
    float maximumAngularSquared = 0.0F;
    std::size_t dynamicCount = 0U;
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        if (m_bodies[index].kinematic) continue;
        ++dynamicCount;
        maximumLinearSquared = std::max(maximumLinearSquared, Math::LengthSquaredUVE(m_bodies[index].linearVelocity));
        maximumAngularSquared = std::max(maximumAngularSquared, Math::LengthSquaredUVE(m_bodies[index].angularVelocity));
    }
    if (dynamicCount == 0U) {
        m_sleepTimer = 0.0F;
        return;
    }
    if (std::sqrt(maximumLinearSquared) < settings.sleepLinearSpeed &&
        std::sqrt(maximumAngularSquared) < settings.sleepAngularSpeed) {
        m_sleepTimer += deltaTimeSeconds;
        if (m_sleepTimer >= settings.sleepDelaySeconds) SleepUVE();
    } else {
        m_sleepTimer = 0.0F;
    }
}

RagdollStepReportUVE RagdollSolverUVE::StepUVE(const Math::Vector3UVE& gravity, const float deltaTimeSeconds,
                                               const std::span<const RagdollContactUVE> contacts,
                                               const RagdollStepSettingsUVE& settings) {
    RagdollStepReportUVE report;
    report.sleeping = m_sleeping;
    m_lastContactFeedbackCount = 0U;
    if (m_bodyCount == 0U) return report;
    if (m_sleeping) return report;
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return report;
    if (!Math::IsFiniteUVE(gravity)) return report;

    const float stepSeconds = std::clamp(deltaTimeSeconds, 1.0e-5F, 0.1F);
    m_lastStepSeconds = stepSeconds;

    PrepareEffectiveInertiasUVE(settings);
    IntegrateVelocitiesUVE(gravity, stepSeconds);

    // Build combined bounded contact buffer (world contacts + optional non-adjacent self-contacts).
    constexpr std::size_t kCombinedContactCap = kMaximumRagdollContactsUVE + kMaximumRagdollSelfContactsUVE;
    std::array<RagdollContactUVE, kCombinedContactCap> activeContacts{};
    std::size_t worldContactCount = 0U;
    for (const RagdollContactUVE& contact : contacts) {
        if (worldContactCount >= kMaximumRagdollContactsUVE) break;
        activeContacts[worldContactCount++] = contact;
    }
    std::size_t selfContactCount = 0U;
    if (settings.enableSelfCollision) {
        selfContactCount = GenerateSelfContactsUVE(
            std::span<RagdollContactUVE>{activeContacts.data() + worldContactCount, kMaximumRagdollSelfContactsUVE});
    }
    const std::size_t totalContactCount = worldContactCount + selfContactCount;
    const std::span<const RagdollContactUVE> activeContactSpan{activeContacts.data(), totalContactCount};

    std::array<ContactSolveStateUVE, kCombinedContactCap> contactStates{};
    for (std::size_t c = 0U; c < totalContactCount; ++c) {
        PrepareContactSolveStateUVE(activeContacts[c], contactStates[c]);
    }
    std::array<JointAccumulatorsUVE, kMaximumRagdollJointsUVE> jointAccumulators{};
    std::array<BodyDriveAccumulatorUVE, kMaximumRagdollBodiesUVE> bodyDriveAccumulators{};

    const std::size_t velocityIterations = std::max<std::size_t>(1U, settings.velocityIterations);
    for (std::size_t iteration = 0U; iteration < velocityIterations; ++iteration) {
        for (std::size_t bodyIndex = 0U; bodyIndex < m_bodyCount; ++bodyIndex) {
            if (m_bodies[bodyIndex].worldDrive.enabled) {
                SolveBodyDriveUVE(bodyIndex, bodyDriveAccumulators[bodyIndex], stepSeconds);
            }
        }
        for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
            const RagdollJointUVE& joint = m_joints[jointIndex];
            SolveJointVelocityUVE(joint);
            SolveJointMotorUVE(joint, jointAccumulators[jointIndex], stepSeconds);
            SolveJointLimitsUVE(joint, jointAccumulators[jointIndex]);
        }
        for (std::size_t c = 0U; c < totalContactCount; ++c) {
            SolveContactVelocityUVE(activeContacts[c], contactStates[c]);
        }
    }
    report.velocityIterations = velocityIterations;

    // Record world contact impulses for dynamic Rigid3D pushback.
    for (std::size_t c = 0U; c < worldContactCount; ++c) {
        const RagdollContactUVE& contact = activeContacts[c];
        const ContactSolveStateUVE& state = contactStates[c];
        if (!state.valid || contact.colliderIndex == kInvalidRagdollColliderIndexUVE) continue;
        if (!(state.accumulatedNormal > 0.0F)) continue;
        if (m_lastContactFeedbackCount >= m_lastContactFeedback.size()) break;
        const Math::Vector3UVE totalImpulse = state.normal * state.accumulatedNormal +
                                              state.tangent1 * state.accumulatedTangent1 +
                                              state.tangent2 * state.accumulatedTangent2;
        if (!Math::IsFiniteUVE(totalImpulse)) continue;
        m_lastContactFeedback[m_lastContactFeedbackCount++] = RagdollContactFeedbackUVE{
            contact.body,
            contact.colliderIndex,
            contact.point,
            totalImpulse,
        };
    }

    IntegratePositionsUVE(stepSeconds);

    const std::size_t positionIterations = settings.positionIterations;
    for (std::size_t iteration = 0U; iteration < positionIterations; ++iteration) {
        ProjectJointsUVE();
        ProjectContactsUVE(activeContactSpan, settings.contactSlop, settings.contactCorrectionAlpha);
    }
    report.positionIterations = positionIterations;

    // Final bookkeeping: how far the joints actually are from coincident, and what was resolved.
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJointUVE& joint = m_joints[jointIndex];
        const Math::Vector3UVE firstOffset = Math::RotateVectorUVE(m_bodies[joint.firstBody].rotation,
                                                                   joint.firstAnchor);
        const Math::Vector3UVE secondOffset = Math::RotateVectorUVE(m_bodies[joint.secondBody].rotation,
                                                                    joint.secondAnchor);
        const Math::Vector3UVE error = (m_bodies[joint.secondBody].position + secondOffset) -
                                       (m_bodies[joint.firstBody].position + firstOffset);
        const float driftSquared = Math::LengthSquaredUVE(error);
        if (std::isfinite(driftSquared)) {
            const float distance = std::sqrt(driftSquared);
            report.maximumJointDrift = std::max(report.maximumJointDrift, distance);
            if (distance > kJointDriftToleranceUVE) ++report.jointDriftCount;
        }
        if (joint.twistMotion != RagdollMotionUVE::Free || joint.swing1Motion != RagdollMotionUVE::Free ||
            joint.swing2Motion != RagdollMotionUVE::Free) {
            ++report.constrainedJointCount;
        }
    }
    for (std::size_t c = 0U; c < worldContactCount; ++c) {
        if (activeContacts[c].body < m_bodyCount) ++report.resolvedContactCount;
    }
    report.selfContactCount = selfContactCount;

    UpdateSleepUVE(stepSeconds, settings);
    report.stepped = true;
    report.sleeping = m_sleeping;
    return report;
}

void RagdollSolverUVE::ApplyImpulseUVE(const std::uint32_t body, const Math::Vector3UVE& impulse,
                                       const Math::Vector3UVE& worldPoint) noexcept {
    if (body >= m_bodyCount) return;
    if (!Math::IsFiniteUVE(impulse) || !Math::IsFiniteUVE(worldPoint)) return;
    WakeUVE();
    ApplyImpulseAtPointUVE(body, impulse, worldPoint - m_bodies[body].position);
}

} // namespace UVE::Physics
