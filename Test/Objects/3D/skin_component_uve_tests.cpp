// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/skin_component_uve.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene::Tests {
namespace {

TEST(SkinComponentUVETest, DefaultSkinComponentIsValid) {
    const SkinComponentUVE skin{};
    EXPECT_EQ(skin.bindCount, 0U);
    EXPECT_TRUE(skin.boneBinds.empty());
    EXPECT_TRUE(IsSkinComponentValidUVE(skin));
}

TEST(SkinComponentUVETest, ConfiguredBindsMatchingCountIsValid) {
    SkinComponentUVE skin{};
    skin.bindCount = 3U;

    SkinBoneBindUVE bind0{};
    bind0.boneName = "Root";
    bind0.boneIndex = 0;
    bind0.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();

    SkinBoneBindUVE bind1{};
    bind1.boneName = "Spine";
    bind1.boneIndex = 1;
    bind1.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();

    SkinBoneBindUVE bind2{};
    bind2.boneName = "Head";
    bind2.boneIndex = 2;
    bind2.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();

    skin.boneBinds = {bind0, bind1, bind2};
    EXPECT_TRUE(IsSkinComponentValidUVE(skin));
}

TEST(SkinComponentUVETest, MismatchedBindCountIsInvalid) {
    SkinComponentUVE skin{};
    skin.bindCount = 5U;

    SkinBoneBindUVE bind0{};
    bind0.boneName = "Root";
    bind0.boneIndex = 0;
    skin.boneBinds = {bind0};

    // 5 != 1
    EXPECT_FALSE(IsSkinComponentValidUVE(skin));

    // 0 != 1
    skin.bindCount = 0U;
    EXPECT_FALSE(IsSkinComponentValidUVE(skin));
}

TEST(SkinComponentUVETest, BoneBindEqualityAndValues) {
    SkinBoneBindUVE a{};
    a.boneName = "Arm_L";
    a.boneIndex = 4;
    a.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();

    SkinBoneBindUVE b = a;
    EXPECT_EQ(a, b);

    b.boneIndex = 5;
    EXPECT_NE(a, b);

    b = a;
    b.boneName = "Arm_R";
    EXPECT_NE(a, b);
}

TEST(SkinComponentUVETest, InverseBindPoseTransformation) {
    SkinBoneBindUVE bind{};
    bind.boneName = "Hand_R";
    bind.boneIndex = 10;

    // Create an inverse bind translation matrix offsetting by (-1.0, -2.0, -3.0)
    Math::Matrix4x4UVE invPose = Math::Matrix4x4UVE::IdentityUVE();
    invPose.m[0][3] = -1.0F;
    invPose.m[1][3] = -2.0F;
    invPose.m[2][3] = -3.0F;
    bind.inverseBindPose = invPose;

    const Math::Vector3UVE worldPoint{1.0F, 2.0F, 3.0F};
    const Math::Vector3UVE localPoint = Math::TransformPointUVE(bind.inverseBindPose, worldPoint);
    EXPECT_NEAR(localPoint.x, 0.0F, 1e-5F);
    EXPECT_NEAR(localPoint.y, 0.0F, 1e-5F);
    EXPECT_NEAR(localPoint.z, 0.0F, 1e-5F);
}

TEST(SkinComponentUVETest, MultiBoneHumanoidSkeletonConfiguration) {
    const std::vector<std::string> boneNames = {
        "Hips", "Spine", "Chest", "Neck", "Head",
        "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
        "RightShoulder", "RightArm", "RightForeArm", "RightHand",
        "LeftUpLeg", "LeftLeg", "LeftFoot",
        "RightUpLeg", "RightLeg", "RightFoot"
    };

    SkinComponentUVE skin{};
    skin.bindCount = static_cast<std::uint32_t>(boneNames.size());
    for (std::size_t i = 0; i < boneNames.size(); ++i) {
        SkinBoneBindUVE bind{};
        bind.boneName = boneNames[i];
        bind.boneIndex = static_cast<std::int32_t>(i);
        bind.inverseBindPose = Math::Matrix4x4UVE::IdentityUVE();
        skin.boneBinds.push_back(bind);
    }

    EXPECT_EQ(skin.bindCount, 19U);
    EXPECT_EQ(skin.boneBinds.size(), 19U);
    EXPECT_TRUE(IsSkinComponentValidUVE(skin));

    for (std::size_t i = 0; i < skin.boneBinds.size(); ++i) {
        EXPECT_EQ(skin.boneBinds[i].boneName, boneNames[i]);
        EXPECT_EQ(skin.boneBinds[i].boneIndex, static_cast<std::int32_t>(i));
    }
}

} // namespace
} // namespace UVE::Scene::Tests
