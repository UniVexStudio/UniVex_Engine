// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "Retarget/retarget_test_rig_uve.h"
#include "uve/retarget/retarget_conform_uve.h"

namespace UVE::Retarget {
namespace {

using Math::QuaternionUVE;
using Math::Vector3UVE;
using namespace TestRigUVE;

void ExpectNear(const Vector3UVE& actual, const Vector3UVE& expected, const float tolerance, const std::string& what) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, tolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, tolerance) << what;
}

struct ConformedFixtureUVE final {
    TPoseRigUVE rig;
    ConformedRigUVE conformed;
    std::vector<WorldTransformUVE> world;

    ConformedFixtureUVE() {
        const HumanoidMatchUVE match = MatchHumanoidUVE(rig.skeleton, GetHumanoidReferenceUVE());
        std::string error;
        const std::optional<ConformedRigUVE> result = ConformSkeletonUVE(rig.skeleton, match, GetHumanoidReferenceUVE(), &error);
        EXPECT_TRUE(result.has_value()) << error;
        if (result.has_value()) {
            conformed = *result;
            world = ComputeWorldTransformsUVE(conformed.skeleton);
        }
    }

    [[nodiscard]] Vector3UVE At(const std::string& name) const {
        const std::int32_t bone = FindBoneUVE(conformed.skeleton, name);
        EXPECT_GE(bone, 0) << name;
        return bone < 0 ? Vector3UVE{} : world[static_cast<std::size_t>(bone)].position;
    }
};

[[nodiscard]] Vector3UVE ReferenceAt(const std::string& name) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    return ComputeWorldTransformsUVE(reference.skeleton)[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, name))].position;
}

TEST(RetargetConformUVETest, ConformSkeletonUVE_GivesTheRigTheHumanoidsBonesNamesAndFrames) {
    const ConformedFixtureUVE fixture;
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const ConformedRigUVE& conformed = fixture.conformed;
    ASSERT_EQ(conformed.skeleton.bones.size(), reference.skeleton.bones.size() + 1U);
    ASSERT_TRUE(IsRetargetSkeletonValidUVE(conformed.skeleton));
    EXPECT_NEAR(conformed.heightScale, 1.1F, 1e-3F);
    const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const RetargetBoneUVE& bone = conformed.skeleton.bones[index];
        EXPECT_EQ(bone.name, reference.skeleton.bones[index].name);
        EXPECT_EQ(bone.parent, reference.skeleton.bones[index].parent) << bone.name;
        EXPECT_EQ(conformed.referenceOfBone[index], static_cast<std::int32_t>(index));
        // Every humanoid bone's frame is the reference's, whatever frame the rig used.
        const QuaternionUVE& a = fixture.world[index].rotation;
        const QuaternionUVE& b = referenceWorld[index].rotation;
        EXPECT_NEAR(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 1.0F, 1e-4F) << bone.name;
    }
    // The rig's own end bone is kept, under the head.
    const RetargetBoneUVE& kept = conformed.skeleton.bones.back();
    EXPECT_EQ(kept.name, "HeadTop_End");
    EXPECT_EQ(kept.parent, FindBoneUVE(conformed.skeleton, "Head"));
    EXPECT_FALSE(conformed.added.back());
    EXPECT_TRUE(conformed.added[static_cast<std::size_t>(FindBoneUVE(conformed.skeleton, "Spine2"))]);
    EXPECT_FALSE(conformed.added[static_cast<std::size_t>(FindBoneUVE(conformed.skeleton, "Spine3"))]);
}

