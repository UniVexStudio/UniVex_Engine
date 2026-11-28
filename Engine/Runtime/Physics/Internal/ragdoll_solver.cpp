// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "ragdoll_solver.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Physics {
namespace {

constexpr float kPi = 3.14159265358979323846F;
constexpr float kTwoPi = 6.28318530717958647692F;
/// Below this a direction or an angle is noise, and dividing by it produces the kind of large number
/// that turns a solver into a fountain.
constexpr float kDirectionEpsilon = 1.0e-6F;
/// A bias that can drive an angular velocity of thousands of rad/s is a bug, not a correction. Ten
/// rad/s is already far past any joint that is about to be seen.
constexpr float kMaximumJointBias = 10.0F;
/// Restitution only means something above this approach speed. Below it a resting body would bounce
/// on the last millimetre of every contact and never settle.
constexpr float kRestitutionThreshold = 1.0F;
/// One position correction is capped so a single bad frame cannot teleport a limb across the map.
constexpr float kMaximumContactCorrection = 0.25F;
/// Anchor error below this is float noise in a nineteen-body chain, not drift worth reporting.
constexpr float kJointDriftTolerance = 0.002F;
/// A rotation below this many radians is not worth a quaternion multiply in the projection pass.
constexpr float kMinimumProjectionAngle = 1.0e-4F;
/// Two radians of swing is past any human joint, so a decomposition that reports more than this is
/// reading a degenerate quaternion rather than a pose.
constexpr float kMaximumSwingAngle = kPi + 0.01F;
/// Small bind-pose overlap margin (1 cm) when building pairwise self-collision pair masks.
constexpr float kBindOverlapMargin = 0.01F;

[[nodiscard]] float WrapAngle(const float radians) noexcept {
    if (!std::isfinite(radians)) return 0.0F;
    float wrapped = std::fmod(radians + kPi, kTwoPi);
    if (wrapped < 0.0F) wrapped += kTwoPi;
    return wrapped - kPi;
}

[[nodiscard]] Math::Matrix3x3 Skew(const Math::Vector3& value) noexcept {
    Math::Matrix3x3 result;
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

[[nodiscard]] Math::Vector3 Multiply(const Math::Matrix3x3& matrix,
                                           const Math::Vector3& vector) noexcept {
    return Math::Vector3{
        matrix.m[0][0] * vector.x + matrix.m[0][1] * vector.y + matrix.m[0][2] * vector.z,
        matrix.m[1][0] * vector.x + matrix.m[1][1] * vector.y + matrix.m[1][2] * vector.z,
        matrix.m[2][0] * vector.x + matrix.m[2][1] * vector.y + matrix.m[2][2] * vector.z,
    };
}

/// Rotation matrix of a unit quaternion, row-major.
[[nodiscard]] Math::Matrix3x3 RotationMatrix(const Math::Quaternion& rotation) noexcept {
    const float xx = rotation.x * rotation.x;
    const float yy = rotation.y * rotation.y;
    const float zz = rotation.z * rotation.z;
    const float xy = rotation.x * rotation.y;
    const float xz = rotation.x * rotation.z;
    const float yz = rotation.y * rotation.z;
    const float wx = rotation.w * rotation.x;
    const float wy = rotation.w * rotation.y;
    const float wz = rotation.w * rotation.z;

    Math::Matrix3x3 result;
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
[[nodiscard]] Math::Matrix3x3 ComputePointEffectiveMass(
    const std::array<Math::Vector3, 2U>& offsets,
    const std::array<Math::Matrix3x3, 2U>& worldInverseInertia, const float inverseMassSum) noexcept {
    Math::Matrix3x3 result;
    for (std::size_t row = 0U; row < 3U; ++row) {
        for (std::size_t column = 0U; column < 3U; ++column) {
            result.m[row][column] = row == column ? inverseMassSum : 0.0F;
        }
    }
    for (std::size_t body = 0U; body < 2U; ++body) {
        const Math::Matrix3x3 skew = Skew(offsets[body]);
        const Math::Matrix3x3 term = skew * worldInverseInertia[body] * skew;
        for (std::size_t row = 0U; row < 3U; ++row) {
            for (std::size_t column = 0U; column < 3U; ++column) {
                result.m[row][column] -= term.m[row][column];
            }
        }
    }
    return result;
}

/// Two orthonormal tangents for a unit normal, chosen deterministically from the normal itself.
void BuildTangents(const Math::Vector3& normal, Math::Vector3& outTangent1,
                      Math::Vector3& outTangent2) noexcept {
    const Math::Vector3 reference =
        std::fabs(normal.y) < 0.9F ? Math::Vector3{0.0F, 1.0F, 0.0F} : Math::Vector3{1.0F, 0.0F, 0.0F};
    const Math::Vector3 first = Math::Cross(normal, reference);
    const float lengthSquared = Math::LengthSquared(first);
    if (!(lengthSquared > kDirectionEpsilon)) {
        outTangent1 = Math::Vector3{1.0F, 0.0F, 0.0F};
        outTangent2 = Math::Vector3{0.0F, 0.0F, 1.0F};
        return;
    }
    outTangent1 = first * (1.0F / std::sqrt(lengthSquared));
    outTangent2 = Math::Cross(normal, outTangent1);
}

/// Closest points between two finite line segments (Ericson, Real-Time Collision Detection §5.1.9).
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

/// Shortest-arc axis-angle error vector representing `target * conjugate(current)`.
[[nodiscard]] Math::Vector3 QuaternionRotationErrorVector(const Math::Quaternion& current,
                                                                const Math::Quaternion& target) noexcept {
    Math::Quaternion error = Math::Multiply(target, Math::Conjugate(current));
    if (!Math::IsFinite(error)) return Math::Vector3{};
    if (error.w < 0.0F) {
        error.x = -error.x;
        error.y = -error.y;
        error.z = -error.z;
        error.w = -error.w;
    }
    const Math::Vector3 imag{error.x, error.y, error.z};
    const float sinHalf = std::sqrt(Math::LengthSquared(imag));
    if (sinHalf > 1.0e-6F) {
        const float angle = 2.0F * std::atan2(sinHalf, std::clamp(error.w, -1.0F, 1.0F));
        return imag * (angle / sinHalf);
    }
    return imag * 2.0F;
}

/// Conditioning on a single diagonal inverse-inertia vector: caps the ratio
/// between largest and smallest positive inverse-inertia components (lifting tiny axial inertia).
[[nodiscard]] Math::Vector3 ConditionSingleInverseInertia(const Math::Vector3& invI,
                                                                const float maxPrincipalRatio) noexcept {
    if (!Math::IsFinite(invI)) return Math::Vector3{};
    const float ratio = std::max(1.0F, std::isfinite(maxPrincipalRatio) ? maxPrincipalRatio : 4.0F);
    float minPositive = std::numeric_limits<float>::infinity();
    if (invI.x > 0.0F) minPositive = std::min(minPositive, invI.x);
    if (invI.y > 0.0F) minPositive = std::min(minPositive, invI.y);
    if (invI.z > 0.0F) minPositive = std::min(minPositive, invI.z);
    if (!std::isfinite(minPositive) || !(minPositive > 0.0F)) return invI;
    const float cap = minPositive * ratio;
    return Math::Vector3{
        invI.x > 0.0F ? std::min(invI.x, cap) : 0.0F,
        invI.y > 0.0F ? std::min(invI.y, cap) : 0.0F,
        invI.z > 0.0F ? std::min(invI.z, cap) : 0.0F,
    };
}

/// Swing and twist of a joint's relative rotation, about the frame's local X, Y and Z.
struct JointAngles final {
    float twist = 0.0F;
    float swing1 = 0.0F;
    float swing2 = 0.0F;
    Math::Vector3 swingAxis{};
};

[[nodiscard]] JointAngles DecomposeJointAngles(const Math::Quaternion& relative) noexcept {
    JointAngles angles;
    if (!Math::IsFinite(relative)) return angles;

    // Twist: the component of the quaternion about the frame's X axis.
    const float twistLength = std::sqrt(relative.x * relative.x + relative.w * relative.w);
    Math::Quaternion twist{};
    if (twistLength > kDirectionEpsilon) {
        twist = Math::Quaternion{relative.x / twistLength, 0.0F, 0.0F, relative.w / twistLength};
    }
    angles.twist = WrapAngle(2.0F * std::atan2(twist.x, twist.w));

    // Swing: whatever is left once the twist is divided out.
    const Math::Quaternion swing = Math::Multiply(relative, Math::Conjugate(twist));
    const float swingW = std::clamp(swing.w, -1.0F, 1.0F);
    const float swingVectorLength = std::sqrt(std::max(0.0F, 1.0F - swingW * swingW));
    float swingAngle = 0.0F;
    Math::Vector3 swingAxis{};
    if (swingVectorLength > kDirectionEpsilon) {
        swingAngle = std::min(2.0F * std::acos(swingW), kMaximumSwingAngle);
        swingAxis = Math::Vector3{swing.x, swing.y, swing.z} * (1.0F / swingVectorLength);
    }
    // Swing1 is rotation about the frame's Z, swing2 about its Y: twist took X, so the three always
    // describe the whole rotation with no leftovers.
    angles.swing1 = swingAxis.z * swingAngle;
    angles.swing2 = swingAxis.y * swingAngle;
    angles.swingAxis = swingAxis * swingAngle;
    return angles;
}

/// The interval one angular axis is allowed to occupy, given its authored motion and limit.
struct AxisRange final {
    bool constrained = false;
    bool bilateral = false;
    float lower = 0.0F;
    float upper = 0.0F;
};

[[nodiscard]] AxisRange ResolveAxisRange(const RagdollMotion motion, const float angle,
                                               const float lowerRadians, const float upperRadians) noexcept {
    AxisRange range;
    if (motion == RagdollMotion::Free) return range;
    if (motion == RagdollMotion::Locked) {
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

Math::Vector3 ComputeCapsuleInverseInertia(const float mass, const float radius,
                                                 const float halfHeight) noexcept {
    const float safeRadius = std::isfinite(radius) && radius > 0.0F ? radius : 0.0F;
    const float safeHalfHeight = std::isfinite(halfHeight) && halfHeight > 0.0F ? halfHeight : 0.0F;
    if (!(safeRadius > 0.0F) || !std::isfinite(mass) || mass <= 0.0F) {
        return Math::Vector3{};
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
    if (!(axial > 0.0) || !(lateral > 0.0)) return Math::Vector3{};
    const double maximum = static_cast<double>(std::numeric_limits<float>::max());
    const double inverseAxial = 1.0 / axial;
    const double inverseLateral = 1.0 / lateral;
    if (!std::isfinite(inverseAxial) || !std::isfinite(inverseLateral) || inverseAxial > maximum ||
        inverseLateral > maximum) {
        return Math::Vector3{};
    }
    // Local +Y is the capsule's long axis.
    return Math::Vector3{static_cast<float>(inverseLateral), static_cast<float>(inverseAxial),
                            static_cast<float>(inverseLateral)};
}

void ComputeRagdollCapsuleWorldSegment(const RagdollBody& body, Math::Vector3& outStart,
                                          Math::Vector3& outEnd) noexcept {
    const float halfHeight = std::isfinite(body.halfHeight) && body.halfHeight > 0.0F ? body.halfHeight : 0.0F;
    const Math::Vector3 offset = Math::RotateVector(body.rotation, Math::Vector3{0.0F, halfHeight, 0.0F});
    outStart = body.position - offset;
    outEnd = body.position + offset;
}

Math::Quaternion MakeJointFrameFromTwistAxis(const Math::Vector3& axis) noexcept {
    const float lengthSquared = Math::LengthSquared(axis);
    if (!std::isfinite(lengthSquared) || !(lengthSquared > kDirectionEpsilon)) return Math::Quaternion{};
    const Math::Vector3 normalised = axis * (1.0F / std::sqrt(lengthSquared));
    const Math::Vector3 from{1.0F, 0.0F, 0.0F};
    const float cosine = Math::Dot(from, normalised);
    if (cosine >= 1.0F - 1.0e-6F) return Math::Quaternion{};

    Math::Vector3 rotationAxis = Math::Cross(from, normalised);
    const float sine = std::sqrt(std::max(0.0F, 1.0F - cosine * cosine));
    const float rotationLengthSquared = Math::LengthSquared(rotationAxis);
    if (!(rotationLengthSquared > kDirectionEpsilon)) {
        rotationAxis = Math::Vector3{0.0F, 1.0F, 0.0F};
    } else {
        rotationAxis = rotationAxis * (1.0F / std::sqrt(rotationLengthSquared));
    }

    Math::Quaternion rotation;
    if (!Math::TryMakeAxisAngle(rotationAxis, std::atan2(sine, cosine), rotation)) {
        return Math::Quaternion{};
    }
    return rotation;
}

Math::Quaternion ComputeJointSecondFrame(const Math::Quaternion& firstBindRotation,
                                               const Math::Quaternion& secondBindRotation,
                                               const Math::Quaternion& firstFrame) noexcept {
    Math::Quaternion inverseSecond;
    if (!Math::TryInverse(secondBindRotation, inverseSecond)) return Math::Quaternion{};
    const Math::Quaternion candidate =
        Math::Multiply(Math::Multiply(inverseSecond, firstBindRotation), firstFrame);
    Math::Quaternion normalised;
    return Math::TryNormalize(candidate, normalised) ? normalised : Math::Quaternion{};
}

RagdollJoint MakeBallSocketJoint(const std::uint32_t firstBody, const std::uint32_t secondBody,
                                       const Math::Vector3& firstAnchor,
                                       const Math::Vector3& secondAnchor,
                                       const float swingLimitDegrees) noexcept {
    RagdollJoint joint;
    joint.firstBody = firstBody;
    joint.secondBody = secondBody;
    joint.firstAnchor = firstAnchor;
    joint.secondAnchor = secondAnchor;
    joint.swing1Motion = RagdollMotion::Limited;
    joint.swing2Motion = RagdollMotion::Limited;
    joint.twistMotion = RagdollMotion::Limited;
    joint.swing1LimitDegrees = swingLimitDegrees;
    joint.swing2LimitDegrees = swingLimitDegrees;
    joint.twistLimitDegrees = swingLimitDegrees;
    return joint;
}

RagdollJoint MakeHingeJoint(const std::uint32_t firstBody, const std::uint32_t secondBody,
                                  const Math::Vector3& firstAnchor, const Math::Vector3& secondAnchor,
                                  const float twistLimitDegrees) noexcept {
    RagdollJoint joint = MakeBallSocketJoint(firstBody, secondBody, firstAnchor, secondAnchor);
    joint.swing1Motion = RagdollMotion::Locked;
    joint.swing2Motion = RagdollMotion::Locked;
    joint.twistMotion = RagdollMotion::Limited;
    joint.twistLimitDegrees = twistLimitDegrees;
    return joint;
}

bool RagdollSolver::Reset(const std::span<const RagdollBody> bodies) {
    if (bodies.size() > kMaximumRagdollBodies) return false;
    m_bodies = {};
    m_effectiveInverseInertiaLocal = {};
    m_selfCollisionMask = {};
    m_worldContactMask = 0U;
    m_bodyCount = 0U;
    m_joints = {};
    m_jointCount = 0U;
    m_lastContactFeedbackCount = 0U;
    m_sleepTimer = 0.0F;
    m_smoothedLinearSpeed = -1.0F;
    m_smoothedAngularSpeed = -1.0F;
    m_sleeping = false;
    m_lastStepSeconds = kRagdollFixedStepSeconds;
    for (const RagdollBody& authored : bodies) {
        RagdollBody body = authored;
        body.inverseInertiaLocal = ComputeCapsuleInverseInertia(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
        m_bodies[m_bodyCount] = body;
        m_effectiveInverseInertiaLocal[m_bodyCount] = body.kinematic ? Math::Vector3{} : body.inverseInertiaLocal;
        ++m_bodyCount;
    }
    // Enable all non-self pairs by default; AddJoint automatically clears direct parent-child pairs.
    const std::uint32_t allBodiesMask =
        m_bodyCount >= 64U ? ~std::uint64_t{0U} : ((std::uint64_t{1U} << m_bodyCount) - 1U);
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_selfCollisionMask[index] = allBodiesMask & ~(std::uint64_t{1U} << index);
    }
    return true;
}

bool RagdollSolver::AddJoint(const RagdollJoint& joint) {
    if (joint.firstBody >= m_bodyCount || joint.secondBody >= m_bodyCount ||
        joint.firstBody == joint.secondBody || m_jointCount >= kMaximumRagdollJoints) {
        return false;
    }
    m_joints[m_jointCount++] = joint;
    // Direct parent-child bodies overlap at the joint pivot; always disable their self-collision.
    SetSelfCollisionPairEnabled(joint.firstBody, joint.secondBody, false);
    return true;
}

const RagdollBody& RagdollSolver::GetBody(const std::size_t index) const noexcept {
    return m_bodies[std::min(index, kMaximumRagdollBodies - 1U)];
}

RagdollBody& RagdollSolver::GetBody(const std::size_t index) noexcept {
    return m_bodies[std::min(index, kMaximumRagdollBodies - 1U)];
}

const RagdollJoint& RagdollSolver::GetJoint(const std::size_t index) const noexcept {
    return m_joints[std::min(index, kMaximumRagdollJoints - 1U)];
}

RagdollJoint& RagdollSolver::GetJoint(const std::size_t index) noexcept {
    return m_joints[std::min(index, kMaximumRagdollJoints - 1U)];
}

void RagdollSolver::RebuildInertia() noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBody& body = m_bodies[index];
        body.inverseInertiaLocal = ComputeCapsuleInverseInertia(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
        m_effectiveInverseInertiaLocal[index] = body.kinematic ? Math::Vector3{} : body.inverseInertiaLocal;
    }
}

void RagdollSolver::StabilizeInertia(const float maxPrincipalRatio, const float maxChainRatio) noexcept {
    const float principalRatio = std::max(1.0F, std::isfinite(maxPrincipalRatio) ? maxPrincipalRatio : 4.0F);
    const float chainRatio = std::max(1.0F, std::isfinite(maxChainRatio) ? maxChainRatio : 16.0F);

    float minBodyMaxInvI = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBody& body = m_bodies[index];
        body.inverseInertiaLocal = ConditionSingleInverseInertia(body.inverseInertiaLocal, principalRatio);
        const float maxComp =
            std::max({body.inverseInertiaLocal.x, body.inverseInertiaLocal.y, body.inverseInertiaLocal.z});
        if (!body.kinematic && maxComp > 0.0F) {
            minBodyMaxInvI = std::min(minBodyMaxInvI, maxComp);
        }
    }
    if (std::isfinite(minBodyMaxInvI) && minBodyMaxInvI > 0.0F) {
        const float chainCap = minBodyMaxInvI * chainRatio;
        for (std::size_t index = 0U; index < m_bodyCount; ++index) {
            RagdollBody& body = m_bodies[index];
            body.inverseInertiaLocal.x = std::min(body.inverseInertiaLocal.x, chainCap);
            body.inverseInertiaLocal.y = std::min(body.inverseInertiaLocal.y, chainCap);
            body.inverseInertiaLocal.z = std::min(body.inverseInertiaLocal.z, chainCap);
            m_effectiveInverseInertiaLocal[index] =
                body.kinematic ? Math::Vector3{} : body.inverseInertiaLocal;
        }
    }
}

void RagdollSolver::SetSelfCollisionPairEnabled(const std::uint32_t firstBody,
                                                      const std::uint32_t secondBody,
                                                      const bool enabled) noexcept {
    if (firstBody >= m_bodyCount || secondBody >= m_bodyCount || firstBody == secondBody) return;
    const std::uint64_t bitFirst = std::uint64_t{1U} << firstBody;
    const std::uint64_t bitSecond = std::uint64_t{1U} << secondBody;
    if (enabled) {
        m_selfCollisionMask[firstBody] |= bitSecond;
        m_selfCollisionMask[secondBody] |= bitFirst;
    } else {
        m_selfCollisionMask[firstBody] &= ~bitSecond;
        m_selfCollisionMask[secondBody] &= ~bitFirst;
    }
}

bool RagdollSolver::CanSelfCollide(const std::uint32_t firstBody,
                                         const std::uint32_t secondBody) const noexcept {
    if (firstBody >= m_bodyCount || secondBody >= m_bodyCount || firstBody == secondBody) return false;
    return (m_selfCollisionMask[firstBody] & (std::uint64_t{1U} << secondBody)) != 0U;
}

void RagdollSolver::RebuildSelfCollisionFilter(const bool disableCurrentPoseOverlaps,
                                                     const std::uint32_t minimumJointHops) noexcept {
    const std::uint32_t allBodiesMask =
        m_bodyCount >= 64U ? ~std::uint64_t{0U} : ((std::uint64_t{1U} << m_bodyCount) - 1U);
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_selfCollisionMask[index] = allBodiesMask & ~(std::uint64_t{1U} << index);
    }

    // All-pairs shortest hop distance along the joint graph (bounded by kMaximumRagdollBodies).
    constexpr std::uint32_t kInfHops = static_cast<std::uint32_t>(kMaximumRagdollBodies) + 1U;
    std::array<std::array<std::uint32_t, kMaximumRagdollBodies>, kMaximumRagdollBodies> dist{};
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        for (std::size_t j = 0U; j < m_bodyCount; ++j) {
            dist[i][j] = (i == j) ? 0U : kInfHops;
        }
    }
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJoint& joint = m_joints[jointIndex];
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

    std::array<Math::Vector3, kMaximumRagdollBodies> segStarts{};
    std::array<Math::Vector3, kMaximumRagdollBodies> segEnds{};
    if (disableCurrentPoseOverlaps) {
        for (std::size_t i = 0U; i < m_bodyCount; ++i) {
            ComputeRagdollCapsuleWorldSegment(m_bodies[i], segStarts[i], segEnds[i]);
        }
    }

    const std::uint32_t minHops = std::max<std::uint32_t>(1U, minimumJointHops);
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        for (std::size_t j = i + 1U; j < m_bodyCount; ++j) {
            if (dist[i][j] < minHops) {
                SetSelfCollisionPairEnabled(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j), false);
                continue;
            }
            if (disableCurrentPoseOverlaps) {
                Math::Vector3 c1{};
                Math::Vector3 c2{};
                ClosestPointsBetweenSegments(segStarts[i], segEnds[i], segStarts[j], segEnds[j], c1, c2);
                const float combinedRadius =
                    std::max(0.0F, m_bodies[i].radius) + std::max(0.0F, m_bodies[j].radius) + kBindOverlapMargin;
                if (Math::LengthSquared(c2 - c1) < combinedRadius * combinedRadius) {
                    SetSelfCollisionPairEnabled(static_cast<std::uint32_t>(i), static_cast<std::uint32_t>(j),
                                                   false);
                }
            }
        }
    }
}

