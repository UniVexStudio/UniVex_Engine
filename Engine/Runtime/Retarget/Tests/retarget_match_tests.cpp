// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_match.h"

namespace UVE::Retarget {
namespace {

using Math::Vector3;

/// An auto-rigger's skeleton (fewer bones: no root, three spine bones, one neck bone, no twists,
/// metacarpals, IK or props) standing where the reference's matching bones stand, `scale` times
/// as tall. Each entry is the rig's bone, its parent and the reference bone it sits on.
struct AutoRig final {
    RetargetSkeleton skeleton;

    AutoRig(const float scale, const std::vector<std::pair<std::string, Vector3>>& moved = {}) {
        const HumanoidReference& reference = GetHumanoidReference();
        const std::vector<WorldTransform> world = ComputeWorldTransforms(reference.skeleton);
        std::vector<std::tuple<std::string, std::string, std::string>> bones{
            {"mixamorig:Hips", "", "Hips"},
            {"mixamorig:Spine", "mixamorig:Hips", "Spine1"},
            {"mixamorig:Spine1", "mixamorig:Spine", "Spine3"},
            {"mixamorig:Spine2", "mixamorig:Spine1", "Spine5"},
            {"mixamorig:Neck", "mixamorig:Spine2", "Neck1"},
            {"mixamorig:Head", "mixamorig:Neck", "Head"},
            {"mixamorig:HeadTop_End", "mixamorig:Head", "Head"},
        };
        for (const char* side : {"Left", "Right"}) {
            const std::string s = side;
            const std::string r = s == "Left" ? "_L" : "_R";
            const std::string p = "mixamorig:" + s;
            bones.emplace_back(p + "Shoulder", "mixamorig:Spine2", "Clavicle" + r);
            bones.emplace_back(p + "Arm", p + "Shoulder", "UpperArm" + r);
            bones.emplace_back(p + "ForeArm", p + "Arm", "ForeArm" + r);
            bones.emplace_back(p + "Hand", p + "ForeArm", "Hand" + r);
            bones.emplace_back(p + "HandIndex1", p + "Hand", "Index1" + r);
            bones.emplace_back(p + "HandIndex2", p + "HandIndex1", "Index2" + r);
            bones.emplace_back(p + "UpLeg", "mixamorig:Hips", "Thigh" + r);
            bones.emplace_back(p + "Leg", p + "UpLeg", "Shin" + r);
            bones.emplace_back(p + "Foot", p + "Leg", "Foot" + r);
            bones.emplace_back(p + "ToeBase", p + "Foot", "Toe" + r);
        }
        std::vector<Vector3> placed;
        for (const auto& [name, parent, on] : bones) {
            Vector3 position = world[static_cast<std::size_t>(FindBone(reference.skeleton, on))].position * scale;
            for (const auto& [movedName, to] : moved) {
                if (movedName == name) {
                    position = to;
                }
            }
            RetargetBone bone;
            bone.name = name;
            bone.parent = parent.empty() ? -1 : FindBone(skeleton, parent);
            bone.position = bone.parent < 0 ? position : position - placed[static_cast<std::size_t>(bone.parent)];
            skeleton.bones.push_back(bone);
            placed.push_back(position);
        }
    }

