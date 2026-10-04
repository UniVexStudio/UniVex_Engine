// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {

/// TwoBoneIK3D: a limb solved back from where it should end instead of forward from its joints.
///
/// Animation that is authored by hand or captured from a performer cannot know what a foot will land
/// on or what a hand will pick up, so a rig that is only ever posed forward ends up stepping through
/// floors and gripping thin air. This modifier closes that gap by running after the pose: the chain
/// it names is rotated so that its end lands on a target, which is what foot planting and hand
/// placement actually are.
///
/// The chain is three successive bones - root, middle, end - and the end bone's own ORIGIN is the
/// point that reaches the target. Naming the end bone rather than assuming the middle bone's tip is
/// what keeps the contract checkable: a bone's length is not stored anywhere, it is the distance to
/// the child that starts where it ends, so the skeleton itself is the only place that distance can
/// come from. Two bones therefore have to be rotated and a third has to exist to say where the
/// second one ends.
///
/// The solve is analytic - two circles in the plane the pole defines - so it costs no iterations and
/// returns the same answer every time it is asked: an IK that searched would leave a limb shivering
/// between two nearly-equal answers. Reach is respected rather than exceeded: a target further away
/// than the two bones put together leaves the limb straight, aimed at the target and short of it,
/// and `reached` reports which of the two happened.
struct TwoBoneIK3DComponentUVE final {
    /// The skeleton this chain belongs to, as a real entity reference, serialized as the file-local
    /// id under the `skeletonLocalId` key and remapped back on load - the same reference every other
    /// skeleton-shaped object carries.
    EntityUVE skeleton = kInvalidEntityUVE;

    /// Which bone the chain starts at - the shoulder, the hip. By index, which takes precedence when
    /// it names a bone that exists; the sentinel means "ask the name instead".
    std::uint32_t rootBoneIndex = kInvalidSkeletonBoneIndexUVE;
    /// The root bone's name, for a reference that survives a re-export with its bones reordered.
    std::string rootBoneName;

    /// The bone the chain bends at - the elbow, the knee. Must be a child of the root bone.
    std::uint32_t middleBoneIndex = kInvalidSkeletonBoneIndexUVE;
    std::string middleBoneName;

    /// The bone whose origin is the effector that reaches the target - the wrist, the ankle. Must be
    /// a child of the middle bone. Its own rotation is left alone: it is the point the chain aims,
    /// not a third joint the solver drives.
    std::uint32_t endBoneIndex = kInvalidSkeletonBoneIndexUVE;
    std::string endBoneName;

    /// Where the effector should go: another object's world position, which is how a hand is placed
    /// on a moving prop. When it names nothing, `targetPosition` is used instead, so a target that is
    /// a fixed point in the rig needs no helper object in the scene.
    EntityUVE target = kInvalidEntityUVE;
    /// A point in the SKELETON's own space, scaled and rotated with it. Ignored while `target` names
    /// a live object.
    Math::Vector3UVE targetPosition{};

    /// Which way the joint should bend: another object's world position. An elbow has a circle of
    /// positions that all put the wrist on the target, and this is what picks one of them - place it
    /// behind the elbow and the arm bends backwards, place it at the hip and the arm bends down.
    EntityUVE poleTarget = kInvalidEntityUVE;
    /// The bend direction instead of a pole object, in the SKELETON's own space. Used when
    /// `poleTarget` names nothing. Zero means no authored pole at all: the chain then keeps the plane
    /// the pose already put it in, which is the answer that does not pop when nothing asked for a
    /// particular one.
    Math::Vector3UVE poleDirection{};

    /// Off leaves the skeleton alone. An IK chain that is switched off in the editor is inert, the
    /// same as one whose bones do not resolve.
    bool enabled = true;

    // ---- Runtime state, written by the solver; never saved --------------------------------------
    /// The bones that were actually solved this pass: `*BoneIndex` when it named a real bone, else the
    /// index the matching name resolved to, else the sentinel. This is what makes a name-based chain
    /// observable - a bone renamed in the source file stops the chain binding instead of silently
    /// driving a different limb.
    std::uint32_t resolvedRootBoneIndex = kInvalidSkeletonBoneIndexUVE;
    std::uint32_t resolvedMiddleBoneIndex = kInvalidSkeletonBoneIndexUVE;
    std::uint32_t resolvedEndBoneIndex = kInvalidSkeletonBoneIndexUVE;

    /// True while the chain was solved and its two rotations written into the skeleton's pose. False
    /// is the authored state: the pose is left exactly as the animation wrote it.
    bool solved = false;

    /// True when the effector landed on the target. False means the target was out of reach and the
    /// limb was left straight, aimed at it and as close as its own length allows - which is the pose
    /// an animator would key, and a thing gameplay can see.
    bool reached = false;

    /// How far the effector ended from the target, in metres: zero (to float noise) when `reached`.
    float endToTargetDistanceMetres = 0.0F;