float RagdollSolver::GetTotalMass() const noexcept {
    float total = 0.0F;
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        const float mass = m_bodies[index].mass;
        if (std::isfinite(mass) && mass > 0.0F) total += mass;
    }
    return total;
}

void RagdollSolver::Sleep() noexcept {
    m_sleeping = true;
    m_sleepTimer = 0.0F;
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        m_bodies[index].linearVelocity = Math::Vector3{};
        m_bodies[index].angularVelocity = Math::Vector3{};
    }
}

void RagdollSolver::Wake() noexcept {
    m_sleeping = false;
    m_sleepTimer = 0.0F;
    m_smoothedLinearSpeed = -1.0F; // re-seeded from the first sample after waking
    m_smoothedAngularSpeed = -1.0F;
}

float RagdollSolver::EffectiveInverseMass(const std::size_t bodyIndex) const noexcept {
    const RagdollBody& body = m_bodies[bodyIndex];
    if (body.kinematic || !std::isfinite(body.inverseMass) || !(body.inverseMass > 0.0F)) return 0.0F;
    return body.inverseMass;
}

void RagdollSolver::PrepareEffectiveInertias(const RagdollStepSettings& settings) noexcept {
    float minBodyMaxInvI = std::numeric_limits<float>::infinity();
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        const RagdollBody& body = m_bodies[index];
        if (body.kinematic) {
            m_effectiveInverseInertiaLocal[index] = Math::Vector3{};
            continue;
        }
        Math::Vector3 invI = body.inverseInertiaLocal;
        if (settings.stabilizeInertia) {
            invI = ConditionSingleInverseInertia(invI, settings.maxPrincipalInertiaRatio);
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

std::size_t RagdollSolver::GenerateSelfContacts(const std::span<RagdollContact> outSelfContacts) const noexcept {
    if (outSelfContacts.empty() || m_bodyCount < 2U) return 0U;
    std::array<Math::Vector3, kMaximumRagdollBodies> segStarts{};
    std::array<Math::Vector3, kMaximumRagdollBodies> segEnds{};
    std::array<Math::Vector3, kMaximumRagdollBodies> aabbMin{};
    std::array<Math::Vector3, kMaximumRagdollBodies> aabbMax{};

    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        ComputeRagdollCapsuleWorldSegment(m_bodies[i], segStarts[i], segEnds[i]);
        const float r = std::max(0.0F, m_bodies[i].radius);
        aabbMin[i] = Math::Vector3{std::min(segStarts[i].x, segEnds[i].x) - r,
                                      std::min(segStarts[i].y, segEnds[i].y) - r,
                                      std::min(segStarts[i].z, segEnds[i].z) - r};
        aabbMax[i] = Math::Vector3{std::max(segStarts[i].x, segEnds[i].x) + r,
                                      std::max(segStarts[i].y, segEnds[i].y) + r,
                                      std::max(segStarts[i].z, segEnds[i].z) + r};
    }

    std::size_t count = 0U;
    for (std::size_t i = 0U; i < m_bodyCount; ++i) {
        const float ri = m_bodies[i].radius;
        if (!std::isfinite(ri) || !(ri > 1.0e-4F)) continue;
        const std::uint64_t mask = m_selfCollisionMask[i];
        for (std::size_t j = i + 1U; j < m_bodyCount; ++j) {
            if ((mask & (std::uint64_t{1U} << j)) == 0U) continue;
            if (m_bodies[i].kinematic && m_bodies[j].kinematic) continue;
            const float rj = m_bodies[j].radius;
            if (!std::isfinite(rj) || !(rj > 1.0e-4F)) continue;

            // Fast AABB rejection before segment-segment distance.
            if (aabbMax[i].x < aabbMin[j].x || aabbMin[i].x > aabbMax[j].x ||
                aabbMax[i].y < aabbMin[j].y || aabbMin[i].y > aabbMax[j].y ||
                aabbMax[i].z < aabbMin[j].z || aabbMin[i].z > aabbMax[j].z) {
                continue;
            }

            Math::Vector3 c1{};
            Math::Vector3 c2{};
            ClosestPointsBetweenSegments(segStarts[i], segEnds[i], segStarts[j], segEnds[j], c1, c2);
            const Math::Vector3 delta = c1 - c2; // Points from j -> i (pushes body i out of j).
            const float distSq = Math::LengthSquared(delta);
            const float radiusSum = ri + rj;
            if (!(distSq < radiusSum * radiusSum)) continue;

            Math::Vector3 normal{0.0F, 1.0F, 0.0F};
            float dist = 0.0F;
            if (distSq > 1.0e-12F) {
                dist = std::sqrt(distSq);
                normal = delta * (1.0F / dist);
            } else {
                const Math::Vector3 fallback = m_bodies[i].position - m_bodies[j].position;
                const float fbSq = Math::LengthSquared(fallback);
                if (fbSq > 1.0e-12F) {
                    normal = fallback * (1.0F / std::sqrt(fbSq));
                }
            }
            const float depth = radiusSum - dist;
            if (!(depth > 0.0F)) continue;

            RagdollContact contact;
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

Math::Vector3 RagdollSolver::TransformInverseInertia(const std::size_t bodyIndex,
                                                              const Math::Vector3& worldVector) const noexcept {
    const RagdollBody& body = m_bodies[bodyIndex];
    const Math::Vector3& invI = m_effectiveInverseInertiaLocal[bodyIndex];
    if (body.kinematic || (invI.x == 0.0F && invI.y == 0.0F && invI.z == 0.0F)) {
        return Math::Vector3{};
    }
    const Math::Vector3 local = Math::RotateVector(Math::Conjugate(body.rotation), worldVector);
    const Math::Vector3 scaled{local.x * invI.x, local.y * invI.y, local.z * invI.z};
    return Math::RotateVector(body.rotation, scaled);
}

Math::Matrix3x3 RagdollSolver::WorldInverseInertia(const std::size_t bodyIndex) const noexcept {
    const RagdollBody& body = m_bodies[bodyIndex];
    const Math::Vector3& invI = m_effectiveInverseInertiaLocal[bodyIndex];
    if (body.kinematic || (invI.x == 0.0F && invI.y == 0.0F && invI.z == 0.0F)) {
        return Math::Matrix3x3{};
    }
    const Math::Matrix3x3 rotation = RotationMatrix(body.rotation);
    Math::Matrix3x3 diagonal;
    diagonal.m[0][0] = invI.x;
    diagonal.m[1][1] = invI.y;
    diagonal.m[2][2] = invI.z;
    return rotation * diagonal * Math::Transpose(rotation);
}

Math::Vector3 RagdollSolver::AnchorVelocity(const std::size_t bodyIndex,
                                                     const Math::Vector3& worldOffset) const noexcept {
    const RagdollBody& body = m_bodies[bodyIndex];
    return body.linearVelocity + Math::Cross(body.angularVelocity, worldOffset);
}

void RagdollSolver::IntegrateVelocities(const Math::Vector3& gravity,
                                              const float deltaTimeSeconds) noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBody& body = m_bodies[index];
        if (body.kinematic) continue;
        const float invMass = EffectiveInverseMass(index);
        if (!(invMass > 0.0F) && Math::LengthSquared(m_effectiveInverseInertiaLocal[index]) <= 0.0F) continue;
        if (body.gravityEnabled && invMass > 0.0F) body.linearVelocity += gravity * deltaTimeSeconds;
        const bool touching = (m_worldContactMask & (std::uint64_t{1U} << index)) != 0U;
        const float linearFactor =
            1.0F / (1.0F + (std::max(0.0F, body.linearDamping) + (touching ? kRagdollContactLinearDamping : 0.0F)) *
                               deltaTimeSeconds);
        const float angularFactor =
            1.0F / (1.0F + (std::max(0.0F, body.angularDamping) + (touching ? kRagdollContactAngularDamping : 0.0F)) *
                               deltaTimeSeconds);
        body.linearVelocity = body.linearVelocity * linearFactor;
        body.angularVelocity = body.angularVelocity * angularFactor;
    }
}

void RagdollSolver::SolveJointVelocity(const RagdollJoint& joint) noexcept {
    RagdollBody& first = m_bodies[joint.firstBody];
    RagdollBody& second = m_bodies[joint.secondBody];
    const float firstInvMass = EffectiveInverseMass(joint.firstBody);
    const float secondInvMass = EffectiveInverseMass(joint.secondBody);
    const float inverseMassSum = firstInvMass + secondInvMass;
    if (!(inverseMassSum > 0.0F)) return;

    const Math::Vector3 firstOffset = Math::RotateVector(first.rotation, joint.firstAnchor);
    const Math::Vector3 secondOffset = Math::RotateVector(second.rotation, joint.secondAnchor);
    const Math::Vector3 relativeVelocity =
        AnchorVelocity(joint.secondBody, secondOffset) - AnchorVelocity(joint.firstBody, firstOffset);
    if (!Math::IsFinite(relativeVelocity)) return;

    const Math::Matrix3x3 effectiveMass =
        ComputePointEffectiveMass({firstOffset, secondOffset},
                                     {WorldInverseInertia(joint.firstBody), WorldInverseInertia(joint.secondBody)},
                                     inverseMassSum);
    Math::Matrix3x3 inverseEffectiveMass;
    if (!Math::TryInverse(effectiveMass, inverseEffectiveMass)) return;

    const Math::Vector3 impulse = Multiply(inverseEffectiveMass, relativeVelocity) * -1.0F;
    if (!Math::IsFinite(impulse)) return;

    first.linearVelocity -= impulse * firstInvMass;
    first.angularVelocity -= TransformInverseInertia(joint.firstBody, Math::Cross(firstOffset, impulse));
    second.linearVelocity += impulse * secondInvMass;
    second.angularVelocity += TransformInverseInertia(joint.secondBody, Math::Cross(secondOffset, impulse));
}

void RagdollSolver::SolveJointMotor(const RagdollJoint& joint, JointAccumulators& accum,
                                          const float deltaTimeSeconds) noexcept {
    const RagdollJointMotor& motor = joint.motor;
    if (!motor.enabled) return;
    const float scale = std::clamp(motor.strengthScale, 0.0F, 1.0F);
    if (!(scale > 0.0F)) return;
    const float stiffness = std::max(0.0F, std::isfinite(motor.angularStiffness) ? motor.angularStiffness : 0.0F) * scale;
    const float damping = std::max(0.0F, std::isfinite(motor.angularDamping) ? motor.angularDamping : 0.0F) * scale;
    const float maxTorque = std::max(0.0F, std::isfinite(motor.maxTorque) ? motor.maxTorque : 0.0F) * scale;
    if (!(maxTorque > 0.0F) || (!(stiffness > 0.0F) && !(damping > 0.0F))) return;

    RagdollBody& first = m_bodies[joint.firstBody];
    RagdollBody& second = m_bodies[joint.secondBody];
    const Math::Quaternion firstFrame = Math::Multiply(first.rotation, joint.firstFrame);
    const Math::Quaternion secondFrame = Math::Multiply(second.rotation, joint.secondFrame);
    Math::Quaternion inverseFirstFrame;
    if (!Math::TryInverse(firstFrame, inverseFirstFrame)) return;

    const Math::Quaternion currentRelative = Math::Multiply(inverseFirstFrame, secondFrame);
    Math::Quaternion targetRelative = motor.targetRelativeRotation;
    if (!Math::TryNormalize(targetRelative, targetRelative)) {
        targetRelative = Math::Quaternion{};
    }
    const Math::Vector3 localAngleError = QuaternionRotationErrorVector(currentRelative, targetRelative);
    if (!Math::IsFinite(localAngleError)) return;

    const Math::Vector3 targetAngVel =
        Math::IsFinite(motor.targetAngularVelocity) ? motor.targetAngularVelocity : Math::Vector3{};

    // Unconditionally stable implicit spring-damper denominator: 1 + c*dt + k*dt^2.
    const float denom = 1.0F + damping * deltaTimeSeconds + stiffness * deltaTimeSeconds * deltaTimeSeconds;
    const float maxImpulse = maxTorque * deltaTimeSeconds;

    const std::array<Math::Vector3, 3U> localAxes{
        Math::Vector3{1.0F, 0.0F, 0.0F},
        Math::Vector3{0.0F, 1.0F, 0.0F},
        Math::Vector3{0.0F, 0.0F, 1.0F},
    };
    const std::array<float, 3U> errors{localAngleError.x, localAngleError.y, localAngleError.z};
    const std::array<float, 3U> targetVels{targetAngVel.x, targetAngVel.y, targetAngVel.z};
    std::array<float*, 3U> accumSlots{&accum.motorAngular.x, &accum.motorAngular.y, &accum.motorAngular.z};

    for (std::size_t axisIndex = 0U; axisIndex < 3U; ++axisIndex) {
        const Math::Vector3 worldAxis = Math::RotateVector(firstFrame, localAxes[axisIndex]);
        const float axisLenSq = Math::LengthSquared(worldAxis);
        if (!(axisLenSq > kDirectionEpsilon)) continue;
        const Math::Vector3 unitAxis = worldAxis * (1.0F / std::sqrt(axisLenSq));

        const float effInv =
            Math::Dot(unitAxis, TransformInverseInertia(joint.firstBody, unitAxis)) +
            Math::Dot(unitAxis, TransformInverseInertia(joint.secondBody, unitAxis));
        if (!(effInv > kDirectionEpsilon)) continue;

        const float relOmega = Math::Dot(second.angularVelocity - first.angularVelocity, unitAxis);
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

        const Math::Vector3 angularImpulse = unitAxis * applied;
        first.angularVelocity -= TransformInverseInertia(joint.firstBody, angularImpulse);
        second.angularVelocity += TransformInverseInertia(joint.secondBody, angularImpulse);
    }
}

void RagdollSolver::SolveBodyDrive(const std::size_t bodyIndex, BodyDriveAccumulator& accum,
                                         const float deltaTimeSeconds) noexcept {
    RagdollBody& body = m_bodies[bodyIndex];
    if (body.kinematic || !body.worldDrive.enabled) return;
    const RagdollBodyDrive& drive = body.worldDrive;

    // World-space linear PD drive.
    const float invMass = EffectiveInverseMass(bodyIndex);
    const float linK = std::max(0.0F, std::isfinite(drive.linearStiffness) ? drive.linearStiffness : 0.0F);
    const float linC = std::max(0.0F, std::isfinite(drive.linearDamping) ? drive.linearDamping : 0.0F);
    const float maxLinForce = std::max(0.0F, std::isfinite(drive.maxLinearForce) ? drive.maxLinearForce : 0.0F);
    if (invMass > 0.0F && maxLinForce > 0.0F && (linK > 0.0F || linC > 0.0F) &&
        Math::IsFinite(drive.targetPosition)) {
        const Math::Vector3 targetVel =
            Math::IsFinite(drive.targetLinearVelocity) ? drive.targetLinearVelocity : Math::Vector3{};
        const Math::Vector3 posErr = drive.targetPosition - body.position;
        const Math::Vector3 velErr = targetVel - body.linearVelocity;
        const float denom = 1.0F + linC * deltaTimeSeconds + linK * deltaTimeSeconds * deltaTimeSeconds;
        const Math::Vector3 desiredDeltaV = (posErr * linK + velErr * linC) * (deltaTimeSeconds / denom);
        const Math::Vector3 deltaImpulse = desiredDeltaV * (1.0F / invMass);
        if (Math::IsFinite(deltaImpulse)) {
            Math::Vector3 cand = accum.linearImpulse + deltaImpulse;
            const float maxImpulse = maxLinForce * deltaTimeSeconds;
            const float magSq = Math::LengthSquared(cand);
            if (magSq > maxImpulse * maxImpulse && magSq > 1.0e-12F) {
                cand = cand * (maxImpulse / std::sqrt(magSq));
            }
            const Math::Vector3 applied = cand - accum.linearImpulse;
            accum.linearImpulse = cand;
            body.linearVelocity += applied * invMass;
        }
    }

    // World-space angular PD drive.
    const float angK = std::max(0.0F, std::isfinite(drive.angularStiffness) ? drive.angularStiffness : 0.0F);
    const float angC = std::max(0.0F, std::isfinite(drive.angularDamping) ? drive.angularDamping : 0.0F);
    const float maxAngTorque = std::max(0.0F, std::isfinite(drive.maxAngularTorque) ? drive.maxAngularTorque : 0.0F);
    if (maxAngTorque > 0.0F && (angK > 0.0F || angC > 0.0F)) {
        Math::Quaternion targetRot = drive.targetRotation;
        if (!Math::TryNormalize(targetRot, targetRot)) targetRot = Math::Quaternion{};
        const Math::Vector3 rotErr = QuaternionRotationErrorVector(body.rotation, targetRot);
        const Math::Vector3 targetOmega =
            Math::IsFinite(drive.targetAngularVelocity) ? drive.targetAngularVelocity : Math::Vector3{};
        const float denom = 1.0F + angC * deltaTimeSeconds + angK * deltaTimeSeconds * deltaTimeSeconds;
        const float maxImpulse = maxAngTorque * deltaTimeSeconds;

        const std::array<Math::Vector3, 3U> worldAxes{
            Math::Vector3{1.0F, 0.0F, 0.0F},
            Math::Vector3{0.0F, 1.0F, 0.0F},
            Math::Vector3{0.0F, 0.0F, 1.0F},
        };
        const std::array<float, 3U> errComps{rotErr.x, rotErr.y, rotErr.z};
        const std::array<float, 3U> omegaTargets{targetOmega.x, targetOmega.y, targetOmega.z};
        std::array<float*, 3U> accumSlots{&accum.angularImpulse.x, &accum.angularImpulse.y, &accum.angularImpulse.z};

        for (std::size_t k = 0U; k < 3U; ++k) {
            const Math::Vector3& axis = worldAxes[k];
            const float effInv = Math::Dot(axis, TransformInverseInertia(bodyIndex, axis));
            if (!(effInv > kDirectionEpsilon)) continue;
            const float velErr = omegaTargets[k] - Math::Dot(body.angularVelocity, axis);
            const float desiredDeltaOmega = (angK * errComps[k] + angC * velErr) * deltaTimeSeconds / denom;
            const float deltaImpulse = desiredDeltaOmega / effInv;
            if (!std::isfinite(deltaImpulse)) continue;

            const float oldVal = *accumSlots[k];
            const float newVal = std::clamp(oldVal + deltaImpulse, -maxImpulse, maxImpulse);
            const float applied = newVal - oldVal;
            *accumSlots[k] = newVal;
            body.angularVelocity += TransformInverseInertia(bodyIndex, axis * applied);
        }
    }
}

void RagdollSolver::SolveJointLimits(const RagdollJoint& joint, JointAccumulators& accum) noexcept {
    RagdollBody& first = m_bodies[joint.firstBody];
    RagdollBody& second = m_bodies[joint.secondBody];
    if (joint.twistMotion == RagdollMotion::Free && joint.swing1Motion == RagdollMotion::Free &&
        joint.swing2Motion == RagdollMotion::Free) {
        return;
    }

    const Math::Quaternion firstFrame = Math::Multiply(first.rotation, joint.firstFrame);
    const Math::Quaternion secondFrame = Math::Multiply(second.rotation, joint.secondFrame);
    Math::Quaternion inverseFirstFrame;
    if (!Math::TryInverse(firstFrame, inverseFirstFrame)) return;
    const JointAngles angles = DecomposeJointAngles(Math::Multiply(inverseFirstFrame, secondFrame));

    const float degreesToRadians = kPi / 180.0F;
    const float swing1Limit = std::max(0.0F, joint.swing1LimitDegrees) * degreesToRadians;
    const float swing2Limit = std::max(0.0F, joint.swing2LimitDegrees) * degreesToRadians;
    const float twistLimit = std::max(0.0F, joint.twistLimitDegrees) * degreesToRadians;
    const float twistLower = joint.twistAsymmetric ? joint.twistLowerLimitDegrees * degreesToRadians : -twistLimit;
    const float twistUpper = joint.twistAsymmetric ? joint.twistUpperLimitDegrees * degreesToRadians : twistLimit;

    const float stiffness = std::clamp(joint.limitStiffness, 0.0F, 1.0F);
    const float damping = std::clamp(joint.limitDamping, 0.0F, 2.0F);

    const auto solveAxis = [&](const Math::Vector3& localAxis, const float violation,
                               const bool bilateral, const bool upperBound, float& accumulated) {
        const Math::Vector3 worldAxis = Math::RotateVector(firstFrame, localAxis);
        const float axisLengthSquared = Math::LengthSquared(worldAxis);
        if (!(axisLengthSquared > kDirectionEpsilon)) return;
        const Math::Vector3 normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));

        const float effectiveMass =
            Math::Dot(normalisedAxis, TransformInverseInertia(joint.firstBody, normalisedAxis)) +
            Math::Dot(normalisedAxis, TransformInverseInertia(joint.secondBody, normalisedAxis));
        if (!(effectiveMass > kDirectionEpsilon)) return;

        const float relativeAngular = Math::Dot(second.angularVelocity - first.angularVelocity, normalisedAxis);
        const float bias = std::clamp(violation * stiffness / m_lastStepSeconds, -kMaximumJointBias,
                                      kMaximumJointBias);
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

        const Math::Vector3 angularImpulse = normalisedAxis * applied;
        first.angularVelocity -= TransformInverseInertia(joint.firstBody, angularImpulse);
        second.angularVelocity += TransformInverseInertia(joint.secondBody, angularImpulse);
    };

    // 1. Twist limit (X axis).
    {
        const AxisRange twistRange = ResolveAxisRange(joint.twistMotion, angles.twist, twistLower, twistUpper);
        if (twistRange.constrained) {
            const float clamped = std::clamp(angles.twist, twistRange.lower, twistRange.upper);
            const float violation = angles.twist - clamped;
            solveAxis(Math::Vector3{1.0F, 0.0F, 0.0F}, violation, twistRange.bilateral, violation > 0.0F,
                      accum.twistLimit);
        }
    }

    // 2. Swing limits: true elliptical cone when both axes are Limited, else per-axis.
    if (joint.swing1Motion == RagdollMotion::Limited && joint.swing2Motion == RagdollMotion::Limited &&
        swing1Limit > kDirectionEpsilon && swing2Limit > kDirectionEpsilon) {
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
            if (gLen > kDirectionEpsilon) {
                const float n1 = g1 / gLen; // Along swing1 (local +Z)
                const float n2 = g2 / gLen; // Along swing2 (local +Y)
                const float violation = (angles.swing1 - target1) * n1 + (angles.swing2 - target2) * n2;
                solveAxis(Math::Vector3{0.0F, n2, n1}, violation, /*bilateral=*/false, /*upperBound=*/true,
                          accum.swingConeLimit);
            }
        }
    } else {
        const AxisRange s1Range =
            ResolveAxisRange(joint.swing1Motion, angles.swing1, -swing1Limit, swing1Limit);
        if (s1Range.constrained) {
            const float violation = angles.swing1 - std::clamp(angles.swing1, s1Range.lower, s1Range.upper);
            solveAxis(Math::Vector3{0.0F, 0.0F, 1.0F}, violation, s1Range.bilateral, violation > 0.0F,
                      accum.swing1Limit);
        }
        const AxisRange s2Range =
            ResolveAxisRange(joint.swing2Motion, angles.swing2, -swing2Limit, swing2Limit);
        if (s2Range.constrained) {
            const float violation = angles.swing2 - std::clamp(angles.swing2, s2Range.lower, s2Range.upper);
            solveAxis(Math::Vector3{0.0F, 1.0F, 0.0F}, violation, s2Range.bilateral, violation > 0.0F,
                      accum.swing2Limit);
        }
    }
}

