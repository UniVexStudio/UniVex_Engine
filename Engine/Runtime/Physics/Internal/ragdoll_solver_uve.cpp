// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/ragdoll_solver_uve.h"

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
///
/// The minus signs are not a typo. An impulse `lambda` at offset `r` changes the anchor velocity by
/// `-skew(r) I^-1 skew(r) lambda`, which is positive semi-definite because
/// `x^T (-skew(r) I^-1 skew(r)) x = (r x)^T I^-1 (r x) >= 0`.
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

/// Swing and twist of a joint's relative rotation, about the frame's local X, Y and Z.
///
/// This is the decomposition Unreal's constraint editor draws as one twist gauge and a two-axis
/// swing cone, and it is the reason a knee and a shoulder need no separate joint types.
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
    float lower = 0.0F;
    float upper = 0.0F;
};

[[nodiscard]] AxisRangeUVE ResolveAxisRangeUVE(const RagdollMotionUVE motion, const float angle,
                                               const float lowerRadians, const float upperRadians) noexcept {
    AxisRangeUVE range;
    if (motion == RagdollMotionUVE::Free) return range;
    if (motion == RagdollMotionUVE::Locked) {
        range.constrained = true;
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
    // A capsule with no barrel is a sphere, and it is NOT a cylinder that happens to be short: a
    // zero inverse inertia here would make a hand or a ball jointed to it unable to turn at all,
    // which reads as a limb that has been welded to the rig.
    double axial = 0.4 * massValue * radiusValue * radiusValue;
    double lateral = axial;
    if (safeHalfHeight > 0.0F) {
        // A solid cylinder of the capsule's full length (barrel plus both caps). Treating the caps
        // as cylinder overstates the long axis by a few percent - invisible in motion, and far
        // cheaper than the closed form for a spherocylinder.
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
        // Anti-parallel: the shortest arc is undefined, so pick a fixed perpendicular. Deterministic
        // rather than "whatever the cross product returned", because a frame that differed between
        // two runs would make every limit in the rig differ with it.
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
    // The two frames must coincide at bind: `second * F2 == first * F1`, so
    // `F2 = conjugate(second) * first * F1`.
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
    // Unreal's "Swing1/Swing2 Locked, Twist Limited": two hard angular constraints and one limited
    // axis. That is the whole difference between a knee and a knee that bends backwards.
    joint.swing1Motion = RagdollMotionUVE::Locked;
    joint.swing2Motion = RagdollMotionUVE::Locked;
    joint.twistMotion = RagdollMotionUVE::Limited;
    joint.twistLimitDegrees = twistLimitDegrees;
    return joint;
}

bool RagdollSolverUVE::ResetUVE(const std::span<const RagdollBodyUVE> bodies) {
    if (bodies.size() > kMaximumRagdollBodiesUVE) return false;
    m_bodies = {};
    m_bodyCount = 0U;
    m_joints = {};
    m_jointCount = 0U;
    m_sleepTimer = 0.0F;
    m_sleeping = false;
    m_lastStepSeconds = kRagdollFixedStepSecondsUVE;
    for (const RagdollBodyUVE& authored : bodies) {
        RagdollBodyUVE body = authored;
        body.inverseInertiaLocal = ComputeCapsuleInverseInertiaUVE(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
        m_bodies[m_bodyCount++] = body;
    }
    return true;
}

bool RagdollSolverUVE::AddJointUVE(const RagdollJointUVE& joint) {
    if (joint.firstBody >= m_bodyCount || joint.secondBody >= m_bodyCount ||
        joint.firstBody == joint.secondBody || m_jointCount >= kMaximumRagdollJointsUVE) {
        return false;
    }
    m_joints[m_jointCount++] = joint;
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

void RagdollSolverUVE::RebuildInertiaUVE() noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        body.inverseInertiaLocal = ComputeCapsuleInverseInertiaUVE(body.mass, body.radius, body.halfHeight);
        body.inverseMass = std::isfinite(body.mass) && body.mass > 0.0F ? 1.0F / body.mass : 0.0F;
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

Math::Vector3UVE RagdollSolverUVE::TransformInverseInertiaUVE(const std::size_t bodyIndex,
                                                             const Math::Vector3UVE& worldVector) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    // Body space, scale by the diagonal tensor, back to world. A full 3x3 would be exact for an
    // arbitrary tensor; a capsule's is diagonal, and this is the cheap form of exactly that product.
    const Math::Vector3UVE local = Math::RotateVectorUVE(Math::ConjugateUVE(body.rotation), worldVector);
    const Math::Vector3UVE scaled{local.x * body.inverseInertiaLocal.x, local.y * body.inverseInertiaLocal.y,
                                  local.z * body.inverseInertiaLocal.z};
    return Math::RotateVectorUVE(body.rotation, scaled);
}

Math::Matrix3x3UVE RagdollSolverUVE::WorldInverseInertiaUVE(const std::size_t bodyIndex) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    const Math::Matrix3x3UVE rotation = RotationMatrixUVE(body.rotation);
    Math::Matrix3x3UVE diagonal;
    diagonal.m[0][0] = body.inverseInertiaLocal.x;
    diagonal.m[1][1] = body.inverseInertiaLocal.y;
    diagonal.m[2][2] = body.inverseInertiaLocal.z;
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
        if (!(body.inverseMass > 0.0F) && Math::LengthSquaredUVE(body.inverseInertiaLocal) <= 0.0F) continue;
        if (body.gravityEnabled) body.linearVelocity += gravity * deltaTimeSeconds;
        // Implicit damping: v /= (1 + d*dt). Unconditional, and it is Unreal's own per-body damping
        // form - the difference from exp(-d*dt) is under a tenth of a percent per step.
        const float linearFactor = 1.0F / (1.0F + std::max(0.0F, body.linearDamping) * deltaTimeSeconds);
        const float angularFactor = 1.0F / (1.0F + std::max(0.0F, body.angularDamping) * deltaTimeSeconds);
        body.linearVelocity = body.linearVelocity * linearFactor;
        body.angularVelocity = body.angularVelocity * angularFactor;
    }
}

void RagdollSolverUVE::SolveJointVelocityUVE(const RagdollJointUVE& joint) noexcept {
    RagdollBodyUVE& first = m_bodies[joint.firstBody];
    RagdollBodyUVE& second = m_bodies[joint.secondBody];
    const float inverseMassSum = first.inverseMass + second.inverseMass;
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

    first.linearVelocity -= impulse * first.inverseMass;
    first.angularVelocity -= TransformInverseInertiaUVE(joint.firstBody, Math::CrossUVE(firstOffset, impulse));
    second.linearVelocity += impulse * second.inverseMass;
    second.angularVelocity += TransformInverseInertiaUVE(joint.secondBody, Math::CrossUVE(secondOffset, impulse));
}

void RagdollSolverUVE::SolveJointLimitsUVE(const RagdollJointUVE& joint) noexcept {
    RagdollBodyUVE& first = m_bodies[joint.firstBody];
    RagdollBodyUVE& second = m_bodies[joint.secondBody];
    if (joint.twistMotion == RagdollMotionUVE::Free && joint.swing1Motion == RagdollMotionUVE::Free &&
        joint.swing2Motion == RagdollMotionUVE::Free) {
        return;
    }

    // Relative rotation in the FIRST joint frame: identity when the two frames coincide, which is
    // exactly the bind pose. Every angle below is a deviation from that.
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

    // A cone limit on two limited swing axes is an ellipse in angle space, so a swing past one axis
    // pulls the other back with it. Without this the joint is a rectangular pyramid, which on a
    // shoulder reads as "the arm reaches the corner but not the diagonal".
    float swing1Target = angles.swing1;
    float swing2Target = angles.swing2;
    if (joint.swing1Motion == RagdollMotionUVE::Limited && joint.swing2Motion == RagdollMotionUVE::Limited &&
        swing1Limit > kDirectionEpsilonUVE && swing2Limit > kDirectionEpsilonUVE) {
        const float ratio1 = angles.swing1 / swing1Limit;
        const float ratio2 = angles.swing2 / swing2Limit;
        const float magnitudeSquared = ratio1 * ratio1 + ratio2 * ratio2;
        if (magnitudeSquared > 1.0F) {
            const float scale = 1.0F / std::sqrt(magnitudeSquared);
            swing1Target = angles.swing1 * scale;
            swing2Target = angles.swing2 * scale;
        }
    }

    struct AxisUVE final {
        RagdollMotionUVE motion = RagdollMotionUVE::Free;
        float angle = 0.0F;
        float target = 0.0F;
        float lower = 0.0F;
        float upper = 0.0F;
        Math::Vector3UVE localAxis{1.0F, 0.0F, 0.0F};
    };
    const std::array<AxisUVE, 3U> axes{
        AxisUVE{joint.twistMotion, angles.twist, angles.twist, twistLower, twistUpper, {1.0F, 0.0F, 0.0F}},
        AxisUVE{joint.swing1Motion, angles.swing1, swing1Target, -swing1Limit, swing1Limit, {0.0F, 0.0F, 1.0F}},
        AxisUVE{joint.swing2Motion, angles.swing2, swing2Target, -swing2Limit, swing2Limit, {0.0F, 1.0F, 0.0F}}};

    const float stiffness = std::clamp(joint.limitStiffness, 0.0F, 1.0F);
    const float damping = std::clamp(joint.limitDamping, 0.0F, 2.0F);

    for (const AxisUVE& axis : axes) {
        const AxisRangeUVE range = ResolveAxisRangeUVE(axis.motion, axis.angle, axis.lower, axis.upper);
        if (!range.constrained) continue;
        const float target = std::clamp(axis.target, range.lower, range.upper);

        const Math::Vector3UVE worldAxis = Math::RotateVectorUVE(firstFrame, axis.localAxis);
        const float axisLengthSquared = Math::LengthSquaredUVE(worldAxis);
        if (!(axisLengthSquared > kDirectionEpsilonUVE)) continue;
        const Math::Vector3UVE normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));

        const float effectiveFirst =
            Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.firstBody, normalisedAxis));
        const float effectiveSecond =
            Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.secondBody, normalisedAxis));
        const float effectiveMass = effectiveFirst + effectiveSecond;
        if (!(effectiveMass > kDirectionEpsilonUVE)) continue;

        const float relativeAngular = Math::DotUVE(second.angularVelocity - first.angularVelocity, normalisedAxis);
        // PD: P drives the angle back to the limit, D drains the momentum that overshot it.
        const float bias = std::clamp((axis.angle - target) * stiffness / m_lastStepSeconds, -kMaximumJointBiasUVE,
                                      kMaximumJointBiasUVE);
        const float impulse = -(relativeAngular * (1.0F + damping) + bias) / effectiveMass;
        if (!std::isfinite(impulse)) continue;

        const Math::Vector3UVE angularImpulse = normalisedAxis * impulse;
        first.angularVelocity -= TransformInverseInertiaUVE(joint.firstBody, angularImpulse);
        second.angularVelocity += TransformInverseInertiaUVE(joint.secondBody, angularImpulse);
    }
}

