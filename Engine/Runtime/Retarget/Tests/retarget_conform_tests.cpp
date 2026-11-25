// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_test_rig.h"
#include "mesh_skinning.h"
#include "retarget_conform.h"

namespace UVE::Retarget {
namespace {

using Math::Quaternion;
using Math::Vector3;
using namespace TestRig;

void ExpectNear(const Vector3& actual, const Vector3& expected, const float tolerance, const std::string& what) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, tolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, tolerance) << what;
}

struct ConformedFixture final {
    TPoseRig rig;
    ConformedRig conformed;
    std::vector<WorldTransform> world;

    ConformedFixture() {
        const HumanoidMatch match = MatchHumanoid(rig.skeleton, GetHumanoidReference());
        std::string error;
        const std::optional<ConformedRig> result = ConformSkeleton(rig.skeleton, match, GetHumanoidReference(), &error);
        EXPECT_TRUE(result.has_value()) << error;
        if (result.has_value()) {
            conformed = *result;
            world = ComputeWorldTransforms(conformed.skeleton);
        }
    }

    [[nodiscard]] Vector3 At(const std::string& name) const {
        const std::int32_t bone = FindBone(conformed.skeleton, name);
        EXPECT_GE(bone, 0) << name;
        return bone < 0 ? Vector3{} : world[static_cast<std::size_t>(bone)].position;
    }
};

[[nodiscard]] Vector3 ReferenceAt(const std::string& name) {
    const HumanoidReference& reference = GetHumanoidReference();
    return ComputeWorldTransforms(reference.skeleton)[static_cast<std::size_t>(FindBone(reference.skeleton, name))].position;
}

TEST(RetargetConformTest, ConformSkeleton_GivesTheRigTheHumanoidsBonesNamesAndFrames) {
    const ConformedFixture fixture;
    const HumanoidReference& reference = GetHumanoidReference();
    const ConformedRig& conformed = fixture.conformed;
    // The humanoid's core bones (the optional correctives the rig lacks are not made), then its own.
    const std::size_t core = static_cast<std::size_t>(std::ranges::count_if(reference.info, [](const HumanoidBoneInfo& info) { return !info.optional; }));
    ASSERT_EQ(conformed.skeleton.bones.size(), core + 1U);
    ASSERT_TRUE(IsRetargetSkeletonValid(conformed.skeleton));
    EXPECT_NEAR(conformed.heightScale, 1.1F, 1e-3F);
    const std::vector<WorldTransform> referenceWorld = ComputeWorldTransforms(reference.skeleton);
    for (std::size_t index = 0U; index < core; ++index) {
        const RetargetBone& bone = conformed.skeleton.bones[index];
        const std::int32_t ref = conformed.referenceOfBone[index];
        ASSERT_GE(ref, 0) << bone.name;
        EXPECT_FALSE(reference.info[static_cast<std::size_t>(ref)].optional) << bone.name;
        EXPECT_EQ(bone.name, reference.skeleton.bones[static_cast<std::size_t>(ref)].name);
        const std::int32_t parent = reference.skeleton.bones[static_cast<std::size_t>(ref)].parent;
        EXPECT_EQ(bone.parent < 0 ? -1 : conformed.referenceOfBone[static_cast<std::size_t>(bone.parent)], parent) << bone.name;
        // Every humanoid bone's frame is the reference's, whatever frame the rig used.
        const Quaternion& a = fixture.world[index].rotation;
        const Quaternion& b = referenceWorld[static_cast<std::size_t>(ref)].rotation;
        EXPECT_NEAR(std::abs(a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w), 1.0F, 1e-4F) << bone.name;
    }
    // The rig's own end bone is kept, under the head.
    const RetargetBone& kept = conformed.skeleton.bones.back();
    EXPECT_EQ(kept.name, "HeadTop_End");
    EXPECT_EQ(kept.parent, FindBone(conformed.skeleton, "Head"));
    EXPECT_FALSE(conformed.added.back());
    EXPECT_TRUE(conformed.added[static_cast<std::size_t>(FindBone(conformed.skeleton, "Spine2"))]);
    EXPECT_FALSE(conformed.added[static_cast<std::size_t>(FindBone(conformed.skeleton, "Spine3"))]);
}

