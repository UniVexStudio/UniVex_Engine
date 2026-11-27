// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>

#include <gtest/gtest.h>

#include "animation_clip_editing.h"

namespace UVE::Editor {
namespace {

/// "Hips" with a key every 10 frames at 10 fps over one second, moving 1 m per key along Z.
[[nodiscard]] Asset::AnimationClipAsset MakeClip() {
    Asset::AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetBoneTrack hips{"Hips", {}};
    for (int frame = 0; frame <= 10; frame += 5) {
        Asset::AnimationAssetSample sample;
        sample.timeSeconds = frame / 10.0;
        sample.pose.position = Math::Vector3{0.0F, 1.0F, static_cast<float>(frame) / 5.0F};
        hips.samples.push_back(sample);
    }
    clip.bones = {hips};
    return clip;
}

[[nodiscard]] std::vector<double> Times(const Asset::AnimationClipAsset& clip) {
    std::vector<double> times;
    for (const auto& sample : clip.bones.front().samples) {
        times.push_back(sample.timeSeconds);
    }
    return times;
}

TEST(AnimationClipEditingTest, DeletesKeysButNeverEmptiesATrack) {
    Asset::AnimationClipAsset clip = MakeClip();
    EXPECT_EQ(DeleteClipKeys(clip, {{"Hips", 0.5}}), 1U);
    EXPECT_EQ(Times(clip), (std::vector<double>{0.0, 1.0}));
    EXPECT_EQ(DeleteClipKeys(clip, {{"Hips", 0.0}, {"Hips", 1.0}}), 1U) << "the last key stays";
    EXPECT_EQ(clip.bones.front().samples.size(), 1U);
    EXPECT_EQ(DeleteClipKeys(clip, {{"Tail", 0.0}}), 0U) << "no such track";
}

TEST(AnimationClipEditingTest, MovesKeysOntoFramesAndReplacesWhatIsThere) {
    Asset::AnimationClipAsset clip = MakeClip();
    // 0.5 s + 0.23 s lands on frame 7 at 10 fps.
    const std::vector<ClipKey> landed = MoveClipKeys(clip, {{"Hips", 0.5}}, 0.23, 10.0);
    ASSERT_EQ(landed.size(), 1U);
    EXPECT_NEAR(landed.front().timeSeconds, 0.7, 1e-9);
    EXPECT_EQ(Times(clip).size(), 3U);
    EXPECT_NEAR(Times(clip)[1], 0.7, 1e-9);

    // Onto the last key: it replaces it, and the track stays sorted.
    MoveClipKeys(clip, {{"Hips", 0.7}}, 0.3, 10.0);
    EXPECT_EQ(Times(clip), (std::vector<double>{0.0, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples.back().pose.position.z, 1.0F, 1e-6F) << "the moved key's pose";

    // Past the end clamps to the end.
    Asset::AnimationClipAsset other = MakeClip();
    MoveClipKeys(other, {{"Hips", 0.0}}, 5.0, 10.0);
    EXPECT_EQ(Times(other), (std::vector<double>{0.5, 1.0}));
}

TEST(AnimationClipEditingTest, KeysMovingPastEachOtherDoNotOverwriteOneAnother) {
    Asset::AnimationClipAsset clip = MakeClip();
    MoveClipKeys(clip, {{"Hips", 0.0}, {"Hips", 0.5}}, 0.5, 10.0);
    ASSERT_EQ(Times(clip), (std::vector<double>{0.5, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples[0].pose.position.z, 0.0F, 1e-6F) << "the key from 0 s";
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F) << "the key from 0.5 s";
}

TEST(AnimationClipEditingTest, CopiesAndPastesAtThePlayhead) {
    Asset::AnimationClipAsset clip = MakeClip();
    const ClipKeyClipboard clipboard = CopyClipKeys(clip, {{"Hips", 0.5}, {"Hips", 1.0}});
    ASSERT_EQ(clipboard.entries.size(), 2U);
    EXPECT_NEAR(clipboard.entries[0].offsetSeconds, 0.0, 1e-9);
    EXPECT_NEAR(clipboard.entries[1].offsetSeconds, 0.5, 1e-9);

    const std::vector<ClipKey> pasted = PasteClipKeys(clip, clipboard, 0.2, 10.0);
    ASSERT_EQ(pasted.size(), 2U);
    EXPECT_EQ(Times(clip), (std::vector<double>{0.0, 0.2, 0.5, 0.7, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F);

    // Past the end is dropped.
    EXPECT_EQ(PasteClipKeys(clip, clipboard, 0.8, 10.0).size(), 1U);
}

TEST(AnimationClipEditingTest, InsertsAKeyHoldingTheCurrentMotion) {
    Asset::AnimationClipAsset clip = MakeClip();
    ASSERT_TRUE(InsertClipKey(clip, "Hips", 0.25, 10.0)) << "0.25 s snaps to frame 3 at 10 fps";
    ASSERT_EQ(clip.bones.front().samples.size(), 4U);
    const auto& inserted = clip.bones.front().samples[1];
    EXPECT_NEAR(inserted.timeSeconds, 0.3, 1e-9);
    EXPECT_NEAR(inserted.pose.position.z, 0.6F, 1e-5F) << "on the line between its neighbours";
    EXPECT_FALSE(InsertClipKey(clip, "Hips", 0.3, 10.0)) << "already a key there";
    EXPECT_FALSE(InsertClipKey(clip, "Tail", 0.3, 10.0));
}

TEST(AnimationClipEditingTest, ReadsAndSetsOneNumberOfAKey) {
    Asset::AnimationClipAsset clip = MakeClip();
    ASSERT_TRUE(SetClipKeyComponent(clip, "Hips", 0.5, 0, 1, 2.5F));
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.y, 2.5F, 1e-6F);
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F) << "the other axes stay";

    // Rotation goes through Euler degrees and back.
    ASSERT_TRUE(SetClipKeyComponent(clip, "Hips", 0.5, 1, 1, 30.0F));
    EXPECT_NEAR(GetClipPoseComponent(clip.bones.front().samples[1].pose, 1, 1), 30.0F, 1e-3F);
    EXPECT_NEAR(GetClipPoseComponent(clip.bones.front().samples[1].pose, 1, 0), 0.0F, 1e-3F);

    ASSERT_TRUE(SetClipKeyComponent(clip, "Hips", 1.0, 2, 0, 2.0F));
    EXPECT_NEAR(clip.bones.front().samples[2].pose.scale.x, 2.0F, 1e-6F);

    EXPECT_FALSE(SetClipKeyComponent(clip, "Hips", 0.25, 0, 0, 1.0F)) << "no key at 0.25 s";
    EXPECT_FALSE(SetClipKeyComponent(clip, "Hips", 0.5, 0, 0, std::nanf(""))) << "not finite";
}

TEST(AnimationClipEditingTest, AddsMovesRenamesAndRemovesEventsKeepingThemInOrder) {
    Asset::AnimationClipAsset clip = MakeClip();
    EXPECT_EQ(AddClipEvent(clip, 0.62, 10.0), 0U);
    EXPECT_NEAR(clip.events[0].timeSeconds, 0.6, 1e-9) << "snapped to a frame";
    EXPECT_EQ(clip.events[0].eventId, "event");
    EXPECT_EQ(AddClipEvent(clip, 0.2, 10.0), 0U) << "earlier, so first";
    EXPECT_EQ(clip.events[0].eventId, "event_2") << "a name no other event has";

    const std::optional<std::size_t> moved = MoveClipEvent(clip, 0U, 0.9, 10.0);
    ASSERT_TRUE(moved.has_value());
    EXPECT_EQ(*moved, 1U);
    EXPECT_EQ(clip.events[1].eventId, "event_2");
    EXPECT_FALSE(MoveClipEvent(clip, 5U, 0.1, 10.0).has_value());

    EXPECT_TRUE(RenameClipEvent(clip, 1U, "footstep"));
    EXPECT_EQ(clip.events[1].eventId, "footstep");
    EXPECT_FALSE(RenameClipEvent(clip, 1U, "")) << "an event needs a name";

    EXPECT_TRUE(RemoveClipEvent(clip, 0U));
    ASSERT_EQ(clip.events.size(), 1U);
    EXPECT_EQ(clip.events[0].eventId, "footstep");
    EXPECT_FALSE(RemoveClipEvent(clip, 3U));
}

TEST(AnimationClipEditingTest, MovesAllRootTrackSamplesWithoutNullDereference) {
    Asset::AnimationClipAsset clip;
    clip.clipId = "root_slide";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetSample s0;
    s0.timeSeconds = 0.0;
    s0.pose.position = Math::Vector3{0.0F, 0.0F, 0.0F};
    Asset::AnimationAssetSample s1;
    s1.timeSeconds = 0.5;
    s1.pose.position = Math::Vector3{1.0F, 0.0F, 0.0F};
    clip.samples = {s0, s1};

    const std::vector<ClipKey> landed = MoveClipKeys(clip, {{"", 0.0}, {"", 0.5}}, 0.2, 10.0);
    ASSERT_EQ(landed.size(), 2U);
    ASSERT_EQ(clip.samples.size(), 2U);
    EXPECT_NEAR(clip.samples[0].timeSeconds, 0.2, 1e-9);
    EXPECT_NEAR(clip.samples[1].timeSeconds, 0.7, 1e-9);
}

} // namespace
} // namespace UVE::Editor
