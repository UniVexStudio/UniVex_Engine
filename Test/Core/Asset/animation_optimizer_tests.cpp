// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <gtest/gtest.h>

#include "animation_optimizer.h"

#include <cmath>
#include <string>

namespace UVE::Asset {
namespace {

/// A three-bone arm swinging slowly at 60 fps for two seconds: every frame stored, though smooth.
[[nodiscard]] AnimationClipAssetUVE MakeSwingUVE() {
    AnimationClipAssetUVE clip;
    clip.clipId = "Swing";
    clip.durationSeconds = 2.0;
    const char* names[] = {"Root", "Upper", "Lower"};
    for (int bone = 0; bone < 3; ++bone) {
        AnimationAssetRestBoneUVE rest;
        rest.bone = names[bone];
        rest.parent = bone - 1;
        rest.position = Math::Vector3UVE{0.0F, bone == 0 ? 1.0F : 0.3F, 0.0F};
        clip.rest.push_back(rest);
        AnimationAssetBoneTrackUVE track;
        track.bone = names[bone];
        for (int frame = 0; frame <= 120; ++frame) {
            const double t = frame / 60.0;
            const float angle = 0.6F * static_cast<float>(std::sin(t * 3.14159265)) * static_cast<float>(bone);
            AnimationAssetSampleUVE sample;
            sample.timeSeconds = t;
            sample.pose.position = rest.position;
            sample.pose.rotation = Math::QuaternionUVE{0.0F, 0.0F, std::sin(angle * 0.5F), std::cos(angle * 0.5F)};
            track.samples.push_back(sample);
        }
        clip.bones.push_back(std::move(track));
    }
    return clip;
}

TEST(AnimationOptimizerUVETest, KeepsFarFewerKeysWithinTheJointBudget) {
    AnimationClipAssetUVE clip = MakeSwingUVE();
    ASSERT_TRUE(IsAnimationClipAssetValidUVE(clip));
    const std::size_t before = CountAnimationClipSamplesUVE(clip);
    const AnimationOptimizeResultUVE result = OptimizeAnimationClipUVE(clip);
    ASSERT_TRUE(result.changed);
    EXPECT_EQ(result.samplesBefore, before);
    EXPECT_LT(result.samplesAfter * 2U, before) << "a smooth swing needs far fewer than every frame";
    EXPECT_LE(result.worstBudgetRatio, 1.0) << "every joint within its budget";
    EXPECT_LE(result.worstErrorMetres, 0.01);
    EXPECT_TRUE(IsAnimationClipAssetValidUVE(clip));
    EXPECT_EQ(clip.rest.size(), 3U) << "the skeleton is kept";
    EXPECT_EQ(clip.durationSeconds, 2.0);
}

TEST(AnimationOptimizerUVETest, LeavesAnInvalidOrTinyClipAlone) {
    AnimationClipAssetUVE empty;
    EXPECT_FALSE(OptimizeAnimationClipUVE(empty).changed);
    AnimationClipAssetUVE clip = MakeSwingUVE();
    clip.bones[0].samples.resize(1U);
    clip.bones[1].samples.resize(1U);
    clip.bones[2].samples.resize(1U);
    EXPECT_FALSE(OptimizeAnimationClipUVE(clip).changed);
    EXPECT_EQ(CountAnimationClipSamplesUVE(clip), 3U) << "left as it was";
}

} // namespace
} // namespace UVE::Asset
