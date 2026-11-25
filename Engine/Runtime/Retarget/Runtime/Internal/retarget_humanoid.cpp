// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_humanoid.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include <nlohmann/json.hpp>

#include "retarget_keys.h"
#include "retarget_pose.h"
#include "retarget_names.h"

namespace UVE::Retarget {
namespace {

#include "humanoid_reference_json.inc"

constexpr const char* kFormat = "uve-humanoid-v1";
/// A-pose: how far below level the arms hang.
constexpr float kArmDropRadians = 0.7853982F;

void SetError(std::string* error, std::string text) {
    if (error != nullptr) {
        *error = std::move(text);
    }
}

[[nodiscard]] HumanoidBoneKind KindOfKey(const BoneKeyParts& key) {
    if (key.Is("root")) {
        return HumanoidBoneKind::Root;
    }
    if (key.Has("ik")) {
        return HumanoidBoneKind::IK;
    }
    if (key.Has("prop") || key.Has("mount") || key.Has("com") || key.Has("interaction")) {
        return HumanoidBoneKind::Socket;
    }
    static const std::array<const char*, 16> kCorrective{"corrective", "front",  "back",  "in",  "out",   "lower",
                                                         "tricep",     "bicep",  "knee",  "pec", "scap",  "latissimus",
                                                         "ankle",      "wrist",  "lat",   "muscle"};
    for (const char* const word : kCorrective) {
        if (key.Has(word)) {
            return HumanoidBoneKind::Corrective;
        }
    }
    if (key.Has("twist")) {
        return HumanoidBoneKind::Twist;
    }
    if (key.IsFinger()) {
        return HumanoidBoneKind::Finger;
    }
    return HumanoidBoneKind::Body;
}

/// The key of the bone a body or finger bone points at, tried in order; empty for none.
[[nodiscard]] std::vector<std::string> AimCandidates(const BoneKeyParts& key) {
    const std::string side = std::string{key.side} + "|";
    if (key.Is("hips")) {
        return {"C|spine 1", "C|spine"};
    }
    if (key.words.size() == 2U && key.words[0] == "spine" && key.Number() > 0) {
        return {"C|spine " + std::to_string(key.Number() + 1), "C|neck 1", "C|neck"};
    }
    if (key.words.size() == 2U && key.words[0] == "neck" && key.Number() > 0) {
        return {"C|neck " + std::to_string(key.Number() + 1), "C|head"};
    }
    if (key.Is("neck")) {
        return {"C|head"};
    }
    if (key.Is("clavicle")) {
        return {side + "upperarm"};
    }
    if (key.Is("upperarm")) {
        return {side + "forearm"};
    }
    if (key.Is("forearm")) {
        return {side + "hand"};
    }
    if (key.Is("hand")) {
        return {side + "middle metacarpal", side + "middle 1"};
    }
    if (key.Is("thigh")) {
        return {side + "shin"};
    }
    if (key.Is("shin")) {
        return {side + "foot"};
    }
    if (key.Is("foot")) {
        return {side + "toe"};
    }
    if (key.words.size() == 2U && key.IsFinger()) {
        const std::string& finger = key.words[0];
        if (key.words[1] == "metacarpal") {
            return {side + finger + " 1"};
        }
        if (key.Number() > 0) {
            return {side + finger + " " + std::to_string(key.Number() + 1)};
        }
    }
    return {};
}

/// The key of the bone an IK bone rests on.
[[nodiscard]] std::string FollowKey(const BoneKeyParts& key) {
    if (!key.Has("ik")) {
        return {};
    }
    if (key.Has("root")) {
        return "C|root";
    }
    if (key.Has("prop")) {
        return "R|hand"; // the held prop's solver target rests on the right hand
    }
    if (key.Has("foot")) {
        return std::string{key.side} + "|foot";
    }
    if (key.Has("hand")) {
        return std::string{key.side} + "|hand";
    }
    return {};
}

} // namespace

const char* HumanoidBoneKindName(const HumanoidBoneKind kind) noexcept {
    switch (kind) {
        case HumanoidBoneKind::Root: return "root";
        case HumanoidBoneKind::Body: return "body";
        case HumanoidBoneKind::Finger: return "finger";
        case HumanoidBoneKind::Twist: return "twist";
        case HumanoidBoneKind::Corrective: return "corrective";
        case HumanoidBoneKind::IK: return "ik";
        case HumanoidBoneKind::Socket: return "socket";
    }
    return "body";
}

float MeasureHipsHeight(const RetargetSkeleton& skeleton, const std::vector<WorldTransform>& world) {
    std::vector<BoneKeyParts> keys;
    keys.reserve(skeleton.bones.size());
    std::int32_t hips = -1;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        keys.push_back(SplitBoneKey(MakeBoneKey(skeleton.bones[index].name)));
        if (hips < 0 && keys.back().Is("hips")) {
            hips = static_cast<std::int32_t>(index);
        }
    }
    bool found = false;
    const float ground = LowestFoot(keys, world, found);
    if (hips < 0 || !found || static_cast<std::size_t>(hips) >= world.size()) {
        return 0.0F;
    }
    return std::max(world[static_cast<std::size_t>(hips)].position.y - ground, 0.0F);
}

