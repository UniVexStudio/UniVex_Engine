// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "uve/retarget/retarget_humanoid_uve.h"
#include "uve/retarget/retarget_names_uve.h"

namespace UVE::Retarget {
namespace {

TEST(RetargetNamesUVETest, MakeBoneKeyUVE_ReadsTheSameBoneTheSameWayAcrossNamingStyles) {
    const std::vector<std::pair<const char*, const char*>> cases{
        {"upperarm_twist_01_l", "L|upperarm twist 1"},
        {"UpperArmTwist1_L", "L|upperarm twist 1"},
        {"mixamorig:LeftForeArm", "L|forearm"},
        {"lowerarm_r", "R|forearm"},
        {"calf_kneeBack_r", "R|shin knee back"},
        {"pelvis", "C|hips"},
        {"Hips", "C|hips"},
        {"spine_05", "C|spine 5"},
        {"IKFoot_L", "L|ik foot"},
        {"ik_foot_l", "L|ik foot"},
        {"Armature|Hand_L", "L|hand"},
        {"DEF-upper_arm.L", "L|upperarm"},
        {"center_of_mass", "C|com"},
        {"weapon_r", "R|prop"},
        {"attach", "C|mount"},
    };
    for (const auto& [name, key] : cases) {
        EXPECT_EQ(MakeBoneKeyUVE(name), key) << name;
    }
}

TEST(RetargetNamesUVETest, MakeBoneKeyUVE_ReadsAnAutoRiggerSkeleton) {
    const std::vector<std::pair<const char*, const char*>> cases{
        {"mixamorig:LeftShoulder", "L|clavicle"},  {"mixamorig:LeftArm", "L|upperarm"},
        {"mixamorig:LeftHand", "L|hand"},          {"mixamorig:LeftHandIndex1", "L|index 1"},
        {"mixamorig:LeftHandThumb3", "L|thumb 3"}, {"mixamorig:LeftUpLeg", "L|thigh"},
        {"mixamorig:RightLeg", "R|shin"},          {"mixamorig:LeftToeBase", "L|toe"},
        {"mixamorig:Spine2", "C|spine 2"},         {"mixamorig:HeadTop_End", "C|head top end"},
    };
    for (const auto& [name, key] : cases) {
        EXPECT_EQ(MakeBoneKeyUVE(name), key) << name;
    }
}

TEST(RetargetNamesUVETest, MakeHumanoidBoneNameUVE_SpellsKeysInPascalCaseWithASideSuffix) {
    EXPECT_EQ(MakeHumanoidBoneNameUVE("L|upperarm twist 1"), "UpperArmTwist1_L");
    EXPECT_EQ(MakeHumanoidBoneNameUVE("R|ik hand"), "IKHand_R");
    EXPECT_EQ(MakeHumanoidBoneNameUVE("C|com"), "CenterOfMass");
    EXPECT_EQ(MakeHumanoidBoneNameUVE("C|hips"), "Hips");
    EXPECT_EQ(MakeHumanoidBoneNameUVE("L|forearm out"), "ForeArmOuter_L");
}

// Every name the shipped reference uses reads back as its own key, so the reference's names are
// recognised by the matcher exactly like any other rig's.
TEST(RetargetNamesUVETest, EveryReferenceNameKeysBackToItself) {
    const HumanoidReferenceUVE& reference = GetHumanoidReferenceUVE();
    ASSERT_FALSE(reference.skeleton.bones.empty());
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const std::string& name = reference.skeleton.bones[index].name;
        EXPECT_EQ(MakeBoneKeyUVE(name), reference.info[index].key) << name;
        EXPECT_EQ(MakeHumanoidBoneNameUVE(reference.info[index].key), name) << name;
    }
}

} // namespace
} // namespace UVE::Retarget