TEST(RetargetConformTest, ConformSkeleton_LowersTheArmsToTheAPoseKeepingTheirLength) {
    const ConformedFixture fixture;
    for (const auto& [side, source] : {std::pair{std::string{"_L"}, std::string{"Left"}}, std::pair{std::string{"_R"}, std::string{"Right"}}}) {
        for (const auto& [from, to, rigFrom, rigTo] :
             {std::tuple{"UpperArm", "ForeArm", "Arm", "ForeArm"}, std::tuple{"ForeArm", "Hand", "ForeArm", "Hand"}}) {
            const Vector3 conformedBone = fixture.At(std::string{to} + side) - fixture.At(std::string{from} + side);
            const Vector3 referenceBone = ReferenceAt(std::string{to} + side) - ReferenceAt(std::string{from} + side);
            ExpectNear(Math::Normalize(conformedBone), Math::Normalize(referenceBone), 1e-4F, std::string{from} + side);
            EXPECT_NEAR(Math::Length(conformedBone),
                        Math::Length(fixture.rig.At(source + rigTo) - fixture.rig.At(source + rigFrom)), 1e-4F);
        }
    }
    // The shoulders stay where the rig had them.
    ExpectNear(fixture.At("Clavicle_L"), fixture.rig.At("LeftShoulder"), 1e-4F, "clavicle");
}

TEST(RetargetConformTest, ConformSkeleton_FitsTheBonesTheRigLacksToItsOwnLimbs) {
    const ConformedFixture fixture;
    // A spine bone between its neighbours, where the humanoid has it along the chain.
    const float below = Math::Length(fixture.At("Spine2") - fixture.At("Spine1"));
    const float above = Math::Length(fixture.At("Spine3") - fixture.At("Spine2"));
    EXPECT_NEAR(below + above, Math::Length(fixture.At("Spine3") - fixture.At("Spine1")), 2e-3F);
    const float referenceBelow = Math::Length(ReferenceAt("Spine2") - ReferenceAt("Spine1"));
    const float referenceAbove = Math::Length(ReferenceAt("Spine3") - ReferenceAt("Spine2"));
    EXPECT_NEAR(below / (below + above), referenceBelow / (referenceBelow + referenceAbove), 1e-3F);
    // A twist bone along the upper arm, at the humanoid's fraction of it.
    const Vector3 upper = fixture.At("ForeArm_L") - fixture.At("UpperArm_L");
    const Vector3 twist = fixture.At("UpperArmTwist1_L") - fixture.At("UpperArm_L");
    const Vector3 referenceUpper = ReferenceAt("ForeArm_L") - ReferenceAt("UpperArm_L");
    const Vector3 referenceTwist = ReferenceAt("UpperArmTwist1_L") - ReferenceAt("UpperArm_L");
    EXPECT_NEAR(Math::Dot(twist, upper) / Math::Dot(upper, upper),
                Math::Dot(referenceTwist, referenceUpper) / Math::Dot(referenceUpper, referenceUpper), 1e-3F);
    // IK targets on what they follow; the root on the ground under the hips.
    ExpectNear(fixture.At("IKFoot_L"), fixture.At("Foot_L"), 1e-5F, "ik foot");
    ExpectNear(fixture.At("IKHand_R"), fixture.At("Hand_R"), 1e-5F, "ik hand");
    ExpectNear(fixture.At("IKFootRoot"), fixture.At("Root"), 1e-5F, "ik root");
    const float toe = std::min(fixture.At("Toe_L").y, fixture.At("Foot_L").y);
    EXPECT_NEAR(fixture.At("Root").y, toe - ReferenceAt("Toe_L").y * 1.1F, 5e-3F);
    // Each of the rig's bones moved rigidly into its conformed place.
    const std::vector<WorldTransform> rigWorld = ComputeWorldTransforms(fixture.rig.skeleton);
    for (std::size_t bone = 0U; bone < rigWorld.size(); ++bone) {
        const auto index = static_cast<std::size_t>(fixture.conformed.boneOfRigBone[bone]);
        ExpectNear(fixture.conformed.moveOfRigBone[bone].Apply(rigWorld[bone].position), fixture.world[index].position, 1e-4F,
                   fixture.rig.skeleton.bones[bone].name);
    }
}