float RagdollSolver::ComputeContactEffectiveMass(const std::size_t bodyIndex,
                                                       const Math::Vector3& worldOffset,
                                                       const Math::Vector3& direction) const noexcept {
    const float invMass = EffectiveInverseMass(bodyIndex);
    const Math::Vector3 lever = Math::Cross(worldOffset, direction);
    const float result = invMass + Math::Dot(TransformInverseInertia(bodyIndex, lever), lever);
    return std::isfinite(result) ? result : 0.0F;
}

void RagdollSolver::ApplyImpulseAtPoint(const std::size_t bodyIndex, const Math::Vector3& impulse,
                                              const Math::Vector3& worldOffset) noexcept {
    RagdollBody& body = m_bodies[bodyIndex];
    if (body.kinematic) return;
    body.linearVelocity += impulse * EffectiveInverseMass(bodyIndex);
    body.angularVelocity += TransformInverseInertia(bodyIndex, Math::Cross(worldOffset, impulse));
}

void RagdollSolver::PrepareContactSolveState(const RagdollContact& contact,
                                                   ContactSolveState& outState) const noexcept {
    outState = {};
    if (contact.body >= m_bodyCount) return;
    const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
    const bool bodyActive = EffectiveInverseMass(contact.body) > 0.0F ||
                            Math::LengthSquared(m_effectiveInverseInertiaLocal[contact.body]) > 0.0F;
    const bool otherActive = hasOtherBody &&
                             (EffectiveInverseMass(contact.otherBody) > 0.0F ||
                              Math::LengthSquared(m_effectiveInverseInertiaLocal[contact.otherBody]) > 0.0F);
    if (!bodyActive && !otherActive) return;

    const float normalLengthSquared = Math::LengthSquared(contact.normal);
    if (!std::isfinite(normalLengthSquared) || !(normalLengthSquared > kDirectionEpsilon)) return;
    outState.normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));
    BuildTangents(outState.normal, outState.tangent1, outState.tangent2);

    const Math::Vector3 offsetA = contact.point - m_bodies[contact.body].position;
    Math::Vector3 otherVel = Math::IsFinite(contact.otherVelocity) ? contact.otherVelocity : Math::Vector3{};
    if (hasOtherBody) {
        otherVel = AnchorVelocity(contact.otherBody, contact.point - m_bodies[contact.otherBody].position);
    }
    const float initialNormalVel = Math::Dot(AnchorVelocity(contact.body, offsetA) - otherVel, outState.normal);
    if (!std::isfinite(initialNormalVel)) return;

    const float restitution = std::clamp(contact.restitution, 0.0F, 1.0F);
    outState.restitutionTarget =
        initialNormalVel < -kRestitutionThreshold ? -restitution * initialNormalVel : 0.0F;
    outState.valid = true;
}

