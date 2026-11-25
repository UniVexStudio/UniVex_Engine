// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_pose.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <optional>

namespace UVE::Retarget {
namespace {

/// sin(10 degrees): an elbow bent less than this says too little about which way it bends.
constexpr float kMinimumElbowBendSine = 0.1736482F;

[[nodiscard]] Math::Quaternion Inverse(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryInverse(value, out) ? out : Math::Quaternion{};
}

[[nodiscard]] std::int32_t Find(const std::unordered_map<std::string, std::int32_t>& byKey, const std::string& key) {
    const auto found = byKey.find(key);
    return found == byKey.end() ? -1 : found->second;
}

[[nodiscard]] Math::Quaternion Normalized(const Math::Quaternion& value) noexcept {
    Math::Quaternion out{};
    return Math::TryNormalize(value, out) ? out : Math::Quaternion{};
}

/// cos(25 degrees): a direction this close to an axis is that axis.
constexpr float kAxisSnapCosine = 0.9063078F;

/// The axis (+-X, +-Y, +-Z) `direction` points along, when it is within 25 degrees of one; itself
/// otherwise. Rigs are authored standing along an axis: a spine that leans forward (the head ahead
/// of the hips, as most game rigs have it) does not mean the rig is tilted, and reading it as a
/// tilt turned the whole clip by a few degrees, so a run's root sank as it travelled forward.
[[nodiscard]] Math::Vector3 SnapToAxis(const Math::Vector3& direction) {
    const float length = Math::Length(direction);
    if (!(length > 1e-6F)) {
        return direction;
    }
    const Math::Vector3 unit = direction * (1.0F / length);
    Math::Vector3 best = unit;
    float bestDot = kAxisSnapCosine;
    for (const Math::Vector3 axis : {Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
                                        Math::Vector3{0.0F, 0.0F, 1.0F}}) {
        for (const float sign : {1.0F, -1.0F}) {
            if (const float dot = Math::Dot(unit, axis * sign); dot >= bestDot) {
                bestDot = dot;
                best = axis * sign;
            }
        }
    }
    return best;
}

} // namespace

/// The turn that stands a rig up +Y and turns its left side to +X, read from its own bones: up is
/// hips to head (or the highest spine or neck bone), left is right thigh to left thigh, each taken
/// as the axis it is nearest when within 25 degrees of one (so the turn is a whole number of
/// quarter turns for any rig authored along the axes). Identity when the rig already stands that
/// way, or when it lacks the bones to tell.
Math::Quaternion OrientationOfRig(const std::vector<WorldTransform>& rigWorld,
                                                      const HumanoidMatch& match, const HumanoidReference& reference) {
    const auto positionOf = [&](const char* key) -> std::optional<Math::Vector3> {
        for (std::size_t index = 0U; index < reference.info.size(); ++index) {
            if (reference.info[index].key == key && match.joints[index].bone >= 0) {
                return rigWorld[static_cast<std::size_t>(match.joints[index].bone)].position;
            }
        }
        return std::nullopt;
    };
    const std::optional<Math::Vector3> hips = positionOf("C|hips");
    std::optional<Math::Vector3> top = positionOf("C|head");
    for (const char* key : {"C|neck 1", "C|neck", "C|spine 5", "C|spine 3", "C|spine"}) {
        if (top.has_value()) {
            break;
        }
        top = positionOf(key);
    }
    const std::optional<Math::Vector3> left = positionOf("L|thigh");
    const std::optional<Math::Vector3> right = positionOf("R|thigh");
    if (!hips.has_value() || !top.has_value()) {
        return Math::Quaternion{};
    }
    const Math::Vector3 up = SnapToAxis(*top - *hips);
    if (!(Math::Length(up) > 1e-6F)) {
        return Math::Quaternion{};
    }
    const Math::Quaternion stand = RotationBetween(up, kUp);
    Math::Quaternion yaw{};
    if (left.has_value() && right.has_value()) {
        Math::Vector3 across = Math::RotateVector(stand, *left - *right);
        across.y = 0.0F;
        across = SnapToAxis(across);
        if (across.x * across.x + across.z * across.z > 1e-10F &&
            !Math::TryMakeAxisAngle(kUp, std::atan2(across.z, across.x), yaw)) {
            yaw = Math::Quaternion{};
        }
    }
    const Math::Quaternion both = Normalized(Math::Multiply(yaw, stand));
    // A rig that already stands right keeps its exact numbers rather than an almost-identity.
    return std::abs(both.w) > 0.999999F ? Math::Quaternion{} : both;
}

bool IsBelow(const RetargetSkeleton& skeleton, std::int32_t bone, const std::int32_t ancestor) {
    while (bone >= 0) {
        if (bone == ancestor) {
            return true;
        }
        bone = skeleton.bones[static_cast<std::size_t>(bone)].parent;
    }
    return false;
}

float LowestFoot(const std::vector<BoneKeyParts>& keys, const std::vector<WorldTransform>& world, bool& found) {
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

Math::Vector3 NearestAxis(const Math::Quaternion& rotation, const Math::Vector3& toward) {
    Math::Vector3 best{};
    float bestDot = -2.0F;
    for (const Math::Vector3 axis : {Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F},
                                        Math::Vector3{0.0F, 0.0F, 1.0F}}) {
        for (const float sign : {1.0F, -1.0F}) {
            const Math::Vector3 world = Math::RotateVector(rotation, axis * sign);
            if (const float dot = Math::Dot(world, toward); dot > bestDot) {
                bestDot = dot;
                best = world;
            }
        }
    }
    return best;
}

std::vector<std::optional<Math::Vector3>> ReadElbowAxes(const std::vector<BoneKeyParts>& keys,
                                                               const std::vector<std::int32_t>& aims,
                                                               const std::vector<WorldTransform>& world) {
    std::vector<std::optional<Math::Vector3>> axes(keys.size());
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const std::int32_t forearm = aims[index];
        if (!keys[index].Is("upperarm") || forearm < 0) {
            continue;
        }
        const std::int32_t hand = aims[static_cast<std::size_t>(forearm)];
        if (hand < 0) {
            continue;
        }
        const Math::Vector3 upper = world[static_cast<std::size_t>(forearm)].position - world[index].position;
        const Math::Vector3 lower =
            world[static_cast<std::size_t>(hand)].position - world[static_cast<std::size_t>(forearm)].position;
        const Math::Vector3 axis = Math::Cross(upper, lower);
        if (Math::Length(axis) > kMinimumElbowBendSine * Math::Length(upper) * Math::Length(lower)) {
            axes[index] = Math::RotateVector(Inverse(world[index].rotation), Math::Normalize(axis));
        }
    }
    return axes;
}

