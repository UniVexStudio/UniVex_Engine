// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "retarget_playback.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "retarget_humanoid.h"

namespace UVE::Retarget {
namespace {

[[nodiscard]] const std::unordered_set<std::string>& DrivingBones() {
    static const std::unordered_set<std::string> bones = [] {
        std::unordered_set<std::string> names;
        const HumanoidReference& reference = GetHumanoidReference();
        for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
            const HumanoidBoneKind kind = reference.info[index].kind;
            if (kind == HumanoidBoneKind::Root || kind == HumanoidBoneKind::IK ||
                reference.info[index].key == "C|hips") {
                names.insert(reference.skeleton.bones[index].name);
            }
        }
        return names;
    }();
    return bones;
}

struct World final {
    Math::Vector3 position{};
    Math::Quaternion rotation{};
};

/// A bone's world position, walking down from its root; false when the parents do not lead to it.
[[nodiscard]] bool WorldOf(std::span<const RestBoneView> skeleton, const std::size_t bone, Math::Vector3& out) {
    std::vector<std::size_t> chain;
    for (std::int32_t at = static_cast<std::int32_t>(bone); at >= 0; at = skeleton[static_cast<std::size_t>(at)].parent) {
        if (chain.size() > skeleton.size()) {
            return false; // a loop
        }
        chain.push_back(static_cast<std::size_t>(at));
    }
    World world;
    world.rotation = Math::Quaternion{};
    for (auto link = chain.rbegin(); link != chain.rend(); ++link) {
        const RestBoneView& part = skeleton[*link];
        world.position = world.position + Math::RotateVector(world.rotation, part.position);
        world.rotation = Math::Multiply(world.rotation, part.rotation);
    }
    out = world.position;
    return true;
}

[[nodiscard]] std::size_t IndexOf(std::span<const RestBoneView> skeleton, const std::string_view name) {
    const auto found = std::ranges::find(skeleton, name, &RestBoneView::name);
    return found == skeleton.end() ? skeleton.size() : static_cast<std::size_t>(found - skeleton.begin());
}

} // namespace

float ConformedHipsHeight(const std::span<const RestBoneView> skeleton) {
    Math::Vector3 hips{};
    const std::size_t hipsIndex = IndexOf(skeleton, "Hips");
    if (hipsIndex >= skeleton.size() || !WorldOf(skeleton, hipsIndex, hips)) {
        return 0.0F;
    }
    bool found = false;
    float ground = 0.0F;
    for (const char* name : {"Foot_L", "Foot_R", "Toe_L", "Toe_R"}) {
        const std::size_t index = IndexOf(skeleton, name);
        Math::Vector3 position{};
        if (index < skeleton.size() && WorldOf(skeleton, index, position)) {
            ground = found ? std::min(ground, position.y) : position.y;
            found = true;
        }
    }
    return std::max(hips.y - (found ? ground : 0.0F), 0.0F);
}

bool ConformedPlayback::DrivesTranslation(const std::string_view boneName) {
    return DrivingBones().contains(std::string{boneName});
}

Math::Vector3 ConformedPlayback::Position(const std::string_view boneName, const Math::Vector3& sampled,
                                                   const Math::Vector3& rest) const {
    if (!active) {
        return sampled;
    }
    return DrivesTranslation(boneName) ? sampled * heightScale : rest;
}

ConformedPlayback PlanConformedPlayback(const Asset::AnimationClipAsset& clip,
                                              const std::span<const RestBoneView> skeleton) {
    ConformedPlayback plan;
    if (!clip.conformed) {
        return plan;
    }
    plan.active = true;
    std::vector<RestBoneView> clipRest;
    clipRest.reserve(clip.rest.size());
    for (const Asset::AnimationAssetRestBone& bone : clip.rest) {
        clipRest.push_back(RestBoneView{bone.bone, bone.parent, bone.position, bone.rotation});
    }
    const float clipHeight = ConformedHipsHeight(clipRest);
    const float characterHeight = ConformedHipsHeight(skeleton);
    if (clipHeight > 1e-4F && characterHeight > 1e-4F) {
        plan.heightScale = characterHeight / clipHeight;
    }
    return plan;
}

} // namespace UVE::Retarget
