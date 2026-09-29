// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/animation_clip_asset_uve.h"

#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/uve_file_envelope_uve.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPathUVE(const char* const name) {
    return ::UVE::Tests::ScratchRootUVE() / name;
}

AnimationClipAssetUVE MakeValidClipUVE() {
    AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {
        AnimationAssetSampleUVE{0.0, AnimationAssetPoseUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F},
                                                            Math::QuaternionUVE{},
                                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}}},
        AnimationAssetSampleUVE{1.0, AnimationAssetPoseUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F},
                                                            Math::QuaternionUVE{},
                                                            Math::Vector3UVE{1.0F, 1.0F, 1.0F}}},
    };
    clip.events = {AnimationAssetEventUVE{0.5, "footstep"}};
    return clip;
}

} // namespace

TEST(AnimationClipAssetUVETest, SaveThenLoad_RoundTripsBoundedPoseSamplesAndEvents) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_round_trip.uvanim");
    std::filesystem::remove(path);
    const AnimationClipAssetUVE original = MakeValidClipUVE();
    ASSERT_TRUE(SaveAnimationClipAssetUVE(original, path));

    AnimationClipAssetUVE loaded;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, loaded));
    EXPECT_EQ(loaded.clipId, original.clipId);
    EXPECT_EQ(loaded.durationSeconds, original.durationSeconds);
    ASSERT_EQ(loaded.samples.size(), original.samples.size());
    EXPECT_EQ(loaded.samples[0], original.samples[0]);
    EXPECT_EQ(loaded.samples[1], original.samples[1]);
    ASSERT_EQ(loaded.events.size(), 1U);
    EXPECT_EQ(loaded.events.front(), original.events.front());
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetUVETest, SaveRejectsInvalidClipWithoutPublishingDestination) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_invalid.uvanim");
    std::filesystem::remove(path);
    AnimationClipAssetUVE invalid = MakeValidClipUVE();
    invalid.samples[1].timeSeconds = 1.25;
    EXPECT_FALSE(SaveAnimationClipAssetUVE(invalid, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(AnimationClipAssetUVETest, LoadWrongEnvelopeKindPreservesExistingOutput) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_wrong_kind.uvanim");
    std::filesystem::remove(path);
    const std::vector<std::byte> payload{std::byte{'{'}, std::byte{'}'}};
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Mesh, payload));
    AnimationClipAssetUVE retained = MakeValidClipUVE();
    const AnimationClipAssetUVE original = retained;
    EXPECT_FALSE(LoadAnimationClipAssetUVE(path, retained));
    EXPECT_EQ(retained.clipId, original.clipId);
    EXPECT_EQ(retained.samples, original.samples);
    EXPECT_EQ(retained.events, original.events);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetUVETest, SkeletalClipRoundTripsItsBoneTracks) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_skeletal.uvanim");
    std::filesystem::remove(path);
    AnimationClipAssetUVE clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPoseUVE rest{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::QuaternionUVE{},
                                     Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    AnimationAssetPoseUVE forward = rest;
    forward.position.z = 0.4F;
    clip.bones = {AnimationAssetBoneTrackUVE{"Hips", {{0.0, rest}, {0.5, forward}}},
                  AnimationAssetBoneTrackUVE{"Spine", {{0.0, rest}}}};
    ASSERT_TRUE(IsAnimationClipAssetValidUVE(clip)) << "bones alone are enough: no node track needed";
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));
    AnimationClipAssetUVE loaded;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, loaded));
    EXPECT_TRUE(loaded.IsSkeletalUVE());
    EXPECT_TRUE(loaded.samples.empty());
    EXPECT_EQ(loaded.bones, clip.bones);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetUVETest, RestSkeletonAndConformedFlagRoundTripAsVersionThree) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_rest.uvanim");
    std::filesystem::remove(path);
    AnimationClipAssetUVE clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPoseUVE pose{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::QuaternionUVE{},
                                     Math::Vector3UVE{1.0F, 1.0F, 1.0F}};
    clip.bones = {AnimationAssetBoneTrackUVE{"Hips", {{0.0, pose}}}, AnimationAssetBoneTrackUVE{"Spine", {{0.0, pose}}}};
    clip.rest = {AnimationAssetRestBoneUVE{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
                 AnimationAssetRestBoneUVE{"Spine", 0, {0.0F, 0.1F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    clip.conformed = true;
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));
    AnimationClipAssetUVE loaded;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, loaded));
    EXPECT_EQ(loaded.rest, clip.rest);
    EXPECT_TRUE(loaded.conformed);

    // A clip with neither still saves as version two, so old readers and old files are unchanged.
    clip.rest.clear();
    clip.conformed = false;
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, loaded));
    EXPECT_TRUE(loaded.rest.empty());
    EXPECT_FALSE(loaded.conformed);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetUVETest, RestSkeletonMustBeParentsFirstWithUniqueNames) {
    AnimationClipAssetUVE clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPoseUVE pose{};
    clip.bones = {AnimationAssetBoneTrackUVE{"Hips", {{0.0, pose}}}};
    clip.rest = {AnimationAssetRestBoneUVE{"Hips", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                 AnimationAssetRestBoneUVE{"Spine", 0, {}, {}, {1.0F, 1.0F, 1.0F}}};
    EXPECT_TRUE(IsAnimationClipAssetValidUVE(clip));
    clip.rest[1].parent = 1; // its own index: not a parent that comes first
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(clip));
    clip.rest[1].parent = 0;
    clip.rest[1].bone = "Hips"; // a repeated name
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(clip));
}

TEST(AnimationClipAssetUVETest, BoneTracksAreValidatedLikeTheNodeTrack) {
    AnimationClipAssetUVE clip;
    clip.clipId = "run";
    clip.durationSeconds = 1.0;
    const AnimationAssetPoseUVE rest{};
    clip.bones = {AnimationAssetBoneTrackUVE{"Hips", {{0.0, rest}}}};
    EXPECT_TRUE(IsAnimationClipAssetValidUVE(clip));

    AnimationClipAssetUVE twice = clip;
    twice.bones.push_back(AnimationAssetBoneTrackUVE{"Hips", {{0.5, rest}}});
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(twice)) << "one track per bone";

    AnimationClipAssetUVE late = clip;
    late.bones[0].samples[0].timeSeconds = 2.0;
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(late)) << "past the end";

    AnimationClipAssetUVE unnamed = clip;
    unnamed.bones[0].bone.clear();
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(unnamed));

    AnimationClipAssetUVE empty = clip;
    empty.bones[0].samples.clear();
    EXPECT_FALSE(IsAnimationClipAssetValidUVE(empty)) << "nothing moves";
}

TEST(AnimationClipAssetUVETest, AVersionOneFileStillLoads) {
    const std::filesystem::path path = TestPathUVE("uve_animation_clip_asset_v1.uvanim");
    std::filesystem::remove(path);
    const std::string text =
        R"({"schema":"uve-animation-v1","clipId":"door","durationSeconds":1.0,"events":[],"samples":[)"
        R"({"timeSeconds":0.0,"pose":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]})";
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFileUVE(path, AssetKindUVE::Animation, std::vector<std::byte>(bytes, bytes + text.size())));
    AnimationClipAssetUVE loaded;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(path, loaded));
    EXPECT_EQ(loaded.clipId, "door");
    EXPECT_EQ(loaded.samples.size(), 1U);
    EXPECT_FALSE(loaded.IsSkeletalUVE());
    std::filesystem::remove(path);
}

} // namespace UVE::Asset::Tests