TEST(RetargetConformTest, ConformSkeleton_RefusesARigWithoutHips) {
    RetargetSkeleton rig;
    rig.bones.push_back(RetargetBone{"Thing", -1, {}, {}, 1.0F});
    std::string error;
    const HumanoidMatch match = MatchHumanoid(rig, GetHumanoidReference());
    EXPECT_FALSE(ConformSkeleton(rig, match, GetHumanoidReference(), &error).has_value());
    EXPECT_NE(error.find("hips"), std::string::npos) << error;
}

TEST(RetargetConformTest, ConformMesh_CarriesTheSkinIntoTheAPoseAndRebindsIt) {
    const TPoseRig rig;
    Asset::MeshAsset mesh = MakeMesh(rig);
    std::string error;
    const std::optional<RetargetSkeleton> fromMesh = RigFromMesh(mesh, &error);
    ASSERT_TRUE(fromMesh.has_value()) << error;
    const std::vector<WorldTransform> fromMeshWorld = ComputeWorldTransforms(*fromMesh);
    for (std::size_t index = 0U; index < rig.world.size(); ++index) {
        ExpectNear(fromMeshWorld[index].position, rig.world[index].position, 1e-4F, fromMesh->bones[index].name);
    }

    const HumanoidMatch match = MatchHumanoid(*fromMesh, GetHumanoidReference());
    const std::optional<ConformedRig> conformed = ConformSkeleton(*fromMesh, match, GetHumanoidReference(), &error);
    ASSERT_TRUE(conformed.has_value()) << error;
    ASSERT_TRUE(ConformMesh(mesh, *conformed, &error)) << error;
    ASSERT_TRUE(Asset::IsMeshSkinningDataValid(mesh));
    ASSERT_EQ(mesh.joints.size(), conformed->skeleton.bones.size());
    EXPECT_EQ(mesh.joints[static_cast<std::size_t>(FindBone(conformed->skeleton, "UpperArm_L"))].name, "UpperArm_L");
    EXPECT_EQ(mesh.skinningInfluences[0].joints[0], static_cast<std::uint32_t>(FindBone(conformed->skeleton, "UpperArm_L")));

    // The vertex went down with the arm: still half way along it.
    const std::vector<WorldTransform> world = ComputeWorldTransforms(conformed->skeleton);
    const auto at = [&](const char* name) { return world[static_cast<std::size_t>(FindBone(conformed->skeleton, name))].position; };
    ExpectNear(mesh.vertices[0].position, (at("UpperArm_L") + at("ForeArm_L")) * 0.5F, 1e-4F, "vertex");
    // At the new rest every joint's skinning matrix is the identity: nothing moves until posed.
    for (std::size_t index = 0U; index < mesh.joints.size(); ++index) {
        const Math::Matrix4x4 skin =
            Math::Matrix4x4::ComposeTrs(world[index].position, world[index].rotation, {1.0F, 1.0F, 1.0F}) *
            mesh.joints[index].inverseBindMatrix;
        ExpectNear(Math::TransformPoint(skin, {0.3F, -0.2F, 0.5F}), {0.3F, -0.2F, 0.5F}, 1e-4F, mesh.joints[index].name);
    }
}

