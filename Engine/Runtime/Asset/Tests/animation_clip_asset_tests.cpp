// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip_asset.h"

#include <filesystem>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPath(const char* const name) {
    return ::UVE::Tests::ScratchRoot() / name;
}

AnimationClipAsset MakeValidClip() {
    AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {
        AnimationAssetSample{0.0, AnimationAssetPose{Math::Vector3{0.0F, 0.0F, 0.0F},
                                                            Math::Quaternion{},
                                                            Math::Vector3{1.0F, 1.0F, 1.0F}}},
        AnimationAssetSample{1.0, AnimationAssetPose{Math::Vector3{1.0F, 0.0F, 0.0F},
                                                            Math::Quaternion{},
                                                            Math::Vector3{1.0F, 1.0F, 1.0F}}},
    };
    clip.events = {AnimationAssetEvent{0.5, "footstep"}};
    return clip;
}

} // namespace

TEST(AnimationClipAssetTest, SaveThenLoad_RoundTripsBoundedPoseSamplesAndEvents) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_round_trip.uvanim");
    std::filesystem::remove(path);
    const AnimationClipAsset original = MakeValidClip();
    ASSERT_TRUE(SaveAnimationClipAsset(original, path));

    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    EXPECT_EQ(loaded.clipId, original.clipId);
    EXPECT_EQ(loaded.durationSeconds, original.durationSeconds);
    ASSERT_EQ(loaded.samples.size(), original.samples.size());
    EXPECT_EQ(loaded.samples[0], original.samples[0]);
    EXPECT_EQ(loaded.samples[1], original.samples[1]);
    ASSERT_EQ(loaded.events.size(), 1U);
    EXPECT_EQ(loaded.events.front(), original.events.front());
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, SaveRejectsInvalidClipWithoutPublishingDestination) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_invalid.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset invalid = MakeValidClip();
    invalid.samples[1].timeSeconds = 1.25;
    EXPECT_FALSE(SaveAnimationClipAsset(invalid, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(AnimationClipAssetTest, LoadWrongEnvelopeKindPreservesExistingOutput) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_wrong_kind.uvanim");
    std::filesystem::remove(path);
    const std::vector<std::byte> payload{std::byte{'{'}, std::byte{'}'}};
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Mesh, payload));
    AnimationClipAsset retained = MakeValidClip();
    const AnimationClipAsset original = retained;
    EXPECT_FALSE(LoadAnimationClipAsset(path, retained));
    EXPECT_EQ(retained.clipId, original.clipId);
    EXPECT_EQ(retained.samples, original.samples);
    EXPECT_EQ(retained.events, original.events);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, SkeletalClipRoundTripsItsBoneTracks) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_skeletal.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPose rest{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Quaternion{},
                                     Math::Vector3{1.0F, 1.0F, 1.0F}};
    AnimationAssetPose forward = rest;
    forward.position.z = 0.4F;
    clip.bones = {AnimationAssetBoneTrack{"Hips", {{0.0, rest}, {0.5, forward}}},
                  AnimationAssetBoneTrack{"Spine", {{0.0, rest}}}};
    ASSERT_TRUE(IsAnimationClipAssetValid(clip)) << "bones alone are enough: no object track needed";
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));
    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    EXPECT_TRUE(loaded.IsSkeletal());
    EXPECT_TRUE(loaded.samples.empty());
    EXPECT_EQ(loaded.bones, clip.bones);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, RestSkeletonAndConformedFlagRoundTripAsVersionThree) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_rest.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPose pose{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Quaternion{},
                                     Math::Vector3{1.0F, 1.0F, 1.0F}};
    clip.bones = {AnimationAssetBoneTrack{"Hips", {{0.0, pose}}}, AnimationAssetBoneTrack{"Spine", {{0.0, pose}}}};
    clip.rest = {AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}},
                 AnimationAssetRestBone{"Spine", 0, {0.0F, 0.1F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    clip.conformed = true;
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));
    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    EXPECT_EQ(loaded.rest, clip.rest);
    EXPECT_TRUE(loaded.conformed);

    // A clip with neither still saves as version two, so old readers and old files are unchanged.
    clip.rest.clear();
    clip.conformed = false;
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    EXPECT_TRUE(loaded.rest.empty());
    EXPECT_FALSE(loaded.conformed);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, RestSkeletonMustBeParentsFirstWithUniqueNames) {
    AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 0.5;
    const AnimationAssetPose pose{};
    clip.bones = {AnimationAssetBoneTrack{"Hips", {{0.0, pose}}}};
    clip.rest = {AnimationAssetRestBone{"Hips", -1, {}, {}, {1.0F, 1.0F, 1.0F}},
                 AnimationAssetRestBone{"Spine", 0, {}, {}, {1.0F, 1.0F, 1.0F}}};
    EXPECT_TRUE(IsAnimationClipAssetValid(clip));
    clip.rest[1].parent = 1; // its own index: not a parent that comes first
    EXPECT_FALSE(IsAnimationClipAssetValid(clip));
    clip.rest[1].parent = 0;
    clip.rest[1].bone = "Hips"; // a repeated name
    EXPECT_FALSE(IsAnimationClipAssetValid(clip));
}

