// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

#include "uve/math/matrix3x3_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Physics {

/// Bounded sizes for one articulated ragdoll.
///
/// A humanoid physics asset is 19 bodies; 24 leaves room for a tail, a cape chain or a four-legged
/// character without turning this into a general rigid-body world. Joints are at most bodies - 1
/// for a tree, and contacts are capped separately because the cap is a cost bound, not a shape.
inline constexpr std::size_t kMaximumRagdollBodiesUVE = 24U;
inline constexpr std::size_t kMaximumRagdollJointsUVE = 24U;
inline constexpr std::size_t kMaximumRagdollContactsUVE = 128U;

/// Ragdolls are integrated in fixed steps so the same fall traces the same arc at 30, 60 or 144 fps.
/// 1/240 s is chosen for the same reason every shipping engine does: a limb tip reaches 20 m/s in a
/// hard impact, which is 8 cm per step - smaller than a capsule radius, so discrete contact cannot
/// tunnel through a wall that is not itself thinner than that.
inline constexpr float kRagdollFixedStepSecondsUVE = 1.0F / 240.0F;
/// A frame may spend at most this many fixed steps. A hitch is not replayed as a burst - chasing it
/// would pay unbounded cost in a frame that was already late - so the accumulator is clamped and
/// simulation time slips against wall-clock for one frame instead.
inline constexpr std::uint32_t kMaximumRagdollSubstepsUVE = 8U;

/// Velocity iterations per fixed step. Sequential impulses converge on joint chains slowly; 8 is the
/// point where a 19-body humanoid at a 4:1 mass ratio stops visibly stretching, and a ninth gains
/// less than it costs.
inline constexpr std::size_t kRagdollDefaultVelocityIterationsUVE = 8U;
/// Position iterations. This is Unreal's "Enable Linear/Angular Projection" and it is a different
/// mechanism from the velocity solve: it moves bodies back onto the joint AFTER integration, so it
/// removes drift without injecting energy into the velocity state.
inline constexpr std::size_t kRagdollDefaultPositionIterationsUVE = 4U;

/// Penetration allowed to persist, in metres. Two millimetres: below the threshold where a capsule
/// visibly sinks into a floor, above the depth where contact jitter would keep a resting ragdoll
/// awake forever.
inline constexpr float kRagdollContactSlopUVE = 0.002F;
/// Fraction of a contact's remaining penetration removed per position iteration.
inline constexpr float kRagdollContactCorrectionAlphaUVE = 0.8F;
/// Bodies slower than this for longer than the sleep delay are put to sleep. A dead body that is
/// still being integrated costs the same as a live one, and a settled ragdoll is usually a corpse.
inline constexpr float kRagdollSleepLinearSpeedUVE = 0.05F;
inline constexpr float kRagdollSleepAngularSpeedUVE = 0.2F;
inline constexpr float kRagdollSleepDelaySecondsUVE = 0.5F;

/// A joint's freedom about one rotational axis.
///
/// This is deliberately Unreal's three-way motion model (Free / Limited / Locked from its Physics
/// Constraint editor) rather than a `Hinge`/`BallSocket` pair of closed joint types. The reason is
/// anatomical: a shoulder and a knee are not two species of joint, they are the same spherical joint
/// with different limits. Encoding the limits instead of the type means the twist axis, the cone and
/// the hinge all fall out of one solver path, and a character rig with an unusual elbow needs no new
/// code - only a different number.
enum class RagdollMotionUVE : std::uint8_t {
    Free = 0,
    Limited,
    Locked,
};

