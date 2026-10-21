// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

inline constexpr std::size_t kMaximumFullBodyIKEffectorsUVE = 16U;
inline constexpr std::size_t kMaximumFullBodyIKJointsUVE = 512U;
inline constexpr std::uint32_t kMinimumFullBodyIKIterationsUVE = 1U;
inline constexpr std::uint32_t kMaximumFullBodyIKIterationsUVE = 64U;
inline constexpr float kMinimumFullBodyIKToleranceMetresUVE = 1.0e-6F;
inline constexpr float kMaximumFullBodyIKToleranceMetresUVE = 1.0F;
inline constexpr float kMinimumFullBodyIKStepRadiansUVE = 0.017453292519943295F; // one degree
inline constexpr float kMaximumFullBodyIKStepRadiansUVE = 3.14159265358979323846F;

/// One limb's pull on the solve: a bone to move, where it should end up, and how hard it pulls.
struct FullBodyIKEffectorUVE final {
    /// The bone whose ORIGIN is the point that reaches the target - the wrist, the ankle. Index
    /// first, then the name; the sentinel means "ask the name instead", matching every other bone
    /// reference in the engine.
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
    std::string boneName;

    /// Where the effector should go: another object's world position, which is how a hand is placed
    /// on a moving prop. When it names nothing, `targetPosition` is used instead.
    EntityUVE target = kInvalidEntityUVE;
    /// A point in the SKELETON's own space, scaled and rotated with it. Ignored while `target` names
    /// a live object.
    Math::Vector3UVE targetPosition{};

    /// How much this effector pulls the solve, in [0, 1]. The pull stops at a fixed point part-way
    /// from where the pose put the effector toward its target: a foot at 1 plants on the stair, the
    /// same foot at 0.2 leans a fifth of the way without leaving the animation's own pose behind.
    /// `converged` measures against that stopping point; `reached` still answers the authoring
    /// question - did the limb land on the TARGET?
    float positionWeight = 1.0F;

    // ---- Runtime state, written by the solver; never saved --------------------------------------
    /// The bone this effector actually bound to this pass: `boneIndex` when it named a real bone,
    /// else the index the matching name resolved to, else the sentinel. A renamed bone stops the
    /// effector binding instead of silently driving a different limb.
    std::uint32_t resolvedBoneIndex = kInvalidSkeletonBoneIndexUVE;
    /// True when the effector landed inside `toleranceMetres`. False means the target was out of
    /// reach even with the root moving - the pose is the honest stretch toward it.
    bool reached = false;
    /// How far the effector ended from its target, in metres.
    float distanceMetres = 0.0F;

    /// Authored data only.
    [[nodiscard]] bool operator==(const FullBodyIKEffectorUVE& other) const {
        return boneIndex == other.boneIndex && boneName == other.boneName &&
               target == other.target && targetPosition == other.targetPosition &&
               positionWeight == other.positionWeight;
    }
};

/// FullBodyIK3D: the whole skeleton solved at once, so several limbs can share one answer.
///
/// TwoBoneIK3D solves one limb and that is the end of it: put two of them on a character and the
/// first chain's spine rotation is the second chain's starting pose, so which foot lands where
/// depends on which object the pass happened to visit first. Foot planting on a stair, two hands on
/// a steering wheel and a head that keeps looking forward are one problem - they all draw on the
/// same hips and spine - and this object treats them as one: every effector is a pull on the same
/// pose, and the solve iterates until the pulls agree or the budget runs out.
///
/// The solver is CCD - each joint is rotated to aim its chain at its target, one sweep at a time,
/// from the tip inward - because a pose is made of rotations and a solve that produced positions
/// would then have to guess the rotations back out of them for every branching joint. Rotating the
/// rig keeps bone lengths exact by construction. It is iterative where TwoBoneIK3D is analytic
/// because N effectors sharing a spine have no closed form; what replaces "no iterations" is a hard
/// budget (`maxIterations`), a per-sweep rotation limit (`maxJointStepRadians`) and a distance
/// tolerance, all of which bound what a frame can change by and make the same inputs produce the
/// same pose every time.
struct FullBodyIK3DComponentUVE final {
    /// The skeleton this solve poses, as a real entity reference, serialized as the file-local id
    /// under the `skeletonLocalId` key and remapped back on load - the same reference every other
    /// skeleton-shaped object carries.
    EntityUVE skeleton = kInvalidEntityUVE;