void RagdollSolver::SolveContactVelocity(const RagdollContact& contact,
                                               ContactSolveState& state) noexcept {
    if (!state.valid) return;
    const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
    const Math::Vector3 offsetA = contact.point - m_bodies[contact.body].position;
    const Math::Vector3 offsetB =
        hasOtherBody ? (contact.point - m_bodies[contact.otherBody].position) : Math::Vector3{};
    const float externalInvMass =
        (!hasOtherBody && std::isfinite(contact.otherInverseMass) && contact.otherInverseMass > 0.0F)
            ? contact.otherInverseMass
            : 0.0F;

    const auto relativePointVelocity = [&]() noexcept -> Math::Vector3 {
        const Math::Vector3 velA = AnchorVelocity(contact.body, offsetA);
        if (hasOtherBody) return velA - AnchorVelocity(contact.otherBody, offsetB);
        const Math::Vector3 extVel =
            Math::IsFinite(contact.otherVelocity) ? contact.otherVelocity : Math::Vector3{};
        return velA - extVel;
    };

    const auto combinedEffectiveInvMass = [&](const Math::Vector3& dir) noexcept -> float {
        float eff = ComputeContactEffectiveMass(contact.body, offsetA, dir);
        if (hasOtherBody) {
            eff += ComputeContactEffectiveMass(contact.otherBody, offsetB, dir);
        } else {
            eff += externalInvMass;
        }
        return eff;
    };

    // 1. Accumulated clamped normal impulse (sequential impulses).
    const float normalVelocity = Math::Dot(relativePointVelocity(), state.normal);
    const float normalEffInvMass = combinedEffectiveInvMass(state.normal);
    if (std::isfinite(normalVelocity) && normalEffInvMass > kDirectionEpsilon) {
        const float deltaNormal = (state.restitutionTarget - normalVelocity) / normalEffInvMass;
        if (std::isfinite(deltaNormal)) {
            const float oldNormal = state.accumulatedNormal;
            state.accumulatedNormal = std::max(0.0F, oldNormal + deltaNormal);
            const float appliedNormal = state.accumulatedNormal - oldNormal;
            if (std::fabs(appliedNormal) > 0.0F) {
                const Math::Vector3 impulse = state.normal * appliedNormal;
                ApplyImpulseAtPoint(contact.body, impulse, offsetA);
                if (hasOtherBody) {
                    ApplyImpulseAtPoint(contact.otherBody, impulse * -1.0F, offsetB);
                }
            }
        }
    }

    // 2. Isotropic circular Coulomb friction cone clamped on accumulated normal impulse.
    const float friction = std::clamp(contact.friction, 0.0F, 4.0F);
    if (!(friction > 0.0F) || !(state.accumulatedNormal > 0.0F)) return;

    const Math::Vector3 relVel = relativePointVelocity();
    const float massT1 = combinedEffectiveInvMass(state.tangent1);
    const float massT2 = combinedEffectiveInvMass(state.tangent2);
    if (!(massT1 > kDirectionEpsilon) || !(massT2 > kDirectionEpsilon)) return;

    const float deltaT1 = -Math::Dot(relVel, state.tangent1) / massT1;
    const float deltaT2 = -Math::Dot(relVel, state.tangent2) / massT2;
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

    const Math::Vector3 frictionImpulse = state.tangent1 * appliedT1 + state.tangent2 * appliedT2;
    if (Math::IsFinite(frictionImpulse) && Math::LengthSquared(frictionImpulse) > 0.0F) {
        ApplyImpulseAtPoint(contact.body, frictionImpulse, offsetA);
        if (hasOtherBody) {
            ApplyImpulseAtPoint(contact.otherBody, frictionImpulse * -1.0F, offsetB);
        }
    }
}

