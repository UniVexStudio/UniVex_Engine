// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_humanoid.h"

namespace UVE::Retarget {
namespace {

using Math::Vector3;

constexpr float kHalfRootTwo = 0.70710677F;

[[nodiscard]] Vector3 Unit(const Vector3& value) { return Math::Normalize(value); }

[[nodiscard]] Vector3 Across(const Vector3& axis, const Vector3& value) {
    const Vector3 unit = Unit(axis);
    return Unit(value - unit * Math::Dot(value, unit));
}

void ExpectNear(const Vector3& actual, const Vector3& expected, const float tolerance, const std::string& what) {
    EXPECT_NEAR(actual.x, expected.x, tolerance) << what;
    EXPECT_NEAR(actual.y, expected.y, tolerance) << what;
    EXPECT_NEAR(actual.z, expected.z, tolerance) << what;
}

/// A rig written down as world positions, every bone unrotated.
struct RigBuilder final {
    RetargetSkeleton skeleton;
    std::vector<Vector3> world;

    void Add(const std::string& name, const std::string& parent, const Vector3& position) {
        RetargetBone bone;
        bone.name = name;
        bone.parent = parent.empty() ? -1 : FindBone(skeleton, parent);
        bone.position = bone.parent < 0 ? position : position - world[static_cast<std::size_t>(bone.parent)];
        skeleton.bones.push_back(bone);
        world.push_back(position);
    }
};

/// A small rig in the source rig's naming, facing +Z: arms out level with the elbows bent forward
/// and down (so lowering the arms alone would leave the elbows rolled), palms roughly down, the
/// neck leaning forward and the head level, and IK and prop bones somewhere other than where they
/// rest in the reference.
[[nodiscard]] RigBuilder MakeSourceRig() {
    RigBuilder rig;
    rig.Add("root", "", {0.0F, 0.0F, 0.0F});
    rig.Add("pelvis", "root", {0.0F, 1.0F, 0.0F});
    rig.Add("spine_01", "pelvis", {0.0F, 1.1F, 0.02F});
    rig.Add("spine_02", "spine_01", {0.0F, 1.3F, 0.0F});
    rig.Add("spine_03", "spine_02", {0.0F, 1.45F, -0.02F});
    rig.Add("neck_01", "spine_03", {0.0F, 1.55F, 0.02F});
    rig.Add("head", "neck_01", {0.0F, 1.65F, 0.06F});
    const Vector3 bent{0.5F, -0.4F, 0.7681146F}; // the forearm: forward and down from the upper arm
    const Vector3 thumbSide{-0.8660254F, 0.0F, 0.5F}; // across the palm-down hand, towards the thumb
    for (const char side : {'l', 'r'}) {
        const float x = side == 'l' ? 1.0F : -1.0F;
        const auto mirror = [x](const Vector3& value) { return Vector3{value.x * x, value.y, value.z}; };
        const std::string s = std::string{"_"} + side;
        const Vector3 elbow{0.45F * x, 1.45F, 0.0F};
        const Vector3 wrist = elbow + mirror(bent) * 0.25F;
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
    rig.Add("weapon_r", "hand_r", rig.world[static_cast<std::size_t>(FindBone(rig.skeleton, "hand_r"))] +
                                      Vector3{0.0F, -0.05F, 0.0F});
    rig.Add("ik_foot_root", "root", {0.0F, 0.0F, 0.0F});
    rig.Add("ik_foot_l", "ik_foot_root", {0.3F, 0.1F, 0.3F});
    return rig;
}

[[nodiscard]] Vector3 PositionOf(const HumanoidReference& reference, const std::vector<WorldTransform>& world,
                                    const std::string& name) {
    const std::int32_t bone = FindBone(reference.skeleton, name);
    EXPECT_GE(bone, 0) << name;
    return bone < 0 ? Vector3{} : world[static_cast<std::size_t>(bone)].position;
}

[[nodiscard]] Vector3 Direction(const HumanoidReference& reference, const std::vector<WorldTransform>& world,
                                      const std::string& from, const std::string& to) {
    return Unit(PositionOf(reference, world, to) - PositionOf(reference, world, from));
}

TEST(RetargetHumanoidTest, BuildHumanoidReference_RenamesAndStandsTheRigInTheAPose) {
    const RigBuilder rig = MakeSourceRig();
    std::string error;
    const std::optional<HumanoidReference> reference = BuildHumanoidReference(rig.skeleton, &error);
    ASSERT_TRUE(reference.has_value()) << error;
    const std::vector<WorldTransform> world = ComputeWorldTransforms(reference->skeleton);

    EXPECT_EQ(reference->skeleton.bones[1].name, "Hips");
    EXPECT_GE(FindBone(reference->skeleton, "ForeArm_L"), 0);
    EXPECT_GE(FindBone(reference->skeleton, "Prop_R"), 0);
    EXPECT_GE(FindBone(reference->skeleton, "IKFootRoot"), 0);

    // Spine and neck straight up; arms 45 degrees down and straight to the fingertips; legs straight down.
    ExpectNear(Direction(*reference, world, "Hips", "Spine1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "hips");
    ExpectNear(Direction(*reference, world, "Spine3", "Neck1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "chest");
    ExpectNear(Direction(*reference, world, "Neck1", "Head"), {0.0F, 1.0F, 0.0F}, 1e-4F, "neck");
    for (const auto& [side, x] : {std::pair{std::string{"_L"}, 1.0F}, std::pair{std::string{"_R"}, -1.0F}}) {
        const Vector3 arm{kHalfRootTwo * x, -kHalfRootTwo, 0.0F};
        ExpectNear(Direction(*reference, world, "UpperArm" + side, "ForeArm" + side), arm, 1e-4F, "upper arm" + side);
        ExpectNear(Direction(*reference, world, "ForeArm" + side, "Hand" + side), arm, 1e-4F, "forearm" + side);
        ExpectNear(Direction(*reference, world, "Hand" + side, "Middle1" + side), arm, 1e-4F, "hand" + side);
        ExpectNear(Direction(*reference, world, "Middle1" + side, "Middle2" + side), arm, 1e-4F, "finger" + side);
        ExpectNear(Direction(*reference, world, "Thigh" + side, "Shin" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "thigh" + side);
        ExpectNear(Direction(*reference, world, "Shin" + side, "Foot" + side), {0.0F, -1.0F, 0.0F}, 1e-4F, "shin" + side);
        // Palm toward the body with the thumb side forward: the knuckles run front to back.
        const Vector3 knuckles = PositionOf(*reference, world, "Index1" + side) - PositionOf(*reference, world, "Pinky1" + side);
        ExpectNear(Across(arm, knuckles), {0.0F, 0.0F, 1.0F}, 1e-4F, "palm" + side);
    }
}

TEST(RetargetHumanoidTest, BuildHumanoidReference_KeepsEveryBoneLengthAndTheElbowsBendingForward) {
    const RigBuilder rig = MakeSourceRig();
    const std::optional<HumanoidReference> reference = BuildHumanoidReference(rig.skeleton);
    ASSERT_TRUE(reference.has_value());
    const std::vector<WorldTransform> world = ComputeWorldTransforms(reference->skeleton);
    for (std::size_t index = 0U; index < world.size(); ++index) {
        const std::int32_t parent = reference->skeleton.bones[index].parent;
        const HumanoidBoneKind kind = reference->info[index].kind;
        if (parent < 0 || kind == HumanoidBoneKind::IK || reference->skeleton.bones[index].name == "Hips") {
            continue; // IK bones move onto what they follow; the hips move to put the feet on the ground
        }
        const float before = Math::Length(rig.world[index] - rig.world[static_cast<std::size_t>(parent)]);
        const float after = Math::Length(world[index].position - world[static_cast<std::size_t>(parent)].position);
        EXPECT_NEAR(after, before, 1e-4F) << reference->skeleton.bones[index].name;
    }
    // The source bent each elbow forward (about upper arm x forearm); the straightened arm must
    // still bend that way: the same axis, carried by the upper arm, now lies across the arm and
    // swings the forearm towards +Z.
    for (const auto& [source, name, x] : {std::tuple{std::string{"_l"}, std::string{"_L"}, 1.0F},
                                          std::tuple{std::string{"_r"}, std::string{"_R"}, -1.0F}}) {
        const auto at = [&rig](const std::string& bone) { return rig.world[static_cast<std::size_t>(FindBone(rig.skeleton, bone))]; };
        const Vector3 bendsAbout = Unit(Math::Cross(at("lowerarm" + source) - at("upperarm" + source),
                                                             at("hand" + source) - at("lowerarm" + source)));
        const std::int32_t upper = FindBone(reference->skeleton, "UpperArm" + name);
        const Vector3 now = Math::RotateVector(world[static_cast<std::size_t>(upper)].rotation, bendsAbout);
        const Vector3 arm{kHalfRootTwo * x, -kHalfRootTwo, 0.0F};
        ExpectNear(Unit(Math::Cross(now, arm)), {0.0F, 0.0F, 1.0F}, 1e-4F, "elbow" + name);
    }
}

TEST(RetargetHumanoidTest, BuildHumanoidReference_KeepsTheHeadLevelAndTheFeetOnTheGround) {
    const RigBuilder rig = MakeSourceRig();
    const std::optional<HumanoidReference> reference = BuildHumanoidReference(rig.skeleton);
    ASSERT_TRUE(reference.has_value());
    const std::vector<WorldTransform> world = ComputeWorldTransforms(reference->skeleton);
    // The source head and feet are unrotated (level, looking ahead); straightening the leaning
    // neck and the bent legs must not tip them.
    for (const char* name : {"Head", "Foot_L", "Foot_R"}) {
        const Math::Quaternion rotation = world[static_cast<std::size_t>(FindBone(reference->skeleton, name))].rotation;
        ExpectNear(Math::RotateVector(rotation, {0.0F, 1.0F, 0.0F}), {0.0F, 1.0F, 0.0F}, 1e-4F, name);
        ExpectNear(Math::RotateVector(rotation, {0.0F, 0.0F, 1.0F}), {0.0F, 0.0F, 1.0F}, 1e-4F, name);
    }
    // The lowest toe stays where it touched down; the hips measure from there.
    EXPECT_NEAR(PositionOf(*reference, world, "Toe_L").y, 0.02F, 1e-4F);
    EXPECT_NEAR(reference->hipsHeight, PositionOf(*reference, world, "Hips").y - 0.02F, 1e-4F);
    // IK bones rest on what they follow.
    ExpectNear(PositionOf(*reference, world, "IKFoot_L"), PositionOf(*reference, world, "Foot_L"), 1e-5F, "ik foot");
    EXPECT_EQ(reference->info[static_cast<std::size_t>(FindBone(reference->skeleton, "IKFoot_L"))].follows,
              FindBone(reference->skeleton, "Foot_L"));
}

TEST(RetargetHumanoidTest, BuildHumanoidReference_RefusesARigTurnedTheOtherWay) {
    RigBuilder rig = MakeSourceRig();
    for (RetargetBone& bone : rig.skeleton.bones) {
        bone.position.x = -bone.position.x; // its left side toward -X: facing -Z
    }
    std::string error;
    EXPECT_FALSE(BuildHumanoidReference(rig.skeleton, &error).has_value());
    EXPECT_NE(error.find("facing +Z"), std::string::npos) << error;

    RetargetSkeleton noHips;
    noHips.bones.push_back(RetargetBone{"root", -1, {}, {}, 1.0F});
    EXPECT_FALSE(BuildHumanoidReference(noHips, &error).has_value());
    EXPECT_NE(error.find("hips"), std::string::npos) << error;
}

TEST(RetargetHumanoidTest, WriteAndParseHumanoidReference_RoundTrip) {
    const std::optional<HumanoidReference> built = BuildHumanoidReference(MakeSourceRig().skeleton);
    ASSERT_TRUE(built.has_value());
    std::string error;
    const std::optional<HumanoidReference> parsed = ParseHumanoidReference(WriteHumanoidReference(*built), &error);
    ASSERT_TRUE(parsed.has_value()) << error;
    EXPECT_EQ(*parsed, *built);

    EXPECT_FALSE(ParseHumanoidReference(R"({"format":"something-else","bones":[]})", &error).has_value());
    EXPECT_FALSE(ParseHumanoidReference(
                     R"({"format":"uve-humanoid-v1","bones":[{"name":"Root","parent":"","position":[0,0],"rotation":[0,0,0,1]}]})",
                     &error)
                     .has_value());
    EXPECT_FALSE(ParseHumanoidReference("not json", &error).has_value());
}

// The reference the engine ships, checked number by number: the pose every rig is conformed to.
TEST(RetargetHumanoidTest, GetHumanoidReference_IsASymmetricAPoseStandingOnTheGround) {
    const HumanoidReference& reference = GetHumanoidReference();
    ASSERT_EQ(reference.skeleton.bones.size(), 162U);
    ASSERT_TRUE(IsRetargetSkeletonValid(reference.skeleton));
    EXPECT_EQ(reference.skeleton.bones[0].name, "Root");
    EXPECT_NEAR(reference.hipsHeight, 0.954F, 0.01F);
    const std::vector<WorldTransform> world = ComputeWorldTransforms(reference.skeleton);
    ExpectNear(world[0].position, {}, 1e-6F, "root");

    ExpectNear(Direction(reference, world, "Hips", "Spine1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "hips");
    ExpectNear(Direction(reference, world, "Spine5", "Neck1"), {0.0F, 1.0F, 0.0F}, 1e-4F, "chest");
    ExpectNear(Direction(reference, world, "Neck2", "Head"), {0.0F, 1.0F, 0.0F}, 1e-4F, "neck");
    // The A-pose measured from a production rig: the upper arms about 55 degrees below level, the
    // elbows bent forward, the legs a little apart.
    for (const auto& [side, x] : {std::pair{std::string{"_L"}, 1.0F}, std::pair{std::string{"_R"}, -1.0F}}) {
        const Vector3 upper = Direction(reference, world, "UpperArm" + side, "ForeArm" + side);
        ExpectNear(upper, Math::Normalize(Vector3{0.576F * x, -0.817F, 0.023F}), 2e-3F, "upper arm" + side);
        const float below = std::atan2(-upper.y, std::abs(upper.x)) * 57.29578F;
        EXPECT_GT(below, 54.0F) << side;
        EXPECT_LT(below, 56.0F) << side;
        ExpectNear(Direction(reference, world, "ForeArm" + side, "Hand" + side),
                   Math::Normalize(Vector3{0.468F * x, -0.603F, 0.646F}), 2e-3F, "forearm" + side);
        ExpectNear(Direction(reference, world, "Thigh" + side, "Shin" + side),
                   Math::Normalize(Vector3{0.055F * x, -0.999F, 0.001F}), 2e-3F, "thigh" + side);
        ExpectNear(Direction(reference, world, "Shin" + side, "Foot" + side),
                   Math::Normalize(Vector3{0.042F * x, -0.996F, -0.085F}), 2e-3F, "shin" + side);
        // The palm faces the body, the thumb side forward.
        const Vector3 knuckles = PositionOf(reference, world, "Index1" + side) - PositionOf(reference, world, "Pinky1" + side);
        EXPECT_GT(knuckles.z, 0.0F) << "index ahead of pinky" + side;
        // Toes ahead of the heels.
        EXPECT_GT(PositionOf(reference, world, "Toe" + side).z, PositionOf(reference, world, "Foot" + side).z + 0.1F);
    }
    // The correctives are optional (generated only when asked for); every other bone is core.
    for (std::size_t index = 0U; index < reference.info.size(); ++index) {
        EXPECT_EQ(reference.info[index].optional, reference.info[index].kind == HumanoidBoneKind::Corrective)
            << reference.skeleton.bones[index].name;
        if (reference.info[index].optional) {
            const std::int32_t parent = reference.skeleton.bones[index].parent;
            ASSERT_GE(parent, 0);
            EXPECT_TRUE(reference.info[static_cast<std::size_t>(parent)].optional ||
                        reference.info[static_cast<std::size_t>(parent)].kind != HumanoidBoneKind::Corrective)
                << reference.skeleton.bones[index].name;
        }
    }
    // The head looks straight ahead, level.
    const Math::Quaternion head = world[static_cast<std::size_t>(FindBone(reference.skeleton, "Head"))].rotation;
    float up = -1.0F;
    for (const Vector3 axis : {Vector3{1.0F, 0.0F, 0.0F}, Vector3{0.0F, 1.0F, 0.0F}, Vector3{0.0F, 0.0F, 1.0F}}) {
        up = std::max(up, std::abs(Math::RotateVector(head, axis).y));
    }
    EXPECT_NEAR(up, 1.0F, 1e-4F);

    // Left and right mirror each other; the few millimetres left are the source's own helpers.
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const std::string& name = reference.skeleton.bones[index].name;
        if (name.size() < 2U || name.substr(name.size() - 2U) != "_L") {
            continue;
        }
        const std::int32_t right = FindBone(reference.skeleton, name.substr(0U, name.size() - 2U) + "_R");
        ASSERT_GE(right, 0) << name;
        const Vector3 left = world[index].position;
        const Vector3 mirrored = world[static_cast<std::size_t>(right)].position;
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
