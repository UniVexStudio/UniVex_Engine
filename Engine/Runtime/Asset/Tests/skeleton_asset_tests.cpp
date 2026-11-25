// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "skeleton_asset.h"

#include <filesystem>
#include <fstream>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "animation_clip_asset.h"
#include "uve_file_envelope.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPath(const char* const name) {
    return ::UVE::Tests::ScratchRoot() / name;
}

SkeletonAsset MakeValidSkeleton() {
    SkeletonAsset skeleton;
    skeleton.skeletonId = "humanoid";
    SkeletonJoint root;
    root.name = "Hips";
    root.parent = -1;
    root.position = Math::Vector3{0.0F, 1.0F, 0.0F};
    SkeletonJoint child;
    child.name = "Spine";
    child.parent = 0;
    child.position = Math::Vector3{0.0F, 0.5F, 0.0F};
    child.rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 2.0F}; // normalizable, not normalized
    SkeletonJoint grandchild;
    grandchild.name = "Head";
    grandchild.parent = 1;
    grandchild.position = Math::Vector3{0.0F, 0.5F, 0.0F};
    grandchild.scale = Math::Vector3{2.0F, 2.0F, 2.0F};
    skeleton.joints = {root, child, grandchild};
    return skeleton;
}

} // namespace

TEST(SkeletonAssetTest, ValidationAcceptsMinimalSingleJoint) {
    SkeletonAsset skeleton;
    skeleton.skeletonId = "prop";
    SkeletonJoint joint;
    joint.name = "Root";
    skeleton.joints = {joint};
    EXPECT_TRUE(IsSkeletonAssetValid(skeleton));
    EXPECT_TRUE(IsSkeletonAssetValid(MakeValidSkeleton()));
}

TEST(SkeletonAssetTest, AModelSkeletonBecomesAStandaloneSkeletonUnchanged) {
    // What a model file reads as a skeleton and what the engine persists as one are two shapes, and
    // the bridge between them is the only place the mapping is decided. A bone that comes back
    // re-oriented, re-scaled or re-parented would move every character already standing up.
    GltfSkeleton source;
    source.skinCount = 2U;
    GltfJoint hips;
    hips.name = "Hips";
    hips.parentIndex = -1;
    hips.translation = Math::Vector3{0.0F, 1.0F, 0.0F};
    GltfJoint spine;
    spine.name = "Spine";
    spine.parentIndex = 0;
    spine.translation = Math::Vector3{0.0F, 0.25F, 0.0F};
    spine.rotation = Math::Quaternion{0.0F, 0.0F, 0.7071068F, 0.7071068F};
    spine.scale = Math::Vector3{1.5F, 1.5F, 1.5F};
    source.joints = {hips, spine};

    const SkeletonAsset asset = MakeSkeletonAsset(source, "mixamo_hero");
    EXPECT_EQ(asset.skeletonId, "mixamo_hero");
    ASSERT_EQ(asset.joints.size(), source.joints.size());
    for (std::size_t index = 0U; index < source.joints.size(); ++index) {
        EXPECT_EQ(asset.joints[index].name, source.joints[index].name);
        EXPECT_EQ(asset.joints[index].parent, source.joints[index].parentIndex);
        EXPECT_EQ(asset.joints[index].position, source.joints[index].translation);
        EXPECT_EQ(asset.joints[index].rotation, source.joints[index].rotation);
        EXPECT_EQ(asset.joints[index].scale, source.joints[index].scale);
    }
    // Same numbers, and a descriptor the engine will accept for persistence - a bridge that mapped
    // the fields right but produced something unwritable would be no use to an importer.
    EXPECT_TRUE(IsSkeletonAssetValid(asset));

    // A skeleton with nothing in it maps to nothing, and validation is what refuses to write it.
    const SkeletonAsset empty = MakeSkeletonAsset(GltfSkeleton{}, "empty");
    EXPECT_TRUE(empty.joints.empty());
    EXPECT_FALSE(IsSkeletonAssetValid(empty));
}

TEST(SkeletonAssetTest, SaveThenLoad_RoundTripsJointsVerbatim) {
    const std::filesystem::path path = TestPath("uve_skeleton_asset_round_trip.uvskel");
    std::filesystem::remove(path);
    const SkeletonAsset original = MakeValidSkeleton();
    ASSERT_TRUE(SaveSkeletonAsset(original, path));

    SkeletonAsset loaded;
    ASSERT_TRUE(LoadSkeletonAsset(path, loaded));
    EXPECT_EQ(loaded.skeletonId, original.skeletonId);
    ASSERT_EQ(loaded.joints.size(), original.joints.size());
    EXPECT_EQ(loaded.joints[0], original.joints[0]);
    EXPECT_EQ(loaded.joints[2], original.joints[2]);
    // The middle joint's rotation normalizes on load (the clip asset's read contract).
    EXPECT_EQ(loaded.joints[1].name, original.joints[1].name);
    EXPECT_EQ(loaded.joints[1].parent, original.joints[1].parent);
    EXPECT_EQ(loaded.joints[1].rotation, Math::Quaternion{});
    std::filesystem::remove(path);
}

TEST(SkeletonAssetTest, SaveRejectsInvalidSkeletonWithoutPublishingDestination) {
    const std::filesystem::path path = TestPath("uve_skeleton_asset_invalid.uvskel");
    std::filesystem::remove(path);
    SkeletonAsset skeleton = MakeValidSkeleton();
    skeleton.skeletonId.clear();
    EXPECT_FALSE(SaveSkeletonAsset(skeleton, path));
    EXPECT_FALSE(std::filesystem::exists(path));
}

