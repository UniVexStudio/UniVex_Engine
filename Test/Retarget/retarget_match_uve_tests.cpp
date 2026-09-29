// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/retarget/retarget_match_uve.h"

namespace UVE::Retarget {
namespace {

using Math::Vector3UVE;

/// An auto-rigger's skeleton (fewer bones: no root, three spine bones, one neck bone, no twists,
/// metacarpals, IK or props) standing where the reference's matching bones stand, `scale` times
/// as tall. Each entry is the rig's bone, its parent and the reference bone it sits on.
struct AutoRigUVE final {
    RetargetSkeletonUVE skeleton;

    AutoRigUVE(const float scale, const std::vector<std::pair<std::string, Vector3UVE>>& moved = {}) {
        const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
        const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference.skeleton);
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
        std::vector<Vector3UVE> placed;
        for (const auto& [name, parent, on] : bones) {
            Vector3UVE position = world[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, on))].position * scale;
            for (const auto& [movedName, to] : moved) {
                if (movedName == name) {
                    position = to;
                }
            }
            RetargetBoneUVE bone;
            bone.name = name;
            bone.parent = parent.empty() ? -1 : FindBoneUVE(skeleton, parent);
            bone.position = bone.parent < 0 ? position : position - placed[static_cast<std::size_t>(bone.parent)];
            skeleton.bones.push_back(bone);
            placed.push_back(position);
        }
    }

    void Reparent(const std::string& name, const std::string& parent) {
        const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(skeleton);
        const std::int32_t bone = FindBoneUVE(skeleton, name);
        const std::int32_t newParent = FindBoneUVE(skeleton, parent);
        RetargetBoneUVE& entry = skeleton.bones[static_cast<std::size_t>(bone)];
        entry.parent = newParent;
        entry.position = world[static_cast<std::size_t>(bone)].position - world[static_cast<std::size_t>(newParent)].position;
    }
};

[[nodiscard]] const JointReportUVE& JointUVE(const HumanoidMatchUVE& match, const std::string& referenceName) {
    const std::int32_t index = FindBoneUVE(GetHumanoidReferenceUVE().skeleton, referenceName);
    EXPECT_GE(index, 0) << referenceName;
    return match.joints[static_cast<std::size_t>(index < 0 ? 0 : index)];
}

TEST(RetargetMatchUVETest, MatchHumanoidUVE_FindsEveryBoneOfTheReferenceInItself) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const HumanoidMatchUVE match = MatchHumanoidUVE(reference.skeleton, reference);
    EXPECT_EQ(match.CountUVE(JointStatusUVE::Good), reference.skeleton.bones.size());
    EXPECT_NEAR(match.heightScale, 1.0F, 1e-5F);
    for (std::size_t index = 0U; index < match.referenceOfBone.size(); ++index) {
        EXPECT_EQ(match.referenceOfBone[index], static_cast<std::int32_t>(index)) << reference.skeleton.bones[index].name;
    }
}

TEST(RetargetMatchUVETest, MatchHumanoidUVE_FitsAShorterSpineAndMarksWhatTheRigLacks) {
    const AutoRigUVE rig(1.1F);
    const HumanoidMatchUVE match = MatchHumanoidUVE(rig.skeleton, GetHumanoidReferenceUVE());
    EXPECT_NEAR(match.heightScale, 1.1F, 1e-3F);

    // Three spine bones spread over five: ends to ends, the middle to the middle.
    EXPECT_EQ(JointUVE(match, "Spine1").bone, FindBoneUVE(rig.skeleton, "mixamorig:Spine"));
    EXPECT_EQ(JointUVE(match, "Spine3").bone, FindBoneUVE(rig.skeleton, "mixamorig:Spine1"));
    EXPECT_EQ(JointUVE(match, "Spine5").bone, FindBoneUVE(rig.skeleton, "mixamorig:Spine2"));
    EXPECT_EQ(JointUVE(match, "Spine2").status, JointStatusUVE::Missing);
    EXPECT_EQ(JointUVE(match, "Neck1").bone, FindBoneUVE(rig.skeleton, "mixamorig:Neck"));
    EXPECT_EQ(JointUVE(match, "Neck2").status, JointStatusUVE::Missing);

    for (const char* name : {"Hips", "Spine1", "Spine5", "Head", "Clavicle_L", "UpperArm_L", "ForeArm_R", "Hand_L",
                             "Index1_L", "Thigh_R", "Shin_L", "Foot_R", "Toe_L"}) {
        EXPECT_EQ(JointUVE(match, name).status, JointStatusUVE::Good) << name << ": " << JointUVE(match, name).reason;
    }
    for (const char* name : {"Root", "UpperArmTwist1_L", "IKFoot_L", "Prop_R", "IndexMetacarpal_L", "Thumb1_L"}) {
        EXPECT_EQ(JointUVE(match, name).status, JointStatusUVE::Missing) << name;
    }
    // The end-of-head helper has no place in the humanoid and is kept as it is.
    EXPECT_EQ(match.referenceOfBone[static_cast<std::size_t>(FindBoneUVE(rig.skeleton, "mixamorig:HeadTop_End"))], -1);

    const std::size_t found = match.CountUVE(JointStatusUVE::Good) + match.CountUVE(JointStatusUVE::Warning) +
                              match.CountUVE(JointStatusUVE::Broken);
    EXPECT_EQ(found, rig.skeleton.bones.size() - 1U); // all but HeadTop_End
    EXPECT_EQ(found + match.CountUVE(JointStatusUVE::Missing), GetHumanoidReferenceUVE().skeleton.bones.size());
}