    /// The joint the solve pivots around and may translate - the hips. Every effector chain runs
    /// from this bone out to its effector, so an effector outside this bone's subtree is refused
    /// rather than solved against some other root. Index first (it is the reference that cannot go
    /// stale), then the name; the sentinel means "ask the name instead".
    std::uint32_t rootBoneIndex = kInvalidSkeletonBoneIndexUVE;
    std::string rootBoneName;

    /// Where each limb should end up. Order matters: sweeps run in array order, so a later effector
    /// is solved against what earlier ones already did - the author's own priority, stored in the
    /// file, is what decides.
    std::vector<FullBodyIKEffectorUVE> effectors;

    /// How many full sweeps over every effector the solve may run before it stops either way.
    /// More iterations trade cost for tighter placement; the cap is what makes the frame bounded.
    /// Two effectors sharing a spine converge against each other geometrically but not instantly -
    /// each sweep lands its own limb and disturbs the other's - and the default is budgeted to
    /// settle that fight on ordinary rigs rather than to look cheap.
    std::uint32_t maxIterations = 64U;

    /// How close, in metres, an effector has to be before it counts as placed and stops being
    /// pulled. Below the solver's own floor the quantization of floats would chase noise.
    float toleranceMetres = 1.0e-3F;

    /// How much of the average pull each iteration may spend moving the ROOT (the hips) instead of
    /// bending joints: 0 pins the character in place, 1 lets it step toward its targets every
    /// iteration. This is what makes a reach that the limbs alone cannot cover solvable at all -
    /// a foot planted behind the character either bends the knee impossibly or the hips shift back.
    float rootTranslationWeight = 0.0F;

    /// The most any one joint may rotate in a single sweep, in radians. A solve without this can
    /// snap a limb through a full fold in one frame; with it, a hard pull arrives over a few frames
    /// instead, which is what an animator would key.
    float maxJointStepRadians = 0.7853981633974483F;

    /// Off leaves the skeleton alone. A solve that is switched off in the editor is inert, the same
    /// as one whose bones do not resolve.
    bool enabled = true;

    // ---- Runtime state, written by the solver; never saved --------------------------------------
    /// True when at least one effector's chain was solved and its rotations written into the pose.
    bool solved = false;
    /// True when every pulling effector landed inside `toleranceMetres` of its weighted stopping
    /// point within the iteration budget. False means the budget ran out first - the pose is the
    /// best the solve had, not the authored one.
    bool converged = false;
    /// How many sweeps the solve actually ran. The cost an author can see without a profiler.
    std::uint32_t iterationsUsed = 0U;

    /// Authored data only: a solve that has not run equals its saved self.
    [[nodiscard]] bool operator==(const FullBodyIK3DComponentUVE& other) const {
        return skeleton == other.skeleton && rootBoneIndex == other.rootBoneIndex &&
               rootBoneName == other.rootBoneName && effectors == other.effectors &&
               maxIterations == other.maxIterations && toleranceMetres == other.toleranceMetres &&
               rootTranslationWeight == other.rootTranslationWeight &&
               maxJointStepRadians == other.maxJointStepRadians && enabled == other.enabled;
    }
};

/// Names and vectors must be finite and bounded; weights must be finite; at least one effector.
[[nodiscard]] bool IsFullBodyIK3DObjectComponentValidUVE(const FullBodyIK3DComponentUVE& value) noexcept;

