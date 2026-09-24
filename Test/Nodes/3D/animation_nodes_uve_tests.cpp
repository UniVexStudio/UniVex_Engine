// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>

#include <gtest/gtest.h>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/animation_player_component_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/animation_tree_uve.h"

namespace UVE::Scene {
namespace {

/// A one-second clip sliding along X from 0 to `distance`.
[[nodiscard]] Asset::AnimationClipAssetUVE MakeSlideClipUVE(const float distance = 10.0F, const double duration = 1.0) {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "slide";
    clip.durationSeconds = duration;
    Asset::AnimationAssetSampleUVE start;
    start.timeSeconds = 0.0;
    Asset::AnimationAssetSampleUVE end;
    end.timeSeconds = duration;
    end.pose.position = Math::Vector3UVE{distance, 0.0F, 0.0F};
    end.pose.scale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    clip.samples = {start, end};
    return clip;
}

[[nodiscard]] AnimationPlayerComponentUVE StartedUVE(AnimationPlayerComponentUVE player,
                                                     const TransformComponentUVE& target,
                                                     const Asset::AnimationClipAssetUVE& clip) {
    PlayAnimationPlayerUVE(player, target, clip.durationSeconds);
    return player;
}

TEST(AnimationPlayerUVETest, LoopWrapsBackToTheStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-4F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.75F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_NEAR(target.localPosition.x, 2.5F, 1e-4F);
    EXPECT_TRUE(player.isPlaying);
}

TEST(AnimationPlayerUVETest, PingPongTurnsRoundAtEachEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::PingPong;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.25F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.75F, 1e-4F);
    EXPECT_EQ(player.direction, -1.0F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(player.currentTimeSeconds, 0.25F, 1e-4F);
    EXPECT_EQ(player.direction, 1.0F);
}

TEST(AnimationPlayerUVETest, OnceStopsAtTheEndAndCanReturnToStart) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-3.0F, 1.0F, 0.0F};
    AnimationPlayerComponentUVE settings;
    settings.loopMode = AnimationLoopModeUVE::Once;
    AnimationPlayerComponentUVE hold = StartedUVE(settings, target, clip);
    TransformComponentUVE held = target;
    ASSERT_TRUE(StepAnimationPlayerUVE(hold, clip, 2.0F, held));
    EXPECT_FALSE(hold.isPlaying);
    EXPECT_TRUE(hold.finished);
    EXPECT_NEAR(held.localPosition.x, 10.0F, 1e-4F);
    EXPECT_FALSE(StepAnimationPlayerUVE(hold, clip, 0.1F, held)); // stopped: nothing more written

    settings.onFinish = AnimationFinishActionUVE::ReturnToStart;
    AnimationPlayerComponentUVE back = StartedUVE(settings, target, clip);
    TransformComponentUVE returned = target;
    ASSERT_TRUE(StepAnimationPlayerUVE(back, clip, 2.0F, returned));
    EXPECT_EQ(returned.localPosition, target.localPosition);
}

TEST(AnimationPlayerUVETest, NegativeSpeedPlaysBackwardsFromTheEnd) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    AnimationPlayerComponentUVE settings;
    settings.speed = -1.0F;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    EXPECT_NEAR(player.currentTimeSeconds, 1.0F, 1e-6F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.25F, target));
    EXPECT_NEAR(target.localPosition.x, 7.5F, 1e-4F);
}

TEST(AnimationPlayerUVETest, BlendInEasesFromWhereTheTargetWas) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE(10.0F, 10.0);
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{0.0F, 4.0F, 0.0F};
    AnimationPlayerComponentUVE settings;
    settings.blendInSeconds = 1.0F;
    settings.startOffsetSeconds = 5.0F; // the clip is at x=5, y=0 there
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    // Halfway through the blend: halfway between the start (0,4) and the clip (5.5,0).
    EXPECT_NEAR(target.localPosition.x, 2.75F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 2.0F, 1e-3F);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 1.0F, target));
    EXPECT_NEAR(target.localPosition.x, 6.5F, 1e-3F);
    EXPECT_NEAR(target.localPosition.y, 0.0F, 1e-3F);
}

TEST(AnimationPlayerUVETest, RelativeLaysTheMotionOnTopOfTheStartPose) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{100.0F, 0.0F, 7.0F};
    AnimationPlayerComponentUVE settings;
    settings.relative = true;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 105.0F, 1e-3F);
    EXPECT_NEAR(target.localPosition.z, 7.0F, 1e-3F);
}