TEST(RetargetConformUVETest, ConformSkeletonUVE_LowersTheArmsToTheAPoseKeepingTheirLength) {
    const ConformedFixtureUVE fixture;
    for (const auto& [side, source] : {std::pair{std::string{"_L"}, std::string{"Left"}}, std::pair{std::string{"_R"}, std::string{"Right"}}}) {
        for (const auto& [from, to, rigFrom, rigTo] :
             {std::tuple{"UpperArm", "ForeArm", "Arm", "ForeArm"}, std::tuple{"ForeArm", "Hand", "ForeArm", "Hand"}}) {
            const Vector3UVE conformedBone = fixture.At(std::string{to} + side) - fixture.At(std::string{from} + side);
            const Vector3UVE referenceBone = ReferenceAt(std::string{to} + side) - ReferenceAt(std::string{from} + side);
            ExpectNear(Math::NormalizeUVE(conformedBone), Math::NormalizeUVE(referenceBone), 1e-4F, std::string{from} + side);
            EXPECT_NEAR(Math::LengthUVE(conformedBone),
                        Math::LengthUVE(fixture.rig.At(source + rigTo) - fixture.rig.At(source + rigFrom)), 1e-4F);
        }
    }
    // The shoulders stay where the rig had them.
    ExpectNear(fixture.At("Clavicle_L"), fixture.rig.At("LeftShoulder"), 1e-4F, "clavicle");
}

TEST(RetargetConformUVETest, ConformSkeletonUVE_FitsTheBonesTheRigLacksToItsOwnLimbs) {
    const ConformedFixtureUVE fixture;
    // A spine bone between its neighbours, where the humanoid has it along the chain.
    const float below = Math::LengthUVE(fixture.At("Spine2") - fixture.At("Spine1"));
    const float above = Math::LengthUVE(fixture.At("Spine3") - fixture.At("Spine2"));
    EXPECT_NEAR(below + above, Math::LengthUVE(fixture.At("Spine3") - fixture.At("Spine1")), 2e-3F);
    const float referenceBelow = Math::LengthUVE(ReferenceAt("Spine2") - ReferenceAt("Spine1"));
    const float referenceAbove = Math::LengthUVE(ReferenceAt("Spine3") - ReferenceAt("Spine2"));
    EXPECT_NEAR(below / (below + above), referenceBelow / (referenceBelow + referenceAbove), 1e-3F);
    // A twist bone along the upper arm, at the humanoid's fraction of it.
    const Vector3UVE upper = fixture.At("ForeArm_L") - fixture.At("UpperArm_L");
    const Vector3UVE twist = fixture.At("UpperArmTwist1_L") - fixture.At("UpperArm_L");
    const Vector3UVE referenceUpper = ReferenceAt("ForeArm_L") - ReferenceAt("UpperArm_L");
    const Vector3UVE referenceTwist = ReferenceAt("UpperArmTwist1_L") - ReferenceAt("UpperArm_L");
    EXPECT_NEAR(Math::DotUVE(twist, upper) / Math::DotUVE(upper, upper),
                Math::DotUVE(referenceTwist, referenceUpper) / Math::DotUVE(referenceUpper, referenceUpper), 1e-3F);
    // IK targets on what they follow; the root on the ground under the hips.
    ExpectNear(fixture.At("IKFoot_L"), fixture.At("Foot_L"), 1e-5F, "ik foot");
    ExpectNear(fixture.At("IKHand_R"), fixture.At("Hand_R"), 1e-5F, "ik hand");
    ExpectNear(fixture.At("IKFootRoot"), fixture.At("Root"), 1e-5F, "ik root");
    const float toe = std::min(fixture.At("Toe_L").y, fixture.At("Foot_L").y);
    EXPECT_NEAR(fixture.At("Root").y, toe - ReferenceAt("Toe_L").y * 1.1F, 5e-3F);
    // Each of the rig's bones moved rigidly into its conformed place.
    const std::vector<WorldTransformUVE> rigWorld = ComputeWorldTransformsUVE(fixture.rig.skeleton);
    for (std::size_t bone = 0U; bone < rigWorld.size(); ++bone) {
        const auto index = static_cast<std::size_t>(fixture.conformed.boneOfRigBone[bone]);
        ExpectNear(fixture.conformed.moveOfRigBone[bone].ApplyUVE(rigWorld[bone].position), fixture.world[index].position, 1e-4F,
                   fixture.rig.skeleton.bones[bone].name);
    }
}