TEST(RetargetConformTest, ConformClip_KeepsEveryBoneWhereTheClipPutItInTheHumanoidsNames) {
    const ConformedFixture fixture;
    const TPoseRig& rig = fixture.rig;
    const Asset::AnimationClipAsset clip = MakeClip(rig, "Wave");
    std::string error;
    const std::optional<Asset::AnimationClipAsset> result =
        ConformClip(clip, rig.skeleton, fixture.conformed, GetHumanoidReference(), &error);
    ASSERT_TRUE(result.has_value()) << error;
    ASSERT_EQ(result->bones.size(), fixture.conformed.skeleton.bones.size());
    EXPECT_EQ(result->clipId, "Wave");
    EXPECT_EQ(result->bones[1].bone, "Hips");

    for (int frame = 0; frame < 3; ++frame) {
        RetargetSkeleton source = rig.skeleton;
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            source.bones[bone].position = clip.bones[bone].samples[static_cast<std::size_t>(frame)].pose.position;
            source.bones[bone].rotation = clip.bones[bone].samples[static_cast<std::size_t>(frame)].pose.rotation;
        }
        RetargetSkeleton played = fixture.conformed.skeleton;
        for (std::size_t index = 0U; index < played.bones.size(); ++index) {
            const Asset::AnimationAssetBoneTrack& track = result->bones[index];
            const Asset::AnimationAssetSample& sample = track.samples[track.samples.size() == 1U ? 0U : static_cast<std::size_t>(frame)];
            played.bones[index].position = sample.pose.position;
            played.bones[index].rotation = sample.pose.rotation;
        }
        const std::vector<WorldTransform> sourceWorld = ComputeWorldTransforms(source);
        const std::vector<WorldTransform> playedWorld = ComputeWorldTransforms(played);
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            const auto index = static_cast<std::size_t>(fixture.conformed.boneOfRigBone[bone]);
            ExpectNear(playedWorld[index].position, sourceWorld[bone].position, 1e-4F,
                       source.bones[bone].name + " frame " + std::to_string(frame));
        }
        // Added IK targets follow the moving feet and hands.
        ExpectNear(playedWorld[static_cast<std::size_t>(FindBone(played, "IKHand_L"))].position,
                   playedWorld[static_cast<std::size_t>(FindBone(played, "Hand_L"))].position, 1e-4F, "ik hand");
    }
    // A bone that never moves relative to its parent (the head, and the hand riding the forearm)
    // keeps one sample; the bones the clip moves keep all three.
    const auto samplesOf = [&](const char* name) {
        return result->bones[static_cast<std::size_t>(FindBone(fixture.conformed.skeleton, name))].samples.size();
    };
    EXPECT_EQ(samplesOf("Head"), 1U);
    EXPECT_EQ(samplesOf("Hand_L"), 1U);
    EXPECT_EQ(samplesOf("ForeArm_L"), 3U);
    EXPECT_EQ(samplesOf("Hips"), 3U);
}

// Rigs from other tools face any way and stand any way up. Every one comes out the same humanoid.
struct TurnedRigCase final {
    const char* what;
    Quaternion turn;
};

[[nodiscard]] std::vector<TurnedRigCase> TurnedRigCases() {
    return {{"facing -Z", AxisAngle({0.0F, 1.0F, 0.0F}, 3.1415927F)},
            {"facing +X", AxisAngle({0.0F, 1.0F, 0.0F}, 1.5707964F)},
            {"facing -X", AxisAngle({0.0F, 1.0F, 0.0F}, -1.5707964F)},
            {"Z up", AxisAngle({1.0F, 0.0F, 0.0F}, 1.5707964F)},
            {"Z up, turned", Math::Multiply(AxisAngle({0.0F, 1.0F, 0.0F}, 0.9F), AxisAngle({1.0F, 0.0F, 0.0F}, 1.5707964F))}};
}

[[nodiscard]] ConformedRig Conform(const TPoseRig& rig) {
    const HumanoidMatch match = MatchHumanoid(rig.skeleton, GetHumanoidReference());
    std::string error;
    const std::optional<ConformedRig> result = ConformSkeleton(rig.skeleton, match, GetHumanoidReference(), &error);
    EXPECT_TRUE(result.has_value()) << error;
    return result.value_or(ConformedRig{});
}

TEST(RetargetConformTest, ConformSkeleton_StandsRigsUpFacingForwardWhateverWayTheyCame) {
    const TPoseRig upright;
    const ConformedRig reference = Conform(upright);
    EXPECT_NEAR(std::abs(reference.orientation.w), 1.0F, 1e-6F) << "an upright rig is left exactly as it stands";
    const std::vector<WorldTransform> referenceWorld = ComputeWorldTransforms(reference.skeleton);
    for (const auto& [what, turn] : TurnedRigCases()) {
        const TPoseRig turned(turn);
        const ConformedRig conformed = Conform(turned);
        EXPECT_LT(std::abs(conformed.orientation.w), 0.9999F) << what << ": it was turned";
        ASSERT_EQ(conformed.skeleton.bones.size(), reference.skeleton.bones.size()) << what;
        const std::vector<WorldTransform> world = ComputeWorldTransforms(conformed.skeleton);
        for (std::size_t index = 0U; index < world.size(); ++index) {
            ExpectNear(world[index].position, referenceWorld[index].position, 2e-3F,
                       std::string{what} + ": " + conformed.skeleton.bones[index].name);
        }
    }
}