void RagdollSolver::IntegratePositions(const float deltaTimeSeconds) noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBody& body = m_bodies[index];
        if (body.kinematic) continue;
        if (!Math::IsFinite(body.linearVelocity) || !Math::IsFinite(body.angularVelocity)) {
            body.linearVelocity = Math::Vector3{};
            body.angularVelocity = Math::Vector3{};
            continue;
        }
        body.position += body.linearVelocity * deltaTimeSeconds;

        const Math::Vector3 spin = body.angularVelocity;
        if (Math::LengthSquared(spin) > 1.0e-12F) {
            const Math::Quaternion spinQuaternion{spin.x, spin.y, spin.z, 0.0F};
            const Math::Quaternion delta = Math::Multiply(spinQuaternion, body.rotation);
            const Math::Quaternion candidate{body.rotation.x + delta.x * 0.5F * deltaTimeSeconds,
                                                body.rotation.y + delta.y * 0.5F * deltaTimeSeconds,
                                                body.rotation.z + delta.z * 0.5F * deltaTimeSeconds,
                                                body.rotation.w + delta.w * 0.5F * deltaTimeSeconds};
            Math::Quaternion normalised;
            if (Math::TryNormalize(candidate, normalised)) body.rotation = normalised;
        }
    }
}

void RagdollSolver::ApplyRotation(const std::size_t bodyIndex, const Math::Vector3& axis,
                                        const float radians) noexcept {
    if (m_bodies[bodyIndex].kinematic) return;
    if (!Math::IsFinite(axis) || !std::isfinite(radians) || std::fabs(radians) <= 0.0F) return;
    Math::Quaternion delta;
    if (!Math::TryMakeAxisAngle(axis, radians, delta)) return;
    const Math::Quaternion candidate = Math::Multiply(delta, m_bodies[bodyIndex].rotation);
    Math::Quaternion normalised;
    if (Math::TryNormalize(candidate, normalised)) m_bodies[bodyIndex].rotation = normalised;
}

