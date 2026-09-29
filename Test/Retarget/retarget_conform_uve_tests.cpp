// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <gtest/gtest.h>

#include "Retarget/retarget_test_rig_uve.h"
#include "uve/asset/mesh_skinning_uve.h"
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
    const Asset::AnimationClipAssetUVE clip = MakeClipUVE(rig, "Wave");
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

// Rigs from other tools face any way and stand any way up. Every one comes out the same humanoid.
struct TurnedRigCaseUVE final {
    const char* what;
    QuaternionUVE turn;
};

[[nodiscard]] std::vector<TurnedRigCaseUVE> TurnedRigCasesUVE() {
    return {{"facing -Z", AxisAngle({0.0F, 1.0F, 0.0F}, 3.1415927F)},
            {"facing +X", AxisAngle({0.0F, 1.0F, 0.0F}, 1.5707964F)},
            {"facing -X", AxisAngle({0.0F, 1.0F, 0.0F}, -1.5707964F)},
            {"Z up", AxisAngle({1.0F, 0.0F, 0.0F}, 1.5707964F)},
            {"Z up, turned", Math::MultiplyUVE(AxisAngle({0.0F, 1.0F, 0.0F}, 0.9F), AxisAngle({1.0F, 0.0F, 0.0F}, 1.5707964F))}};
}

[[nodiscard]] ConformedRigUVE ConformUVE(const TPoseRigUVE& rig) {
    const HumanoidMatchUVE match = MatchHumanoidUVE(rig.skeleton, GetHumanoidReferenceUVE());
    std::string error;
    const std::optional<ConformedRigUVE> result = ConformSkeletonUVE(rig.skeleton, match, GetHumanoidReferenceUVE(), &error);
    EXPECT_TRUE(result.has_value()) << error;
    return result.value_or(ConformedRigUVE{});
}

TEST(RetargetConformUVETest, ConformSkeletonUVE_StandsRigsUpFacingForwardWhateverWayTheyCame) {
    const TPoseRigUVE upright;
    const ConformedRigUVE reference = ConformUVE(upright);
    EXPECT_NEAR(std::abs(reference.orientation.w), 1.0F, 1e-6F) << "an upright rig is left exactly as it stands";
    const std::vector<WorldTransformUVE> referenceWorld = ComputeWorldTransformsUVE(reference.skeleton);
    for (const auto& [what, turn] : TurnedRigCasesUVE()) {
        const TPoseRigUVE turned(turn);
        const ConformedRigUVE conformed = ConformUVE(turned);
        EXPECT_LT(std::abs(conformed.orientation.w), 0.9999F) << what << ": it was turned";
        ASSERT_EQ(conformed.skeleton.bones.size(), reference.skeleton.bones.size()) << what;
        const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(conformed.skeleton);
        for (std::size_t index = 0U; index < world.size(); ++index) {
            ExpectNear(world[index].position, referenceWorld[index].position, 2e-3F,
                       std::string{what} + ": " + conformed.skeleton.bones[index].name);
        }
    }
}

TEST(RetargetConformUVETest, ConformClipUVE_TurnsTheAnimationWithTheRig) {
    for (const auto& [what, turn] : TurnedRigCasesUVE()) {
        const TPoseRigUVE rig(turn);
        const ConformedRigUVE conformed = ConformUVE(rig);
        const Asset::AnimationClipAssetUVE clip = MakeClipUVE(rig, "Wave");
        std::string error;
        const std::optional<Asset::AnimationClipAssetUVE> result =
            ConformClipUVE(clip, rig.skeleton, conformed, GetHumanoidReferenceUVE(), &error);
        ASSERT_TRUE(result.has_value()) << what << ": " << error;
        RetargetSkeletonUVE source = rig.skeleton;
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            source.bones[bone].position = clip.bones[bone].samples[2].pose.position;
            source.bones[bone].rotation = clip.bones[bone].samples[2].pose.rotation;
        }
        RetargetSkeletonUVE played = conformed.skeleton;
        for (std::size_t index = 0U; index < played.bones.size(); ++index) {
            const auto& samples = result->bones[index].samples;
            played.bones[index].position = samples[samples.size() == 1U ? 0U : 2U].pose.position;
            played.bones[index].rotation = samples[samples.size() == 1U ? 0U : 2U].pose.rotation;
        }
        const std::vector<WorldTransformUVE> sourceWorld = ComputeWorldTransformsUVE(source);
        const std::vector<WorldTransformUVE> playedWorld = ComputeWorldTransformsUVE(played);
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            const auto index = static_cast<std::size_t>(conformed.boneOfRigBone[bone]);
            ExpectNear(playedWorld[index].position, Math::RotateVectorUVE(conformed.orientation, sourceWorld[bone].position), 2e-4F,
                       std::string{what} + ": " + source.bones[bone].name);
        }
        // The hips travel exactly as far as they did in the source (0.4 forward and 0.05 down a frame).
        const auto hips = static_cast<std::size_t>(FindBoneUVE(conformed.skeleton, "Hips"));
        const Vector3UVE travelled = playedWorld[hips].position - ComputeWorldTransformsUVE(conformed.skeleton)[hips].position;
        EXPECT_NEAR(Math::LengthUVE(travelled), std::sqrt(0.8F * 0.8F + 0.1F * 0.1F), 1e-3F) << what;
    }
}