TEST(RetargetConformTest, ConformClip_TurnsTheAnimationWithTheRig) {
    for (const auto& [what, turn] : TurnedRigCases()) {
        const TPoseRig rig(turn);
        const ConformedRig conformed = Conform(rig);
        const Asset::AnimationClipAsset clip = MakeClip(rig, "Wave");
        std::string error;
        const std::optional<Asset::AnimationClipAsset> result =
            ConformClip(clip, rig.skeleton, conformed, GetHumanoidReference(), &error);
        ASSERT_TRUE(result.has_value()) << what << ": " << error;
        RetargetSkeleton source = rig.skeleton;
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            source.bones[bone].position = clip.bones[bone].samples[2].pose.position;
            source.bones[bone].rotation = clip.bones[bone].samples[2].pose.rotation;
        }
        RetargetSkeleton played = conformed.skeleton;
        for (std::size_t index = 0U; index < played.bones.size(); ++index) {
            const auto& samples = result->bones[index].samples;
            played.bones[index].position = samples[samples.size() == 1U ? 0U : 2U].pose.position;
            played.bones[index].rotation = samples[samples.size() == 1U ? 0U : 2U].pose.rotation;
        }
        const std::vector<WorldTransform> sourceWorld = ComputeWorldTransforms(source);
        const std::vector<WorldTransform> playedWorld = ComputeWorldTransforms(played);
        for (std::size_t bone = 0U; bone < source.bones.size(); ++bone) {
            const auto index = static_cast<std::size_t>(conformed.boneOfRigBone[bone]);
            ExpectNear(playedWorld[index].position, Math::RotateVector(conformed.orientation, sourceWorld[bone].position), 2e-4F,
                       std::string{what} + ": " + source.bones[bone].name);
        }
        // The hips travel exactly as far as they did in the source (0.4 forward and 0.05 down a frame).
        const auto hips = static_cast<std::size_t>(FindBone(conformed.skeleton, "Hips"));
        const Vector3 travelled = playedWorld[hips].position - ComputeWorldTransforms(conformed.skeleton)[hips].position;
        EXPECT_NEAR(Math::Length(travelled), std::sqrt(0.8F * 0.8F + 0.1F * 0.1F), 1e-3F) << what;
    }
}

TEST(RetargetConformTest, ConformSkeleton_ALeaningSpineDoesNotTiltTheRig) {
    // Most game rigs carry the head ahead of the hips. That lean is the rig's shape, not a tilt: the
    // rig still stands along its up axis, so a run's root keeps to the floor as it travels.
    for (const auto& [what, turn] : std::vector<std::pair<std::string, Quaternion>>{
             {"Y up", Quaternion{}}, {"Z up", AxisAngle({1.0F, 0.0F, 0.0F}, 1.5707964F)}}) {
        const TPoseRig upright(turn);
        TPoseRig leaning(turn);
        std::vector<WorldTransform> world = ComputeWorldTransforms(leaning.skeleton);
        const Vector3 forward = Math::RotateVector(turn, Vector3{0.0F, 0.0F, 0.12F});
        for (const char* name : {"Neck", "Head", "HeadTop_End"}) {
            WorldTransform& bone = world[static_cast<std::size_t>(FindBone(leaning.skeleton, name))];
            bone.position = bone.position + forward;
        }
        leaning.skeleton = SkeletonFromWorld(leaning.skeleton, world);
        const ConformedRig a = Conform(upright);
        const ConformedRig b = Conform(leaning);
        const Quaternion& p = a.orientation;
        const Quaternion& q = b.orientation;
        EXPECT_NEAR(std::abs(p.x * q.x + p.y * q.y + p.z * q.z + p.w * q.w), 1.0F, 1e-6F) << what;
        // Travel along the rig's forward stays level.
        const Vector3 travelled = Math::RotateVector(b.orientation, Math::RotateVector(turn, Vector3{0.0F, 0.0F, 3.0F}));
        EXPECT_NEAR(travelled.y, 0.0F, 1e-5F) << what;
    }
}

