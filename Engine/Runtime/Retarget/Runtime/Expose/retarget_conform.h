// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "retarget_humanoid.h"
#include "retarget_match.h"
#include "retarget_skeleton.h"

namespace UVE::Retarget {

/// A rigid move in world space: rotate, then translate. How one of a rig's bones got from its own
/// rest pose into the humanoid's.
struct RigidMove final {
    Math::Quaternion rotation{};
    Math::Vector3 translation{};

    [[nodiscard]] Math::Vector3 Apply(const Math::Vector3& point) const {
        return Math::RotateVector(rotation, point) + translation;
    }
};

/// A rig made into the humanoid (ConformSkeleton). Its own proportions stay; everything else
/// is the humanoid's.
struct ConformedRig final {
    /// The humanoid's bones first, with its names, parents and order (every one of them: the ones
    /// the rig lacked are added), then the rig's own bones the humanoid has no place for, kept
    /// under their parents. At rest in the A-pose, each humanoid bone turned to the reference's
    /// frame, so a humanoid clip's rotations mean the same on this rig as on any other. Metres,
    /// scale 1.
    RetargetSkeleton skeleton;
    /// Per bone of `skeleton`: the reference bone it is, or -1 for a kept bone of the rig's own.
    std::vector<std::int32_t> referenceOfBone;
    /// Per bone of `skeleton`: true when the rig did not have it and conforming added it.
    std::vector<bool> added;
    /// Per bone of the input rig: where it went in `skeleton`.
    std::vector<std::int32_t> boneOfRigBone;
    /// Per bone of the input rig: how it moved from its rest into the A-pose (the rig's world, in
    /// metres). Carries a skin along: a vertex bound to the bone moves the same way.
    std::vector<RigidMove> moveOfRigBone;
    /// The rig's hips height over the reference's.
    float heightScale = 1.0F;
    /// The turn that stood the rig up +Y and faced it +Z before anything else (identity for a rig
    /// that already did). Rigs from other tools face any way: Z up, or looking down -X. It is part
    /// of every entry of `moveOfRigBone`, and clips are turned by it too.
    Math::Quaternion orientation{};
    /// The same bones, names and frames as `skeleton`, resting where the rig itself rests (its own
    /// T-pose, or whatever it was made in), only stood up +Y. A mesh bound to this keeps every
    /// vertex where it was (ConformMeshMode::KeepRestPose): a clip conformed to the humanoid then
    /// poses it into the A-pose and beyond, and nothing is baked into its shape.
    RetargetSkeleton restSkeleton;
};

/// Makes `rig` (any rest pose, any unit scale, any way up or facing) the humanoid:
///  - it is first stood up +Y with its left side toward +X (facing +Z), read from its hips, head
///    and thighs;
///  - its bones take the humanoid's names and hierarchy (MatchHumanoid's pairing, `match`);
///  - arms, hands, fingers and legs turn to the A-pose (feet stay flat on the ground, which the
///    body is lifted back onto), elbows bend forward and palms face the body; hips, spine, neck,
///    head, clavicles and feet keep the rig's own shape;
///  - bones the rig lacks are added where the humanoid has them, fitted to the rig's own limbs
///    (a twist along its forearm, a spine bone between its neighbours, IK targets on what they
///    follow, the root on the ground under the hips);
///  - every humanoid bone's rest rotation becomes the reference's.
/// Nothing when the rig is malformed or has no hips.
/// What conforming adds beyond the humanoid's core.
struct ConformOptions final {
    /// Also generate the optional bones (the corrective shape helpers) the rig lacks. Off: a rig
    /// gains only the core bones it is missing (a complete body gains just its root, IK targets
    /// and sockets).
    bool includeOptional = false;
    /// Body bones (spine, limbs, fingers, twists) the rig lacks are added only when it lacks many
    /// of them (more than `lackingFraction` of the humanoid's): a rig that is all but complete is
    /// renamed, not rebuilt, and a bone it lacks is left out (a bone it has under one keeps it).
    /// Root, IK targets and sockets are still added when missing.
    bool addBodyBonesOnlyWhenLacking = false;
    float lackingFraction = 0.25F;
};

/// How many body bones (spine, limbs, fingers, twists; not the optional correctives) `match` lacks,
/// and how many the humanoid has.
struct BodyBoneCount final {
    std::size_t missing = 0U;
    std::size_t total = 0U;
    /// Root, IK targets and sockets the rig lacks.
    std::size_t missingHelpers = 0U;
};
[[nodiscard]] BodyBoneCount CountBodyBones(const HumanoidMatch& match, const HumanoidReference& reference);
/// Whether the rig lacks enough body bones that conforming adds them (see ConformOptions).
[[nodiscard]] bool IsRigVeryLacking(const HumanoidMatch& match, const HumanoidReference& reference, float lackingFraction = 0.25F);

[[nodiscard]] std::optional<ConformedRig> ConformSkeleton(const RetargetSkeleton& rig, const HumanoidMatch& match,
                                                                const HumanoidReference& reference,
                                                                std::string* error = nullptr, const ConformOptions& options = {});

/// A skinned mesh's skeleton as a rig: each joint's rest is the inverse of its inverse bind (the
/// mesh's model space, metres), named as the joint is. Nothing for a static mesh, unnamed joints
/// or a bind that is not a rotation, translation and uniform scale.
[[nodiscard]] std::optional<RetargetSkeleton> RigFromMesh(const Asset::MeshAsset& mesh, std::string* error = nullptr);

/// How the bones' moves are blended at a vertex several of them hold. Linear is what the runtime
/// skins with, but where two bones turn differently (a shoulder, lowering an arm) it pinches the
/// skin; dual quaternion blending keeps the volume, so it is what conforming a mesh uses.
enum class SkinBlend : std::uint8_t { Linear = 0, DualQuaternion };

/// What conforming a mesh does to its shape.
enum class ConformMeshMode : std::uint8_t {
    /// Moves the vertices into the A-pose and binds them there (older content).
    BakeAPose = 0,
    /// Leaves every vertex where it is (only stood up +Y when the rig lay another way) and binds the
    /// humanoid's bones at the rig's own rest (`restSkeleton`). Nothing can be damaged: the A-pose
    /// is a pose a clip puts it in, not a new shape.
    KeepRestPose,
};

/// Re-skins `mesh` for `conformed` (made from RigFromMesh of the same mesh): every vertex,
/// normal and tangent moves with its joints into the A-pose, the joints become the conformed
/// skeleton's (renamed, reordered, added) with inverse binds for its rest, influences point at the
/// new joints, and the bounds are remeasured. The weights, UVs and triangles are untouched: the
/// mesh is moved, never rebuilt. False, leaving `mesh` alone, when the two do not belong together.
[[nodiscard]] bool ConformMesh(Asset::MeshAsset& mesh, const ConformedRig& conformed, std::string* error = nullptr,
                                  SkinBlend blend = SkinBlend::DualQuaternion,
                                  ConformMeshMode mode = ConformMeshMode::BakeAPose);

/// How much a mesh's shape changed: over every triangle edge, how much it grew or shrank.
struct MeshDistortion final {
    /// The worst edge, as a fraction (0.25 = a quarter longer or shorter).
    float maximumEdgeChange = 0.0F;
    /// The share of edges that changed by more than 10%.
    float fractionOverTenPercent = 0.0F;
};

/// Compares two meshes with the same triangles (a mesh before and after ConformMesh). Zero for
/// meshes that do not match.
[[nodiscard]] MeshDistortion MeasureMeshDistortion(const Asset::MeshAsset& before, const Asset::MeshAsset& after);

/// Re-expresses `clip` - made for `rig` at rest as given - for the conformed rig: at every frame,
/// each bone the rig had sits in the world exactly where the clip put it, now spoken of in the
/// humanoid's names and frames; bones conforming added follow their parents (IK targets follow
/// what they follow). Every bone of the conformed skeleton gets a track, sampled on the clip's own
/// frames; a bone that never moves keeps one sample. Scale is folded away (metres, scale 1). The
/// clip's object track and events are kept.
[[nodiscard]] std::optional<Asset::AnimationClipAsset> ConformClip(const Asset::AnimationClipAsset& clip,
                                                                        const RetargetSkeleton& rig,
                                                                        const ConformedRig& conformed,
                                                                        const HumanoidReference& reference,
                                                                        std::string* error = nullptr);

} // namespace UVE::Retarget