TEST(RetargetMatchUVETest, MatchHumanoidUVE_FlagsAWrongParentAndABoneWithNoLength) {
    AutoRigUVE rig(1.0F);
    rig.Reparent("mixamorig:LeftForeArm", "mixamorig:Spine2");
    HumanoidMatchUVE match = MatchHumanoidUVE(rig.skeleton, GetHumanoidReferenceUVE());
    EXPECT_EQ(JointUVE(match, "ForeArm_L").status, JointStatusUVE::Broken);
    EXPECT_NE(JointUVE(match, "ForeArm_L").reason.find("not under"), std::string::npos) << JointUVE(match, "ForeArm_L").reason;
    EXPECT_EQ(JointUVE(match, "ForeArm_R").status, JointStatusUVE::Good);

    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const Vector3UVE elbow =
        ComputeWorldTransformsUVE(reference.skeleton)[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, "ForeArm_R"))].position;
    const AutoRigUVE flat(1.0F, {{"mixamorig:RightHand", elbow}});
    match = MatchHumanoidUVE(flat.skeleton, reference);
    EXPECT_EQ(JointUVE(match, "ForeArm_R").status, JointStatusUVE::Broken);
    EXPECT_NE(JointUVE(match, "ForeArm_R").reason.find("no length"), std::string::npos) << JointUVE(match, "ForeArm_R").reason;
}

TEST(RetargetMatchUVETest, MatchHumanoidUVE_WarnsAboutALimbFarLongerThanExpected) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    const std::vector<WorldTransformUVE> world = ComputeWorldTransformsUVE(reference.skeleton);
    const auto at = [&](const char* name) { return world[static_cast<std::size_t>(FindBoneUVE(reference.skeleton, name))].position; };
    // The left hand twice as far from the elbow: the forearm is too long, and no longer matches the right.
    const Vector3UVE hand = at("ForeArm_L") + (at("Hand_L") - at("ForeArm_L")) * 2.0F;
    const AutoRigUVE rig(1.0F, {{"mixamorig:LeftHand", hand}});
    const HumanoidMatchUVE match = MatchHumanoidUVE(rig.skeleton, reference);
    EXPECT_EQ(JointUVE(match, "ForeArm_L").status, JointStatusUVE::Warning);
    EXPECT_NE(JointUVE(match, "ForeArm_L").reason.find("200%"), std::string::npos) << JointUVE(match, "ForeArm_L").reason;
    EXPECT_EQ(JointUVE(match, "ForeArm_R").status, JointStatusUVE::Warning);
    EXPECT_NE(JointUVE(match, "ForeArm_R").reason.find("left and right"), std::string::npos);
}

TEST(RetargetMatchUVETest, MatchHumanoidNamesUVE_MatchesAClipsTracksByName) {
    const HumanoidMatchUVE match =
        MatchHumanoidNamesUVE({"root", "pelvis", "spine_01", "upperarm_l", "lowerarm_l", "hand_l", "weapon_r", "thing"},
                              GetHumanoidReferenceUVE());
    EXPECT_EQ(match.CountUVE(JointStatusUVE::Good), 7U);
    EXPECT_EQ(JointUVE(match, "Prop_R").bone, 6);
    EXPECT_EQ(match.referenceOfBone[7], -1);
    EXPECT_STREQ(JointStatusNameUVE(JointStatusUVE::Missing), "missing");
}

} // namespace
} // namespace UVE::Retarget