float RagdollSolverUVE::ComputeContactEffectiveMassUVE(const std::size_t bodyIndex,
                                                       const Math::Vector3UVE& worldOffset,
                                                       const Math::Vector3UVE& direction) const noexcept {
    const RagdollBodyUVE& body = m_bodies[bodyIndex];
    const Math::Vector3UVE lever = Math::CrossUVE(worldOffset, direction);
    const float result = body.inverseMass + Math::DotUVE(TransformInverseInertiaUVE(bodyIndex, lever), lever);
    return std::isfinite(result) ? result : 0.0F;
}

void RagdollSolverUVE::ApplyImpulseAtPointUVE(const std::size_t bodyIndex, const Math::Vector3UVE& impulse,
                                              const Math::Vector3UVE& worldOffset) noexcept {
    RagdollBodyUVE& body = m_bodies[bodyIndex];
    body.linearVelocity += impulse * body.inverseMass;
    body.angularVelocity += TransformInverseInertiaUVE(bodyIndex, Math::CrossUVE(worldOffset, impulse));
}

void RagdollSolverUVE::SolveContactVelocityUVE(const RagdollContactUVE& contact) noexcept {
    if (contact.body >= m_bodyCount) return;
    RagdollBodyUVE& body = m_bodies[contact.body];
    if (!(body.inverseMass > 0.0F) && Math::LengthSquaredUVE(body.inverseInertiaLocal) <= 0.0F) return;

    const float normalLengthSquared = Math::LengthSquaredUVE(contact.normal);
    if (!std::isfinite(normalLengthSquared) || !(normalLengthSquared > kDirectionEpsilonUVE)) return;
    const Math::Vector3UVE normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));

    const Math::Vector3UVE offset = contact.point - body.position;
    const float normalVelocity = Math::DotUVE(AnchorVelocityUVE(contact.body, offset), normal);
    if (!std::isfinite(normalVelocity) || normalVelocity > 0.0F) return;

    const float effectiveMass = ComputeContactEffectiveMassUVE(contact.body, offset, normal);
    if (!(effectiveMass > 0.0F)) return;

    // Restitution only above the threshold, so a resting body does not chatter on its own ties.
    const float restitution = std::clamp(contact.restitution, 0.0F, 1.0F);
    const float targetVelocity = normalVelocity < -kRestitutionThresholdUVE ? -restitution * normalVelocity : 0.0F;
    const float normalImpulse = (targetVelocity - normalVelocity) / effectiveMass;
    if (!std::isfinite(normalImpulse) || normalImpulse <= 0.0F) return;
    ApplyImpulseAtPointUVE(contact.body, normal * normalImpulse, offset);

    // Coulomb friction, bounded by mu times THIS iteration's normal impulse. Two tangents rather
    // than one so a body sliding diagonally is slowed in both directions instead of only in
    // whichever direction the normal happened to form a well-conditioned cross product with.
    const float friction = std::clamp(contact.friction, 0.0F, 4.0F);
    if (!(friction > 0.0F)) return;
    Math::Vector3UVE tangent1{};
    Math::Vector3UVE tangent2{};
    BuildTangentsUVE(normal, tangent1, tangent2);
    const float maximumFrictionImpulse = friction * normalImpulse;
    for (const Math::Vector3UVE& tangent : {tangent1, tangent2}) {
        const float tangentVelocity = Math::DotUVE(AnchorVelocityUVE(contact.body, offset), tangent);
        const float tangentMass = ComputeContactEffectiveMassUVE(contact.body, offset, tangent);
        if (!(tangentMass > 0.0F)) continue;
        float tangentImpulse = -tangentVelocity / tangentMass;
        tangentImpulse = std::clamp(tangentImpulse, -maximumFrictionImpulse, maximumFrictionImpulse);
        if (!std::isfinite(tangentImpulse) || tangentImpulse == 0.0F) continue;
        ApplyImpulseAtPointUVE(contact.body, tangent * tangentImpulse, offset);
    }
}