TEST(AnimationClipAssetTest, BoneTracksAreValidatedLikeTheObjectTrack) {
    AnimationClipAsset clip;
    clip.clipId = "run";
    clip.durationSeconds = 1.0;
    const AnimationAssetPose rest{};
    clip.bones = {AnimationAssetBoneTrack{"Hips", {{0.0, rest}}}};
    EXPECT_TRUE(IsAnimationClipAssetValid(clip));

    AnimationClipAsset twice = clip;
    twice.bones.push_back(AnimationAssetBoneTrack{"Hips", {{0.5, rest}}});
    EXPECT_FALSE(IsAnimationClipAssetValid(twice)) << "one track per bone";

    AnimationClipAsset late = clip;
    late.bones[0].samples[0].timeSeconds = 2.0;
    EXPECT_FALSE(IsAnimationClipAssetValid(late)) << "past the end";

    AnimationClipAsset unnamed = clip;
    unnamed.bones[0].bone.clear();
    EXPECT_FALSE(IsAnimationClipAssetValid(unnamed));

    AnimationClipAsset empty = clip;
    empty.bones[0].samples.clear();
    EXPECT_FALSE(IsAnimationClipAssetValid(empty)) << "nothing moves";
}

TEST(AnimationClipAssetTest, AVersionOneFileStillLoads) {
    const std::filesystem::path path = TestPath("uve_animation_clip_asset_v1.uvanim");
    std::filesystem::remove(path);
    const std::string text =
        R"({"schema":"uve-animation-v1","clipId":"door","durationSeconds":1.0,"events":[],"samples":[)"
        R"({"timeSeconds":0.0,"pose":{"position":[0,0,0],"rotation":[0,0,0,1],"scale":[1,1,1]}}]})";
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Animation, std::vector<std::byte>(bytes, bytes + text.size())));
    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    EXPECT_EQ(loaded.clipId, "door");
    EXPECT_EQ(loaded.samples.size(), 1U);
    EXPECT_FALSE(loaded.IsSkeletal());
    std::filesystem::remove(path);
}

/// A facial clip: no object samples, no bones, only two shapes on a "Head" mesh. This is the shape
/// of a real lip-sync take, and it is why the validator's "nothing moves" rule has to count morph
/// tracks as movement.
[[nodiscard]] AnimationClipAsset MakeFacialClip() {
    AnimationClipAsset clip;
    clip.clipId = "visemes";
    clip.durationSeconds = 0.5;
    AnimationAssetMorphTrack track;
    track.node = "Head";
    track.samples = {
        AnimationAssetMorphWeightSample{0.0, {0.0F, 1.0F}},
        AnimationAssetMorphWeightSample{0.5, {1.0F, 0.0F}},
    };
    clip.morphWeights = {std::move(track)};
    return clip;
}