std::optional<HumanoidReference> BuildHumanoidReference(const RetargetSkeleton& source, std::string* error) {
    if (!IsRetargetSkeletonValid(source) || source.bones.empty()) {
        SetError(error, "the source skeleton is empty or malformed");
        return std::nullopt;
    }
    const RetargetSkeleton folded = FoldScale(source);
    std::vector<WorldTransform> world = ComputeWorldTransforms(folded);
    const std::size_t count = folded.bones.size();

    // Names: the rig's own words, spelled the humanoid's way.
    HumanoidReference reference;
    reference.skeleton = folded;
    std::vector<BoneKeyParts> keys(count);
    std::unordered_map<std::string, std::int32_t> byKey;
    std::unordered_set<std::string> names;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::string key = MakeBoneKey(folded.bones[index].name);
        keys[index] = SplitBoneKey(key);
        const std::string name = MakeHumanoidBoneName(key);
        if (!names.insert(name).second || !byKey.emplace(key, static_cast<std::int32_t>(index)).second) {
            SetError(error, "two bones read as the same bone: " + name);
            return std::nullopt;
        }
        reference.skeleton.bones[index].name = name;
        HumanoidBoneInfo info;
        info.key = key;
        info.kind = KindOfKey(keys[index]);
        reference.info.push_back(std::move(info));
    }
    const auto find = [&byKey](const std::string& key) {
        const auto found = byKey.find(key);
        return found == byKey.end() ? -1 : found->second;
    };
    const std::int32_t hips = find("C|hips");
    if (hips < 0) {
        SetError(error, "the source rig has no hips");
        return std::nullopt;
    }
    for (std::size_t index = 0U; index < count; ++index) {
        for (const std::string& candidate : AimCandidates(keys[index])) {
            const std::int32_t aim = find(candidate);
            if (aim >= 0 && IsBelow(folded, aim, static_cast<std::int32_t>(index))) {
                reference.info[index].aim = aim;
                break;
            }
        }
        const std::string follow = FollowKey(keys[index]);
        reference.info[index].follows = follow.empty() ? -1 : find(follow);
        reference.info[index].optional = reference.info[index].kind == HumanoidBoneKind::Corrective;
    }

    // The pose below is written for a rig standing up +Y with its left side toward +X (so facing
    // +Z); a rig turned any other way would be bent the wrong way round, so it is refused.
    const std::int32_t leftThigh = find("L|thigh");
    const std::int32_t rightThigh = find("R|thigh");
    const std::int32_t leftShin = find("L|shin");
    if (leftThigh < 0 || rightThigh < 0 || leftShin < 0) {
        SetError(error, "the source rig needs both thighs and a shin");
        return std::nullopt;
    }
    const Math::Vector3 across =
        world[static_cast<std::size_t>(leftThigh)].position - world[static_cast<std::size_t>(rightThigh)].position;
    const Math::Vector3 thigh =
        world[static_cast<std::size_t>(leftShin)].position - world[static_cast<std::size_t>(leftThigh)].position;
    if (!(across.x > 0.7F * Math::Length(across)) || !(-thigh.y > 0.7F * Math::Length(thigh))) {
        SetError(error, "the source rig must stand up +Y with its left side toward +X (facing +Z)");
        return std::nullopt;
    }

    // ---- The A-pose ---------------------------------------------------------------------------
    // Each body bone is turned, with everything below it, so it points the way the pose wants;
    // parents go first, so a child's turn starts from where its parent left it.
    bool hadFeet = false;
    const float groundBefore = LowestFoot(keys, world, hadFeet);
    std::vector<Math::Quaternion> sourceRotation(count);
    for (std::size_t index = 0U; index < count; ++index) {
        sourceRotation[index] = world[index].rotation;
    }
    // Which way each elbow bends, read off the source pose before the arms straighten.
    std::vector<std::int32_t> aims(count);
    for (std::size_t index = 0U; index < count; ++index) {
        aims[index] = reference.info[index].aim;
    }
    const std::vector<std::optional<Math::Vector3>> elbowAxes = ReadElbowAxes(keys, aims, world);
    const float drop = kArmDropRadians;
    for (std::size_t index = 0U; index < count; ++index) {
        const std::int32_t aim = reference.info[index].aim;
        if (aim < 0) {
            continue;
        }
        const BoneKeyParts& key = keys[index];
        const std::int32_t parent = folded.bones[index].parent;
        Math::Vector3 want{};
        bool pose = true;
        if (key.Is("hips") || key.words[0] == "spine" || key.words[0] == "neck") {
            want = Math::Vector3{0.0F, 1.0F, 0.0F};
        } else if (key.Is("upperarm")) {
            want = Math::Vector3{key.side == 'L' ? std::cos(drop) : -std::cos(drop), -std::sin(drop), 0.0F};
        } else if (key.Is("thigh")) {
            want = Math::Vector3{0.0F, -1.0F, 0.0F};
        } else if ((key.Is("forearm") || key.Is("hand") || key.Is("shin") ||
                    (key.IsFinger() && key.Number() > 0 && !(key.words[0] == "thumb" && key.Number() == 1))) &&
                   parent >= 0) {
            // Straight on from the bone above: no bend at the elbow, wrist, knee or knuckle.
            want = world[index].position - world[static_cast<std::size_t>(parent)].position;
        } else {
            pose = false; // clavicles, metacarpals, the thumb's base and the feet keep their set
        }
        if (!pose || Math::LengthSquared(want) <= 0.0F) {
            continue;
        }
        const Math::Vector3 now = world[static_cast<std::size_t>(aim)].position - world[index].position;
        RotateSubtree(folded, world, index, RotationBetween(now, want));
    }
    // Roll, which pointing leaves open: elbows bend forward, palms face the body.
    RollArms(folded, world, keys, aims, elbowAxes, byKey);
    // The head and the feet keep how they sat in the source: the feet as they met the ground, the
    // head looking ahead (and set exactly level); then the body goes down (or up) to stand on the
    // ground again.
    for (std::size_t index = 0U; index < count; ++index) {
        const bool head = keys[index].Is("head");
        if (!head && !(keys[index].Is("foot") && keys[index].side != 'C')) {
            continue;
        }
        RestoreRotation(folded, world, index, sourceRotation[index]);
        if (head) {
            RotateSubtree(folded, world, index, RotationBetween(NearestAxis(world[index].rotation, kUp), kUp));
        }
    }
    bool hasFeet = false;
    const float groundAfter = LowestFoot(keys, world, hasFeet);
    if (hadFeet && hasFeet) {
        Lift(folded, world, hips, groundBefore - groundAfter);
    }
    // IK bones rest on what they follow.
    for (std::size_t index = 0U; index < count; ++index) {
        const std::int32_t follows = reference.info[index].follows;
        if (follows >= 0) {
            world[index].position = world[static_cast<std::size_t>(follows)].position;
            world[index].rotation = world[static_cast<std::size_t>(follows)].rotation;
        }
    }
    reference.skeleton = SkeletonFromWorld(reference.skeleton, world);
    reference.hipsHeight = MeasureHipsHeight(reference.skeleton, world);
    return reference;
}

