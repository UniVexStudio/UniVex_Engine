// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "skin_component.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "mesh_asset.h"
#include "matrix4x4.h"
#include "vector3.h"
#include "skeleton_3d.h"

namespace UVE::Scene::Tests {
namespace {

TEST(SkinComponentTest, DefaultSkinComponentIsValid) {
    const SkinComponent skin{};
    EXPECT_EQ(skin.bindCount, 0U);
    EXPECT_TRUE(skin.boneBinds.empty());
    EXPECT_TRUE(IsSkinComponentValid(skin));
}

TEST(SkinComponentTest, ConfiguredBindsMatchingCountIsValid) {
    SkinComponent skin{};
    skin.bindCount = 3U;

    SkinBoneBind bind0{};
    bind0.boneName = "Root";
    bind0.boneIndex = 0;
    bind0.inverseBindPose = Math::Matrix4x4::Identity();

    SkinBoneBind bind1{};
    bind1.boneName = "Spine";
    bind1.boneIndex = 1;
    bind1.inverseBindPose = Math::Matrix4x4::Identity();

    SkinBoneBind bind2{};
    bind2.boneName = "Head";
    bind2.boneIndex = 2;
    bind2.inverseBindPose = Math::Matrix4x4::Identity();

    skin.boneBinds = {bind0, bind1, bind2};
    EXPECT_TRUE(IsSkinComponentValid(skin));
}

TEST(SkinComponentTest, MismatchedBindCountIsInvalid) {
    SkinComponent skin{};
    skin.bindCount = 5U;

    SkinBoneBind bind0{};
    bind0.boneName = "Root";
    bind0.boneIndex = 0;
    skin.boneBinds = {bind0};

    // 5 != 1
    EXPECT_FALSE(IsSkinComponentValid(skin));

    // 0 != 1
    skin.bindCount = 0U;
    EXPECT_FALSE(IsSkinComponentValid(skin));
}

TEST(SkinComponentTest, BoneBindEqualityAndValues) {
    SkinBoneBind a{};
    a.boneName = "Arm_L";
    a.boneIndex = 4;
    a.inverseBindPose = Math::Matrix4x4::Identity();

    SkinBoneBind b = a;
    EXPECT_EQ(a, b);

    b.boneIndex = 5;
    EXPECT_NE(a, b);

    b = a;
    b.boneName = "Arm_R";
    EXPECT_NE(a, b);
}

TEST(SkinComponentTest, InverseBindPoseTransformation) {
    SkinBoneBind bind{};
    bind.boneName = "Hand_R";
    bind.boneIndex = 10;

    // Create an inverse bind translation matrix offsetting by (-1.0, -2.0, -3.0)
    Math::Matrix4x4 invPose = Math::Matrix4x4::Identity();
    invPose.m[0][3] = -1.0F;
    invPose.m[1][3] = -2.0F;
    invPose.m[2][3] = -3.0F;
    bind.inverseBindPose = invPose;

    const Math::Vector3 worldPoint{1.0F, 2.0F, 3.0F};
    const Math::Vector3 localPoint = Math::TransformPoint(bind.inverseBindPose, worldPoint);
    EXPECT_NEAR(localPoint.x, 0.0F, 1e-5F);
    EXPECT_NEAR(localPoint.y, 0.0F, 1e-5F);
    EXPECT_NEAR(localPoint.z, 0.0F, 1e-5F);
}

TEST(SkinComponentTest, MultiBoneHumanoidSkeletonConfiguration) {
    const std::vector<std::string> boneNames = {
        "Hips", "Spine", "Chest", "Neck", "Head",
        "LeftShoulder", "LeftArm", "LeftForeArm", "LeftHand",
        "RightShoulder", "RightArm", "RightForeArm", "RightHand",
        "LeftUpLeg", "LeftLeg", "LeftFoot",
        "RightUpLeg", "RightLeg", "RightFoot"
    };

    SkinComponent skin{};
    skin.bindCount = static_cast<std::uint32_t>(boneNames.size());
    for (std::size_t i = 0; i < boneNames.size(); ++i) {
        SkinBoneBind bind{};
        bind.boneName = boneNames[i];
        bind.boneIndex = static_cast<std::int32_t>(i);
        bind.inverseBindPose = Math::Matrix4x4::Identity();
        skin.boneBinds.push_back(bind);
    }

    EXPECT_EQ(skin.bindCount, 19U);
    EXPECT_EQ(skin.boneBinds.size(), 19U);
    EXPECT_TRUE(IsSkinComponentValid(skin));

    for (std::size_t i = 0; i < skin.boneBinds.size(); ++i) {
        EXPECT_EQ(skin.boneBinds[i].boneName, boneNames[i]);
        EXPECT_EQ(skin.boneBinds[i].boneIndex, static_cast<std::int32_t>(i));
    }
}

[[nodiscard]] SkeletonBone MakeBone(std::string name, const std::int32_t parentIndex = -1) {
    SkeletonBone bone;
    bone.name = std::move(name);
    bone.parentIndex = parentIndex;
    return bone;
}

TEST(SkinBindingTest, ANameTheSkeletonCarriesExactlyBindsThatBone) {
    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Hips"), MakeBone("Spine", 0), MakeBone("Head", 1)};
    SkeletonBoneLookup lookup;
    lookup.Build(skeleton);

    std::uint32_t index = 999U;
    EXPECT_TRUE(lookup.TryFind("Spine", index));
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFind("Head", index));
    EXPECT_EQ(index, 2U);