/// One simulated bone: a rigid capsule with mass, a diagonal local inertia tensor, and damping.
///
/// Capsules, not boxes, because that is what every production physics asset generates for limbs: a
/// capsule has no orientation ambiguity about its long axis, so a limb cannot collide "edge-on" and
/// jam. Hands and feet are short capsules, which is a close enough shape at their scale.
struct RagdollBodyUVE final {
    // ---- Authored ----
    /// Kilograms. A 75 kg character is roughly pelvis 12, torso 20, head 5, upper arm 2.5,
    /// forearm 1.5, hand 0.5, thigh 9, shin 3.5, foot 1.
    float mass = 1.0F;
    /// Capsule radius in metres.
    float radius = 0.06F;
    /// Half the CYLINDRICAL part of the capsule, along the body's local +Y. The total length is
    /// `2 * (halfHeight + radius)`.
    float halfHeight = 0.08F;
    /// Unreal's per-body Linear/Angular Damping, and its default advice applies: distal limbs want
    /// more angular damping than proximal ones or the extremities spin after everything else stops.
    /// `velocity *= 1 / (1 + damping * dt)` - implicit, so it cannot overshoot at any step size.
    float linearDamping = 0.05F;
    float angularDamping = 0.5F;
    float friction = 0.8F;
    /// Zero, deliberately, and it is the single biggest realism decision here. A human body does not
    /// bounce; every engine default of 0.3-0.5 is what makes physical characters read as rubber.
    float restitution = 0.0F;
    bool gravityEnabled = true;

    // ---- Runtime ----
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE linearVelocity{};
    Math::Vector3UVE angularVelocity{};
    /// Diagonal inverse inertia in BODY-LOCAL space; the solver rotates it into the world per step.
    Math::Vector3UVE inverseInertiaLocal{};
    float inverseMass = 0.0F;

    [[nodiscard]] bool operator==(const RagdollBodyUVE&) const noexcept = default;
};

/// One joint between two bodies.
///
/// Angular limits are measured as the deviation of the two bodies' joint frames from coincident -
/// the same "constraint frame" model Unreal's Physics Asset uses. Each frame is a single quaternion
/// in its own body's local space, and the builder is expected to choose the second frame so the two
/// coincide at bind: `secondFrame = conjugate(secondBind) * firstBind * firstFrame`. A frame whose
/// local +X is the twist axis then means, with no extra state anywhere, that twist is rotation about
/// X, swing1 is rotation about Z and swing2 is rotation about Y.
struct RagdollJointUVE final {
    std::uint32_t firstBody = 0U;
    std::uint32_t secondBody = 0U;
    /// Joint origin in each body's local space; the two must coincide at bind.
    Math::Vector3UVE firstAnchor{};
    Math::Vector3UVE secondAnchor{};
    /// Constraint frames in body-local space. Identity means "identity axes", which is what a
    /// builder gets for free by leaving it alone and letting `secondFrame` absorb the bind rotation.
    Math::QuaternionUVE firstFrame{};
    Math::QuaternionUVE secondFrame{};

    /// Unreal's Linear Limits, which for a ragdoll are always Locked: a ball socket joint is a point
    /// coincidence, and a limb that could slide along its bone axis would grow.
    /// ---- Angular limits ----
    RagdollMotionUVE swing1Motion = RagdollMotionUVE::Limited;
    RagdollMotionUVE swing2Motion = RagdollMotionUVE::Limited;
    RagdollMotionUVE twistMotion = RagdollMotionUVE::Limited;
    /// Half-angles, in degrees, from the bind pose. A `Locked` motion ignores its angle.
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// An asymmetric twist range, for the joints that only bend one way.
    ///
    /// A knee and an elbow are hinges with a sign, and a symmetric `+/-` limit is what lets a ragdoll
    /// fold a shin forward through the thigh. Sign convention: positive twist rotates the second body
    /// about the joint frame's +X by the right-hand rule, so with the frame's X along the character's
    /// right, a knee's range is negative (the foot goes behind) and an elbow's is positive.
    bool twistAsymmetric = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;
    /// How hard an exceeded limit is pushed back, 0..1. Unreal exposes this as Limit Stiffness; the
    /// useful range there is the same, and the default is deliberately short of 1 so a hard stop
    /// reads as a joint stopping rather than as a latch snapping shut.
    float limitStiffness = 0.6F;
    /// Extra damping applied only while a limit is exceeded, draining the momentum that drove the
    /// joint into it instead of letting it ring. Unreal's Limit Damping, same purpose.
    float limitDamping = 0.8F;
    /// Whether position projection corrects this joint's drift. Unreal gates projection per joint
    /// because projecting a joint that is being pushed apart by a collision fights the collision.
    bool projectionEnabled = true;