void RollArms(const RetargetSkeleton& skeleton, std::vector<WorldTransform>& world,
                 const std::vector<BoneKeyParts>& keys, const std::vector<std::int32_t>& aims,
                 const std::vector<std::optional<Math::Vector3>>& elbowAxes,
                 const std::unordered_map<std::string, std::int32_t>& byKey) {
    for (std::size_t index = 0U; index < keys.size(); ++index) {
        const std::int32_t aim = aims[index];
        if (aim < 0) {
            continue;
        }
        const Math::Vector3 along = world[static_cast<std::size_t>(aim)].position - world[index].position;
        if (keys[index].Is("upperarm") && elbowAxes[index].has_value()) {
            const Math::Vector3 bendsAbout = Math::RotateVector(world[index].rotation, *elbowAxes[index]);
            RotateSubtree(skeleton, world, index, TwistBetween(along, bendsAbout, Math::Cross(along, kForward)));
        } else if (keys[index].Is("forearm")) {
            const std::string side = std::string{keys[index].side} + "|";
            std::int32_t index1 = Find(byKey, side + "index 1");
            std::int32_t pinky1 = Find(byKey, side + "pinky 1");
            if (index1 < 0 || pinky1 < 0) {
                index1 = Find(byKey, side + "index metacarpal");
                pinky1 = Find(byKey, side + "pinky metacarpal");
            }
            const auto self = static_cast<std::int32_t>(index);
            if (index1 >= 0 && pinky1 >= 0 && IsBelow(skeleton, index1, self) && IsBelow(skeleton, pinky1, self)) {
                const Math::Vector3 knuckles =
                    world[static_cast<std::size_t>(index1)].position - world[static_cast<std::size_t>(pinky1)].position;
                RotateSubtree(skeleton, world, index, TwistBetween(along, knuckles, kForward));
            }
        }
    }
}

void RestoreRotation(const RetargetSkeleton& skeleton, std::vector<WorldTransform>& world, const std::size_t bone,
                        const Math::Quaternion& rotation) {
    RotateSubtree(skeleton, world, bone, Math::Multiply(rotation, Inverse(world[bone].rotation)));
}

void Lift(const RetargetSkeleton& skeleton, std::vector<WorldTransform>& world, const std::int32_t hips,
             const float lift) {
    for (std::size_t index = 0U; index < world.size(); ++index) {
        if (IsBelow(skeleton, static_cast<std::int32_t>(index), hips)) {
            world[index].position.y += lift;
        }
    }
}

} // namespace UVE::Retarget