void RagdollSolver::ProjectJoints() noexcept {
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJoint& joint = m_joints[jointIndex];
        if (!joint.projectionEnabled) continue;
        RagdollBody& first = m_bodies[joint.firstBody];
        RagdollBody& second = m_bodies[joint.secondBody];
        const float firstInvMass = EffectiveInverseMass(joint.firstBody);
        const float secondInvMass = EffectiveInverseMass(joint.secondBody);
        const float inverseMassSum = firstInvMass + secondInvMass;
        if (!(inverseMassSum > 0.0F)) continue;

        const auto projectLinearAnchors = [&]() noexcept {
            const Math::Vector3 firstOffset = Math::RotateVector(first.rotation, joint.firstAnchor);
            const Math::Vector3 secondOffset = Math::RotateVector(second.rotation, joint.secondAnchor);
            const Math::Vector3 error = (second.position + secondOffset) - (first.position + firstOffset);
            if (Math::IsFinite(error)) {
                first.position += error * (firstInvMass / inverseMassSum);
                second.position -= error * (secondInvMass / inverseMassSum);
            }
        };

        // --- Linear: move both bodies until the two anchors coincide. ---
        projectLinearAnchors();

        // --- Angular: rotate both bodies back inside their limits (true elliptical cone). ---
        if (joint.twistMotion == RagdollMotion::Free && joint.swing1Motion == RagdollMotion::Free &&
            joint.swing2Motion == RagdollMotion::Free) {
            continue;
        }
        const Math::Quaternion firstFrame = Math::Multiply(first.rotation, joint.firstFrame);
        const Math::Quaternion secondFrame = Math::Multiply(second.rotation, joint.secondFrame);
        Math::Quaternion inverseFirstFrame;
        if (!Math::TryInverse(firstFrame, inverseFirstFrame)) continue;
        const JointAngles angles = DecomposeJointAngles(Math::Multiply(inverseFirstFrame, secondFrame));

        const float degreesToRadians = kPi / 180.0F;
        const float swing1Limit = std::max(0.0F, joint.swing1LimitDegrees) * degreesToRadians;
        const float swing2Limit = std::max(0.0F, joint.swing2LimitDegrees) * degreesToRadians;
        const float twistLimit = std::max(0.0F, joint.twistLimitDegrees) * degreesToRadians;
        const float twistLower = joint.twistAsymmetric ? joint.twistLowerLimitDegrees * degreesToRadians : -twistLimit;
        const float twistUpper = joint.twistAsymmetric ? joint.twistUpperLimitDegrees * degreesToRadians : twistLimit;

        bool rotated = false;
        const auto projectAxis = [&](const Math::Vector3& localAxis, const float excess) noexcept {
            if (std::fabs(excess) <= kMinimumProjectionAngle) return;
            const Math::Vector3 worldAxis = Math::RotateVector(firstFrame, localAxis);
            const float axisLengthSquared = Math::LengthSquared(worldAxis);
            if (!(axisLengthSquared > kDirectionEpsilon)) return;
            const Math::Vector3 normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));
            const float firstResponse =
                Math::Dot(normalisedAxis, TransformInverseInertia(joint.firstBody, normalisedAxis));
            const float secondResponse =
                Math::Dot(normalisedAxis, TransformInverseInertia(joint.secondBody, normalisedAxis));
            const float responseSum = firstResponse + secondResponse;
            if (!(responseSum > kDirectionEpsilon)) return;
            ApplyRotation(joint.secondBody, normalisedAxis, -excess * (secondResponse / responseSum));
            ApplyRotation(joint.firstBody, normalisedAxis, excess * (firstResponse / responseSum));
            rotated = true;
        };

        // Twist projection.
        const AxisRange twistRange = ResolveAxisRange(joint.twistMotion, angles.twist, twistLower, twistUpper);
        if (twistRange.constrained) {
            const float excess = angles.twist - std::clamp(angles.twist, twistRange.lower, twistRange.upper);
            projectAxis(Math::Vector3{1.0F, 0.0F, 0.0F}, excess);
        }

        // Elliptical swing-cone projection (matching SolveJointLimits).
        if (joint.swing1Motion == RagdollMotion::Limited && joint.swing2Motion == RagdollMotion::Limited &&
            swing1Limit > kDirectionEpsilon && swing2Limit > kDirectionEpsilon) {
            const float r1 = angles.swing1 / swing1Limit;
            const float r2 = angles.swing2 / swing2Limit;
            const float magSq = r1 * r1 + r2 * r2;
            if (magSq > 1.0F) {
                const float scale = 1.0F / std::sqrt(magSq);
                const float excess1 = angles.swing1 - angles.swing1 * scale;
                const float excess2 = angles.swing2 - angles.swing2 * scale;
                const float excessMag = std::hypot(excess1, excess2);
                if (excessMag > kMinimumProjectionAngle) {
                    projectAxis(Math::Vector3{0.0F, excess2 / excessMag, excess1 / excessMag}, excessMag);
                }
            }
        } else {
            const AxisRange s1Range =
                ResolveAxisRange(joint.swing1Motion, angles.swing1, -swing1Limit, swing1Limit);
            if (s1Range.constrained) {
                projectAxis(Math::Vector3{0.0F, 0.0F, 1.0F},
                            angles.swing1 - std::clamp(angles.swing1, s1Range.lower, s1Range.upper));
            }
            const AxisRange s2Range =
                ResolveAxisRange(joint.swing2Motion, angles.swing2, -swing2Limit, swing2Limit);
            if (s2Range.constrained) {
                projectAxis(Math::Vector3{0.0F, 1.0F, 0.0F},
                            angles.swing2 - std::clamp(angles.swing2, s2Range.lower, s2Range.upper));
            }
        }

        // Re-project linear anchors after angular rotation so clamping never pulls joints apart!
        if (rotated) {
            projectLinearAnchors();
        }
    }
}