void RagdollSolverUVE::IntegratePositionsUVE(const float deltaTimeSeconds) noexcept {
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        RagdollBodyUVE& body = m_bodies[index];
        if (!Math::IsFiniteUVE(body.linearVelocity) || !Math::IsFiniteUVE(body.angularVelocity)) {
            // A non-finite velocity is a bug upstream, but propagating it would poison every body it
            // touches through the joints. Kill the motion and keep the pose.
            body.linearVelocity = Math::Vector3UVE{};
            body.angularVelocity = Math::Vector3UVE{};
            continue;
        }
        body.position += body.linearVelocity * deltaTimeSeconds;

        const Math::Vector3UVE spin = body.angularVelocity;
        if (Math::LengthSquaredUVE(spin) > 1.0e-12F) {
            // q' = q + 0.5 * dt * (omega, 0) * q, then renormalise. First order, but exactly
            // rotation-preserving once normalised, and stable for the step sizes a fixed ragdoll step
            // is allowed to use.
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
        const float inverseMassSum = first.inverseMass + second.inverseMass;
        if (!(inverseMassSum > 0.0F)) continue;

        // --- Linear: move both bodies until the two anchors coincide. ---
        const Math::Vector3UVE firstOffset = Math::RotateVectorUVE(first.rotation, joint.firstAnchor);
        const Math::Vector3UVE secondOffset = Math::RotateVectorUVE(second.rotation, joint.secondAnchor);
        const Math::Vector3UVE error = (second.position + secondOffset) - (first.position + firstOffset);
        if (Math::IsFiniteUVE(error)) {
            first.position += error * (first.inverseMass / inverseMassSum);
            second.position -= error * (second.inverseMass / inverseMassSum);
        }

        // --- Angular: rotate both bodies back inside their limits. ---
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
        struct ProjectAxisUVE final {
            RagdollMotionUVE motion = RagdollMotionUVE::Free;
            float angle = 0.0F;
            float lower = 0.0F;
            float upper = 0.0F;
            Math::Vector3UVE localAxis{1.0F, 0.0F, 0.0F};
        };
        const std::array<ProjectAxisUVE, 3U> axes{
            ProjectAxisUVE{joint.twistMotion, angles.twist,
                           joint.twistAsymmetric ? joint.twistLowerLimitDegrees * degreesToRadians : -twistLimit,
                           joint.twistAsymmetric ? joint.twistUpperLimitDegrees * degreesToRadians : twistLimit,
                           {1.0F, 0.0F, 0.0F}},
            ProjectAxisUVE{joint.swing1Motion, angles.swing1, -swing1Limit, swing1Limit, {0.0F, 0.0F, 1.0F}},
            ProjectAxisUVE{joint.swing2Motion, angles.swing2, -swing2Limit, swing2Limit, {0.0F, 1.0F, 0.0F}}};

        for (const ProjectAxisUVE& axis : axes) {
            const AxisRangeUVE range = ResolveAxisRangeUVE(axis.motion, axis.angle, axis.lower, axis.upper);
            if (!range.constrained) continue;
            const float excess = axis.angle - std::clamp(axis.angle, range.lower, range.upper);
            if (std::fabs(excess) <= kMinimumProjectionAngleUVE) continue;

            const Math::Vector3UVE worldAxis = Math::RotateVectorUVE(firstFrame, axis.localAxis);
            const float axisLengthSquared = Math::LengthSquaredUVE(worldAxis);
            if (!(axisLengthSquared > kDirectionEpsilonUVE)) continue;
            const Math::Vector3UVE normalisedAxis = worldAxis * (1.0F / std::sqrt(axisLengthSquared));
            const float firstResponse =
                Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.firstBody, normalisedAxis));
            const float secondResponse =
                Math::DotUVE(normalisedAxis, TransformInverseInertiaUVE(joint.secondBody, normalisedAxis));
            const float responseSum = firstResponse + secondResponse;
            if (!(responseSum > kDirectionEpsilonUVE)) continue;
            // Rotating the second body by -excess*share and the first by +excess*share removes the
            // violation, split by how much each body resists being rotated.
            ApplyRotationUVE(joint.secondBody, normalisedAxis, -excess * (secondResponse / responseSum));
            ApplyRotationUVE(joint.firstBody, normalisedAxis, excess * (firstResponse / responseSum));
        }
    }
}

