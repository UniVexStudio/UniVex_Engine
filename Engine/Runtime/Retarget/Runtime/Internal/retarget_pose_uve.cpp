// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_pose_uve.h"

#include <algorithm>
#include <cstddef>

namespace UVE::Retarget {
namespace {

/// sin(10 degrees): an elbow bent less than this says too little about which way it bends.
constexpr float kMinimumElbowBendSineUVE = 0.1736482F;

[[nodiscard]] Math::QuaternionUVE InverseUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE out{};
    return Math::TryInverseUVE(value, out) ? out : Math::QuaternionUVE{};
}

[[nodiscard]] std::int32_t FindUVE(const std::unordered_map<std::string, std::int32_t>& byKey, const std::string& key) {
    const auto found = byKey.find(key);
    return found == byKey.end() ? -1 : found->second;
}

} // namespace

bool IsBelowUVE(const RetargetSkeletonUVE& skeleton, std::int32_t bone, const std::int32_t ancestor) {
    while (bone >= 0) {
        if (bone == ancestor) {
            return true;
        }
        bone = skeleton.bones[static_cast<std::size_t>(bone)].parent;
    }
    return false;
}

float LowestFootUVE(const std::vector<BoneKeyPartsUVE>& keys, const std::vector<WorldTransformUVE>& world, bool& found) {
    float lowest = 0.0F;
    found = false;
    for (std::size_t index = 0U; index < keys.size() && index < world.size(); ++index) {
        if (keys[index].side != 'C' && (keys[index].Is("foot") || keys[index].Is("toe"))) {
            lowest = found ? std::min(lowest, world[index].position.y) : world[index].position.y;
            found = true;
        }
    }
    return lowest;
}

Math::Vector3UVE NearestAxisUVE(const Math::QuaternionUVE& rotation, const Math::Vector3UVE& toward) {
    Math::Vector3UVE best{};
    float bestDot = -2.0F;
    for (const Math::Vector3UVE axis : {Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F},
                                        Math::Vector3UVE{0.0F, 0.0F, 1.0F}}) {
        for (const float sign : {1.0F, -1.0F}) {
            const Math::Vector3UVE world = Math::RotateVectorUVE(rotation, axis * sign);
            if (const float dot = Math::DotUVE(world, toward); dot > bestDot) {
                bestDot = dot;
                best = world;
            }
        }
    }
    return best;
}

std::vector<std::optional<Math::Vector3UVE>> ReadElbowAxesUVE(const std::vector<BoneKeyPartsUVE>& keys,
                                                               const std::vector<std::int32_t>& aims,
                                                               const std::vector<WorldTransformUVE>& world) {
    std::vector<std::optional<Math::Vector3UVE>> axes(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const std::int32_t forearm = aims[index];
        if (!keys[index].Is("upperarm") || forearm < 0) {
            continue;
        }
        const std::int32_t hand = aims[static_cast<std::size_t>(forearm)];
        if (hand < 0) {
            continue;
        }
        const Math::Vector3UVE upper = world[static_cast<std::size_t>(forearm)].position - world[index].position;
        const Math::Vector3UVE lower =
            world[static_cast<std::size_t>(hand)].position - world[static_cast<std::size_t>(forearm)].position;
        const Math::Vector3UVE axis = Math::CrossUVE(upper, lower);
        if (Math::LengthUVE(axis) > kMinimumElbowBendSineUVE * Math::LengthUVE(upper) * Math::LengthUVE(lower)) {
            axes[index] = Math::RotateVectorUVE(InverseUVE(world[index].rotation), Math::NormalizeUVE(axis));
        }
    }
    return axes;
}

void RollArmsUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world,
                 const std::vector<BoneKeyPartsUVE>& keys, const std::vector<std::int32_t>& aims,
                 const std::vector<std::optional<Math::Vector3UVE>>& elbowAxes,
                 const std::unordered_map<std::string, std::int32_t>& byKey) {
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const std::int32_t aim = aims[index];
        if (aim < 0) {
            continue;
        }
        const Math::Vector3UVE along = world[static_cast<std::size_t>(aim)].position - world[index].position;
        if (keys[index].Is("upperarm") && elbowAxes[index].has_value()) {
            const Math::Vector3UVE bendsAbout = Math::RotateVectorUVE(world[index].rotation, *elbowAxes[index]);
            RotateSubtreeUVE(skeleton, world, index, TwistBetweenUVE(along, bendsAbout, Math::CrossUVE(along, kForwardUVE)));
        } else if (keys[index].Is("forearm")) {
            const std::string side = std::string{keys[index].side} + "|";
            std::int32_t index1 = FindUVE(byKey, side + "index 1");
            std::int32_t pinky1 = FindUVE(byKey, side + "pinky 1");
            if (index1 < 0 || pinky1 < 0) {
                index1 = FindUVE(byKey, side + "index metacarpal");
                pinky1 = FindUVE(byKey, side + "pinky metacarpal");
            }
            const auto self = static_cast<std::int32_t>(index);
            if (index1 >= 0 && pinky1 >= 0 && IsBelowUVE(skeleton, index1, self) && IsBelowUVE(skeleton, pinky1, self)) {
                const Math::Vector3UVE knuckles =
                    world[static_cast<std::size_t>(index1)].position - world[static_cast<std::size_t>(pinky1)].position;
                RotateSubtreeUVE(skeleton, world, index, TwistBetweenUVE(along, knuckles, kForwardUVE));
            }
        }
    }
}

void RestoreRotationUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, const std::size_t bone,
                        const Math::QuaternionUVE& rotation) {
    RotateSubtreeUVE(skeleton, world, bone, Math::MultiplyUVE(rotation, InverseUVE(world[bone].rotation)));
}

void LiftUVE(const RetargetSkeletonUVE& skeleton, std::vector<WorldTransformUVE>& world, const std::int32_t hips,
             const float lift) {
    for (std::size_t index = 0U; index < world.size(); ++index) {
        if (IsBelowUVE(skeleton, static_cast<std::int32_t>(index), hips)) {
            world[index].position.y += lift;
        }
    }
}

} // namespace UVE::Retarget
