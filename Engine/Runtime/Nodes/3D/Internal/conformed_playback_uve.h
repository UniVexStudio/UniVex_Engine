// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <vector>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"
#include "uve/retarget/retarget_playback_uve.h"

namespace UVE::Scene {

/// How `clip` is played on `skeleton`: inactive unless the clip is conformed, in which case only the
/// bones that place the body (root, hips, IK) take its translation, scaled to this character.
[[nodiscard]] inline Retarget::ConformedPlaybackUVE PlanConformedPlaybackForUVE(
    const Asset::AnimationClipAssetUVE& clip, const Skeleton3DNodeComponentUVE& skeleton) {
    if (!clip.conformed) {
        return {};
    }
    std::vector<Retarget::RestBoneViewUVE> rest;
    rest.reserve(skeleton.bones.size());
    for (const SkeletonBoneUVE& bone : skeleton.bones) {
        rest.push_back(Retarget::RestBoneViewUVE{bone.name, bone.parentIndex, bone.localPosition, bone.localRotation});
    }
    return Retarget::PlanConformedPlaybackUVE(clip, rest);
}

} // namespace UVE::Scene
