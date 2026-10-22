// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"
#include "uve/physics/ragdoll_world_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::uint32_t kMaximumRagdollBonesUVE = static_cast<std::uint32_t>(Physics::kMaximumRagdollBodiesUVE);

/// One simulated bone, as authored.
///
/// The shape is a capsule along the bone's own axis, and its LENGTH is taken from the rig at build
/// time rather than from this struct: a physics asset that hard-coded a thigh length would be wrong
/// for every character that was not the one it was authored on. `radius` is authored because a
/// capsule's thickness is a modelling choice; `halfHeight` is only used for leaf bones, which have no
/// child to measure toward.
struct RagdollBoneSetupUVE final {
    /// Exact, case-sensitive bone name. Resolution failure drops the bone rather than guessing.
    std::string boneName;
    /// The bone this one is jointed to. Empty means "nearest simulated ancestor in the skeleton",
    /// which is what a rig wants in almost every case and what the generator fills in.
    std::string parentBoneName;
    float mass = 1.0F;
    float radius = 0.05F;
    /// Half the cylindrical part, used only when the bone has no child to measure toward.
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    /// Distal limbs want more of this than proximal ones; see the humanoid profile below.
    float angularDamping = 0.5F;
    /// How much of the step's motion is bled off before the solver sees it. A ragdoll that has just
    /// been hit wants a little, or every limb rings like a bell.
    float friction = 0.8F;

    Physics::RagdollMotionUVE swing1Motion = Physics::RagdollMotionUVE::Limited;
    Physics::RagdollMotionUVE swing2Motion = Physics::RagdollMotionUVE::Limited;
    Physics::RagdollMotionUVE twistMotion = Physics::RagdollMotionUVE::Limited;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// One-way ranges for the two joints that only bend one way. See RagdollJointUVE's own note on
    /// the sign convention; these are in degrees and only used when `twistAsymmetric` is set.
    bool twistAsymmetric = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;

    /// The joint frame's X axis, in the PARENT body's local space. Left zero, the builder derives it:
    /// along the bone for a cone joint (twist follows the limb), and along the character's lateral
    /// axis for a hinge (a knee bends sideways across the body, not around the shin).
    Math::Vector3UVE jointAxisLocal{0.0F, 0.0F, 0.0F};
};

/// The runtime half of one simulated bone. Never persisted.
struct RagdollBoneBindingUVE final {
    std::uint32_t bodyIndex = 0U;
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
    std::uint32_t parentBoneIndex = kInvalidSkeletonBoneIndexUVE;
    /// `conjugate(bodyBindRotation) * boneBindWorldRotation` - the capsule's fixed orientation
    /// relative to the bone it stands for. Writing a pose back is then one quaternion multiply,
    /// which is why the write-back cannot accumulate error the way a per-frame inverse would.
    Math::QuaternionUVE boneOffset{};
    /// Distance from the root along the drawn hierarchy. Bodies are written back in ascending depth
    /// so a child always reads a parent that has already been posed this frame.
    std::uint32_t depth = 0U;
};

/// A ragdoll: the simulated body a skeleton becomes when animation stops driving it.
///
/// This is the passive half of what Unreal calls a Physics Asset plus the ragdoll mode of its skeletal
/// mesh component, and it makes the same three choices that decide whether the result reads as a
/// body or as a bag of sticks:
///
///  * Capsules sized from the rig, not from a table. A thigh capsule that spans the actual bone is
///    the difference between a leg and a leg-shaped object that sinks to the knee.
///  * Angular limits authored per joint, measured from the bind pose. A shoulder is a cone with a
///    twist; a knee and an elbow are hinges. Getting this wrong is the single most visible ragdoll
///    bug, because a joint without limits looks like a broken puppet rather than a character.
///  * Rotation-only write-back. The simulated capsule decides how the bone TURNS; the bone's length
///    and position still come from the skeleton. A skeleton whose bones move would stretch the mesh,
///    and no amount of good joint solving hides that.
struct Ragdoll3DComponentUVE final {
    EntityUVE skeleton = kInvalidEntityUVE;
    /// Empty means "generate the standard humanoid profile from the skeleton's bone names". A
    /// non-empty list is used verbatim, so a rig with unusual naming is a data problem, not a code
    /// change.
    std::vector<RagdollBoneSetupUVE> bones;
    Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    /// Which collider layers the ragdoll body may hit, and what it must ignore.
    Physics::RagdollCollisionFilterUVE filter{};
    Physics::RagdollStepSettingsUVE stepSettings{};
    Physics::RagdollSolverUVE solver{};
    /// Velocity the body inherits at the moment it starts simulating - a corpse keeps running, and
    /// without this a sprinting character's ragdoll drops straight down, which reads as a glitch.
    Math::Vector3UVE initialVelocity{};
    bool enabled = true;

    // ---- Runtime, never persisted ----
    bool initialized = false;
    std::vector<RagdollBoneBindingUVE> bindings;
    /// Fixed-step accumulator: the fraction of a frame that did not fill a whole step stays here.
    float pendingSeconds = 0.0F;
    Physics::RagdollStepReportUVE lastReport{};