std::string WriteHumanoidReference(const HumanoidReference& reference) {
    nlohmann::ordered_json bones = nlohmann::ordered_json::array();
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const RetargetBone& bone = reference.skeleton.bones[index];
        const HumanoidBoneInfo& info = reference.info[index];
        const auto nameOf = [&reference](const std::int32_t other) {
            return other < 0 ? std::string{} : reference.skeleton.bones[static_cast<std::size_t>(other)].name;
        };
        nlohmann::ordered_json entry;
        entry["name"] = bone.name;
        entry["parent"] = nameOf(bone.parent);
        entry["kind"] = HumanoidBoneKindName(info.kind);
        if (info.aim >= 0) {
            entry["aim"] = nameOf(info.aim);
        }
        if (info.follows >= 0) {
            entry["follows"] = nameOf(info.follows);
        }
        if (info.optional) {
            entry["optional"] = true;
        }
        entry["position"] = {bone.position.x, bone.position.y, bone.position.z};
        entry["rotation"] = {bone.rotation.x, bone.rotation.y, bone.rotation.z, bone.rotation.w};
        bones.push_back(std::move(entry));
    }
    nlohmann::ordered_json root;
    root["format"] = kFormat;
    root["name"] = "UniVex Humanoid";
    root["units"] = "metres, +Y up, facing +Z, left toward +X, A-pose";
    root["hipsHeight"] = reference.hipsHeight;
    root["bones"] = std::move(bones);
    return root.dump(1, '\t');
}