TEST(RetargetConformUVETest, ConformSkeletonUVE_RefusesARigWithoutHips) {
    RetargetSkeletonUVE rig;
    rig.bones.push_back(RetargetBoneUVE{"Thing", -1, {}, {}, 1.0F});
    std::string error;
    const HumanoidMatchUVE match = MatchHumanoidUVE(rig, GetHumanoidReferenceUVE());
    EXPECT_FALSE(ConformSkeletonUVE(rig, match, GetHumanoidReferenceUVE(), &error).has_value());
    EXPECT_NE(error.find("hips"), std::string::npos) << error;
}

TEST(RetargetConformUVETest, ConformMeshUVE_CarriesTheSkinIntoTheAPoseAndRebindsIt) {
    const TPoseRigUVE rig;
    Asset::MeshAssetUVE mesh = MakeMeshUVE(rig);
    std::string error;
    const std::optional<RetargetSkeletonUVE> fromMesh = RigFromMeshUVE(mesh, &error);
    ASSERT_TRUE(fromMesh.has_value()) << error;
    const std::vector<WorldTransformUVE> fromMeshWorld = ComputeWorldTransformsUVE(*fromMesh);
    for (std::size_t index = 0U; index < rig.world.size(); ++index) {
        ExpectNear(fromMeshWorld[index].position, rig.world[index].position, 1e-4F, fromMesh->bones[index].name);
    }

    const HumanoidMatchUVE match = MatchHumanoidUVE(*fromMesh, GetHumanoidReferenceUVE());
    const std::optional<ConformedRigUVE> conformed = ConformSkeletonUVE(*fromMesh, match, GetHumanoidReferenceUVE(), &error);
    ASSERT_TRUE(conformed.has_value()) << error;
    ASSERT_TRUE(ConformMeshUVE(mesh, *conformed, &error)) << error;
    ASSERT_TRUE(Asset::IsMeshSkinningDataValidUVE(mesh));
    ASSERT_EQ(mesh.joints.size(), conformed->skeleton.bones.size());
    EXPECT_EQ(mesh.joints[static_cast<std::size_t>(FindBoneUVE(conformed->skeleton, "UpperArm_L"))].name, "UpperArm_L");
    EXPECT_EQ(mesh.skinningInfluences[0].joints[0], static_cast<std::uint32_t>(FindBoneUVE(conformed->skeleton, "UpperArm_L")));

    // The vertex went down with the arm: still half way along it.
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(conformed->skeleton);
    const auto at = [&](const char* name) { return world[static_cast<std::size_t>(FindBoneUVE(conformed->skeleton, name))].position; };
    ExpectNear(mesh.vertices[0].position, (at("UpperArm_L") + at("ForeArm_L")) * 0.5F, 1e-4F, "vertex");
    // At the new rest every joint's skinning matrix is the identity: nothing moves until posed.
    for (std::size_t index = 0U; index < mesh.joints.size(); ++index) {
        const Math::Matrix4x4UVE skin =
            Math::Matrix4x4UVE::ComposeTrsUVE(world[index].position, world[index].rotation, {1.0F, 1.0F, 1.0F}) *
            mesh.joints[index].inverseBindMatrix;
        ExpectNear(Math::TransformPointUVE(skin, {0.3F, -0.2F, 0.5F}), {0.3F, -0.2F, 0.5F}, 1e-4F, mesh.joints[index].name);
    }
}