    // A name no bone carries, and an empty one, bind nothing - and leave the output alone rather
    // than writing a wrong index the caller might use.
    index = 999U;
    EXPECT_FALSE(lookup.TryFind("Tail", index));
    EXPECT_EQ(index, 999U);
    EXPECT_FALSE(lookup.TryFind("", index));
    EXPECT_EQ(index, 999U);
}

TEST(SkinBindingTest, ANamespaceTheMeshKeptAndTheSkeletonDroppedStillBinds) {
    // One side of an export routinely keeps the namespace and the other drops it, and the case
    // does not always survive either. Both spellings have to reach the same bone or a model
    // imported from one tool cannot be driven by a skeleton authored in another.
    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Hips"), MakeBone("mixamorig:Spine")};
    SkeletonBoneLookup lookup;
    lookup.Build(skeleton);

    std::uint32_t index = 0U;
    EXPECT_TRUE(lookup.TryFind("mixamorig:Spine", index));
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFind("Spine", index)) << "the mesh dropped the namespace";
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFind("MIXAMORIG:spine", index)) << "case differs";
    EXPECT_EQ(index, 1U);

    Skeleton3DComponent armature;
    armature.bones = {MakeBone("Armature|Hips")};
    SkeletonBoneLookup armatureLookup;
    armatureLookup.Build(armature);
    EXPECT_TRUE(armatureLookup.TryFind("hips", index));
    EXPECT_EQ(index, 0U);
}

TEST(SkinBindingTest, ADuplicatedBoneNameBelongsToTheFirstBone) {
    // Duplicate bone names are an asset defect; picking the first is the only answer that does not
    // depend on iteration order, and it is the answer the by-name bone query already gives.
    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Root"), MakeBone("Hand"), MakeBone("Hand")};
    SkeletonBoneLookup lookup;
    lookup.Build(skeleton);
    std::uint32_t index = 0U;
    ASSERT_TRUE(lookup.TryFind("Hand", index));
    EXPECT_EQ(index, 1U);
}

TEST(SkinBindingTest, AnEmptySkeletonAnswersNothing) {
    const Skeleton3DComponent skeleton;
    SkeletonBoneLookup lookup;
    lookup.Build(skeleton);
    std::uint32_t index = 7U;
    EXPECT_FALSE(lookup.TryFind("Hips", index));
    EXPECT_EQ(index, 7U);

    // A lookup that was never built is just as harmless.
    const SkeletonBoneLookup unbuilt;
    EXPECT_FALSE(unbuilt.TryFind("Hips", index));
}

