// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Retarget {

/// One bone at rest, relative to its parent. Scale is uniform: rigs fold a unit change (FBX
/// centimetres) into it, and retargeting works in metres with the scale folded away.
struct RetargetBoneUVE final {
    std::string name;
    /// Lower than the bone's own index; -1 for a root.
    std::int32_t parent = -1;
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    float scale = 1.0F;

    [[nodiscard]] bool operator==(const RetargetBoneUVE&) const = default;
};

/// A rig's bones, parents before children.
struct RetargetSkeletonUVE final {
    std::vector<RetargetBoneUVE> bones;

    [[nodiscard]] bool operator==(const RetargetSkeletonUVE&) const = default;
};

/// Where a bone is in the world: position, rotation and the uniform scale it inherits.
struct WorldTransformUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    float scale = 1.0F;
};

/// True when every parent precedes its child, names are unique and non-empty, and every value
/// is finite with a positive scale.
[[nodiscard]] bool IsRetargetSkeletonValidUVE(const RetargetSkeletonUVE& skeleton);

/// Every bone's world transform, in one pass from the roots.
[[nodiscard]] std::vector<WorldTransformUVE> ComputeWorldTransformsUVE(const RetargetSkeletonUVE& skeleton);

/// The same rig with every scale 1 and positions in the units the world uses: each bone's
/// position is its world position, so a centimetre rig whose root scales by 0.01 comes out in
/// metres with the same shape.
[[nodiscard]] RetargetSkeletonUVE FoldScaleUVE(const RetargetSkeletonUVE& skeleton);

/// Rebuilds local transforms from world ones (scale 1), keeping names and parents.
[[nodiscard]] RetargetSkeletonUVE SkeletonFromWorldUVE(const RetargetSkeletonUVE& layout,
                                                       const std::vector<WorldTransformUVE>& world);

/// The index of the bone called `name`, or -1.
[[nodiscard]] std::int32_t FindBoneUVE(const RetargetSkeletonUVE& skeleton, const std::string& name);

/// The shortest rotation taking direction `from` to `to` (both need not be unit length).
/// Identity for a zero vector.
[[nodiscard]] Math::QuaternionUVE RotationBetweenUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to);

/// The rotation about `axis` that turns `from` to face `to`, both seen across the axis (their
/// parts along it ignored), so a bone can be rolled without changing where it points. Identity
/// when the axis is zero or either direction lies along it.
[[nodiscard]] Math::QuaternionUVE TwistBetweenUVE(const Math::Vector3UVE& axis, const Math::Vector3UVE& from,
                                                  const Math::Vector3UVE& to);

/// Rotates bone `bone` about its own world position by `turn`, carrying every bone below it
/// along (positions and rotations), in `world`.
void RotateSubtreeUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, std::size_t bone,
                      const Math::QuaternionUVE& turn);

} // namespace UVE::Retarget