/// Whether a solve has enough to be worth running: enabled, pointed at a skeleton, naming the root
/// bone by index or by name, and carrying at least one effector that names a bone. A default object
/// is valid scene data and stays inert until this is true.
[[nodiscard]] inline bool IsFullBodyIK3DObjectComponentResolvableUVE(
    const FullBodyIK3DComponentUVE& value) noexcept {
    if (!value.enabled || value.skeleton == kInvalidEntityUVE) {
        return false;
    }
    const bool hasRoot =
        value.rootBoneIndex != kInvalidSkeletonBoneIndexUVE || !value.rootBoneName.empty();
    if (!hasRoot || value.effectors.empty()) {
        return false;
    }
    for (const FullBodyIKEffectorUVE& effector : value.effectors) {
        if (effector.boneIndex != kInvalidSkeletonBoneIndexUVE || !effector.boneName.empty()) {
            return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------------------------
// The solver: pure, world-space, and the thing the pass and the tests both speak to.
// ---------------------------------------------------------------------------------------------

/// One joint of the posed rig the solve starts from. Positions and rotations are WORLD space - the
/// frame the limbs meet in - and `localOffset` is where the joint sits in its PARENT's frame, which
/// is what the forward kinematics recompute after every rotation. Joints are listed in hierarchy
/// order: a joint's parent always appears before it.
struct FullBodyIKJointUVE final {
    std::int32_t parent = -1;
    Math::Vector3UVE worldPosition{};
    Math::QuaternionUVE worldRotation{};
    Math::Vector3UVE localOffset{};
};

/// The whole posed problem: the rig and every pull on it. `effectorJoints`, `effectorTargets` and
/// `effectorWeights` are parallel arrays - one effector per index, all three sizes equal.
struct FullBodyIKProblemUVE final {
    std::vector<FullBodyIKJointUVE> joints;
    std::vector<std::size_t> effectorJoints;
    std::vector<Math::Vector3UVE> effectorTargets;
    std::vector<float> effectorWeights;
};

struct FullBodyIKSolverSettingsUVE final {
    /// The joint the solve pivots around and may translate - the hips. Every effector must sit in
    /// this joint's subtree; the chains the solver sweeps are the paths from here to each effector.
    std::size_t rootJoint = 0U;
    std::uint32_t maxIterations = 64U;
    float toleranceMetres = 1.0e-3F;
    float rootTranslationWeight = 0.0F;
    float maxJointStepRadians = 0.7853981633974483F;
};

/// What one solve did: the pose it reached, per-effector outcomes, and what the budget cost.
struct FullBodyIKSolverReportUVE final {
    /// Every joint's solved WORLD position - already including `rootTranslation`.
    std::vector<Math::Vector3UVE> solvedPositions;
    /// Every joint's solved WORLD rotation. Local rotations are the caller's to compose: each
    /// joint's world rotation against its parent's solved world rotation.
    std::vector<Math::QuaternionUVE> solvedRotations;
    /// How far the root was moved in world space, as one rigid shift applied before any rotation.
    Math::Vector3UVE rootTranslation{};
    /// Per effector: the distance its bone ended from its target, in metres.
    std::vector<float> distances;
    /// Per effector: whether that distance landed inside the tolerance.
    std::vector<bool> reached;
    std::size_t iterationsUsed = 0U;
    bool converged = false;
};

/// Solves the whole rig so every effector lands on its target.
///
/// Each iteration may shift the root toward the weighted average of the pulls (when
/// `rootTranslationWeight` says so), then runs one CCD sweep per effector in array order: from the
/// effector's parent inward to the root, every joint is rotated to aim its chain at its target,
/// limited to `maxJointStepRadians` per sweep and recomputed forward so the next joint down sees
/// the pose the last one left. Bone lengths never change - only rotations and the one rigid root
/// shift - so the answer is always a pose the rig actually has.
///
/// Returns nullopt, leaving the caller's pose alone, when the problem cannot be solved at all:
/// joints that are not a hierarchy in order, an effector outside the root's subtree, non-finite
/// input, or settings outside their bounds. None of those is an ordinary pose a rig reaches; each
/// one means the data or the caller is wrong. A target too far away is NOT one of them - a
/// straight, stretched-looking chain aimed at it (with `reached` false) is the pose an animator
/// would key, and a thing gameplay can see.
[[nodiscard]] std::optional<FullBodyIKSolverReportUVE> SolveFullBodyIKUVE(
    const FullBodyIKProblemUVE& problem, const FullBodyIKSolverSettingsUVE& settings) noexcept;

struct FullBodyIK3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "FullBodyIK3D";
};

/// Applies the Object3D recipe and the BoneModifier component every modifier carries, then this
/// solve's own component.
void ApplyFullBodyIK3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                          const FullBodyIK3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