/// One vertex on every bone, bound to it alone, so the skin is exact and any difference is the
/// animation's.
[[nodiscard]] Asset::MeshAssetUVE MakeRigidMeshUVE(const TPoseRigUVE& rig) {
    Asset::MeshAssetUVE mesh = MakeMeshUVE(rig);
    mesh.vertices.clear();
    mesh.skinningInfluences.clear();
    for (std::size_t bone = 0U; bone < rig.world.size(); ++bone) {
        Asset::MeshVertexUVE vertex;
        vertex.position = rig.world[bone].position + Vector3UVE{0.02F, 0.03F, 0.01F};
        vertex.normal = {0.0F, 1.0F, 0.0F};
        mesh.vertices.push_back(vertex);
        Asset::MeshSkinningInfluenceUVE influence;
        influence.joints[0] = static_cast<std::uint32_t>(bone);
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences.push_back(influence);
    }
    mesh.indices = {0U, 1U, 2U};
    return mesh;
}

[[nodiscard]] std::vector<Asset::MeshVertexUVE> PlayUVE(const Asset::MeshAssetUVE& mesh, const std::vector<RetargetBoneUVE>& bones,
                                                        const std::vector<Asset::AnimationAssetBoneTrackUVE>& tracks, const std::size_t frame) {
    std::vector<Math::Matrix4x4UVE> local;
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        const auto& samples = tracks[index].samples;
        const Asset::AnimationAssetPoseUVE& pose = samples[samples.size() == 1U ? 0U : frame].pose;
        local.push_back(Math::Matrix4x4UVE::ComposeTrsUVE(pose.position, pose.rotation, {1.0F, 1.0F, 1.0F}));
    }
    std::vector<Math::Matrix4x4UVE> skin;
    std::vector<Asset::MeshVertexUVE> out;
    EXPECT_TRUE(Asset::TryResolvePoseUVE(mesh.joints, local, skin));
    EXPECT_TRUE(Asset::TrySkinMeshUVE(mesh, skin, out));
    return out;
}

// The point of retargeting: the character and its animation keep looking exactly as they did.
// Played through the engine's own skinner, the conformed model with the conformed clip lands every
// vertex where the original model with the original clip did (turned to face +Z if it faced away).
TEST(RetargetConformUVETest, ConformedModelWithConformedClipLooksExactlyLikeTheOriginal) {
    std::vector<TurnedRigCaseUVE> cases = TurnedRigCasesUVE();
    cases.insert(cases.begin(), TurnedRigCaseUVE{"as it came", QuaternionUVE{}});
    for (const auto& [what, turn] : cases) {
        const TPoseRigUVE rig(turn);
        const Asset::MeshAssetUVE original = MakeRigidMeshUVE(rig);
        const Asset::AnimationClipAssetUVE clip = MakeClipUVE(rig, "Wave");
        std::string error;
        const std::optional<RetargetSkeletonUVE> fromMesh = RigFromMeshUVE(original, &error);
        ASSERT_TRUE(fromMesh.has_value()) << error;
        const HumanoidMatchUVE match = MatchHumanoidUVE(*fromMesh, GetHumanoidReferenceUVE());
        const std::optional<ConformedRigUVE> conformed = ConformSkeletonUVE(*fromMesh, match, GetHumanoidReferenceUVE(), &error);
        ASSERT_TRUE(conformed.has_value()) << error;
        Asset::MeshAssetUVE mesh = original;
        ASSERT_TRUE(ConformMeshUVE(mesh, *conformed, &error)) << error;
        const std::optional<Asset::AnimationClipAssetUVE> result =
            ConformClipUVE(clip, *fromMesh, *conformed, GetHumanoidReferenceUVE(), &error);
        ASSERT_TRUE(result.has_value()) << error;

        for (const std::size_t frame : {0U, 1U, 2U}) {
            const std::vector<Asset::MeshVertexUVE> before = PlayUVE(original, rig.skeleton.bones, clip.bones, frame);
            const std::vector<Asset::MeshVertexUVE> after = PlayUVE(mesh, conformed->skeleton.bones, result->bones, frame);
            ASSERT_EQ(before.size(), after.size());
            for (std::size_t vertex = 0U; vertex < before.size(); ++vertex) {
                ExpectNear(after[vertex].position, Math::RotateVectorUVE(conformed->orientation, before[vertex].position), 2e-4F,
                           std::string{what} + ", frame " + std::to_string(frame) + ", " + rig.skeleton.bones[vertex].name);
            }
        }
    }
}