TEST(AnimationClipAssetTest, FacialClipWithOnlyMorphTracksIsValid) {
    const AnimationClipAsset clip = MakeFacialClip();
    EXPECT_TRUE(clip.samples.empty());
    EXPECT_TRUE(clip.bones.empty());
    EXPECT_TRUE(clip.HasMorphTracks());
    EXPECT_TRUE(IsAnimationClipAssetValid(clip));
}

TEST(AnimationClipAssetTest, MorphTracksRoundTripAsVersionFour) {
    const std::filesystem::path path = TestPath("morph_track_round_trip.uvanim");
    std::filesystem::remove(path);
    const AnimationClipAsset original = MakeFacialClip();
    ASSERT_TRUE(SaveAnimationClipAsset(original, path));

    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    ASSERT_EQ(loaded.morphWeights.size(), 1U);
    EXPECT_EQ(loaded.morphWeights[0].node, "Head");
    EXPECT_EQ(loaded.morphWeights[0].samples, original.morphWeights[0].samples);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, ClipWithoutMorphTracksStaysOnItsOldSchema) {
    // The schema version is a promise about what an older engine can read. A clip with no blend
    // shapes must not claim v4 merely because the format learned about them.
    const std::filesystem::path path = TestPath("morph_free_clip.uvanim");
    std::filesystem::remove(path);
    const AnimationClipAsset clip = MakeValidClip();
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));

    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    ASSERT_TRUE(file.has_value());
    const std::string payload(reinterpret_cast<const char*>(file->second.data()), file->second.size());
    EXPECT_NE(payload.find("uve-animation-v2"), std::string::npos);
    EXPECT_EQ(payload.find("morphWeights"), std::string::npos);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, MorphTracksAreValidated) {
    // Keys must be ordered and inside the clip.
    AnimationClipAsset outOfOrder = MakeFacialClip();
    std::swap(outOfOrder.morphWeights[0].samples[0], outOfOrder.morphWeights[0].samples[1]);
    EXPECT_FALSE(IsAnimationClipAssetValid(outOfOrder));

    AnimationClipAsset pastTheEnd = MakeFacialClip();
    pastTheEnd.morphWeights[0].samples[1].timeSeconds = 9.0;
    EXPECT_FALSE(IsAnimationClipAssetValid(pastTheEnd));

    // Every key of a track drives the same shapes; a key that disagrees cannot be interpolated.
    AnimationClipAsset raggedWeights = MakeFacialClip();
    raggedWeights.morphWeights[0].samples[1].weights.pop_back();
    EXPECT_FALSE(IsAnimationClipAssetValid(raggedWeights));

    AnimationClipAsset nonFinite = MakeFacialClip();
    nonFinite.morphWeights[0].samples[0].weights[0] = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsAnimationClipAssetValid(nonFinite));

    AnimationClipAsset emptyNode = MakeFacialClip();
    emptyNode.morphWeights[0].node.clear();
    EXPECT_FALSE(IsAnimationClipAssetValid(emptyNode));

    AnimationClipAsset noKeys = MakeFacialClip();
    noKeys.morphWeights[0].samples.clear();
    EXPECT_FALSE(IsAnimationClipAssetValid(noKeys));

    // One track per mesh.
    AnimationClipAsset duplicated = MakeFacialClip();
    duplicated.morphWeights.push_back(duplicated.morphWeights[0]);
    EXPECT_FALSE(IsAnimationClipAssetValid(duplicated));
}

TEST(AnimationClipAssetTest, AClipWithNoMovementAtAllIsStillRejected) {
    // The relaxation for facial clips must not turn into "anything is valid": a clip that moves
    // nothing at all is still a defect.
    AnimationClipAsset empty;
    empty.clipId = "nothing";
    empty.durationSeconds = 1.0;
    EXPECT_FALSE(IsAnimationClipAssetValid(empty));
}

} // namespace UVE::Asset::Tests