    void Reparent(const std::string& name, const std::string& parent) {
        const std::vector<WorldTransform> world = ComputeWorldTransforms(skeleton);
        const std::int32_t bone = FindBone(skeleton, name);
        const std::int32_t newParent = FindBone(skeleton, parent);
        RetargetBone& entry = skeleton.bones[static_cast<std::size_t>(bone)];
        entry.parent = newParent;
        entry.position = world[static_cast<std::size_t>(bone)].position - world[static_cast<std::size_t>(newParent)].position;
    }
};

[[nodiscard]] const JointReport& Joint(const HumanoidMatch& match, const std::string& referenceName) {
    const std::int32_t index = FindBone(GetHumanoidReference().skeleton, referenceName);
    EXPECT_GE(index, 0) << referenceName;
    return match.joints[static_cast<std::size_t>(index < 0 ? 0 : index)];
}

TEST(RetargetMatchTest, MatchHumanoid_FindsEveryBoneOfTheReferenceInItself) {
    const HumanoidReference& reference = GetHumanoidReference();
    const HumanoidMatch match = MatchHumanoid(reference.skeleton, reference);
    EXPECT_EQ(match.Count(JointStatus::Good), reference.skeleton.bones.size());
    EXPECT_NEAR(match.heightScale, 1.0F, 1e-5F);
    for (std::size_t index = 0U; index < match.referenceOfBone.size(); ++index) {
        EXPECT_EQ(match.referenceOfBone[index], static_cast<std::int32_t>(index)) << reference.skeleton.bones[index].name;
    }
}

TEST(RetargetMatchTest, MatchHumanoid_FitsAShorterSpineAndMarksWhatTheRigLacks) {
    const AutoRig rig(1.1F);
    const HumanoidMatch match = MatchHumanoid(rig.skeleton, GetHumanoidReference());
    EXPECT_NEAR(match.heightScale, 1.1F, 1e-3F);

    // Three spine bones spread over five: ends to ends, the middle to the middle.
    EXPECT_EQ(Joint(match, "Spine1").bone, FindBone(rig.skeleton, "mixamorig:Spine"));
    EXPECT_EQ(Joint(match, "Spine3").bone, FindBone(rig.skeleton, "mixamorig:Spine1"));
    EXPECT_EQ(Joint(match, "Spine5").bone, FindBone(rig.skeleton, "mixamorig:Spine2"));
    EXPECT_EQ(Joint(match, "Spine2").status, JointStatus::Missing);
    EXPECT_EQ(Joint(match, "Neck1").bone, FindBone(rig.skeleton, "mixamorig:Neck"));
    EXPECT_EQ(Joint(match, "Neck2").status, JointStatus::Missing);

    for (const char* name : {"Hips", "Spine1", "Spine5", "Head", "Clavicle_L", "UpperArm_L", "ForeArm_R", "Hand_L",
                             "Index1_L", "Thigh_R", "Shin_L", "Foot_R", "Toe_L"}) {
        EXPECT_EQ(Joint(match, name).status, JointStatus::Good) << name << ": " << Joint(match, name).reason;
    }
    for (const char* name : {"Root", "UpperArmTwist1_L", "IKFoot_L", "Prop_R", "IndexMetacarpal_L", "Thumb1_L"}) {
        EXPECT_EQ(Joint(match, name).status, JointStatus::Missing) << name;
    }
    // The end-of-head helper has no place in the humanoid and is kept as it is.
    EXPECT_EQ(match.referenceOfBone[static_cast<std::size_t>(FindBone(rig.skeleton, "mixamorig:HeadTop_End"))], -1);

    const std::size_t found = match.Count(JointStatus::Good) + match.Count(JointStatus::Warning) +
                              match.Count(JointStatus::Broken);
    EXPECT_EQ(found, rig.skeleton.bones.size() - 1U); // all but HeadTop_End
    EXPECT_EQ(found + match.Count(JointStatus::Missing), GetHumanoidReference().skeleton.bones.size());
}

TEST(RetargetMatchTest, MatchHumanoid_FlagsAWrongParentAndABoneWithNoLength) {
    AutoRig rig(1.0F);
    rig.Reparent("mixamorig:LeftForeArm", "mixamorig:Spine2");
    HumanoidMatch match = MatchHumanoid(rig.skeleton, GetHumanoidReference());
    EXPECT_EQ(Joint(match, "ForeArm_L").status, JointStatus::Broken);
    EXPECT_NE(Joint(match, "ForeArm_L").reason.find("not under"), std::string::npos) << Joint(match, "ForeArm_L").reason;
    EXPECT_EQ(Joint(match, "ForeArm_R").status, JointStatus::Good);

    const HumanoidReference& reference = GetHumanoidReference();
    const Vector3 elbow =
        ComputeWorldTransforms(reference.skeleton)[static_cast<std::size_t>(FindBone(reference.skeleton, "ForeArm_R"))].position;
    const AutoRig flat(1.0F, {{"mixamorig:RightHand", elbow}});
    match = MatchHumanoid(flat.skeleton, reference);
    EXPECT_EQ(Joint(match, "ForeArm_R").status, JointStatus::Broken);
    EXPECT_NE(Joint(match, "ForeArm_R").reason.find("no length"), std::string::npos) << Joint(match, "ForeArm_R").reason;
}

TEST(RetargetMatchTest, MatchHumanoid_WarnsAboutALimbFarLongerThanExpected) {
    const HumanoidReference& reference = GetHumanoidReference();
    const std::vector<WorldTransform> world = ComputeWorldTransforms(reference.skeleton);
    const auto at = [&](const char* name) { return world[static_cast<std::size_t>(FindBone(reference.skeleton, name))].position; };
    // The left hand twice as far from the elbow: the forearm is too long, and no longer matches the right.
    const Vector3 hand = at("ForeArm_L") + (at("Hand_L") - at("ForeArm_L")) * 2.0F;
    const AutoRig rig(1.0F, {{"mixamorig:LeftHand", hand}});
    const HumanoidMatch match = MatchHumanoid(rig.skeleton, reference);
    EXPECT_EQ(Joint(match, "ForeArm_L").status, JointStatus::Warning);
    EXPECT_NE(Joint(match, "ForeArm_L").reason.find("200%"), std::string::npos) << Joint(match, "ForeArm_L").reason;
    EXPECT_EQ(Joint(match, "ForeArm_R").status, JointStatus::Warning);
    EXPECT_NE(Joint(match, "ForeArm_R").reason.find("left and right"), std::string::npos);
}

TEST(RetargetMatchTest, MatchHumanoidNames_MatchesAClipsTracksByName) {
    const HumanoidMatch match =
        MatchHumanoidNames({"root", "pelvis", "spine_01", "upperarm_l", "lowerarm_l", "hand_l", "weapon_r", "thing"},
                              GetHumanoidReference());
    EXPECT_EQ(match.Count(JointStatus::Good), 7U);
    EXPECT_EQ(Joint(match, "Prop_R").bone, 6);
    EXPECT_EQ(match.referenceOfBone[7], -1);
    EXPECT_STREQ(JointStatusName(JointStatus::Missing), "missing");
}

TEST(RetargetMatchTest, AreHumanoidNames_RecognisesAConformedRigAndNotOneFromAnotherTool) {
    const HumanoidReference& reference = GetHumanoidReference();
    std::vector<std::string> conformed;
    for (const RetargetBone& bone : reference.skeleton.bones) {
        conformed.push_back(bone.name);
    }
    conformed.emplace_back("HeadTop_End"); // a kept extra bone does not matter
    EXPECT_TRUE(AreHumanoidNames(conformed, reference));
    // Missing a fifth of the bones is no longer the humanoid.
    conformed.resize(reference.skeleton.bones.size() * 3U / 4U);
    EXPECT_FALSE(AreHumanoidNames(conformed, reference));
    // A rig from another tool names its bones its own way, even where the meaning is the same.
    EXPECT_FALSE(AreHumanoidNames({"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Head"}, reference));
    EXPECT_FALSE(AreHumanoidNames({"pelvis", "spine_01", "upperarm_l", "thigh_r"}, reference));
    EXPECT_FALSE(AreHumanoidNames({}, reference));
}

} // namespace
} // namespace UVE::Retarget