TEST(AnimationPlayerUVETest, ChannelMasksLeaveTheOtherChannelsAlone) {
    const Asset::AnimationClipAssetUVE clip = MakeSlideClipUVE();
    TransformComponentUVE target;
    target.localPosition = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
    AnimationPlayerComponentUVE settings;
    settings.animatePosition = false;
    AnimationPlayerComponentUVE player = StartedUVE(settings, target, clip);
    ASSERT_TRUE(StepAnimationPlayerUVE(player, clip, 0.5F, target));
    EXPECT_EQ(target.localPosition, (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));
    EXPECT_NEAR(target.localScale.x, 1.5F, 1e-4F);
}

TEST(AnimationPlayerUVETest, AnEmptyClipStopsWithoutWriting) {
    Asset::AnimationClipAssetUVE empty;
    TransformComponentUVE target;
    AnimationPlayerComponentUVE player = StartedUVE(AnimationPlayerComponentUVE{}, target, empty);
    EXPECT_FALSE(StepAnimationPlayerUVE(player, empty, 0.1F, target));
    EXPECT_FALSE(player.isPlaying);
}

TEST(AnimationPlayerUVETest, ValidationRejectsBadSettings) {
    AnimationPlayerComponentUVE player;
    EXPECT_TRUE(IsAnimationPlayerComponentValidUVE(player));
    player.blendInSeconds = -1.0F;
    EXPECT_FALSE(IsAnimationPlayerComponentValidUVE(player));
    player = AnimationPlayerComponentUVE{};
    player.loopMode = static_cast<AnimationLoopModeUVE>(9);
    EXPECT_FALSE(IsAnimationPlayerComponentValidUVE(player));
}

TEST(AnimationTreeUVETest, BlendMixesTheTwoClips) {
    const Asset::AnimationClipAssetUVE walk = MakeSlideClipUVE(10.0F);
    const Asset::AnimationClipAssetUVE run = MakeSlideClipUVE(20.0F);
    TransformComponentUVE target;
    AnimationTreeComponentUVE tree;
    tree.syncPhase = false;
    tree.blend = 0.0F;
    ASSERT_TRUE(StepAnimationTreeUVE(tree, &walk, &run, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-4F);

    AnimationTreeComponentUVE half = tree;
    half.timeA = 0.0F;
    half.timeB = 0.0F;
    half.blend = 0.5F;
    half.started = false;
    ASSERT_TRUE(StepAnimationTreeUVE(half, &walk, &run, 0.5F, target));
    EXPECT_NEAR(target.localPosition.x, 7.5F, 1e-4F);
}

TEST(AnimationTreeUVETest, SyncPhaseKeepsBothClipsAtTheSamePointInTheirCycle) {
    const Asset::AnimationClipAssetUVE walk = MakeSlideClipUVE(10.0F, 1.0);
    const Asset::AnimationClipAssetUVE run = MakeSlideClipUVE(10.0F, 2.0);
    TransformComponentUVE target;
    AnimationTreeComponentUVE tree;
    tree.blend = 0.5F; // shared cycle length 1.5 s
    ASSERT_TRUE(StepAnimationTreeUVE(tree, &walk, &run, 0.75F, target));
    EXPECT_NEAR(tree.timeA, 0.5F, 1e-4F); // halfway through the shared phase
    EXPECT_NEAR(target.localPosition.x, 5.0F, 1e-3F); // both clips halfway: both at x=5
}

TEST(AnimationTreeUVETest, SmoothingEasesTheBlendAndAMissingClipLeavesTheOther) {
    const Asset::AnimationClipAssetUVE walk = MakeSlideClipUVE(10.0F);
    TransformComponentUVE target;
    AnimationTreeComponentUVE tree;
    tree.blendSmoothing = 2.0F;
    ASSERT_TRUE(StepAnimationTreeUVE(tree, &walk, nullptr, 0.1F, target));
    tree.blend = 1.0F;
    ASSERT_TRUE(StepAnimationTreeUVE(tree, &walk, nullptr, 0.1F, target));
    EXPECT_NEAR(tree.currentBlend, 0.2F, 1e-4F);
    tree.active = false;
    EXPECT_FALSE(StepAnimationTreeUVE(tree, &walk, nullptr, 0.1F, target));
    EXPECT_FALSE(StepAnimationTreeUVE(tree, nullptr, nullptr, 0.1F, target));
}

} // namespace
} // namespace UVE::Scene