/// A tube from the left shoulder along the arm, its middle ring held half by the shoulder and half
/// by the arm: the place lowering an arm pinches.
[[nodiscard]] Asset::MeshAssetUVE MakeShoulderTubeUVE(const TPoseRigUVE& rig) {
    Asset::MeshAssetUVE mesh = MakeMeshUVE(rig);
    mesh.vertices.clear();
    mesh.skinningInfluences.clear();
    const Vector3UVE shoulder = rig.At("LeftShoulder");
    const Vector3UVE elbow = rig.At("LeftForeArm");
    const std::uint32_t clavicle = static_cast<std::uint32_t>(FindBoneUVE(rig.skeleton, "LeftShoulder"));
    const std::uint32_t arm = static_cast<std::uint32_t>(FindBoneUVE(rig.skeleton, "LeftArm"));
    constexpr int kSides = 8;
    for (int ring = 0; ring < 3; ++ring) {
        const Vector3UVE centre = shoulder + (elbow - shoulder) * (0.05F + 0.15F * static_cast<float>(ring));
        for (int side = 0; side < kSides; ++side) {
            const float angle = 6.2831853F * static_cast<float>(side) / static_cast<float>(kSides);
            Asset::MeshVertexUVE vertex;
            vertex.position = centre + Vector3UVE{0.0F, 0.06F * std::cos(angle), 0.06F * std::sin(angle)};
            vertex.normal = {0.0F, std::cos(angle), std::sin(angle)};
            mesh.vertices.push_back(vertex);
            Asset::MeshSkinningInfluenceUVE influence;
            influence.joints[0] = clavicle;
            influence.joints[1] = arm;
            influence.weights[0] = ring == 0 ? 1.0F : (ring == 1 ? 0.5F : 0.0F);
            influence.weights[1] = 1.0F - influence.weights[0];
            mesh.skinningInfluences.push_back(influence);
        }
    }
    mesh.indices.clear();
    for (int ring = 0; ring < 2; ++ring) {
        for (int side = 0; side < kSides; ++side) {
            const auto a = static_cast<std::uint32_t>(ring * kSides + side);
            const auto b = static_cast<std::uint32_t>(ring * kSides + (side + 1) % kSides);
            const std::uint32_t c = a + kSides;
            const std::uint32_t d = b + kSides;
            mesh.indices.insert(mesh.indices.end(), {a, b, c, b, d, c});
        }
    }
    return mesh;
}

TEST(RetargetConformUVETest, ConformMeshUVE_DualQuaternionBlendingPinchesTheShoulderLessThanLinear) {
    const TPoseRigUVE rig;
    const Asset::MeshAssetUVE original = MakeShoulderTubeUVE(rig);
    const ConformedRigUVE conformed = ConformUVE(rig);
    Asset::MeshAssetUVE linear = original;
    Asset::MeshAssetUVE dual = original;
    std::string error;
    ASSERT_TRUE(ConformMeshUVE(linear, conformed, &error, SkinBlendUVE::Linear)) << error;
    ASSERT_TRUE(ConformMeshUVE(dual, conformed, &error, SkinBlendUVE::DualQuaternion)) << error;
    const MeshDistortionUVE linearChange = MeasureMeshDistortionUVE(original, linear);
    const MeshDistortionUVE dualChange = MeasureMeshDistortionUVE(original, dual);
    EXPECT_GT(linearChange.maximumEdgeChange, 0.02F) << "lowering the arm 45 degrees does pinch a linear blend";
    EXPECT_LT(dualChange.maximumEdgeChange, linearChange.maximumEdgeChange);
    EXPECT_LE(dualChange.fractionOverTenPercent, linearChange.fractionOverTenPercent);
    // The mesh was moved, not rebuilt: same triangles, same weights, same UVs.
    EXPECT_EQ(dual.indices, original.indices);
    ASSERT_EQ(dual.vertices.size(), original.vertices.size());
    for (std::size_t vertex = 0U; vertex < dual.vertices.size(); ++vertex) {
        EXPECT_EQ(dual.vertices[vertex].u, original.vertices[vertex].u);
        EXPECT_EQ(dual.skinningInfluences[vertex].weights[0], original.skinningInfluences[vertex].weights[0]);
    }
    EXPECT_FLOAT_EQ(MeasureMeshDistortionUVE(original, original).maximumEdgeChange, 0.0F);
}

} // namespace
} // namespace UVE::Retarget