/// One vertex on every bone, bound to it alone, so the skin is exact and any difference is the
/// animation's.
[[nodiscard]] Asset::MeshAsset MakeRigidMesh(const TPoseRig& rig) {
    Asset::MeshAsset mesh = MakeMesh(rig);
    mesh.vertices.clear();
    mesh.skinningInfluences.clear();
    for (std::size_t bone = 0U; bone < rig.world.size(); ++bone) {
        Asset::MeshVertex vertex;
        vertex.position = rig.world[bone].position + Vector3{0.02F, 0.03F, 0.01F};
        vertex.normal = {0.0F, 1.0F, 0.0F};
        mesh.vertices.push_back(vertex);
        Asset::MeshSkinningInfluence influence;
        influence.joints[0] = static_cast<std::uint32_t>(bone);
        influence.weights[0] = 1.0F;
        mesh.skinningInfluences.push_back(influence);
    }
    mesh.indices = {0U, 1U, 2U};
    return mesh;
}

[[nodiscard]] std::vector<Asset::MeshVertex> Play(const Asset::MeshAsset& mesh, const std::vector<RetargetBone>& bones,
                                                        const std::vector<Asset::AnimationAssetBoneTrack>& tracks, const std::size_t frame) {
    std::vector<Math::Matrix4x4> local;
    for (std::size_t index = 0U; index < bones.size(); ++index) {
        const auto& samples = tracks[index].samples;
        const Asset::AnimationAssetPose& pose = samples[samples.size() == 1U ? 0U : frame].pose;
        local.push_back(Math::Matrix4x4::ComposeTrs(pose.position, pose.rotation, {1.0F, 1.0F, 1.0F}));
    }
    std::vector<Math::Matrix4x4> skin;
    std::vector<Asset::MeshVertex> out;
    EXPECT_TRUE(Asset::TryResolvePose(mesh.joints, local, skin));
    EXPECT_TRUE(Asset::TrySkinMesh(mesh, skin, out));
    return out;
}

// The point of retargeting: the character and its animation keep looking exactly as they did.
// Played through the engine's own skinner, the conformed model with the conformed clip lands every
// vertex where the original model with the original clip did (turned to face +Z if it faced away).
TEST(RetargetConformTest, ConformedModelWithConformedClipLooksExactlyLikeTheOriginal) {
    std::vector<TurnedRigCase> cases = TurnedRigCases();
    cases.insert(cases.begin(), TurnedRigCase{"as it came", Quaternion{}});
    for (const auto& [what, turn] : cases) {
        const TPoseRig rig(turn);
        const Asset::MeshAsset original = MakeRigidMesh(rig);
        const Asset::AnimationClipAsset clip = MakeClip(rig, "Wave");
        std::string error;
        const std::optional<RetargetSkeleton> fromMesh = RigFromMesh(original, &error);
        ASSERT_TRUE(fromMesh.has_value()) << error;
        const HumanoidMatch match = MatchHumanoid(*fromMesh, GetHumanoidReference());
        const std::optional<ConformedRig> conformed = ConformSkeleton(*fromMesh, match, GetHumanoidReference(), &error);
        ASSERT_TRUE(conformed.has_value()) << error;
        Asset::MeshAsset mesh = original;
        ASSERT_TRUE(ConformMesh(mesh, *conformed, &error)) << error;
        const std::optional<Asset::AnimationClipAsset> result =
            ConformClip(clip, *fromMesh, *conformed, GetHumanoidReference(), &error);
        ASSERT_TRUE(result.has_value()) << error;

        for (const std::size_t frame : {0U, 1U, 2U}) {
            const std::vector<Asset::MeshVertex> before = Play(original, rig.skeleton.bones, clip.bones, frame);
            const std::vector<Asset::MeshVertex> after = Play(mesh, conformed->skeleton.bones, result->bones, frame);
            ASSERT_EQ(before.size(), after.size());
            for (std::size_t vertex = 0U; vertex < before.size(); ++vertex) {
                ExpectNear(after[vertex].position, Math::RotateVector(conformed->orientation, before[vertex].position), 2e-4F,
                           std::string{what} + ", frame " + std::to_string(frame) + ", " + rig.skeleton.bones[vertex].name);
            }
        }
    }
}