    [[nodiscard]] bool operator==(const RagdollJointUVE&) const noexcept = default;
};

/// One contact between a ragdoll body and something the world layer found. The solver never looks at
/// the scene: it is handed these and nothing else, which is what keeps it testable without an ECS.
struct RagdollContactUVE final {
    std::uint32_t body = 0U;
    /// World-space contact position, on the body's surface.
    Math::Vector3UVE point{};
    /// Unit vector pushing the body OUT of the other shape.
    Math::Vector3UVE normal{0.0F, 1.0F, 0.0F};
    /// How deep the body is inside the other shape, in metres.
    float depth = 0.0F;
    float friction = 0.8F;
    float restitution = 0.0F;

    [[nodiscard]] bool operator==(const RagdollContactUVE&) const noexcept = default;
};

struct RagdollStepSettingsUVE final {
    std::size_t velocityIterations = kRagdollDefaultVelocityIterationsUVE;
    std::size_t positionIterations = kRagdollDefaultPositionIterationsUVE;
    float contactSlop = kRagdollContactSlopUVE;
    float contactCorrectionAlpha = kRagdollContactCorrectionAlphaUVE;
    /// Zero disables sleeping, which is what a test wants and what a corpse on a moving platform
    /// does not.
    float sleepDelaySeconds = kRagdollSleepDelaySecondsUVE;
    float sleepLinearSpeed = kRagdollSleepLinearSpeedUVE;
    float sleepAngularSpeed = kRagdollSleepAngularSpeedUVE;
};

/// What one fixed step actually did. Reported rather than hidden because every number here is
/// something a reviewer would otherwise have to infer from a behaviour change.
struct RagdollStepReportUVE final {
    bool stepped = false;
    bool sleeping = false;
    std::size_t velocityIterations = 0U;
    std::size_t positionIterations = 0U;
    std::size_t resolvedContactCount = 0U;
    /// Joints carrying at least one non-Free angular motion - i.e. joints that actually constrain
    /// something. A rig wired entirely with Free joints reports zero, which is the fastest way to
    /// notice a physics asset whose limits never got authored.
    std::size_t constrainedJointCount = 0U;
    /// Joints whose linear anchors could not be made to coincide within tolerance - the honest
    /// measure of "is this ragdoll falling apart".
    std::size_t jointDriftCount = 0U;
    float maximumJointDrift = 0.0F;
};

/// A bounded, deterministic articulated-body solver for ragdolls.
///
/// The method is sequential impulses (also called projected Gauss-Seidel), the family Unreal's
/// PhysX-backed ragdolls, Bullet and Box2D all use: each joint and each contact is solved in turn,
/// applying an impulse that removes the constraint's relative velocity, repeating until the
/// corrections stop fighting each other. It is not an exact solver - a 19-body chain never fully
/// converges - but it is unconditionally stable at any mass ratio, which a direct or a Jacobian
/// solve is not, and it degrades into "slightly soft joints" rather than into an explosion.
///
/// Determinism is structural rather than hoped-for: bodies are visited in index order, joints in
/// index order, contacts in the order the caller supplied them, and there is no threading and no
/// iteration over an unordered container anywhere in the step. Two runs of the same input produce
/// the same bits, which is what `Test/Physics/ragdoll_solver_uve_tests.cpp` asserts directly.
///
/// Deliberately absent, and each for a reason:
///  * No self-collision between the ragdoll's own bodies. Unreal disables collision between
///    constrained bodies for the same reason: adjacent capsules overlap at bind, and solving that
///    overlap costs more than the silhouette it would buy.
///  * No joint motors, no pose matching, no get-up. This is a passive ragdoll, not an active one.
///  * No warm starting across steps. It would help a resting stack and does nothing for a ragdoll
///    that is mostly airborne, and it would make the result depend on history the digest does not
///    carry.
class RagdollSolverUVE final {
public:
    /// Clears every body and joint and re-seeds the inertia of each authored body from its shape.
    /// Returns false only for a body count above `kMaximumRagdollBodiesUVE`.
    [[nodiscard]] bool ResetUVE(std::span<const RagdollBodyUVE> bodies);