TEST(SkeletonAssetTest, ValidationRejects_BadIdentifiers) {
    SkeletonAsset emptyId = MakeValidSkeleton();
    emptyId.skeletonId.clear();
    EXPECT_FALSE(IsSkeletonAssetValid(emptyId));

    SkeletonAsset nulId = MakeValidSkeleton();
    nulId.skeletonId = std::string("a\0b", 3U);
    EXPECT_FALSE(IsSkeletonAssetValid(nulId));

    SkeletonAsset longId = MakeValidSkeleton();
    longId.skeletonId = std::string(kMaximumSkeletonAssetIdentifierBytes + 1U, 'x');
    EXPECT_FALSE(IsSkeletonAssetValid(longId));

    SkeletonAsset emptyName = MakeValidSkeleton();
    emptyName.joints[1].name.clear();
    EXPECT_FALSE(IsSkeletonAssetValid(emptyName));

    SkeletonAsset duplicateName = MakeValidSkeleton();
    duplicateName.joints[2].name = duplicateName.joints[0].name;
    EXPECT_FALSE(IsSkeletonAssetValid(duplicateName));

    SkeletonAsset nulName = MakeValidSkeleton();
    nulName.joints[0].name = std::string("H\0ips", 5U);
    EXPECT_FALSE(IsSkeletonAssetValid(nulName));
}

TEST(SkeletonAssetTest, ValidationRejects_BadHierarchy) {
    SkeletonAsset noJoints = MakeValidSkeleton();
    noJoints.joints.clear();
    EXPECT_FALSE(IsSkeletonAssetValid(noJoints));

    SkeletonAsset selfParent = MakeValidSkeleton();
    selfParent.joints[1].parent = 1;
    EXPECT_FALSE(IsSkeletonAssetValid(selfParent));

    SkeletonAsset forwardParent = MakeValidSkeleton();
    forwardParent.joints[0].parent = 2; // parents must precede children
    EXPECT_FALSE(IsSkeletonAssetValid(forwardParent));

    SkeletonAsset negativeParent = MakeValidSkeleton();
    negativeParent.joints[2].parent = -2;
    EXPECT_FALSE(IsSkeletonAssetValid(negativeParent));

    SkeletonAsset tooMany;
    tooMany.skeletonId = "crowd";
    tooMany.joints.resize(kMaximumSkeletonAssetJoints + 1U);
    for (std::size_t index = 0U; index < tooMany.joints.size(); ++index) {
        tooMany.joints[index].name = "joint" + std::to_string(index);
        tooMany.joints[index].parent = index == 0U ? -1 : 0;
    }
    EXPECT_FALSE(IsSkeletonAssetValid(tooMany));
}

TEST(SkeletonAssetTest, ValidationRejects_BadPoses) {
    SkeletonAsset badPosition = MakeValidSkeleton();
    badPosition.joints[0].position.x = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsSkeletonAssetValid(badPosition));

    SkeletonAsset zeroRotation = MakeValidSkeleton();
    zeroRotation.joints[0].rotation = Math::Quaternion{0.0F, 0.0F, 0.0F, 0.0F};
    EXPECT_FALSE(IsSkeletonAssetValid(zeroRotation));

    SkeletonAsset badScale = MakeValidSkeleton();
    badScale.joints[0].scale.y = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsSkeletonAssetValid(badScale));
}

TEST(SkeletonAssetTest, LoadRejects_WrongKindEnvelope) {
    // A clip envelope is structurally a valid .uve file but the wrong kind — and saving it
    // proves the Animation path is undisturbed by the new Skeleton kind beside it.
    const std::filesystem::path path = TestPath("uve_skeleton_asset_wrong_kind.uvanim");
    std::filesystem::remove(path);
    AnimationClipAsset clip;
    clip.clipId = "walk";
    clip.durationSeconds = 1.0;
    clip.samples = {AnimationAssetSample{0.0, AnimationAssetPose{}}};
    ASSERT_TRUE(SaveAnimationClipAsset(clip, path));

    SkeletonAsset skeleton;
    EXPECT_FALSE(LoadSkeletonAsset(path, skeleton));
    std::filesystem::remove(path);
}

TEST(SkeletonAssetTest, LoadRejects_MalformedPayload) {
    const std::filesystem::path garbage = TestPath("uve_skeleton_asset_garbage.uvskel");
    std::filesystem::remove(garbage);
    {
        std::ofstream stream(garbage, std::ios::binary);
        stream << "not a uve file";
    }
    SkeletonAsset skeleton;
    EXPECT_FALSE(LoadSkeletonAsset(garbage, skeleton));

    // Right kind, wrong bytes: proves Skeleton flows through the envelope bound (a stale
    // Animation-capped bound would reject the kind before the payload is even inspected).
    const std::filesystem::path badJson = TestPath("uve_skeleton_asset_bad_json.uvskel");
    std::filesystem::remove(badJson);
    const std::string payload = "{\"schema\":\"uve-skeleton-v1\"}"; // no skeletonId/joints
    const auto* const bytes = reinterpret_cast<const std::byte*>(payload.data());
    ASSERT_TRUE(
        WriteUveFile(badJson, AssetKind::Skeleton, std::vector<std::byte>(bytes, bytes + payload.size())));
    EXPECT_FALSE(LoadSkeletonAsset(badJson, skeleton));

    std::filesystem::remove(garbage);
    std::filesystem::remove(badJson);
}

} // namespace UVE::Asset::Tests
