// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "retarget_keys_uve.h"
#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/retarget/retarget_match_uve.h"
#include "uve/retarget/retarget_skeleton_uve.h"

namespace UVE::Retarget {

/// The pieces of the A-pose that building the reference and conforming a rig share. Both work on
/// a skeleton's world transforms in place, bone by bone:
///   keys   what each bone is (an empty key for a bone with no humanoid meaning)
///   aims   the bone each one points at, or -1
///   byKey  the bones present, by key (a bone not placed yet is left out)

inline constexpr Math::Vector3UVE kUpUVE{0.0F, 1.0F, 0.0F};
inline constexpr Math::Vector3UVE kForwardUVE{0.0F, 0.0F, 1.0F};

/// The turn that stands a rig up +Y and turns its left side to +X (so it faces +Z), read from its
/// own bones as `match` found them: up is hips to head (or the highest neck or spine bone), left is
/// right thigh to left thigh. Identity when the rig already stands that way, or lacks the bones to
/// tell.
[[nodiscard]] Math::QuaternionUVE OrientationOfRigUVE(const std::vector<WorldTransformUVE>& rigWorld,
                                                      const HumanoidMatchUVE& match, const HumanoidReferenceUVE& reference);

/// True when `bone` is `ancestor` or lies below it.
[[nodiscard]] bool IsBelowUVE(const RetargetSkeletonUVE& skeleton, std::int32_t bone, std::int32_t ancestor);

/// The height of the lowest foot or toe bone; `found` says whether there was one.
[[nodiscard]] float LowestFootUVE(const std::vector<BoneKeyPartsUVE>& keys, const std::vector<WorldTransformUVE>& world,
                                  bool& found);

/// The world direction of whichever of the rotation's own axes (+-X, +-Y, +-Z) is nearest `toward`.
[[nodiscard]] Math::Vector3UVE NearestAxisUVE(const Math::QuaternionUVE& rotation, const Math::Vector3UVE& toward);

/// Which way each elbow bends in the current pose, in its upper arm's own frame; nothing for an
/// arm too straight to tell (under 10 degrees). Read before straightening the arms.
[[nodiscard]] std::vector<std::optional<Math::Vector3UVE>> ReadElbowAxesUVE(const std::vector<BoneKeyPartsUVE>& keys,
                                                                            const std::vector<std::int32_t>& aims,
                                                                            const std::vector<WorldTransformUVE>& world);

/// Rolls straightened arms the humanoid's way, which pointing alone leaves open: each upper arm
/// so its elbow (as ReadElbowAxesUVE read it) bends toward +Z, and each forearm so its hand's
/// knuckle line (index to little finger, at the first knuckle or else the metacarpals) runs along
/// +Z, palm toward the body.
void RollArmsUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world,
                 const std::vector<BoneKeyPartsUVE>& keys, const std::vector<std::int32_t>& aims,
                 const std::vector<std::optional<Math::Vector3UVE>>& elbowAxes,
                 const std::unordered_map<std::string, std::int32_t>& byKey);

/// Turns `bone`, with everything below it, back to `rotation` (a world rotation it had before).
void RestoreRotationUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, std::size_t bone,
                        const Math::QuaternionUVE& rotation);

/// Moves every bone at or below `hips` up by `lift`.
void LiftUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, std::int32_t hips, float lift);

} // namespace UVE::Retarget