    /// Adds one joint. Refused (false, nothing stored) for a joint whose body indices are out of
    /// range or equal, or when the joint table is full.
    [[nodiscard]] bool AddJointUVE(const RagdollJointUVE& joint);

    [[nodiscard]] std::size_t GetBodyCountUVE() const noexcept { return m_bodyCount; }
    [[nodiscard]] std::size_t GetJointCountUVE() const noexcept { return m_jointCount; }
    [[nodiscard]] const RagdollBodyUVE& GetBodyUVE(std::size_t index) const noexcept;
    [[nodiscard]] RagdollBodyUVE& GetBodyUVE(std::size_t index) noexcept;
    [[nodiscard]] const RagdollJointUVE& GetJointUVE(std::size_t index) const noexcept;

    /// Recomputes `inverseMass` and `inverseInertiaLocal` from the authored mass and capsule. Call it
    /// after changing those fields; `ResetUVE` and `AddJointUVE` do not do it for you, because a
    /// caller that knows better (a hand-tuned inertia tensor) must be able to override it.
    void RebuildInertiaUVE() noexcept;

    /// Total mass in kilograms - what a character's ragdoll should sum to, and the cheapest single
    /// check that a physics asset is not authored at ten times human density.
    [[nodiscard]] float GetTotalMassUVE() const noexcept;

    /// One fixed step. `deltaTimeSeconds` is clamped into [1e-5, 0.1]: a zero or negative step is a
    /// caller bug that would divide by zero in the bias terms, and a huge one would tunnel.
    [[nodiscard]] RagdollStepReportUVE StepUVE(const Math::Vector3UVE& gravity, float deltaTimeSeconds,
                                               std::span<const RagdollContactUVE> contacts,
                                               const RagdollStepSettingsUVE& settings = {});

    /// Puts every body to sleep immediately and zeroes its velocity.
    void SleepUVE() noexcept;
    /// Wakes the ragdoll and restarts the sleep timer. Called by anything that applies an external
    /// impulse - a bullet, a blast, a grabbed limb.
    void WakeUVE() noexcept;
    [[nodiscard]] bool IsSleepingUVE() const noexcept { return m_sleeping; }

    /// Wakes `body` and applies an impulse (N.s) at `worldPoint`, which is how a hit that the world
    /// layer never sees - a hitscan leg shot - enters the simulation without a contact.
    void ApplyImpulseUVE(std::uint32_t body, const Math::Vector3UVE& impulse,
                         const Math::Vector3UVE& worldPoint) noexcept;

private:
    struct AngularConstraintUVE final {
        std::size_t jointIndex = 0U;
        /// World-space rotation axis, recomputed each time the joint is visited.
        Math::Vector3UVE axis{};
        /// Signed angle about `axis` and the interval it is allowed in, in radians.
        float angle = 0.0F;
        float lower = 0.0F;
        float upper = 0.0F;
        bool locked = false;
    };

    void IntegrateVelocitiesUVE(const Math::Vector3UVE& gravity, float deltaTimeSeconds) noexcept;
    void SolveJointVelocityUVE(const RagdollJointUVE& joint) noexcept;
    void SolveJointLimitsUVE(const RagdollJointUVE& joint) noexcept;
    void SolveContactVelocityUVE(const RagdollContactUVE& contact) noexcept;
    void IntegratePositionsUVE(float deltaTimeSeconds) noexcept;
    void ProjectJointsUVE() noexcept;
    void ProjectContactsUVE(std::span<const RagdollContactUVE> contacts, float contactSlop,
                            float alpha) noexcept;
    void UpdateSleepUVE(float deltaTimeSeconds, const RagdollStepSettingsUVE& settings) noexcept;

