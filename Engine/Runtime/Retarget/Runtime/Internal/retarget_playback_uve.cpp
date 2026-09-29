// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/retarget/retarget_playback_uve.h"

#include <algorithm>
#include <string>
#include <unordered_set>
#include <vector>

#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Retarget {
namespace {

[[nodiscard]] const std::unordered_set<std::string>& DrivingBonesUVE() {
    static const std::unordered_set<std::string> bones = [] {
        std::unordered_set<std::string> names;
        const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
        for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
            const HumanoidBoneKindUVE kind = reference.info[index].kind;
            if (kind == HumanoidBoneKindUVE::Root || kind == HumanoidBoneKindUVE::IK ||
                reference.info[index].key == "C|hips") {
                names.insert(reference.skeleton.bones[index].name);
            }
        }
        return names;
    }();
    return bones;
}

struct WorldUVE final {
    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
};

/// A bone's world position, walking down from its root; false when the parents do not lead to it.
[[nodiscard]] bool WorldOfUVE(std::span<const RestBoneViewUVE> skeleton, const std::size_t bone, Math::Vector3UVE& out) {
    std::vector<std::size_t> chain;
    for (std::int32_t at = static_cast<std::int32_t>(bone); at >= 0; at = skeleton[static_cast<std::size_t>(at)].parent) {
        if (chain.size() > skeleton.size()) {
            return false; // a loop
        }
        chain.push_back(static_cast<std::size_t>(at));
    }
    WorldUVE world;
    world.rotation = Math::QuaternionUVE{};
    for (auto link = chain.rbegin(); link != chain.rend(); ++link) {
        const RestBoneViewUVE& part = skeleton[*link];
        world.position = world.position + Math::RotateVectorUVE(world.rotation, part.position);
        world.rotation = Math::MultiplyUVE(world.rotation, part.rotation);
    }
    out = world.position;
    return true;
}

[[nodiscard]] std::size_t IndexOfUVE(std::span<const RestBoneViewUVE> skeleton, const std::string_view name) {
    const auto found = std::ranges::find(skeleton, name, &RestBoneViewUVE::name);
    return found == skeleton.end() ? skeleton.size() : static_cast<std::size_t>(found - skeleton.begin());
}

} // namespace

float ConformedHipsHeightUVE(const std::span<const RestBoneViewUVE> skeleton) {
    Math::Vector3UVE hips{};
    const std::size_t hipsIndex = IndexOfUVE(skeleton, "Hips");
    if (hipsIndex >= skeleton.size() || !WorldOfUVE(skeleton, hipsIndex, hips)) {
        return 0.0F;
    }
    bool found = false;
    float ground = 0.0F;
    for (const char* name : {"Foot_L", "Foot_R", "Toe_L", "Toe_R"}) {
        const std::size_t index = IndexOfUVE(skeleton, name);
        Math::Vector3UVE position{};
        if (index < skeleton.size() && WorldOfUVE(skeleton, index, position)) {
            ground = found ? std::min(ground, position.y) : position.y;
            found = true;
        }
    }
    return std::max(hips.y - (found ? ground : 0.0F), 0.0F);
}

bool ConformedPlaybackUVE::DrivesTranslationUVE(const std::string_view boneName) {
    return DrivingBonesUVE().contains(std::string{boneName});
}

Math::Vector3UVE ConformedPlaybackUVE::PositionUVE(const std::string_view boneName, const Math::Vector3UVE& sampled,
                                                   const Math::Vector3UVE& rest) const {
    if (!active) {
        return sampled;
    }
    return DrivesTranslationUVE(boneName) ? sampled * heightScale : rest;
}

ConformedPlaybackUVE PlanConformedPlaybackUVE(const Asset::AnimationClipAssetUVE& clip,
                                              const std::span<const RestBoneViewUVE> skeleton) {
    ConformedPlaybackUVE plan;
    if (!clip.conformed) {
        return plan;
    }
    plan.active = true;
    std::vector<RestBoneViewUVE> clipRest;
    clipRest.reserve(clip.rest.size());
    for (const Asset::AnimationAssetRestBoneUVE& bone : clip.rest) {
        clipRest.push_back(RestBoneViewUVE{bone.bone, bone.parent, bone.position, bone.rotation});
    }
    const float clipHeight = ConformedHipsHeightUVE(clipRest);
    const float characterHeight = ConformedHipsHeightUVE(skeleton);
    if (clipHeight > 1e-4F && characterHeight > 1e-4F) {
        plan.heightScale = characterHeight / clipHeight;
    }
    return plan;
}

} // namespace UVE::Retarget
