// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/retarget/retarget_humanoid_uve.h"

namespace UVE::Retarget {
namespace {

using Math::Vector3UVE;

constexpr float kHalfRootTwoUVE = 0.70710677F;

[[nodiscard]] Vector3UVE UnitUVE(const Vector3UVE& value) { return Math::NormalizeUVE(value); }

[[nodiscard]] Vector3UVE Across(const Vector3UVE& axis, const Vector3UVE& value) {
    const Vector3UVE unit = UnitUVE(axis);
    return UnitUVE(value - unit * Math::DotUVE(value, unit));
}

void ExpectNear(const Vector3UVE& actual, const Vector3UVE& expected, const float tolerance, const std::string& what) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, tolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, tolerance) << what;
}

/// A rig written down as world positions, every bone unrotated.
struct RigBuilderUVE final {
    RetargetSkeletonUVE skeleton;
    std::vector<Vector3UVE> world;

    void Add(const std::string& name, const std::string& parent, const Vector3UVE& position) {
        RetargetBoneUVE bone;
        bone.name = name;
        bone.parent = parent.empty() ? -1 : FindBoneUVE(skeleton, parent);
        bone.position = bone.parent < 0 ? position : position - world[static_cast<std::size_t>(bone.parent)];
        skeleton.bones.push_back(bone);
        world.push_back(position);
    }
};

/// A small rig in the source rig's naming, facing +Z: arms out level with the elbows bent forward
/// and down (so lowering the arms alone would leave the elbows rolled), palms roughly down, the
/// neck leaning forward and the head level, and IK and prop bones somewhere other than where they
/// rest in the reference.
[[nodiscard]] RigBuilderUVE MakeSourceRigUVE() {
    RigBuilderUVE rig;
    rig.Add("root", "", {0.0F, 0.0F, 0.0F});
    rig.Add("pelvis", "root", {0.0F, 1.0F, 0.0F});
    rig.Add("spine_01", "pelvis", {0.0F, 1.1F, 0.02F});
    rig.Add("spine_02", "spine_01", {0.0F, 1.3F, 0.0F});
    rig.Add("spine_03", "spine_02", {0.0F, 1.45F, -0.02F});
    rig.Add("neck_01", "spine_03", {0.0F, 1.55F, 0.02F});
    rig.Add("head", "neck_01", {0.0F, 1.65F, 0.06F});
    const Vector3UVE bent{0.5F, -0.4F, 0.7681146F}; // the forearm: forward and down from the upper arm
    const Vector3UVE thumbSide{-0.8660254F, 0.0F, 0.5F}; // across the palm-down hand, towards the thumb
    for (const char side : {'l', 'r'}) {
        const float x = side == 'l' ? 1.0F : -1.0F;
        const auto mirror = [x](const Vector3UVE& value) { return Vector3UVE{value.x * x, value.y, value.z}; };
        const std::string s = std::string{"_"} + side;
        const Vector3UVE elbow{0.45F * x, 1.45F, 0.0F};
        const Vector3UVE wrist = elbow + mirror(bent) * 0.25F;
        rig.Add("clavicle" + s, "spine_03", {0.02F * x, 1.45F, 0.0F});
        rig.Add("upperarm" + s, "clavicle" + s, {0.18F * x, 1.45F, 0.0F});
        rig.Add("lowerarm" + s, "upperarm" + s, elbow);
        rig.Add("hand" + s, "lowerarm" + s, wrist);
        rig.Add("middle_01" + s, "hand" + s, wrist + mirror(bent) * 0.1F);
        rig.Add("middle_02" + s, "middle_01" + s, wrist + mirror(bent) * 0.14F);
        rig.Add("index_01" + s, "hand" + s, wrist + mirror(bent) * 0.1F + mirror(thumbSide) * 0.025F);
        rig.Add("pinky_01" + s, "hand" + s, wrist + mirror(bent) * 0.09F - mirror(thumbSide) * 0.03F);
        rig.Add("thigh" + s, "pelvis", {0.1F * x, 0.95F, 0.0F});
        rig.Add("calf" + s, "thigh" + s, {0.11F * x, 0.5F, 0.03F});
        rig.Add("foot" + s, "calf" + s, {0.1F * x, 0.08F, 0.0F});
        rig.Add("ball" + s, "foot" + s, {0.11F * x, 0.02F, 0.12F});
    }
    rig.Add("weapon_r", "hand_r", rig.world[static_cast<std::size_t>(FindBoneUVE(rig.skeleton, "hand_r"))] +
                                      Vector3UVE{0.0F, -0.05F, 0.0F});
    rig.Add("ik_foot_root", "root", {0.0F, 0.0F, 0.0F});
    rig.Add("ik_foot_l", "ik_foot_root", {0.3F, 0.1F, 0.3F});
    return rig;
}