TEST(SkinBindingTest, EveryJointIsBoundToTheBoneOfItsNameWithItsOwnInverseBind) {
    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Armature|Root"), MakeBone("Hips", 0), MakeBone("Spine", 1)};

    Asset::MeshAsset mesh;
    // The mesh's joints are in a different order from the skeleton's bones, which is exactly the
    // case a positional binding gets wrong.
    Math::Matrix4x4 hipsInverse = Math::Matrix4x4::Identity();
    hipsInverse.m[1][3] = -2.0F;
    Math::Matrix4x4 spineInverse = Math::Matrix4x4::Identity();
    spineInverse.m[1][3] = -4.0F;
    mesh.joints = {Asset::MeshJoint{0U, hipsInverse, "Hips"},
                   Asset::MeshJoint{1U, spineInverse, "Spine"}};

    SkinComponent skin;
    ASSERT_TRUE(TryBuildSkinFromMeshJoints(mesh, skeleton, skin));
    ASSERT_EQ(skin.boneBinds.size(), 2U);
    EXPECT_EQ(skin.bindCount, 2U);
    EXPECT_TRUE(IsSkinComponentValid(skin));
    // Joint order, not bone order: the Skin is indexed by the mesh's joints.
    EXPECT_EQ(skin.boneBinds[0].boneName, "Hips");
    EXPECT_EQ(skin.boneBinds[0].boneIndex, 1);
    EXPECT_FLOAT_EQ(skin.boneBinds[0].inverseBindPose.m[1][3], -2.0F);
    EXPECT_EQ(skin.boneBinds[1].boneName, "Spine");
    EXPECT_EQ(skin.boneBinds[1].boneIndex, 2);
    EXPECT_FLOAT_EQ(skin.boneBinds[1].inverseBindPose.m[1][3], -4.0F);
}

TEST(SkinBindingTest, AJointNoBoneNamesStaysBoundButUnplaced) {
    // Dropping the entry would shift every later joint against the mesh's joint list, which indexes
    // the Skin; binding it to -1 keeps the list the right shape and says "no bone" the way the
    // renderer reads it - the joint is drawn at its rest pose.
    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Hips")};
    Asset::MeshAsset mesh;
    mesh.joints = {Asset::MeshJoint{0U, Math::Matrix4x4::Identity(), "Hips"},
                   Asset::MeshJoint{1U, Math::Matrix4x4::Identity(), "Tail"}};

    SkinComponent skin;
    ASSERT_TRUE(TryBuildSkinFromMeshJoints(mesh, skeleton, skin));
    ASSERT_EQ(skin.boneBinds.size(), 2U);
    EXPECT_EQ(skin.boneBinds[0].boneIndex, 0);
    EXPECT_EQ(skin.boneBinds[1].boneIndex, -1);
    EXPECT_EQ(skin.boneBinds[1].boneName, "Tail");
}

TEST(SkinBindingTest, ABindingNothingCanBeMadeOfLeavesTheSkinAlone) {
    SkinComponent untouched;
    untouched.boneBinds = {SkinBoneBind{"Original", 0, Math::Matrix4x4::Identity()}};
    untouched.bindCount = 1U;

    Skeleton3DComponent skeleton;
    skeleton.bones = {MakeBone("Hips")};

    // No joints to bind.
    Asset::MeshAsset jointless;
    SkinComponent skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJoints(jointless, skeleton, skin));
    EXPECT_EQ(skin, untouched);

    // No bones to bind to.
    Asset::MeshAsset mesh;
    mesh.joints = {Asset::MeshJoint{0U, Math::Matrix4x4::Identity(), "Hips"}};
    skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJoints(mesh, Skeleton3DComponent{}, skin));
    EXPECT_EQ(skin, untouched);

    // Every name misses: a Skin of unbound entries would look configured and draw nothing.
    Asset::MeshAsset unmatched;
    unmatched.joints = {Asset::MeshJoint{0U, Math::Matrix4x4::Identity(), "Tail"}};
    skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJoints(unmatched, skeleton, skin));
    EXPECT_EQ(skin, untouched);
}

} // namespace
} // namespace UVE::Scene::Tests