std::optional<HumanoidReference> ParseHumanoidReference(const std::string_view text, std::string* error) {
    try {
        const nlohmann::json root = nlohmann::json::parse(text);
        if (root.value("format", std::string{}) != kFormat) {
            SetError(error, "not a uve-humanoid-v1 file");
            return std::nullopt;
        }
        HumanoidReference reference;
        reference.hipsHeight = root.value("hipsHeight", 0.0F);
        const nlohmann::json& bones = root.at("bones");
        std::unordered_map<std::string, std::int32_t> byName;
        for (const nlohmann::json& entry : bones) {
            byName.emplace(entry.at("name").get<std::string>(), static_cast<std::int32_t>(byName.size()));
        }
        const auto indexOf = [&byName](const nlohmann::json& entry, const char* field) {
            const std::string name = entry.value(field, std::string{});
            const auto found = byName.find(name);
            return name.empty() || found == byName.end() ? -1 : found->second;
        };
        for (const nlohmann::json& entry : bones) {
            RetargetBone bone;
            bone.name = entry.at("name").get<std::string>();
            bone.parent = indexOf(entry, "parent");
            const std::vector<float> position = entry.at("position").get<std::vector<float>>();
            const std::vector<float> rotation = entry.at("rotation").get<std::vector<float>>();
            if (position.size() != 3U || rotation.size() != 4U) {
                SetError(error, bone.name + ": a position needs 3 numbers and a rotation 4");
                return std::nullopt;
            }
            bone.position = Math::Vector3{position[0], position[1], position[2]};
            bone.rotation = Math::Quaternion{rotation[0], rotation[1], rotation[2], rotation[3]};
            HumanoidBoneInfo info;
            info.key = MakeBoneKey(bone.name);
            const std::string kind = entry.value("kind", std::string{"body"});
            for (std::uint8_t candidate = 0U; candidate <= static_cast<std::uint8_t>(HumanoidBoneKind::Socket); ++candidate) {
                if (kind == HumanoidBoneKindName(static_cast<HumanoidBoneKind>(candidate))) {
                    info.kind = static_cast<HumanoidBoneKind>(candidate);
                }
            }
            info.aim = indexOf(entry, "aim");
            info.follows = indexOf(entry, "follows");
            info.optional = entry.value("optional", info.kind == HumanoidBoneKind::Corrective);
            reference.skeleton.bones.push_back(std::move(bone));
            reference.info.push_back(std::move(info));
        }
        if (!IsRetargetSkeletonValid(reference.skeleton)) {
            SetError(error, "the bones do not form a skeleton (unique names, parents first)");
            return std::nullopt;
        }
        return reference;
    } catch (const std::exception& exception) {
        SetError(error, exception.what());
        return std::nullopt;
    }
}

const HumanoidReference& GetHumanoidReference() {
    static const HumanoidReference reference = [] {
        const std::string_view text{reinterpret_cast<const char*>(uve_retarget_humanoid_json.data()),
                                    uve_retarget_humanoid_json.size()};
        std::optional<HumanoidReference> parsed = ParseHumanoidReference(text);
        return parsed.has_value() ? std::move(*parsed) : HumanoidReference{};
    }();
    return reference;
}

} // namespace UVE::Retarget