void RagdollSolver::ProjectContacts(const std::span<const RagdollContact> contacts,
                                          const float contactSlop, const float alpha) noexcept {
    const float clampedAlpha = std::clamp(alpha, 0.0F, 1.0F);
    for (const RagdollContact& contact : contacts) {
        if (contact.body >= m_bodyCount) continue;
        const float depth = contact.depth - contactSlop;
        if (!(depth > 0.0F)) continue;
        const float normalLengthSquared = Math::LengthSquared(contact.normal);
        if (!(normalLengthSquared > kDirectionEpsilon)) continue;
        const Math::Vector3 normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));
        const float correction = std::min(depth * clampedAlpha, kMaximumContactCorrection);

        const bool hasOtherBody = contact.otherBody < m_bodyCount && contact.otherBody != contact.body;
        if (hasOtherBody) {
            const float invA = EffectiveInverseMass(contact.body);
            const float invB = EffectiveInverseMass(contact.otherBody);
            const float invSum = invA + invB;
            if (!(invSum > 0.0F)) continue;
            m_bodies[contact.body].position += normal * (correction * (invA / invSum));
            m_bodies[contact.otherBody].position -= normal * (correction * (invB / invSum));
        } else if (!m_bodies[contact.body].kinematic) {
            m_bodies[contact.body].position += normal * correction;
        }
    }
}

