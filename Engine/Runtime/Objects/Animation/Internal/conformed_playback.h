// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "animation_clip_asset.h"
#include "skeleton_3d.h"
#include "retarget_playback.h"

namespace UVE::Scene {

/// How `clip` is played on `skeleton`: inactive unless the clip is conformed, in which case only the
/// bones that place the body (root, hips, IK) take its translation, scaled to this character.
[[nodiscard]] inline Retarget::ConformedPlayback PlanConformedPlaybackFor(
    const Asset::AnimationClipAsset& clip, const Skeleton3DComponent& skeleton) {
    if (!clip.conformed) {
        return {};
    }
    std::vector<Retarget::RestBoneView> rest;
    rest.reserve(skeleton.bones.size());
    for (const SkeletonBone& bone : skeleton.bones) {
        rest.push_back(Retarget::RestBoneView{bone.name, bone.parentIndex, bone.localPosition, bone.localRotation});
    }
    return Retarget::PlanConformedPlayback(clip, rest);
}

} // namespace UVE::Scene
