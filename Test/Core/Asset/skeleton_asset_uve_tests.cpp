// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/skeleton_asset_uve.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPathUVE(const char* const name) {
    return ::UVE::Tests::ScratchRootUVE() / name;
}

SkeletonAssetUVE MakeValidSkeletonUVE() {
    SkeletonAssetUVE skeleton;
    skeleton.skeletonId = "humanoid";
    SkeletonJointUVE root;
    root.name = "Hips";
    root.parent = -1;
    root.position = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    SkeletonJointUVE child;
    child.name = "Spine";
    child.parent = 0;
    child.position = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
    child.rotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 2.0F}; // normalizable, not normalized
    SkeletonJointUVE grandchild;
    grandchild.name = "Head";
    grandchild.parent = 1;
    grandchild.position = Math::Vector3UVE{0.0F, 0.5F, 0.0F};
    grandchild.scale = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    skeleton.joints = {root, child, grandchild};
    return skeleton;
}

} // namespace

TEST(SkeletonAssetUVETest, ValidationAcceptsMinimalSingleJoint) {
    SkeletonAssetUVE skeleton;
    skeleton.skeletonId = "prop";
    SkeletonJointUVE joint;
    joint.name = "Root";
    skeleton.joints = {joint};
    EXPECT_TRUE(IsSkeletonAssetValidUVE(skeleton));
    EXPECT_TRUE(IsSkeletonAssetValidUVE(MakeValidSkeletonUVE()));
}

TEST(SkeletonAssetUVETest, SaveThenLoad_RoundTripsJointsVerbatim) {
    const std::filesystem::path path = TestPathUVE("uve_skeleton_asset_round_trip.uvskel");
    std::filesystem::remove(path);
    const SkeletonAssetUVE original = MakeValidSkeletonUVE();
    ASSERT_TRUE(SaveSkeletonAssetUVE(original, path));

    SkeletonAssetUVE loaded;
    ASSERT_TRUE(LoadSkeletonAssetUVE(path, loaded));
    EXPECT_EQ(loaded.skeletonId, original.skeletonId);
    ASSERT_EQ(loaded.joints.size(), original.joints.size());
    EXPECT_EQ(loaded.joints[0], original.joints[0]);
    EXPECT_EQ(loaded.joints[2], original.joints[2]);
    // The middle joint's rotation normalizes on load (the clip asset's read contract).
    EXPECT_EQ(loaded.joints[1].name, original.joints[1].name);
    EXPECT_EQ(loaded.joints[1].parent, original.joints[1].parent);
    EXPECT_EQ(loaded.joints[1].rotation, Math::QuaternionUVE{});
    std::filesystem::remove(path);
}

