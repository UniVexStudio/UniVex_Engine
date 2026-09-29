// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_skeleton_uve.h"

#include <cmath>
#include <unordered_set>

namespace UVE::Retarget {
namespace {

[[nodiscard]] Math::QuaternionUVE NormalizedUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE out{};
    return Math::TryNormalizeUVE(value, out) ? out : Math::QuaternionUVE{};
}

[[nodiscard]] Math::QuaternionUVE InverseUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE out{};
    return Math::TryInverseUVE(value, out) ? out : Math::QuaternionUVE{};
}

} // namespace

bool IsRetargetSkeletonValidUVE(const RetargetSkeletonUVE& skeleton) {
    std::unordered_set<std::string> names;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const RetargetBoneUVE& bone = skeleton.bones[index];
        if (bone.name.empty() || !names.insert(bone.name).second) {
            return false;
        }
        if (bone.parent < -1 || bone.parent >= static_cast<std::int32_t>(index)) {
            return false;
        }
        if (!Math::IsFiniteUVE(bone.position) || !Math::IsFiniteUVE(bone.rotation) || !std::isfinite(bone.scale) ||
            bone.scale <= 0.0F || Math::LengthSquaredUVE(bone.rotation) <= 0.0F) {
            return false;
        }
    }
    return true;
}

std::vector<WorldTransformUVE> ComputeWorldTransformsUVE(const RetargetSkeletonUVE& skeleton) {
    std::vector<WorldTransformUVE> world(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const RetargetBoneUVE& bone = skeleton.bones[index];
        const Math::QuaternionUVE local = NormalizedUVE(bone.rotation);
        if (bone.parent < 0) {
            world[index] = WorldTransformUVE{bone.position, local, bone.scale};
            continue;
        }
        const WorldTransformUVE& parent = world[static_cast<std::size_t>(bone.parent)];
        world[index].position = parent.position + Math::RotateVectorUVE(parent.rotation, bone.position * parent.scale);
        world[index].rotation = NormalizedUVE(Math::MultiplyUVE(parent.rotation, local));
        world[index].scale = parent.scale * bone.scale;
    }
    return world;
}

RetargetSkeletonUVE SkeletonFromWorldUVE(const RetargetSkeletonUVE& layout, const std::vector<WorldTransformUVE>& world) {
    RetargetSkeletonUVE result = layout;
    for (std::size_t index = 0U; index < result.bones.size() && index < world.size(); ++index) {
        RetargetBoneUVE& bone = result.bones[index];
        bone.scale = 1.0F;
        if (bone.parent < 0) {
            bone.position = world[index].position;
            bone.rotation = NormalizedUVE(world[index].rotation);
            continue;
        }
        const WorldTransformUVE& parent = world[static_cast<std::size_t>(bone.parent)];
        const Math::QuaternionUVE inverse = InverseUVE(parent.rotation);
        bone.position = Math::RotateVectorUVE(inverse, world[index].position - parent.position);
        bone.rotation = NormalizedUVE(Math::MultiplyUVE(inverse, world[index].rotation));
    }
    return result;
}

RetargetSkeletonUVE FoldScaleUVE(const RetargetSkeletonUVE& skeleton) {
    std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(skeleton);
    for (WorldTransformUVE& transform : world) {
        transform.scale = 1.0F;
    }
    return SkeletonFromWorldUVE(skeleton, world);
}

std::int32_t FindBoneUVE(const RetargetSkeletonUVE& skeleton, const std::string& name) {
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        if (skeleton.bones[index].name == name) {
            return static_cast<std::int32_t>(index);
        }
    }
    return -1;
}

Math::QuaternionUVE RotationBetweenUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to) {
    const float fromLength = Math::LengthUVE(from);
    const float toLength = Math::LengthUVE(to);
    if (!(fromLength > 1e-8F) || !(toLength > 1e-8F)) {
        return Math::QuaternionUVE{};
    }
    const Math::Vector3UVE a = from * (1.0F / fromLength);
    const Math::Vector3UVE b = to * (1.0F / toLength);
    const float dot = Math::DotUVE(a, b);
    if (dot < -0.999999F) {
        // Opposite: half a turn about any axis at right angles to `a`.
        Math::Vector3UVE axis = Math::CrossUVE(Math::Vector3UVE{1.0F, 0.0F, 0.0F}, a);
        if (Math::LengthSquaredUVE(axis) < 1e-6F) {
            axis = Math::CrossUVE(Math::Vector3UVE{0.0F, 1.0F, 0.0F}, a);
        }
        axis = Math::NormalizeUVE(axis);
        return Math::QuaternionUVE{axis.x, axis.y, axis.z, 0.0F};
    }
    const Math::Vector3UVE cross = Math::CrossUVE(a, b);
    return NormalizedUVE(Math::QuaternionUVE{cross.x, cross.y, cross.z, 1.0F + dot});
}

Math::QuaternionUVE TwistBetweenUVE(const Math::Vector3UVE& axis, const Math::Vector3UVE& from, const Math::Vector3UVE& to) {
    const float axisLength = Math::LengthUVE(axis);
    if (!(axisLength > 1e-8F)) {
        return Math::QuaternionUVE{};
    }
    const Math::Vector3UVE unit = axis * (1.0F / axisLength);
    const Math::Vector3UVE a = from - unit * Math::DotUVE(from, unit);
    const Math::Vector3UVE b = to - unit * Math::DotUVE(to, unit);
    if (!(Math::LengthSquaredUVE(a) > 1e-12F) || !(Math::LengthSquaredUVE(b) > 1e-12F)) {
        return Math::QuaternionUVE{};
    }
    // Signed about the axis, so a half turn still turns about the axis and nowhere else.
    const float radians = std::atan2(Math::DotUVE(unit, Math::CrossUVE(a, b)), Math::DotUVE(a, b));
    Math::QuaternionUVE turn{};
    return Math::TryMakeAxisAngleUVE(unit, radians, turn) ? turn : Math::QuaternionUVE{};
}

void RotateSubtreeUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, const std::size_t bone,
                      const Math::QuaternionUVE& turn) {
    if (bone >= world.size()) {
        return;
    }
    const Math::Vector3UVE pivot = world[bone].position;
    std::vector<bool> below(skeleton.bones.size(), false);
    below[bone] = true;
    for (std::size_t index = bone; index < skeleton.bones.size() && index < world.size(); ++index) {
        const std::int32_t parent = skeleton.bones[index].parent;
        if (index != bone && (parent < 0 || !below[static_cast<std::size_t>(parent)])) {
            continue;
        }
        below[index] = true;
        world[index].position = pivot + Math::RotateVectorUVE(turn, world[index].position - pivot);
        world[index].rotation = NormalizedUVE(Math::MultiplyUVE(turn, world[index].rotation));
    }
}

} // namespace UVE::Retarget