[[nodiscard]] Vector3UVE PositionOf(const HumanoidReferenceUVE& reference, const std::vector<WorldTransformUVE>& world,
                                    const std::string& name) {
    const std::int32_t bone = FindBoneUVE(reference.skeleton, name);
    EXPECT_GE(bone, 0) << name;
    return bone < 0 ? Vector3UVE{} : world[static_cast<std::size_t>(bone)].position;
}

[[nodiscard]] Vector3UVE DirectionUVE(const HumanoidReferenceUVE& reference, const std::vector<WorldTransformUVE>& world,
                                      const std::string& from, const std::string& to) {
    return UnitUVE(PositionOf(reference, world, to) - PositionOf(reference, world, from));
}

TEST(RetargetHumanoidUVETest, BuildHumanoidReferenceUVE_RenamesAndStandsTheRigInTheAPose) {
    const RigBuilderUVE rig = MakeSourceRigUVE();
    std::string error;
    const std::optional<HumanoidReferenceUVE> reference = BuildHumanoidReferenceUVE(rig.skeleton, &error);
    ASSERT_TRUE(reference.has_value()) << error;
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference->skeleton);

    EXPECT_EQ(reference->skeleton.bones[1].name, "Hips");
    EXPECT_GE(FindBoneUVE(reference->skeleton, "ForeArm_L"), 0);
    EXPECT_GE(FindBoneUVE(reference->skeleton, "Prop_R"), 0);
    EXPECT_GE(FindBoneUVE(reference->skeleton, "IKFootRoot"), 0);

    // Spine and neck straight up; arms 45 degrees down and straight to the fingertips; legs straight down.
    ExpectNear(DirectionUVE(*reference, world, "Hips", "Spine1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "hips");
    ExpectNear(DirectionUVE(*reference, world, "Spine3", "Neck1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "chest");
    ExpectNear(DirectionUVE(*reference, world, "Neck1", "Head"), {0.0F, 1.0F, 0.0F}, 1e-4F, "neck");
    for (const auto& [side, x] : {std::pair{std::string{"_L"}, 1.0F}, std::pair{std::string{"_R"}, -1.0F}}) {
        const Vector3UVE arm{kHalfRootTwoUVE * x, -kHalfRootTwoUVE, 0.0F};
        ExpectNear(DirectionUVE(*reference, world, "UpperArm" + side, "ForeArm" + side), arm, 1e-4F, "upper arm" + side);
        ExpectNear(DirectionUVE(*reference, world, "ForeArm" + side, "Hand" + side), arm, 1e-4F, "forearm" + side);
        ExpectNear(DirectionUVE(*reference, world, "Hand" + side, "Middle1" + side), arm, 1e-4F, "hand" + side);
        ExpectNear(DirectionUVE(*reference, world, "Middle1" + side, "Middle2" + side), arm, 1e-4F, "finger" + side);
        ExpectNear(DirectionUVE(*reference, world, "Thigh" + side, "Shin" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "thigh" + side);
        ExpectNear(DirectionUVE(*reference, world, "Shin" + side, "Foot" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "shin" + side);
        // Palm toward the body with the thumb side forward: the knuckles run front to back.
        const Vector3UVE knuckles = PositionOf(*reference, world, "Index1" + side) - PositionOf(*reference, world, "Pinky1" + side);
        ExpectNear(Across(arm, knuckles), {0.0F, 0.0F, 1.0F}, 1e-4F, "palm" + side);
    }
}

TEST(RetargetHumanoidUVETest, BuildHumanoidReferenceUVE_KeepsEveryBoneLengthAndTheElbowsBendingForward) {
    const RigBuilderUVE rig = MakeSourceRigUVE();
    const std::optional<HumanoidReferenceUVE> reference = BuildHumanoidReferenceUVE(rig.skeleton);
    ASSERT_TRUE(reference.has_value());
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference->skeleton);
    for (std::size_t index = 0U; index < world.size(); ++index) {
        const std::int32_t parent = reference->skeleton.bones[index].parent;
        const HumanoidBoneKindUVE kind = reference->info[index].kind;
        if (parent < 0 || kind == HumanoidBoneKindUVE::IK || reference->skeleton.bones[index].name == "Hips") {
            continue; // IK bones move onto what they follow; the hips move to put the feet on the ground
        }
        const float before = Math::LengthUVE(rig.world[index] - rig.world[static_cast<std::size_t>(parent)]);
        const float after = Math::LengthUVE(world[index].position - world[static_cast<std::size_t>(parent)].position);
        EXPECT_NEAR(after, before, 1e-4F) << reference->skeleton.bones[index].name;
    }
    // The source bent each elbow forward (about upper arm x forearm); the straightened arm must
    // still bend that way: the same axis, carried by the upper arm, now lies across the arm and
    // swings the forearm towards +Z.
    for (const auto& [source, name, x] : {std::tuple{std::string{"_l"}, std::string{"_L"}, 1.0F},
                                          std::tuple{std::string{"_r"}, std::string{"_R"}, -1.0F}}) {
        const auto at = [&rig](const std::string& bone) { return rig.world[static_cast<std::size_t>(FindBoneUVE(rig.skeleton, bone))]; };
        const Vector3UVE bendsAbout = UnitUVE(Math::CrossUVE(at("lowerarm" + source) - at("upperarm" + source),
                                                             at("hand" + source) - at("lowerarm" + source)));
        const std::int32_t upper = FindBoneUVE(reference->skeleton, "UpperArm" + name);
        const Vector3UVE now = Math::RotateVectorUVE(world[static_cast<std::size_t>(upper)].rotation, bendsAbout);
        const Vector3UVE arm{kHalfRootTwoUVE * x, -kHalfRootTwoUVE, 0.0F};
        ExpectNear(UnitUVE(Math::CrossUVE(now, arm)), {0.0F, 0.0F, 1.0F}, 1e-4F, "elbow" + name);
    }
}

TEST(RetargetHumanoidUVETest, BuildHumanoidReferenceUVE_KeepsTheHeadLevelAndTheFeetOnTheGround) {
    const RigBuilderUVE rig = MakeSourceRigUVE();
    const std::optional<HumanoidReferenceUVE> reference = BuildHumanoidReferenceUVE(rig.skeleton);
    ASSERT_TRUE(reference.has_value());
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference->skeleton);
    // The source head and feet are unrotated (level, looking ahead); straightening the leaning
    // neck and the bent legs must not tip them.
    for (const char* name : {"Head", "Foot_L", "Foot_R"}) {
        const Math::QuaternionUVE rotation = world[static_cast<std::size_t>(FindBoneUVE(reference->skeleton, name))].rotation;
        ExpectNear(Math::RotateVectorUVE(rotation, {0.0F, 1.0F, 0.0F}), {0.0F, 1.0F, 0.0F}, 1e-4F, name);
        ExpectNear(Math::RotateVectorUVE(rotation, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F}, 1e-4F, name);
    }
    // The lowest toe stays where it touched down; the hips measure from there.
    EXPECT_NEAR(PositionOf(*reference, world, "Toe_L").y, 0.02F, 1e-4F);
    EXPECT_NEAR(reference->hipsHeight, PositionOf(*reference, world, "Hips").y - 0.02F, 1e-4F);
    // IK bones rest on what they follow.
    ExpectNear(PositionOf(*reference, world, "IKFoot_L"), PositionOf(*reference, world, "Foot_L"), 1e-5F, "ik foot");
    EXPECT_EQ(reference->info[static_cast<std::size_t>(FindBoneUVE(reference->skeleton, "IKFoot_L"))].follows,
              FindBoneUVE(reference->skeleton, "Foot_L"));
}

TEST(RetargetHumanoidUVETest, BuildHumanoidReferenceUVE_RefusesARigTurnedTheOtherWay) {
    RigBuilderUVE rig = MakeSourceRigUVE();
    for (RetargetBoneUVE& bone : rig.skeleton.bones) {
        bone.position.x = -bone.position.x; // its left side toward -X: facing -Z
    }
    std::string error;
    EXPECT_FALSE(BuildHumanoidReferenceUVE(rig.skeleton, &error).has_value());
    EXPECT_NE(error.find("facing +Z"), std::string::npos) << error;

    RetargetSkeletonUVE noHips;
    noHips.bones.push_back(RetargetBoneUVE{"root", -1, {}, {}, 1.0F});
    EXPECT_FALSE(BuildHumanoidReferenceUVE(noHips, &error).has_value());
    EXPECT_NE(error.find("hips"), std::string::npos) << error;
}

TEST(RetargetHumanoidUVETest, WriteAndParseHumanoidReferenceUVE_RoundTrip) {
    const std::optional<HumanoidReferenceUVE> built = BuildHumanoidReferenceUVE(MakeSourceRigUVE().skeleton);
    ASSERT_TRUE(built.has_value());
    std::string error;
    const std::optional<HumanoidReferenceUVE> parsed = ParseHumanoidReferenceUVE(WriteHumanoidReferenceUVE(*built), &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(*parsed, *built);

    EXPECT_FALSE(ParseHumanoidReferenceUVE(R"({"format":"something-else","bones":[]})", &error).has_value());
    EXPECT_FALSE(ParseHumanoidReferenceUVE(
                     R"({"format":"uve-humanoid-v1","bones":[{"name":"Root","parent":"","position":[0,0],"rotation":[0,0,0,1]}]})",
                     &error)
                     .has_value());
    EXPECT_FALSE(ParseHumanoidReferenceUVE("not json", &error).has_value());
}

// The reference the engine ships, checked number by number: the pose every rig is conformed to.
TEST(RetargetHumanoidUVETest, GetHumanoidReferenceUVE_IsASymmetricAPoseStandingOnTheGround) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    ASSERT_EQ(reference.skeleton.bones.size(), 162U);
    ASSERT_TRUE(IsRetargetSkeletonValidUVE(reference.skeleton));
    EXPECT_EQ(reference.skeleton.bones[0].name, "Root");
    EXPECT_NEAR(reference.hipsHeight, 0.954F, 0.01F);
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference.skeleton);
    ExpectNear(world[0].position, {}, 1e-6F, "root");

    ExpectNear(DirectionUVE(reference, world, "Hips", "Spine1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "hips");
    ExpectNear(DirectionUVE(reference, world, "Spine5", "Neck1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "chest");
    ExpectNear(DirectionUVE(reference, world, "Neck2", "Head"), {0.0F, 1.0F, 0.0F}, 1e-4F, "neck");
    for (const auto& [side, x] : {std::pair{std::string{"_L"}, 1.0F}, std::pair{std::string{"_R"}, -1.0F}}) {
        const Vector3UVE arm{kHalfRootTwoUVE * x, -kHalfRootTwoUVE, 0.0F};
        ExpectNear(DirectionUVE(reference, world, "UpperArm" + side, "ForeArm" + side), arm, 1e-4F, "upper arm" + side);
        ExpectNear(DirectionUVE(reference, world, "ForeArm" + side, "Hand" + side), arm, 1e-4F, "forearm" + side);
        ExpectNear(DirectionUVE(reference, world, "Thigh" + side, "Shin" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "thigh" + side);
        ExpectNear(DirectionUVE(reference, world, "Shin" + side, "Foot" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "shin" + side);
        const Vector3UVE knuckles = PositionOf(reference, world, "Index1" + side) - PositionOf(reference, world, "Pinky1" + side);
        ExpectNear(Across(arm, knuckles), {0.0F, 0.0F, 1.0F}, 1e-3F, "palm" + side);
        // Toes ahead of the heels.
        EXPECT_GT(PositionOf(reference, world, "Toe" + side).z, PositionOf(reference, world, "Foot" + side).z + 0.1F);
    }
    // The head looks straight ahead, level.
    const Math::QuaternionUVE head = world[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, "Head"))].rotation;
    float up = -1.0F;
    for (const Vector3UVE axis : {Vector3UVE{1.0F, 0.0F, 0.0F}, Vector3UVE{0.0F, 1.0F, 0.0F}, Vector3UVE{0.0F, 0.0F, 1.0F}}) {
        up = std::max(up, std::abs(Math::RotateVectorUVE(head, axis).y));
    }
    EXPECT_NEAR(up, 1.0F, 1e-4F);

    // Left and right mirror each other; the few millimetres left are the source's own helpers.
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const std::string& name = reference.skeleton.bones[index].name;
        if (name.size() < 2U || name.substr(name.size() - 2U) != "_L") {
            continue;
        }
        const std::int32_t right = FindBoneUVE(reference.skeleton, name.substr(0U, name.size() - 2U) + "_R");
        ASSERT_GE(right, 0) << name;
        const Vector3UVE left = world[index].position;
        const Vector3UVE mirrored = world[static_cast<std::size_t>(right)].position;
        ExpectNear({-left.x, left.y, left.z}, mirrored, 0.01F, name);
    }
    // IK bones rest on the bones they follow.
    for (std::size_t index = 0U; index < reference.info.size(); ++index) {
        if (const std::int32_t follows = reference.info[index].follows; follows >= 0) {
            ExpectNear(world[index].position, world[static_cast<std::size_t>(follows)].position, 1e-4F,
                       reference.skeleton.bones[index].name);
        }
    }
}

} // namespace
} // namespace UVE::Retarget