TEST(SkeletonAssetUVETest, SaveRejectsInvalidSkeletonWithoutPublishingDestination) {
    const std::filesystem::path path = TestPathUVE("uve_skeleton_asset_invalid.uvskel");
    std::filesystem::remove(path);
    SkeletonAssetUVE skeleton = MakeValidSkeletonUVE();
    skeleton.skeletonId.clear();
    EXPECT_FALSE(SaveSkeletonAssetUVE(skeleton, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(SkeletonAssetUVETest, ValidationRejects_BadIdentifiers) {
    SkeletonAssetUVE emptyId = MakeValidSkeletonUVE();
    emptyId.skeletonId.clear();
    EXPECT_FALSE(IsSkeletonAssetValidUVE(emptyId));

    SkeletonAssetUVE nulId = MakeValidSkeletonUVE();
    nulId.skeletonId = std::string("a\0b", 3U);
    EXPECT_FALSE(IsSkeletonAssetValidUVE(nulId));

    SkeletonAssetUVE longId = MakeValidSkeletonUVE();
    longId.skeletonId = std::string(kMaximumSkeletonAssetIdentifierBytesUVE + 1U, 'x');
    EXPECT_FALSE(IsSkeletonAssetValidUVE(longId));

    SkeletonAssetUVE emptyName = MakeValidSkeletonUVE();
    emptyName.joints[1].name.clear();
    EXPECT_FALSE(IsSkeletonAssetValidUVE(emptyName));

    SkeletonAssetUVE duplicateName = MakeValidSkeletonUVE();
    duplicateName.joints[2].name = duplicateName.joints[0].name;
    EXPECT_FALSE(IsSkeletonAssetValidUVE(duplicateName));

    SkeletonAssetUVE nulName = MakeValidSkeletonUVE();
    nulName.joints[0].name = std::string("H\0ips", 5U);
    EXPECT_FALSE(IsSkeletonAssetValidUVE(nulName));
}

TEST(SkeletonAssetUVETest, ValidationRejects_BadHierarchy) {
    SkeletonAssetUVE noJoints = MakeValidSkeletonUVE();
    noJoints.joints.clear();
    EXPECT_FALSE(IsSkeletonAssetValidUVE(noJoints));

    SkeletonAssetUVE selfParent = MakeValidSkeletonUVE();
    selfParent.joints[1].parent = 1;
    EXPECT_FALSE(IsSkeletonAssetValidUVE(selfParent));

    SkeletonAssetUVE forwardParent = MakeValidSkeletonUVE();
    forwardParent.joints[0].parent = 2; // parents must precede children
    EXPECT_FALSE(IsSkeletonAssetValidUVE(forwardParent));

    SkeletonAssetUVE negativeParent = MakeValidSkeletonUVE();
    negativeParent.joints[2].parent = -2;
    EXPECT_FALSE(IsSkeletonAssetValidUVE(negativeParent));

    SkeletonAssetUVE tooMany;
    tooMany.skeletonId = "crowd";
    tooMany.joints.resize(kMaximumSkeletonAssetJointsUVE + 1U);
    for (std::size_t index = 0U; index < tooMany.joints.size(); ++index) {
        tooMany.joints[index].name = "joint" + std::to_string(index);
        tooMany.joints[index].parent = index == 0U ? -1 : 0;
    }
    EXPECT_FALSE(IsSkeletonAssetValidUVE(tooMany));
}

TEST(SkeletonAssetUVETest, ValidationRejects_BadPoses) {
    SkeletonAssetUVE badPosition = MakeValidSkeletonUVE();
    badPosition.joints[0].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSkeletonAssetValidUVE(badPosition));

    SkeletonAssetUVE zeroRotation = MakeValidSkeletonUVE();
    zeroRotation.joints[0].rotation = Math::QuaternionUVE{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(IsSkeletonAssetValidUVE(zeroRotation));

    SkeletonAssetUVE badScale = MakeValidSkeletonUVE();
    badScale.joints[0].scale.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSkeletonAssetValidUVE(badScale));
}

TEST(SkeletonAssetUVETest, LoadRejects_WrongKindEnvelope) {
    // A clip envelope is structurally a valid .uve file but the wrong kind — and saving it
    // proves the Animation path is undisturbed by the new Skeleton kind beside it.
    const std::filesystem::path path = TestPathUVE("uve_skeleton_asset_wrong_kind.uvanim");
    std::filesystem::remove(path);
    AnimationClipAssetUVE clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {AnimationAssetSampleUVE{0.0, AnimationAssetPoseUVE{}}};
    ASSERT_TRUE(SaveAnimationClipAssetUVE(clip, path));

    SkeletonAssetUVE skeleton;
    EXPECT_FALSE(LoadSkeletonAssetUVE(path, skeleton));
    std::filesystem::remove(path);
}

TEST(SkeletonAssetUVETest, LoadRejects_MalformedPayload) {
    const std::filesystem::path garbage = TestPathUVE("uve_skeleton_asset_garbage.uvskel");
    std::filesystem::remove(garbage);
    {
        std::ofstream stream(garbage, std::ios::binary);
        stream << "not a uve file";
    }
    SkeletonAssetUVE skeleton;
    EXPECT_FALSE(LoadSkeletonAssetUVE(garbage, skeleton));

    // Right kind, wrong bytes: proves Skeleton flows through the envelope bound (a stale
    // Animation-capped bound would reject the kind before the payload is even inspected).
    const std::filesystem::path badJson = TestPathUVE("uve_skeleton_asset_bad_json.uvskel");
    std::filesystem::remove(badJson);
    const std::string payload = "{\"schema\":\"uve-skeleton-v1\"}"; // no skeletonId/joints
    const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
    ASSERT_TRUE(
        WriteUveFileUVE(badJson, AssetKindUVE::Skeleton, std::vector<std::byte>(bytes, bytes + payload.size())));
    EXPECT_FALSE(LoadSkeletonAssetUVE(badJson, skeleton));

    std::filesystem::remove(garbage);
    std::filesystem::remove(badJson);
}

} // namespace UVE::Asset::Tests