    /// Authored data only: a chain that has not been solved equals its saved self.
    [[nodiscard]] bool operator==(const TwoBoneIK3DComponentUVE& other) const {
        return skeleton == other.skeleton && rootBoneIndex == other.rootBoneIndex &&
               rootBoneName == other.rootBoneName && middleBoneIndex == other.middleBoneIndex &&
               middleBoneName == other.middleBoneName && endBoneIndex == other.endBoneIndex &&
               endBoneName == other.endBoneName && target == other.target &&
               targetPosition == other.targetPosition && poleTarget == other.poleTarget &&
               poleDirection == other.poleDirection && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsTwoBoneIK3DObjectComponentValidUVE(const TwoBoneIK3DComponentUVE& value) noexcept;

/// Whether a chain has enough to be worth solving: enabled, pointed at a skeleton, naming all three
/// bones by index or by name, and carrying a finite target point and pole direction. A default chain
/// is valid scene data and stays inert until this is true.
[[nodiscard]] inline bool IsTwoBoneIK3DObjectComponentResolvableUVE(
    const TwoBoneIK3DComponentUVE& value) noexcept {
    const bool hasSkeleton = value.skeleton != kInvalidEntityUVE;
    const bool hasRoot = value.rootBoneIndex != kInvalidSkeletonBoneIndexUVE || !value.rootBoneName.empty();
    const bool hasMiddle =
        value.middleBoneIndex != kInvalidSkeletonBoneIndexUVE || !value.middleBoneName.empty();
    const bool hasEnd = value.endBoneIndex != kInvalidSkeletonBoneIndexUVE || !value.endBoneName.empty();
    return value.enabled && hasSkeleton && hasRoot && hasMiddle && hasEnd;
}

/// The three joints of one chain as the pose currently puts them, plus the rotations the solution has
/// to be expressed against.
///
/// Positions are world space and rotations are world space, because that is the frame the bones meet
/// in: a local rotation means nothing until its parent's world rotation is known, and the parent's is
/// exactly what the caller can read off the same pose the joints came from.
struct TwoBoneIKChainUVE final {
    /// The joint the chain starts at (the shoulder's own origin).
    Math::Vector3UVE root{};
    /// The joint the chain bends at (the elbow's own origin).
    Math::Vector3UVE middle{};
    /// The joint the chain ends at (the wrist's own origin).
    Math::Vector3UVE end{};
    /// The world rotation of the root bone's PARENT - the skeleton itself when the root is a top-level
    /// bone. The root's solved rotation is written local to this, the same way its current local
    /// rotation is.
    Math::QuaternionUVE parentRotation{};
    /// The root bone's world rotation right now.
    Math::QuaternionUVE rootRotation{};
    /// The middle bone's world rotation right now.
    Math::QuaternionUVE middleRotation{};
};

/// What one solve did: the two local rotations to write, where the effector landed, and whether it
/// got there.
struct TwoBoneIKSolutionUVE final {
    /// The root bone's rotation in its parent's space.
    Math::QuaternionUVE rootLocalRotation{};
    /// The middle bone's rotation in the root bone's space.
    Math::QuaternionUVE middleLocalRotation{};
    /// Where the effector landed: the target when it was reachable, else the closest point on the
    /// line to it that the chain's own length allows.
    Math::Vector3UVE endPosition{};
    /// Distance from `endPosition` to the target.
    float endToTargetDistanceMetres = 0.0F;
    bool reached = false;
};

/// Solves one two-bone chain so its end lands on `target`.
///
/// The two bones keep their lengths - the pose that is being corrected is a rig, and a rig whose
/// limbs change length is a broken rig, not an IK result - so the joint positions are the
/// intersection of two circles: one around the root through the middle, one around the target
/// through the middle. The circles live in a plane, and that plane is the whole of what `poleDirection`
/// decides; the direction only has to be non-parallel to the root-to-target line, and its component
/// along that line is ignored, so an author cannot accidentally aim it "too directly" at the target.
///
/// Returns nullopt, leaving the caller's pose alone, when the chain cannot be solved at all: joints
/// that do not form a chain of non-zero length, a target on top of the root, or non-finite input.
/// None of those is an ordinary pose a rig reaches; each one means the data or the caller is wrong.
/// A target too far away is NOT one of them - that is a pose that exists, and the straight limb aimed
/// at the target is the honest answer to it.
[[nodiscard]] std::optional<TwoBoneIKSolutionUVE> SolveTwoBoneIKUVE(const TwoBoneIKChainUVE& chain,
                                                                    const Math::Vector3UVE& target,
                                                                    const Math::Vector3UVE& poleDirection) noexcept;

/// Blends a solved rotation over the one the pose has: `influence` 0 keeps the animation's own
/// rotation, 1 takes the solve entirely, and anything between eases the limb onto the target so a
/// character can reach for something instead of snapping to it. Writes `out` and returns true when
/// the result is usable; returns false, leaving `out` untouched, when either rotation is not finite.
/// Shortest-arc blending is deliberate: the long way round would swing a limb through the body.
[[nodiscard]] bool TryBlendTwoBoneIKRotationUVE(const Math::QuaternionUVE& posed,
                                                const Math::QuaternionUVE& solved, float influence,
                                                Math::QuaternionUVE& out) noexcept;

struct TwoBoneIK3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "TwoBoneIK3D";
};

/// Applies the Object3D recipe and the BoneModifier component every modifier carries, then this
/// chain's own component.
void ApplyTwoBoneIK3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const TwoBoneIK3DObjectDefinitionUVE& value);

} // namespace UVE::Scene
