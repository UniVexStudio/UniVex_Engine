// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_skeleton.h"

#include <cmath>
#include <unordered_set>

namespace UVE::Retarget {
namespace {

[[nodiscard]] Math::Quaternion Normalized(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryNormalize(value, out) ? out : Math::Quaternion{};
}

[[nodiscard]] Math::Quaternion Inverse(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryInverse(value, out) ? out : Math::Quaternion{};
}

} // namespace

bool IsRetargetSkeletonValid(const RetargetSkeleton& skeleton) {
    std::unordered_set<std::string> names;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const RetargetBone& bone = skeleton.bones[index];
        if (bone.name.empty() || !names.insert(bone.name).second) {
            return false;
        }
        if (bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(index)) {
            return false;
        }
        if (!Math::IsFinite(bone.position) || !Math::IsFinite(bone.rotation) || !std::isfinite(bone.scale) ||
            bone.scale <= 0.0F || Math::LengthSquared(bone.rotation) <= 0.0F) {
            return false;
        }
    }
    return true;
}

std::vector<WorldTransform> ComputeWorldTransforms(const RetargetSkeleton& skeleton) {
    std::vector<WorldTransform> world(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const RetargetBone& bone = skeleton.bones[index];
        const Math::Quaternion local = Normalized(bone.rotation);
        if (bone.parent < 0) {
            world[index] = WorldTransform{bone.position, local, bone.scale};
            continue;
        }
        const WorldTransform& parent = world[static_cast<std::size_t>(bone.parent)];
        world[index].position = parent.position + Math::RotateVector(parent.rotation, bone.position * parent.scale);
        world[index].rotation = Normalized(Math::Multiply(parent.rotation, local));
        world[index].scale = parent.scale * bone.scale;
    }
    return world;
}

RetargetSkeleton SkeletonFromWorld(const RetargetSkeleton& layout, const std::vector<WorldTransform>& world) {
    RetargetSkeleton result = layout;
    for (std::size_t index = 0U; index < result.bones.size() && index < world.size(); ++index) {
        RetargetBone& bone = result.bones[index];
        bone.scale = 1.0F;
        if (bone.parent < 0) {
            bone.position = world[index].position;
            bone.rotation = Normalized(world[index].rotation);
            continue;
        }
        const WorldTransform& parent = world[static_cast<std::size_t>(bone.parent)];
        const Math::Quaternion inverse = Inverse(parent.rotation);
        bone.position = Math::RotateVector(inverse, world[index].position - parent.position);
        bone.rotation = Normalized(Math::Multiply(inverse, world[index].rotation));
    }
    return result;
}

RetargetSkeleton FoldScale(const RetargetSkeleton& skeleton) {
    std::vector<WorldTransform> world = ComputeWorldTransforms(skeleton);
    for (WorldTransform& transform : world) {
        transform.scale = 1.0F;
    }
    return SkeletonFromWorld(skeleton, world);
}

std::int32_t FindBone(const RetargetSkeleton& skeleton, const std::string& name) {
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        if (skeleton.bones[index].name == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

Math::Quaternion RotationBetween(const Math::Vector3& from, const Math::Vector3& to) {
    const float fromLength = Math::Length(from);
    const float toLength = Math::Length(to);
    if (!(fromLength > 1e-8F) || !(toLength > 1e-8F)) {
        return Math::Quaternion{};
    }
    const Math::Vector3 a = from * (1.0F / fromLength);
    const Math::Vector3 b = to * (1.0F / toLength);
    const float dot = Math::Dot(a, b);
    if (dot < -0.999999F) {
        // Opposite: half a turn about any axis at right angles to `a`.
        Math::Vector3 axis = Math::Cross(Math::Vector3{1.0F, 0.0F, 0.0F}, a);
        if (Math::LengthSquared(axis) < 1e-6F) {
            axis = Math::Cross(Math::Vector3{0.0F, 1.0F, 0.0F}, a);
        }
        axis = Math::Normalize(axis);
        return Math::Quaternion{axis.x, axis.y, axis.z, 0.0F};
    }
    const Math::Vector3 cross = Math::Cross(a, b);
    return Normalized(Math::Quaternion{cross.x, cross.y, cross.z, 1.0F + dot});
}

Math::Quaternion TwistBetween(const Math::Vector3& axis, const Math::Vector3& from, const Math::Vector3& to) {
    const float axisLength = Math::Length(axis);
    if (!(axisLength > 1e-8F)) {
        return Math::Quaternion{};
    }
    const Math::Vector3 unit = axis * (1.0F / axisLength);
    const Math::Vector3 a = from - unit * Math::Dot(from, unit);
    const Math::Vector3 b = to - unit * Math::Dot(to, unit);
    if (!(Math::LengthSquared(a) > 1e-12F) || !(Math::LengthSquared(b) > 1e-12F)) {
        return Math::Quaternion{};
    }
    // Signed about the axis, so a half turn still turns about the axis and nowhere else.
    const float radians = std::atan2(Math::Dot(unit, Math::Cross(a, b)), Math::Dot(a, b));
    Math::Quaternion turn{};
    return Math::TryMakeAxisAngle(unit, radians, turn) ? turn : Math::Quaternion{};
}

void RotateSubtree(const RetargetSkeleton& skeleton, std::vector<WorldTransform>& world, const std::size_t bone,
                      const Math::Quaternion& turn) {
    if (bone >= world.size()) {
        return;
    }
    const Math::Vector3 pivot = world[bone].position;
    std::vector<bool> below(skeleton.bones.size(), false);
    below[bone] = true;
    for (std::size_t index = bone; index < skeleton.bones.size() && index < world.size(); ++index) {
        const std::int32_t parent = skeleton.bones[index].parent;
        if (index != bone && (parent < 0 || !below[static_cast<std::size_t>(parent)])) {
            continue;
        }
        below[index] = true;
        world[index].position = pivot + Math::RotateVector(turn, world[index].position - pivot);
        world[index].rotation = Normalized(Math::Multiply(turn, world[index].rotation));
    }
}

} // namespace UVE::Retarget