TEST(RetargetConformUVETest, ConformClipUVE_KeepsEveryBoneWhereTheClipPutItInTheHumanoidsNames) {
    const ConformedFixtureUVE fixture;
    const TPoseRigUVE& rig = fixture.rig;
    // A clip on the rig: the hips travel and the left forearm bends over two frames.
    Asset::AnimationClipAssetUVE clip;
    clip.clipId = "Wave";
    clip.durationSeconds = 1.0;
    for (std::size_t bone = 0U; bone < rig.skeleton.bones.size(); ++bone) {
        Asset::AnimationAssetBoneTrackUVE track;
        track.bone = rig.skeleton.bones[bone].name;
        for (int frame = 0; frame < 3; ++frame) {
            Asset::AnimationAssetSampleUVE sample;
            sample.timeSeconds = 0.5 * frame;
            sample.pose.position = rig.skeleton.bones[bone].position;
            sample.pose.rotation = rig.skeleton.bones[bone].rotation;
            if (track.bone == "Hips") {
                sample.pose.position = sample.pose.position + Vector3UVE{0.0F, -0.05F, 0.4F} * static_cast<float>(frame);
            }
            if (track.bone == "LeftForeArm") {
                sample.pose.rotation = Math::MultiplyUVE(sample.pose.rotation, AxisAngle({0.0F, 1.0F, 0.0F}, 0.6F * static_cast<float>(frame)));
            }
            track.samples.push_back(sample);
        }
        clip.bones.push_back(track);
    }
    std::string error;
    const std::optional<Asset::AnimationClipAssetUVE> result =
        ConformClipUVE(clip, rig.skeleton, fixture.conformed, GetHumanoidReferenceUVE(), &error);
    ASSERT_TRUE(result.has_value()) << error;
    ASSERT_EQ(result->bones.size(), fixture.conformed.skeleton.bones.size());
    EXPECT_EQ(result->clipId, "Wave");
    EXPECT_EQ(result->bones[1].bone, "Hips");

    for (int frame = 0; frame < 3; ++frame) {
        RetargetSkeletonUVE source = rig.skeleton;
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            source.bones[bone].position = clip.bones[bone].samples[static_cast<std::size_t>(frame)].pose.position;
            source.bones[bone].rotation = clip.bones[bone].samples[static_cast<std::size_t>(frame)].pose.rotation;
        }
        RetargetSkeletonUVE played = fixture.conformed.skeleton;
        for (std::size_t index = 0U; index < played.bones.size(); ++index) {
            const Asset::AnimationAssetBoneTrackUVE& track = result->bones[index];
            const Asset::AnimationAssetSampleUVE& sample = track.samples[track.samples.size() == 1U ? 0U : static_cast<std::size_t>(frame)];
            played.bones[index].position = sample.pose.position;
            played.bones[index].rotation = sample.pose.rotation;
        }
        const std::vector<WorldTransformUVE> sourceWorld = ComputeWorldTransformsUVE(source);
        const std::vector<WorldTransformUVE> playedWorld = ComputeWorldTransformsUVE(played);
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            const auto index = static_cast<std::size_t>(fixture.conformed.boneOfRigBone[bone]);
            ExpectNear(playedWorld[index].position, sourceWorld[bone].position, 1e-4F,
                       source.bones[bone].name + " frame " + std::to_string(frame));
        }
        // Added IK targets follow the moving feet and hands.
        ExpectNear(playedWorld[static_cast<std::size_t>(FindBoneUVE(played, "IKHand_L"))].position,
                   playedWorld[static_cast<std::size_t>(FindBoneUVE(played, "Hand_L"))].position, 1e-4F, "ik hand");
    }
    // A bone that never moves relative to its parent (the head, and the hand riding the forearm)
    // keeps one sample; the bones the clip moves keep all three.
    const auto samplesOf = [&](const char* name) {
        return result->bones[static_cast<std::size_t>(FindBoneUVE(fixture.conformed.skeleton, name))].samples.size();
    };
    EXPECT_EQ(samplesOf("Head"), 1U);
    EXPECT_EQ(samplesOf("Hand_L"), 1U);
    EXPECT_EQ(samplesOf("ForeArm_L"), 3U);
    EXPECT_EQ(samplesOf("Hips"), 3U);
}

} // namespace
} // namespace UVE::Retarget