/// A tube from the left shoulder along the arm, its middle ring held half by the shoulder and half
/// by the arm: the place lowering an arm pinches.
[[nodiscard]] Asset::MeshAsset MakeShoulderTube(const TPoseRig& rig) {
    Asset::MeshAsset mesh = MakeMesh(rig);
    mesh.vertices.clear();
    mesh.skinningInfluences.clear();
    const Vector3 shoulder = rig.At("LeftShoulder");
    const Vector3 elbow = rig.At("LeftForeArm");
    const std::uint32_t clavicle = static_cast<std::uint32_t>(FindBone(rig.skeleton, "LeftShoulder"));
    const std::uint32_t arm = static_cast<std::uint32_t>(FindBone(rig.skeleton, "LeftArm"));
    constexpr int kSides = 8;
    for (int ring = 0; ring < 3; ++ring) {
        const Vector3 centre = shoulder + (elbow - shoulder) * (0.05F + 0.15F * static_cast<float>(ring));
        for (int side = 0; side < kSides; ++side) {
            const float angle = 6.2831853F * static_cast<float>(side) / static_cast<float>(kSides);
            Asset::MeshVertex vertex;
            vertex.position = centre + Vector3{0.0F, 0.06F * std::cos(angle), 0.06F * std::sin(angle)};
            vertex.normal = {0.0F, std::cos(angle), std::sin(angle)};
            mesh.vertices.push_back(vertex);
            Asset::MeshSkinningInfluence influence;
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

TEST(RetargetConformTest, ConformMesh_DualQuaternionBlendingPinchesTheShoulderLessThanLinear) {
    const TPoseRig rig;
    const Asset::MeshAsset original = MakeShoulderTube(rig);
    const ConformedRig conformed = Conform(rig);
    Asset::MeshAsset linear = original;
    Asset::MeshAsset dual = original;
    std::string error;
    ASSERT_TRUE(ConformMesh(linear, conformed, &error, SkinBlend::Linear)) << error;
    ASSERT_TRUE(ConformMesh(dual, conformed, &error, SkinBlend::DualQuaternion)) << error;
    const MeshDistortion linearChange = MeasureMeshDistortion(original, linear);
    const MeshDistortion dualChange = MeasureMeshDistortion(original, dual);
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
    EXPECT_FLOAT_EQ(MeasureMeshDistortion(original, original).maximumEdgeChange, 0.0F);
}


TEST(RetargetConformTest, ConformSkeleton_MakesOptionalBonesOnlyWhenAsked) {
    const TPoseRig rig;
    const HumanoidReference& reference = GetHumanoidReference();
    const HumanoidMatch match = MatchHumanoid(rig.skeleton, reference);
    const std::optional<ConformedRig> all = ConformSkeleton(rig.skeleton, match, reference, nullptr, ConformOptions{true});
    ASSERT_TRUE(all.has_value());
    EXPECT_EQ(all->skeleton.bones.size(), reference.skeleton.bones.size() + 1U);
    EXPECT_GE(FindBone(all->skeleton, "UpperArmBicep_L"), 0);
    const std::optional<ConformedRig> core = ConformSkeleton(rig.skeleton, match, reference);
    ASSERT_TRUE(core.has_value());
    EXPECT_LT(FindBone(core->skeleton, "UpperArmBicep_L"), 0);
    // The core still has its twists, IK targets and sockets.
    for (const char* name : {"UpperArmTwist1_L", "IKFoot_L", "IKHand_R", "IKFootRoot", "CenterOfMass", "Root"}) {
        EXPECT_GE(FindBone(core->skeleton, name), 0) << name;
    }
}

TEST(RetargetConformTest, ConformSkeleton_AnAllButCompleteRigIsRenamedNotRebuilt) {
    const HumanoidReference& reference = GetHumanoidReference();
    ConformOptions renameFirst;
    renameFirst.addBodyBonesOnlyWhenLacking = true;
    renameFirst.includeOptional = true;
    // A rig with every bone, helper and IK target: nothing is added.
    const RetargetSkeleton complete = reference.skeleton;
    const HumanoidMatch all = MatchHumanoid(complete, reference);
    EXPECT_FALSE(IsRigVeryLacking(all, reference));
    EXPECT_EQ(CountBodyBones(all, reference).missing, 0U);
    const std::optional<ConformedRig> same = ConformSkeleton(complete, all, reference, nullptr, renameFirst);
    ASSERT_TRUE(same.has_value());
    EXPECT_EQ(same->skeleton.bones.size(), complete.bones.size());
    EXPECT_TRUE(std::ranges::none_of(same->added, [](const bool added) { return added; }));

    // One twist bone short (a leaf): still renamed only - the twist is not made.
    std::int32_t twist = -1;
    for (std::size_t bone = 0U; bone < reference.info.size(); ++bone) {
        const bool leaf = std::ranges::none_of(complete.bones, [bone](const RetargetBone& b) { return b.parent == static_cast<std::int32_t>(bone); });
        if (reference.info[bone].kind == HumanoidBoneKind::Twist && leaf) {
            twist = static_cast<std::int32_t>(bone);
            break;
        }
    }
    ASSERT_GE(twist, 0);
    RetargetSkeleton short1;
    for (std::size_t bone = 0U; bone < complete.bones.size(); ++bone) {
        if (static_cast<std::int32_t>(bone) == twist) {
            continue;
        }
        RetargetBone copy = complete.bones[bone];
        copy.parent = copy.parent > twist ? copy.parent - 1 : copy.parent;
        short1.bones.push_back(std::move(copy));
    }
    const HumanoidMatch oneShort = MatchHumanoid(short1, reference);
    EXPECT_FALSE(IsRigVeryLacking(oneShort, reference));
    const std::optional<ConformedRig> renamed = ConformSkeleton(short1, oneShort, reference, nullptr, renameFirst);
    ASSERT_TRUE(renamed.has_value());
    EXPECT_EQ(renamed->skeleton.bones.size(), short1.bones.size()) << "no bone added";
    EXPECT_LT(FindBone(renamed->skeleton, complete.bones[static_cast<std::size_t>(twist)].name), 0);
    // Without the option the twist is made, as before.
    ConformOptions rebuild;
    rebuild.includeOptional = true;
    const std::optional<ConformedRig> rebuilt = ConformSkeleton(short1, oneShort, reference, nullptr, rebuild);
    ASSERT_TRUE(rebuilt.has_value());
    EXPECT_GE(FindBone(rebuilt->skeleton, complete.bones[static_cast<std::size_t>(twist)].name), 0);

    // A rig lacking much (the test rig has no fingers or twists) still gets them all.
    const TPoseRig sparse;
    const HumanoidMatch few = MatchHumanoid(sparse.skeleton, reference);
    if (IsRigVeryLacking(few, reference)) {
        const std::optional<ConformedRig> grown = ConformSkeleton(sparse.skeleton, few, reference, nullptr, renameFirst);
        ASSERT_TRUE(grown.has_value());
        EXPECT_GE(FindBone(grown->skeleton, "UpperArmTwist1_L"), 0);
    }
}

TEST(RetargetConformTest, ConformMesh_KeepingTheRestPoseMovesNoVertex) {
    const TPoseRig rig;
    const HumanoidReference& reference = GetHumanoidReference();
    const HumanoidMatch match = MatchHumanoid(rig.skeleton, reference);
    const std::optional<ConformedRig> conformed = ConformSkeleton(rig.skeleton, match, reference);
    ASSERT_TRUE(conformed.has_value());
    ASSERT_EQ(conformed->restSkeleton.bones.size(), conformed->skeleton.bones.size());
    // The rest skeleton is where the rig rests: each of its bones, in the humanoid's names.
    const std::vector<WorldTransform> rigWorld = ComputeWorldTransforms(rig.skeleton);
    const std::vector<WorldTransform> restWorld = ComputeWorldTransforms(conformed->restSkeleton);
    for (std::size_t bone = 0U; bone < rig.skeleton.bones.size(); ++bone) {
        const auto at = static_cast<std::size_t>(conformed->boneOfRigBone[bone]);
        ExpectNear(restWorld[at].position, rigWorld[bone].position, 1e-4F, rig.skeleton.bones[bone].name);
    }
    // A clip at the humanoid's rest poses the mesh into the A-pose: each bone's skin turn is the
    // move conforming measured for it.
    const std::vector<WorldTransform> poseWorld = ComputeWorldTransforms(conformed->skeleton);
    for (std::size_t bone = 0U; bone < rig.skeleton.bones.size(); ++bone) {
        const auto at = static_cast<std::size_t>(conformed->boneOfRigBone[bone]);
        const Quaternion skin = Math::Multiply(poseWorld[at].rotation, Math::Conjugate(restWorld[at].rotation));
        const Quaternion& move = conformed->moveOfRigBone[bone].rotation;
        EXPECT_NEAR(std::abs(skin.x * move.x + skin.y * move.y + skin.z * move.z + skin.w * move.w), 1.0F, 1e-4F)
            << rig.skeleton.bones[bone].name;
    }
}

} // namespace
} // namespace UVE::Retarget
