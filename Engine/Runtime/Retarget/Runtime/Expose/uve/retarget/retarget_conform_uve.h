// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/retarget/retarget_match_uve.h"
#include "uve/retarget/retarget_skeleton_uve.h"

namespace UVE::Retarget {

/// A rigid move in world space: rotate, then translate. How one of a rig's bones got from its own
/// rest pose into the humanoid's.
struct RigidMoveUVE final {
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE translation{};

    [[nodiscard]] Math::Vector3UVE ApplyUVE(const Math::Vector3UVE& point) const {
        return Math::RotateVectorUVE(rotation, point) + translation;
    }
};

/// A rig made into the humanoid (ConformSkeletonUVE). Its own proportions stay; everything else
/// is the humanoid's.
struct ConformedRigUVE final {
    /// The humanoid's bones first, with its names, parents and order (every one of them: the ones
    /// the rig lacked are added), then the rig's own bones the humanoid has no place for, kept
    /// under their parents. At rest in the A-pose, each humanoid bone turned to the reference's
    /// frame, so a humanoid clip's rotations mean the same on this rig as on any other. Metres,
    /// scale 1.
    RetargetSkeletonUVE skeleton;
    /// Per bone of `skeleton`: the reference bone it is, or -1 for a kept bone of the rig's own.
    std::vector<std::int32_t> referenceOfBone;
    /// Per bone of `skeleton`: true when the rig did not have it and conforming added it.
    std::vector<bool> added;
    /// Per bone of the input rig: where it went in `skeleton`.
    std::vector<std::int32_t> boneOfRigBone;
    /// Per bone of the input rig: how it moved from its rest into the A-pose (the rig's world, in
    /// metres). Carries a skin along: a vertex bound to the bone moves the same way.
    std::vector<RigidMoveUVE> moveOfRigBone;
    /// The rig's hips height over the reference's.
    float heightScale = 1.0F;
    /// The turn that stood the rig up +Y and faced it +Z before anything else (identity for a rig
    /// that already did). Rigs from other tools face any way: Z up, or looking down -X. It is part
    /// of every entry of `moveOfRigBone`, and clips are turned by it too.
    Math::QuaternionUVE orientation{};
};

/// Makes `rig` (any rest pose, any unit scale, any way up or facing) the humanoid:
///  - it is first stood up +Y with its left side toward +X (facing +Z), read from its hips, head
///    and thighs;
///  - its bones take the humanoid's names and hierarchy (MatchHumanoidUVE's pairing, `match`);
///  - arms, hands, fingers and legs turn to the A-pose (feet stay flat on the ground, which the
///    body is lifted back onto), elbows bend forward and palms face the body; hips, spine, neck,
///    head, clavicles and feet keep the rig's own shape;
///  - bones the rig lacks are added where the humanoid has them, fitted to the rig's own limbs
///    (a twist along its forearm, a spine bone between its neighbours, IK targets on what they
///    follow, the root on the ground under the hips);
///  - every humanoid bone's rest rotation becomes the reference's.
/// Nothing when the rig is malformed or has no hips.
[[nodiscard]] std::optional<ConformedRigUVE> ConformSkeletonUVE(const RetargetSkeletonUVE& rig, const HumanoidMatchUVE& match,
                                                                const HumanoidReferenceUVE& reference,
                                                                std::string* error = nullptr);

/// A skinned mesh's skeleton as a rig: each joint's rest is the inverse of its inverse bind (the
/// mesh's model space, metres), named as the joint is. Nothing for a static mesh, unnamed joints
/// or a bind that is not a rotation, translation and uniform scale.
[[nodiscard]] std::optional<RetargetSkeletonUVE> RigFromMeshUVE(const Asset::MeshAssetUVE& mesh, std::string* error = nullptr);

/// How the bones' moves are blended at a vertex several of them hold. Linear is what the runtime
/// skins with, but where two bones turn differently (a shoulder, lowering an arm) it pinches the
/// skin; dual quaternion blending keeps the volume, so it is what conforming a mesh uses.
enum class SkinBlendUVE : std::uint8_t { Linear = 0, DualQuaternion };

/// Re-skins `mesh` for `conformed` (made from RigFromMeshUVE of the same mesh): every vertex,
/// normal and tangent moves with its joints into the A-pose, the joints become the conformed
/// skeleton's (renamed, reordered, added) with inverse binds for its rest, influences point at the
/// new joints, and the bounds are remeasured. The weights, UVs and triangles are untouched: the
/// mesh is moved, never rebuilt. False, leaving `mesh` alone, when the two do not belong together.
[[nodiscard]] bool ConformMeshUVE(Asset::MeshAssetUVE& mesh, const ConformedRigUVE& conformed, std::string* error = nullptr,
                                  SkinBlendUVE blend = SkinBlendUVE::DualQuaternion);

/// How much a mesh's shape changed: over every triangle edge, how much it grew or shrank.
struct MeshDistortionUVE final {
    /// The worst edge, as a fraction (0.25 = a quarter longer or shorter).
    float maximumEdgeChange = 0.0F;
    /// The share of edges that changed by more than 10%.
    float fractionOverTenPercent = 0.0F;
};

/// Compares two meshes with the same triangles (a mesh before and after ConformMeshUVE). Zero for
/// meshes that do not match.
[[nodiscard]] MeshDistortionUVE MeasureMeshDistortionUVE(const Asset::MeshAssetUVE& before, const Asset::MeshAssetUVE& after);

/// Re-expresses `clip` - made for `rig` at rest as given - for the conformed rig: at every frame,
/// each bone the rig had sits in the world exactly where the clip put it, now spoken of in the
/// humanoid's names and frames; bones conforming added follow their parents (IK targets follow
/// what they follow). Every bone of the conformed skeleton gets a track, sampled on the clip's own
/// frames; a bone that never moves keeps one sample. Scale is folded away (metres, scale 1). The
/// clip's object track and events are kept.
[[nodiscard]] std::optional<Asset::AnimationClipAssetUVE> ConformClipUVE(const Asset::AnimationClipAssetUVE& clip,
                                                                        const RetargetSkeletonUVE& rig,
                                                                        const ConformedRigUVE& conformed,
                                                                        const HumanoidReferenceUVE& reference,
                                                                        std::string* error = nullptr);

} // namespace UVE::Retarget