    void ApplyImpulseAtPointUVE(std::size_t bodyIndex, const Math::Vector3UVE& impulse,
                                const Math::Vector3UVE& worldOffset) noexcept;
    void ApplyRotationUVE(std::size_t bodyIndex, const Math::Vector3UVE& axis, float radians) noexcept;
    [[nodiscard]] float ComputeContactEffectiveMassUVE(std::size_t bodyIndex, const Math::Vector3UVE& worldOffset,
                                                       const Math::Vector3UVE& direction) const noexcept;
    [[nodiscard]] Math::Vector3UVE TransformInverseInertiaUVE(std::size_t bodyIndex,
                                                              const Math::Vector3UVE& worldVector) const noexcept;
    [[nodiscard]] Math::Matrix3x3UVE WorldInverseInertiaUVE(std::size_t bodyIndex) const noexcept;
    [[nodiscard]] Math::Vector3UVE AnchorVelocityUVE(std::size_t bodyIndex,
                                                     const Math::Vector3UVE& worldOffset) const noexcept;

    std::array<RagdollBodyUVE, kMaximumRagdollBodiesUVE> m_bodies{};
    std::size_t m_bodyCount = 0U;
    std::array<RagdollJointUVE, kMaximumRagdollJointsUVE> m_joints{};
    std::size_t m_jointCount = 0U;
    /// The clamped step the current solve is using. The limit bias is `angle / dt`, so the solver
    /// needs the step size; passing it down every call site would be four more parameters for one
    /// value that cannot change mid-step.
    float m_lastStepSeconds = kRagdollFixedStepSecondsUVE;
    float m_sleepTimer = 0.0F;
    bool m_sleeping = false;
};

/// Inverse inertia of a capsule about its own centre, as a diagonal in body-local space: the axial
/// component of a solid cylinder, and the lateral one of a cylinder whose length is the capsule's
/// FULL length - barrel plus both caps.
///
/// A capsule's exact tensor differs from that by a few percent for limb proportions, invisible in
/// motion, and not worth the closed form. A capsule with no barrel is a sphere and gets the sphere's
/// tensor; zero or negative mass, or a zero radius, reports zero, which the solver reads as
/// "immovable" rather than as a division by zero.
[[nodiscard]] Math::Vector3UVE ComputeCapsuleInverseInertiaUVE(float mass, float radius,
                                                               float halfHeight) noexcept;

/// World-space endpoints of a body's capsule axis, in the order (start, end).
void ComputeRagdollCapsuleWorldSegmentUVE(const RagdollBodyUVE& body, Math::Vector3UVE& outStart,
                                          Math::Vector3UVE& outEnd) noexcept;

/// Builds the shortest-arc rotation taking +X onto `axis`. Deterministic, and degenerate input
/// (zero or non-finite) answers the identity rather than a NaN rotation.
[[nodiscard]] Math::QuaternionUVE MakeJointFrameFromTwistAxisUVE(const Math::Vector3UVE& axis) noexcept;

/// The joint frame of `second` relative to `first`, such that the two frames coincide at bind.
/// `firstFrame` defaults to identity, which is what makes this one call enough to build a joint.
[[nodiscard]] Math::QuaternionUVE ComputeJointSecondFrameUVE(const Math::QuaternionUVE& firstBindRotation,
                                                             const Math::QuaternionUVE& secondBindRotation,
                                                             const Math::QuaternionUVE& firstFrame = {}) noexcept;

/// Convenience constructors for the three shapes a humanoid rig actually needs. Each writes the
/// motions and limits that Unreal's Physics Asset editor would produce for that joint.
[[nodiscard]] RagdollJointUVE MakeBallSocketJointUVE(std::uint32_t firstBody, std::uint32_t secondBody,
                                                     const Math::Vector3UVE& firstAnchor,
                                                     const Math::Vector3UVE& secondAnchor,
                                                     float swingLimitDegrees = 60.0F) noexcept;
/// A hinge: swing locked, twist limited. This is a knee or an elbow - one degree of freedom, and the
/// reason a ragdoll does not bend its leg backwards.
[[nodiscard]] RagdollJointUVE MakeHingeJointUVE(std::uint32_t firstBody, std::uint32_t secondBody,
                                                const Math::Vector3UVE& firstAnchor,
                                                const Math::Vector3UVE& secondAnchor,
                                                float twistLimitDegrees = 70.0F) noexcept;

} // namespace UVE::Physics
