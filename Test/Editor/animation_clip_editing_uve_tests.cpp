// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>

#include <gtest/gtest.h>

#include "uve/editor/animation_clip_editing_uve.h"

namespace UVE::Editor {
namespace {

/// "Hips" with a key every 10 frames at 10 fps over one second, moving 1 m per key along Z.
[[nodiscard]] Asset::AnimationClipAssetUVE MakeClipUVE() {
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    Asset::AnimationAssetBoneTrackUVE hips{"Hips", {}};
    for (int frame = 0; frame <= 10; frame += 5) {
        Asset::AnimationAssetSampleUVE sample;
        sample.timeSeconds = frame / 10.0;
        sample.pose.position = Math::Vector3UVE{0.0F, 1.0F, static_cast<float>(frame) / 5.0F};
        hips.samples.push_back(sample);
    }
    clip.bones = {hips};
    return clip;
}

[[nodiscard]] std::vector<double> TimesUVE(const Asset::AnimationClipAssetUVE& clip) {
    std::vector<double> times;
    for (const auto& sample : clip.bones.front().samples) {
        times.push_back(sample.timeSeconds);
    }
    return times;
}

TEST(AnimationClipEditingUVETest, DeletesKeysButNeverEmptiesATrack) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    EXPECT_EQ(DeleteClipKeysUVE(clip, {{"Hips", 0.5}}), 1U);
    EXPECT_EQ(TimesUVE(clip), (std::vector<double>{0.0, 1.0}));
    EXPECT_EQ(DeleteClipKeysUVE(clip, {{"Hips", 0.0}, {"Hips", 1.0}}), 1U) << "the last key stays";
    EXPECT_EQ(clip.bones.front().samples.size(), 1U);
    EXPECT_EQ(DeleteClipKeysUVE(clip, {{"Tail", 0.0}}), 0U) << "no such track";
}

TEST(AnimationClipEditingUVETest, MovesKeysOntoFramesAndReplacesWhatIsThere) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    // 0.5 s + 0.23 s lands on frame 7 at 10 fps.
    const std::vector<ClipKeyUVE> landed = MoveClipKeysUVE(clip, {{"Hips", 0.5}}, 0.23, 10.0);
    ASSERT_EQ(landed.size(), 1U);
    EXPECT_NEAR(landed.front().timeSeconds, 0.7, 1e-9);
    EXPECT_EQ(TimesUVE(clip).size(), 3U);
    EXPECT_NEAR(TimesUVE(clip)[1], 0.7, 1e-9);

    // Onto the last key: it replaces it, and the track stays sorted.
    MoveClipKeysUVE(clip, {{"Hips", 0.7}}, 0.3, 10.0);
    EXPECT_EQ(TimesUVE(clip), (std::vector<double>{0.0, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples.back().pose.position.z, 1.0F, 1e-6F) << "the moved key's pose";

    // Past the end clamps to the end.
    Asset::AnimationClipAssetUVE other = MakeClipUVE();
    MoveClipKeysUVE(other, {{"Hips", 0.0}}, 5.0, 10.0);
    EXPECT_EQ(TimesUVE(other), (std::vector<double>{0.5, 1.0}));
}

TEST(AnimationClipEditingUVETest, KeysMovingPastEachOtherDoNotOverwriteOneAnother) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    MoveClipKeysUVE(clip, {{"Hips", 0.0}, {"Hips", 0.5}}, 0.5, 10.0);
    ASSERT_EQ(TimesUVE(clip), (std::vector<double>{0.5, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples[0].pose.position.z, 0.0F, 1e-6F) << "the key from 0 s";
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F) << "the key from 0.5 s";
}

TEST(AnimationClipEditingUVETest, CopiesAndPastesAtThePlayhead) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    const ClipKeyClipboardUVE clipboard = CopyClipKeysUVE(clip, {{"Hips", 0.5}, {"Hips", 1.0}});
    ASSERT_EQ(clipboard.entries.size(), 2U);
    EXPECT_NEAR(clipboard.entries[0].offsetSeconds, 0.0, 1e-9);
    EXPECT_NEAR(clipboard.entries[1].offsetSeconds, 0.5, 1e-9);

    const std::vector<ClipKeyUVE> pasted = PasteClipKeysUVE(clip, clipboard, 0.2, 10.0);
    ASSERT_EQ(pasted.size(), 2U);
    EXPECT_EQ(TimesUVE(clip), (std::vector<double>{0.0, 0.2, 0.5, 0.7, 1.0}));
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F);

    // Past the end is dropped.
    EXPECT_EQ(PasteClipKeysUVE(clip, clipboard, 0.8, 10.0).size(), 1U);
}

TEST(AnimationClipEditingUVETest, InsertsAKeyHoldingTheCurrentMotion) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    ASSERT_TRUE(InsertClipKeyUVE(clip, "Hips", 0.25, 10.0)) << "0.25 s snaps to frame 3 at 10 fps";
    ASSERT_EQ(clip.bones.front().samples.size(), 4U);
    const auto& inserted = clip.bones.front().samples[1];
    EXPECT_NEAR(inserted.timeSeconds, 0.3, 1e-9);
    EXPECT_NEAR(inserted.pose.position.z, 0.6F, 1e-5F) << "on the line between its neighbours";
    EXPECT_FALSE(InsertClipKeyUVE(clip, "Hips", 0.3, 10.0)) << "already a key there";
    EXPECT_FALSE(InsertClipKeyUVE(clip, "Tail", 0.3, 10.0));
}

TEST(AnimationClipEditingUVETest, ReadsAndSetsOneNumberOfAKey) {
    Asset::AnimationClipAssetUVE clip = MakeClipUVE();
    ASSERT_TRUE(SetClipKeyComponentUVE(clip, "Hips", 0.5, 0, 1, 2.5F));
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.y, 2.5F, 1e-6F);
    EXPECT_NEAR(clip.bones.front().samples[1].pose.position.z, 1.0F, 1e-6F) << "the other axes stay";

    // Rotation goes through Euler degrees and back.
    ASSERT_TRUE(SetClipKeyComponentUVE(clip, "Hips", 0.5, 1, 1, 30.0F));
    EXPECT_NEAR(GetClipPoseComponentUVE(clip.bones.front().samples[1].pose, 1, 1), 30.0F, 1e-3F);
    EXPECT_NEAR(GetClipPoseComponentUVE(clip.bones.front().samples[1].pose, 1, 0), 0.0F, 1e-3F);

    ASSERT_TRUE(SetClipKeyComponentUVE(clip, "Hips", 1.0, 2, 0, 2.0F));
    EXPECT_NEAR(clip.bones.front().samples[2].pose.scale.x, 2.0F, 1e-6F);

    EXPECT_FALSE(SetClipKeyComponentUVE(clip, "Hips", 0.25, 0, 0, 1.0F)) << "no key at 0.25 s";
    EXPECT_FALSE(SetClipKeyComponentUVE(clip, "Hips", 0.5, 0, 0, std::nanf(""))) << "not finite";
}

} // namespace
} // namespace UVE::Editor
