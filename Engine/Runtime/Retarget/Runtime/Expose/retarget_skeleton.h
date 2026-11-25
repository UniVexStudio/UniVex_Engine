// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "quaternion.h"
#include "vector3.h"

namespace UVE::Retarget {

/// One bone at rest, relative to its parent. Scale is uniform: rigs fold a unit change (FBX
/// centimetres) into it, and retargeting works in metres with the scale folded away.
struct RetargetBone final {
    std::string name;
    /// Lower than the bone's own index; -1 for a root.
    std::int32_t parent = -1;
    Math::Vector3 position{};
    Math::Quaternion rotation{};
    float scale = 1.0F;

    [[nodiscard]] bool operator==(const RetargetBone&) const = default;
};

/// A rig's bones, parents before children.
struct RetargetSkeleton final {
    std::vector<RetargetBone> bones;

    [[nodiscard]] bool operator==(const RetargetSkeleton&) const = default;
};

/// Where a bone is in the world: position, rotation and the uniform scale it inherits.
struct WorldTransform final {
    Math::Vector3 position{};
    Math::Quaternion rotation{};
    float scale = 1.0F;
};

/// True when every parent precedes its child, names are unique and non-empty, and every value
/// is finite with a positive scale.
[[nodiscard]] bool IsRetargetSkeletonValid(const RetargetSkeleton& skeleton);

/// Every bone's world transform, in one pass from the roots.
[[nodiscard]] std::vector<WorldTransform> ComputeWorldTransforms(const RetargetSkeleton& skeleton);

/// The same rig with every scale 1 and positions in the units the world uses: each bone's
/// position is its world position, so a centimetre rig whose root scales by 0.01 comes out in
/// metres with the same shape.
[[nodiscard]] RetargetSkeleton FoldScale(const RetargetSkeleton& skeleton);

/// Rebuilds local transforms from world ones (scale 1), keeping names and parents.
[[nodiscard]] RetargetSkeleton SkeletonFromWorld(const RetargetSkeleton& layout,
                                                       const std::vector<WorldTransform>& world);

/// The index of the bone called `name`, or -1.
[[nodiscard]] std::int32_t FindBone(const RetargetSkeleton& skeleton, const std::string& name);

/// The shortest rotation taking direction `from` to `to` (both need not be unit length).
/// Identity for a zero vector.
[[nodiscard]] Math::Quaternion RotationBetween(const Math::Vector3& from, const Math::Vector3& to);

/// The rotation about `axis` that turns `from` to face `to`, both seen across the axis (their
/// parts along it ignored), so a bone can be rolled without changing where it points. Identity
/// when the axis is zero or either direction lies along it.
[[nodiscard]] Math::Quaternion TwistBetween(const Math::Vector3& axis, const Math::Vector3& from,
                                                  const Math::Vector3& to);

/// Rotates bone `bone` about its own world position by `turn`, carrying every bone below it
/// along (positions and rotations), in `world`.
void RotateSubtree(const RetargetSkeleton& skeleton, std::vector<WorldTransform>& world, std::size_t bone,
                      const Math::Quaternion& turn);

} // namespace UVE::Retarget