namespace UVE::Asset::Tests {

TEST(AnimationClipAssetTest, OtherObjectTracksRoundTripAndOnlyAClipWithThemWritesThem) {
    const std::filesystem::path path = TestPath("object_tracks_round_trip.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset clip = MakeValidClip();
    AnimationAssetBoneTrack wheel;
    wheel.bone = "Wheel";
    wheel.samples = clip.samples;
    clip.objects.push_back(wheel);
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));
    AnimationClipAsset loaded;
    ASSERT_TRUE(LoadAnimationClipAsset(path, loaded));
    ASSERT_EQ(loaded.objects.size(), 1U);
    EXPECT_EQ(loaded.objects[0], wheel);
    std::filesystem::remove(path);

    // Only other objects move: still a clip.
    AnimationClipAsset onlyObjects = clip;
    onlyObjects.samples.clear();
    EXPECT_TRUE(IsAnimationClipAssetValid(onlyObjects));
    // One track per object.
    AnimationClipAsset twice = clip;
    twice.objects.push_back(wheel);
    EXPECT_FALSE(IsAnimationClipAssetValid(twice));

    // A clip without them: no "objects" in the file.
    ASSERT_TRUE(SaveAnimationClipAsset(MakeValidClip(), path));
    const std::optional<std::pair<UveFileHeader, std::vector<std::byte>>> file = ReadUveFile(path);
    ASSERT_TRUE(file.has_value());
    const std::string payload(reinterpret_cast<const char*>(file->second.data()), file->second.size());
    EXPECT_EQ(payload.find("\"objects\""), std::string::npos);
    std::filesystem::remove(path);
}

TEST(AnimationClipAssetTest, InspectSummaryMatchesFullLoadAcrossSchemasAndStates) {
    const std::filesystem::path dir = ::UVE::Tests::MakeTestCaseDirectory();
    // 1. Object-only v2 clip (no rest, no bones).
    const std::filesystem::path objPath = dir / "obj.uvanim";
    ASSERT_TRUE(SaveAnimationClipAsset(MakeValidClip(), objPath));
    const AnimationClipSummary objSummary = InspectAnimationClipFileSummary(objPath);
    EXPECT_TRUE(objSummary.valid);
    EXPECT_FALSE(objSummary.conformed);
    EXPECT_FALSE(objSummary.hasRest);
    EXPECT_FALSE(objSummary.hasBones);

    // 2. Skeletal v3 clip with rest skeleton, unconformed.
    AnimationClipAsset skel;
    skel.clipId = "walk";
    skel.durationSeconds = 0.5;
    const AnimationAssetPose pose{{0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}};
    skel.bones = {AnimationAssetBoneTrack{"Hips", {{0.0, pose}, {0.5, pose}}}};
    skel.rest = {AnimationAssetRestBone{"Hips", -1, {0.0F, 1.0F, 0.0F}, {}, {1.0F, 1.0F, 1.0F}}};
    skel.conformed = false;
    const std::filesystem::path readyPath = dir / "ready.uvanim";
    ASSERT_TRUE(SaveAnimationClipAsset(skel, readyPath));
    const AnimationClipSummary readySummary = InspectAnimationClipFileSummary(readyPath);
    EXPECT_TRUE(readySummary.valid);
    EXPECT_FALSE(readySummary.conformed);
    EXPECT_TRUE(readySummary.hasRest);
    EXPECT_TRUE(readySummary.hasBones);

    // 3. Skeletal v3 clip already conformed.
    skel.conformed = true;
    const std::filesystem::path donePath = dir / "done.uvanim";
    ASSERT_TRUE(SaveAnimationClipAsset(skel, donePath));
    const AnimationClipSummary doneSummary = InspectAnimationClipFileSummary(donePath);
    EXPECT_TRUE(doneSummary.valid);
    EXPECT_TRUE(doneSummary.conformed);
    EXPECT_TRUE(doneSummary.hasRest);
    EXPECT_TRUE(doneSummary.hasBones);

    // 4. Corrupt payload reports invalid.
    const std::vector<std::byte> junk{std::byte{'{'}, std::byte{'!'}};
    EXPECT_FALSE(InspectAnimationClipSummary(junk).valid);
    EXPECT_FALSE(InspectAnimationClipFileSummary(dir / "missing.uvanim").valid);
}

} // namespace UVE::Asset::Tests
