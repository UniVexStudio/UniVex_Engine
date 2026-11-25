// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "retarget_humanoid.h"
#include "retarget_names.h"

namespace UVE::Retarget {
namespace {

TEST(RetargetNamesTest, MakeBoneKey_ReadsTheSameBoneTheSameWayAcrossNamingStyles) {
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
        EXPECT_EQ(MakeBoneKey(name), key) << name;
    }
}

TEST(RetargetNamesTest, MakeBoneKey_ReadsAnAutoRiggerSkeleton) {
    const std::vector<std::pair<const char*, const char*>> cases{
        {"mixamorig:LeftShoulder", "L|clavicle"},  {"mixamorig:LeftArm", "L|upperarm"},
        {"mixamorig:LeftHand", "L|hand"},          {"mixamorig:LeftHandIndex1", "L|index 1"},
        {"mixamorig:LeftHandThumb3", "L|thumb 3"}, {"mixamorig:LeftUpLeg", "L|thigh"},
        {"mixamorig:RightLeg", "R|shin"},          {"mixamorig:LeftToeBase", "L|toe"},
        {"mixamorig:Spine2", "C|spine 2"},         {"mixamorig:HeadTop_End", "C|head top end"},
    };
    for (const auto& [name, key] : cases) {
        EXPECT_EQ(MakeBoneKey(name), key) << name;
    }
}

using NameCases = std::vector<std::pair<const char*, const char*>>;

void ExpectKeys(const NameCases& cases) {
    for (const auto& [name, key] : cases) {
        EXPECT_EQ(MakeBoneKey(name), key) << name;
    }
}

TEST(RetargetNamesTest, MakeBoneKey_ReadsUnityHumanoidBones) {
    ExpectKeys({{"Hips", "C|hips"},
                   {"Spine", "C|spine"},
                   {"UpperChest", "C|upper chest"},
                   {"LeftShoulder", "L|clavicle"},
                   {"LeftUpperArm", "L|upperarm"},
                   {"LeftLowerArm", "L|forearm"},
                   {"RightUpperLeg", "R|thigh"},
                   {"RightLowerLeg", "R|shin"},
                   {"RightToes", "R|toe"},
                   {"Left Thumb Proximal", "L|thumb 1"},
                   {"LeftThumbIntermediate", "L|thumb 2"},
                   {"LeftIndexDistal", "L|index 3"}});
}

TEST(RetargetNamesTest, MakeBoneKey_ReadsRokokoBones) {
    ExpectKeys({{"LeftInHandIndex", "L|index metacarpal"},
                   {"RightInHandPinky", "R|pinky metacarpal"},
                   {"LeftIndexProximal", "L|index 1"},
                   {"RightMiddleDistal", "R|middle 3"},
                   {"LeftHand", "L|hand"},
                   {"RightLowerLeg", "R|shin"}});
}

TEST(RetargetNamesTest, MakeBoneKey_ReadsRigifyDeformBones) {
    ExpectKeys({{"DEF-spine", "C|hips"},
                   {"DEF-spine.001", "C|spine 1"},
                   {"DEF-spine.003", "C|spine 3"},
                   {"DEF-spine.004", "C|neck 1"},
                   {"DEF-spine.005", "C|neck 2"},
                   {"DEF-spine.006", "C|head"},
                   {"DEF-shoulder.L", "L|clavicle"},
                   {"DEF-upper_arm.L", "L|upperarm"},
                   {"DEF-upper_arm.L.001", "L|upperarm twist 1"},
                   {"DEF-forearm.R.002", "R|forearm twist 2"},
                   {"DEF-hand.L", "L|hand"},
                   {"DEF-thigh.R", "R|thigh"},
                   {"DEF-shin.L", "L|shin"},
                   {"DEF-shin.L.001", "L|shin twist 1"},
                   {"DEF-foot.L", "L|foot"},
                   {"DEF-toe.R", "R|toe"},
                   {"DEF-f_index.01.L", "L|index 1"},
                   {"DEF-f_pinky.03.R", "R|pinky 3"},
                   {"DEF-thumb.02.L", "L|thumb 2"},
                   {"DEF-palm.01.L", "L|index metacarpal"},
                   {"DEF-palm.04.R", "R|pinky metacarpal"}});
}

TEST(RetargetNamesTest, MakeBoneKey_ReadsMocapAndBipedRigs) {
    ExpectKeys({{"Bip01 Pelvis", "C|hips"},
                   {"Bip01 Spine1", "C|spine 1"},
                   {"Bip01 L Thigh", "L|thigh"},
                   {"Bip01 L Calf", "L|shin"},
                   {"Bip01 R UpperArm", "R|upperarm"},
                   {"Bip01 R Forearm", "R|forearm"},
                   {"Bip01 L Toe0", "L|toe"},
                   {"Character1_LeftUpLeg", "L|thigh"},
                   {"Character1_RightForeArm", "R|forearm"},
                   {"Pelvis", "C|hips"},
                   {"L5", "C|spine"},
                   {"T8", "C|spine"},
                   {"RightUpperArm", "R|upperarm"},
                   {"RightToe", "R|toe"},
                   {"lThigh", "L|thigh"},
                   {"lThighBend", "L|thigh"},
                   {"rShldrBend", "R|upperarm"},
                   {"lCollar", "L|clavicle"}});
}

TEST(RetargetNamesTest, MakeHumanoidBoneName_SpellsKeysInPascalCaseWithASideSuffix) {
    EXPECT_EQ(MakeHumanoidBoneName("L|upperarm twist 1"), "UpperArmTwist1_L");
    EXPECT_EQ(MakeHumanoidBoneName("R|ik hand"), "IKHand_R");
    EXPECT_EQ(MakeHumanoidBoneName("C|com"), "CenterOfMass");
    EXPECT_EQ(MakeHumanoidBoneName("C|hips"), "Hips");
    EXPECT_EQ(MakeHumanoidBoneName("L|forearm out"), "ForeArmOuter_L");
}

// Every name the shipped reference uses reads back as its own key, so the reference's names are
// recognised by the matcher exactly like any other rig's.
TEST(RetargetNamesTest, EveryReferenceNameKeysBackToItself) {
    const HumanoidReference& reference = GetHumanoidReference();
    ASSERT_FALSE(reference.skeleton.bones.empty());
    for (std::size_t index = 0U; index < reference.skeleton.bones.size(); ++index) {
        const std::string& name = reference.skeleton.bones[index].name;
        EXPECT_EQ(MakeBoneKey(name), reference.info[index].key) << name;
        EXPECT_EQ(MakeHumanoidBoneName(reference.info[index].key), name) << name;
    }
}

} // namespace
} // namespace UVE::Retarget