void RagdollSolverUVE::ProjectContactsUVE(const std::span<const RagdollContactUVE> contacts,
                                          const float contactSlop, const float alpha) noexcept {
    for (const RagdollContactUVE& contact : contacts) {
        if (contact.body >= m_bodyCount) continue;
        const float depth = contact.depth - contactSlop;
        if (!(depth > 0.0F)) continue;
        const float normalLengthSquared = Math::LengthSquaredUVE(contact.normal);
        if (!(normalLengthSquared > kDirectionEpsilonUVE)) continue;
        const Math::Vector3UVE normal = contact.normal * (1.0F / std::sqrt(normalLengthSquared));
        m_bodies[contact.body].position +=
            normal * std::min(depth * std::clamp(alpha, 0.0F, 1.0F), kMaximumContactCorrectionUVE);
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
    for (std::size_t index = 0U; index < m_bodyCount; ++index) {
        maximumLinearSquared = std::max(maximumLinearSquared, Math::LengthSquaredUVE(m_bodies[index].linearVelocity));
        maximumAngularSquared = std::max(maximumAngularSquared, Math::LengthSquaredUVE(m_bodies[index].angularVelocity));
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
    if (m_bodyCount == 0U) return report;
    if (m_sleeping) return report;
    if (!std::isfinite(deltaTimeSeconds) || !(deltaTimeSeconds > 0.0F)) return report;
    if (!Math::IsFiniteUVE(gravity)) return report;

    const float stepSeconds = std::clamp(deltaTimeSeconds, 1.0e-5F, 0.1F);
    m_lastStepSeconds = stepSeconds;

    IntegrateVelocitiesUVE(gravity, stepSeconds);

    const std::size_t velocityIterations = std::max<std::size_t>(1U, settings.velocityIterations);
    for (std::size_t iteration = 0U; iteration < velocityIterations; ++iteration) {
        for (std::size_t jointIndex = 0U; jointIndex < m_jointCount; ++jointIndex) {
            const RagdollJointUVE& joint = m_joints[jointIndex];
            SolveJointVelocityUVE(joint);
            SolveJointLimitsUVE(joint);
        }
        for (const RagdollContactUVE& contact : contacts) {
            SolveContactVelocityUVE(contact);
        }
    }
    report.velocityIterations = velocityIterations;

    IntegratePositionsUVE(stepSeconds);

    const std::size_t positionIterations = settings.positionIterations;
    for (std::size_t iteration = 0U; iteration < positionIterations; ++iteration) {
        ProjectJointsUVE();
        ProjectContactsUVE(contacts, settings.contactSlop, settings.contactCorrectionAlpha);
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
    for (const RagdollContactUVE& contact : contacts) {
        if (contact.body < m_bodyCount) ++report.resolvedContactCount;
    }

    UpdateSleepUVE(stepSeconds, settings);
    report.stepped = true;
    report.sleeping = m_sleeping;
    return report;
}

void RagdollSolverUVE::ApplyImpulseUVE(const std::uint32_t body, const Math::Vector3UVE& impulse,
                                       const Math::Vector3UVE& worldPoint) noexcept {
    if (body >= m_bodyCount) return;
    if (!Math::IsFiniteUVE(impulse) || !Math::IsFiniteUVE(worldPoint)) return;
    // Waking is the point: an impulse on a sleeping ragdoll that did not wake it would be a hit that
    // visibly does nothing.
    WakeUVE();
    ApplyImpulseAtPointUVE(body, impulse, worldPoint - m_bodies[body].position);
}

} // namespace UVE::Physics