    /// Compares authored fields only: a running ragdoll is not a different ragdoll.
    [[nodiscard]] bool operator==(const Ragdoll3DComponentUVE& other) const noexcept {
        if (skeleton != other.skeleton || gravity != other.gravity || enabled != other.enabled ||
            initialVelocity != other.initialVelocity || bones.size() != other.bones.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < bones.size(); ++index) {
            const RagdollBoneSetupUVE& lhs = bones[index];
            const RagdollBoneSetupUVE& rhs = other.bones[index];
            if (lhs.boneName != rhs.boneName || lhs.parentBoneName != rhs.parentBoneName ||
                lhs.mass != rhs.mass || lhs.radius != rhs.radius || lhs.halfHeight != rhs.halfHeight ||
                lhs.linearDamping != rhs.linearDamping || lhs.angularDamping != rhs.angularDamping ||
                lhs.friction != rhs.friction || lhs.swing1Motion != rhs.swing1Motion ||
                lhs.swing2Motion != rhs.swing2Motion || lhs.twistMotion != rhs.twistMotion ||
                lhs.swing1LimitDegrees != rhs.swing1LimitDegrees ||
                lhs.swing2LimitDegrees != rhs.swing2LimitDegrees ||
                lhs.twistLimitDegrees != rhs.twistLimitDegrees || lhs.twistAsymmetric != rhs.twistAsymmetric ||
                lhs.twistLowerLimitDegrees != rhs.twistLowerLimitDegrees ||
                lhs.twistUpperLimitDegrees != rhs.twistUpperLimitDegrees || lhs.jointAxisLocal != rhs.jointAxisLocal) {
                return false;
            }
        }
        return true;
    }
};

[[nodiscard]] bool IsRagdoll3DComponentValidUVE(const Ragdoll3DComponentUVE& value) noexcept;

struct Ragdoll3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Ragdoll3D";
    Ragdoll3DComponentUVE ragdoll{};
};

[[nodiscard]] bool IsRagdoll3DObjectDefinitionValidUVE(const Ragdoll3DObjectDefinitionUVE& value) noexcept;

void ApplyRagdoll3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Ragdoll3DObjectDefinitionUVE& value);

/// The standard humanoid profile: which bone names become bodies, and with what mass, size and
/// limits. Exposed because the generator is a heuristic over names, and a caller that disagrees with
/// one row should be able to inspect the table rather than reverse-engineer it from behaviour.
///
/// The names are matched as lowercase substrings, most specific first, so `lowerarm_l`,
/// `Forearm.R` and `forearmL` all resolve. Masses sum to roughly 75 kg for a rig with three spine
/// bones - the point is not anatomical precision but that the ratios are right, because a solver
/// cares about ratios far more than about absolutes.
struct HumanoidRagdollProfileEntryUVE final {
    std::string_view pattern;
    float mass = 1.0F;
    float radius = 0.05F;
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    float angularDamping = 0.5F;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// A hinge locks both swing axes and limits the twist instead - a knee or an elbow. The signed
    /// range is what keeps a shin from folding through the thigh.
    bool hinge = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;
};

[[nodiscard]] std::span<const HumanoidRagdollProfileEntryUVE> GetHumanoidRagdollProfileUVE() noexcept;

/// Resolves the humanoid profile against `skeleton`'s bone names, producing one setup per matched
/// bone in hierarchy order. Bones with no matching pattern are skipped - a rig with extra twist
/// bones in the forearm keeps them, and they simply follow the parent.
///
/// Returns an empty vector when nothing matched, which is the honest answer for a rig whose bones
/// are named in a language this table does not know; the caller then authors `bones` explicitly.
[[nodiscard]] std::vector<RagdollBoneSetupUVE> BuildHumanoidRagdollBonesUVE(
    const std::vector<SkeletonBoneUVE>& skeletonBones);

/// Builds the solver from the skeleton's current pose. Called automatically on the first update; a
/// caller may call it earlier to inspect the rig before it starts falling.
///
/// Refused (false, component untouched) for an unknown or uninitialised skeleton entity, a skeleton
/// with no bones, or a resolved body list that is empty - all cases where simulating something would
/// mean simulating the wrong thing.
[[nodiscard]] bool InitializeRagdoll3DUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

/// Applies one world-space hit to the ragdoll's body nearest `worldPoint`, waking the ragdoll
/// first.
///
/// This is the impulse half of "the body reacts when it is struck": the world layer stops a
/// ragdoll from sinking into geometry, but a hit that never becomes a contact - a hitscan leg shot,
/// a blast radius, a scripted shove - has to enter as momentum. The impulse is applied at the
/// closest point on that body's capsule rather than at its centre, so a hit on the shoulder turns
/// the torso instead of only sliding it.
///
/// A hit further than `searchRadius` metres from every body is refused rather than snapped to the
/// nearest one: a blast 30 m away should not move a corpse, and silently doing so is how "the body
/// reacted to something that never touched it" bugs are born.
[[nodiscard]] bool ApplyRagdollImpactUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const Math::Vector3UVE& worldPoint,
                                         const Math::Vector3UVE& impulse, float searchRadius = 0.5F);

/// Steps every enabled ragdoll against the world's colliders and writes the result back into each
/// skeleton's runtime pose.
///
/// Time is consumed in whole `Physics::kRagdollFixedStepSecondsUVE` steps, at most
/// `Physics::kMaximumRagdollSubstepsUVE` per call, so the same fall traces the same arc at 30, 60 or
/// 144 fps. The collider snapshot is built once per call and shared by every ragdoll in the scene -
/// one entity scan, not one per corpse.
///
/// A sleeping ragdoll still costs one hash lookup and nothing else; it wakes the moment a contact
/// exists, which is what makes "the body reacts when it hits the mesh" true for a corpse that has
/// already come to rest and is then stepped on.
///
/// Returns the number of ragdolls whose simulation advanced this call.
std::size_t UpdateRagdoll3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds);

} // namespace UVE::Scene