void RagdollSolver::UpdateSleep(const float deltaTimeSeconds,
                                      const RagdollStepSettings& settings) noexcept {
    if (!(settings.sleepDelaySeconds > 0.0F) || !std::isfinite(settings.sleepDelaySeconds)) {
        m_sleepTimer = 0.0F;
        return;
    }
    // A mass-weighted average of body speeds decides, not the single fastest body: one forearm creeping at
    // a few centimetres per second must not keep a settled ragdoll awake. Detail bodies (fingers, toes)
    // are left out; a ragdoll made only of them is judged by all of its bodies instead.
    float weightedLinear = 0.0F;
    float weightedAngular = 0.0F;
    float totalWeight = 0.0F;
    std::size_t dynamicCount = 0U;
    for (const bool includeDetail : {false, true}) {
        for (std::size_t index = 0U; index < m_bodyCount; ++index) {
            if (m_bodies[index].kinematic) continue;
            if (!includeDetail && m_bodies[index].mass <= kRagdollDetailBodyMass) continue;
            ++dynamicCount;
            const float weight = std::isfinite(m_bodies[index].mass) && m_bodies[index].mass > 0.0F
                                     ? m_bodies[index].mass
                                     : 1.0F;
            totalWeight += weight;
            weightedLinear += weight * Math::LengthSquared(m_bodies[index].linearVelocity);
            weightedAngular += weight * Math::LengthSquared(m_bodies[index].angularVelocity);
        }
        if (dynamicCount > 0U) break;
    }
    if (dynamicCount == 0U || !(totalWeight > 0.0F)) {
        m_sleepTimer = 0.0F;
        return;
    }
    const float meanLinearSquared = weightedLinear / totalWeight;
    const float meanAngularSquared = weightedAngular / totalWeight;
    if (m_smoothedLinearSpeed < 0.0F || m_smoothedAngularSpeed < 0.0F) {
        m_smoothedLinearSpeed = std::sqrt(meanLinearSquared);
        m_smoothedAngularSpeed = std::sqrt(meanAngularSquared);
    }
    const float smoothing = std::clamp(deltaTimeSeconds / kRagdollSleepSmoothingSeconds, 0.0F, 1.0F);
    m_smoothedLinearSpeed += (std::sqrt(meanLinearSquared) - m_smoothedLinearSpeed) * smoothing;
    m_smoothedAngularSpeed += (std::sqrt(meanAngularSquared) - m_smoothedAngularSpeed) * smoothing;
    if (m_smoothedLinearSpeed < settings.sleepLinearSpeed && m_smoothedAngularSpeed < settings.sleepAngularSpeed) {
        m_sleepTimer += deltaTimeSeconds;
        if (m_sleepTimer >= settings.sleepDelaySeconds) Sleep();
    } else {
        m_sleepTimer = 0.0F;
    }
}

RagdollStepReport RagdollSolver::Step(const Math::Vector3& gravity, const float deltaTimeSeconds,
                                               const std::span<const RagdollContact> contacts,
                                               const RagdollStepSettings& settings) {
    RagdollStepReport report;
    report.sleeping = m_sleeping;
    m_lastContactFeedbackCount = 0U;
    if (m_bodyCount == 0U) return report;
    if (m_sleeping) return report;
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return report;
    if (!Math::IsFinite(gravity)) return report;

    const float stepSeconds = std::clamp(deltaTimeSeconds, 1.0e-5F, 0.1F);
    m_lastStepSeconds = stepSeconds;

    PrepareEffectiveInertias(settings);
    m_worldContactMask = 0U;
    for (const RagdollContact& contact : contacts) {
        if (contact.body < m_bodyCount && contact.otherBody == kInvalidRagdollBody && contact.depth > 0.0F) {
            m_worldContactMask |= std::uint64_t{1U} << contact.body;
        }
    }
    IntegrateVelocities(gravity, stepSeconds);

    // Build combined bounded contact buffer (world contacts + optional non-adjacent self-contacts).
    constexpr std::size_t kCombinedContactCap = kMaximumRagdollContacts + kMaximumRagdollSelfContacts;
    std::array<RagdollContact, kCombinedContactCap> activeContacts{};
    std::size_t worldContactCount = 0U;
    for (const RagdollContact& contact : contacts) {
        if (worldContactCount >= kMaximumRagdollContacts) break;
        activeContacts[worldContactCount++] = contact;
    }
    std::size_t selfContactCount = 0U;
    if (settings.enableSelfCollision) {
        selfContactCount = GenerateSelfContacts(
            std::span<RagdollContact>{activeContacts.data() + worldContactCount, kMaximumRagdollSelfContacts});
    }
    const std::size_t totalContactCount = worldContactCount + selfContactCount;
    const std::span<const RagdollContact> activeContactSpan{activeContacts.data(), totalContactCount};

    std::array<ContactSolveState, kCombinedContactCap> contactStates{};
    for (std::size_t c = 0U; c < totalContactCount; ++c) {
        PrepareContactSolveState(activeContacts[c], contactStates[c]);
    }
    std::array<JointAccumulators, kMaximumRagdollJoints> jointAccumulators{};
    std::array<BodyDriveAccumulator, kMaximumRagdollBodies> bodyDriveAccumulators{};

    const std::size_t velocityIterations = std::max<std::size_t>(1U, settings.velocityIterations);
    for (std::size_t iteration = 0U; iteration < velocityIterations; ++iteration) {
        for (std::size_t bodyIndex = 0U; bodyIndex < m_bodyCount; ++bodyIndex) {
            if (m_bodies[bodyIndex].worldDrive.enabled) {
                SolveBodyDrive(bodyIndex, bodyDriveAccumulators[bodyIndex], stepSeconds);
            }
        }
        for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
            const RagdollJoint& joint = m_joints[jointIndex];
            SolveJointVelocity(joint);
            SolveJointMotor(joint, jointAccumulators[jointIndex], stepSeconds);
            SolveJointLimits(joint, jointAccumulators[jointIndex]);
        }
        for (std::size_t c = 0U; c < totalContactCount; ++c) {
            SolveContactVelocity(activeContacts[c], contactStates[c]);
        }
    }
    report.velocityIterations = velocityIterations;

    // Record world contact impulses for dynamic Rigid3D pushback.
    for (std::size_t c = 0U; c < worldContactCount; ++c) {
        const RagdollContact& contact = activeContacts[c];
        const ContactSolveState& state = contactStates[c];
        if (!state.valid || contact.colliderIndex == kInvalidRagdollColliderIndex) continue;
        if (!(state.accumulatedNormal > 0.0F)) continue;
        if (m_lastContactFeedbackCount >= m_lastContactFeedback.size()) break;
        const Math::Vector3 totalImpulse = state.normal * state.accumulatedNormal +
                                              state.tangent1 * state.accumulatedTangent1 +
                                              state.tangent2 * state.accumulatedTangent2;
        if (!Math::IsFinite(totalImpulse)) continue;
        m_lastContactFeedback[m_lastContactFeedbackCount++] = RagdollContactFeedback{
            contact.body,
            contact.colliderIndex,
            contact.point,
            totalImpulse,
        };
    }

    IntegratePositions(stepSeconds);

    const std::size_t positionIterations = settings.positionIterations;
    for (std::size_t iteration = 0U; iteration < positionIterations; ++iteration) {
        ProjectJoints();
        ProjectContacts(activeContactSpan, settings.contactSlop, settings.contactCorrectionAlpha);
    }
    report.positionIterations = positionIterations;

    // Final bookkeeping: how far the joints actually are from coincident, and what was resolved.
    for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
        const RagdollJoint& joint = m_joints[jointIndex];
        const Math::Vector3 firstOffset = Math::RotateVector(m_bodies[joint.firstBody].rotation,
                                                                   joint.firstAnchor);
        const Math::Vector3 secondOffset = Math::RotateVector(m_bodies[joint.secondBody].rotation,
                                                                    joint.secondAnchor);
        const Math::Vector3 error = (m_bodies[joint.secondBody].position + secondOffset) -
                                       (m_bodies[joint.firstBody].position + firstOffset);
        const float driftSquared = Math::LengthSquared(error);
        if (std::isfinite(driftSquared)) {
            const float distance = std::sqrt(driftSquared);
            report.maximumJointDrift = std::max(report.maximumJointDrift, distance);
            if (distance > kJointDriftTolerance) ++report.jointDriftCount;
        }
        if (joint.twistMotion != RagdollMotion::Free || joint.swing1Motion != RagdollMotion::Free ||
            joint.swing2Motion != RagdollMotion::Free) {
            ++report.constrainedJointCount;
        }
    }
    for (std::size_t c = 0U; c < worldContactCount; ++c) {
        if (activeContacts[c].body < m_bodyCount) ++report.resolvedContactCount;
    }
    report.selfContactCount = selfContactCount;

    UpdateSleep(stepSeconds, settings);
    report.stepped = true;
    report.sleeping = m_sleeping;
    return report;
}

void RagdollSolver::ApplyImpulse(const std::uint32_t body, const Math::Vector3& impulse,
                                       const Math::Vector3& worldPoint) noexcept {
    if (body >= m_bodyCount) return;
    if (!Math::IsFinite(impulse) || !Math::IsFinite(worldPoint)) return;
    Wake();
    ApplyImpulseAtPoint(body, impulse, worldPoint - m_bodies[body].position);
}

} // namespace UVE::Physics
