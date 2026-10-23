// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/skin_component_uve.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "uve/asset/mesh_asset_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

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

[[nodiscard]] SkeletonBoneUVE MakeBoneUVE(std::string name, const std::int32_t parentIndex = -1) {
    SkeletonBoneUVE bone;
    bone.name = std::move(name);
    bone.parentIndex = parentIndex;
    return bone;
}

TEST(SkinBindingUVETest, ANameTheSkeletonCarriesExactlyBindsThatBone) {
    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Hips"), MakeBoneUVE("Spine", 0), MakeBoneUVE("Head", 1)};
    SkeletonBoneLookupUVE lookup;
    lookup.BuildUVE(skeleton);

    std::uint32_t index = 999U;
    EXPECT_TRUE(lookup.TryFindUVE("Spine", index));
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFindUVE("Head", index));
    EXPECT_EQ(index, 2U);

    // A name no bone carries, and an empty one, bind nothing - and leave the output alone rather
    // than writing a wrong index the caller might use.
    index = 999U;
    EXPECT_FALSE(lookup.TryFindUVE("Tail", index));
    EXPECT_EQ(index, 999U);
    EXPECT_FALSE(lookup.TryFindUVE("", index));
    EXPECT_EQ(index, 999U);
}

TEST(SkinBindingUVETest, ANamespaceTheMeshKeptAndTheSkeletonDroppedStillBinds) {
    // One side of an export routinely keeps the namespace and the other drops it, and the case
    // does not always survive either. Both spellings have to reach the same bone or a model
    // imported from one tool cannot be driven by a skeleton authored in another.
    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Hips"), MakeBoneUVE("mixamorig:Spine")};
    SkeletonBoneLookupUVE lookup;
    lookup.BuildUVE(skeleton);

    std::uint32_t index = 0U;
    EXPECT_TRUE(lookup.TryFindUVE("mixamorig:Spine", index));
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFindUVE("Spine", index)) << "the mesh dropped the namespace";
    EXPECT_EQ(index, 1U);
    EXPECT_TRUE(lookup.TryFindUVE("MIXAMORIG:spine", index)) << "case differs";
    EXPECT_EQ(index, 1U);

    Skeleton3DComponentUVE armature;
    armature.bones = {MakeBoneUVE("Armature|Hips")};
    SkeletonBoneLookupUVE armatureLookup;
    armatureLookup.BuildUVE(armature);
    EXPECT_TRUE(armatureLookup.TryFindUVE("hips", index));
    EXPECT_EQ(index, 0U);
}

TEST(SkinBindingUVETest, ADuplicatedBoneNameBelongsToTheFirstBone) {
    // Duplicate bone names are an asset defect; picking the first is the only answer that does not
    // depend on iteration order, and it is the answer the by-name bone query already gives.
    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Root"), MakeBoneUVE("Hand"), MakeBoneUVE("Hand")};
    SkeletonBoneLookupUVE lookup;
    lookup.BuildUVE(skeleton);
    std::uint32_t index = 0U;
    ASSERT_TRUE(lookup.TryFindUVE("Hand", index));
    EXPECT_EQ(index, 1U);
}

TEST(SkinBindingUVETest, AnEmptySkeletonAnswersNothing) {
    const Skeleton3DComponentUVE skeleton;
    SkeletonBoneLookupUVE lookup;
    lookup.BuildUVE(skeleton);
    std::uint32_t index = 7U;
    EXPECT_FALSE(lookup.TryFindUVE("Hips", index));
    EXPECT_EQ(index, 7U);

    // A lookup that was never built is just as harmless.
    const SkeletonBoneLookupUVE unbuilt;
    EXPECT_FALSE(unbuilt.TryFindUVE("Hips", index));
}

TEST(SkinBindingUVETest, EveryJointIsBoundToTheBoneOfItsNameWithItsOwnInverseBind) {
    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Armature|Root"), MakeBoneUVE("Hips", 0), MakeBoneUVE("Spine", 1)};

    Asset::MeshAssetUVE mesh;
    // The mesh's joints are in a different order from the skeleton's bones, which is exactly the
    // case a positional binding gets wrong.
    Math::Matrix4x4UVE hipsInverse = Math::Matrix4x4UVE::IdentityUVE();
    hipsInverse.m[1][3] = -2.0F;
    Math::Matrix4x4UVE spineInverse = Math::Matrix4x4UVE::IdentityUVE();
    spineInverse.m[1][3] = -4.0F;
    mesh.joints = {Asset::MeshJointUVE{0U, hipsInverse, "Hips"},
                   Asset::MeshJointUVE{1U, spineInverse, "Spine"}};

    SkinComponentUVE skin;
    ASSERT_TRUE(TryBuildSkinFromMeshJointsUVE(mesh, skeleton, skin));
    ASSERT_EQ(skin.boneBinds.size(), 2U);
    EXPECT_EQ(skin.bindCount, 2U);
    EXPECT_TRUE(IsSkinComponentValidUVE(skin));
    // Joint order, not bone order: the Skin is indexed by the mesh's joints.
    EXPECT_EQ(skin.boneBinds[0].boneName, "Hips");
    EXPECT_EQ(skin.boneBinds[0].boneIndex, 1);
    EXPECT_FLOAT_EQ(skin.boneBinds[0].inverseBindPose.m[1][3], -2.0F);
    EXPECT_EQ(skin.boneBinds[1].boneName, "Spine");
    EXPECT_EQ(skin.boneBinds[1].boneIndex, 2);
    EXPECT_FLOAT_EQ(skin.boneBinds[1].inverseBindPose.m[1][3], -4.0F);
}

TEST(SkinBindingUVETest, AJointNoBoneNamesStaysBoundButUnplaced) {
    // Dropping the entry would shift every later joint against the mesh's joint list, which indexes
    // the Skin; binding it to -1 keeps the list the right shape and says "no bone" the way the
    // renderer reads it - the joint is drawn at its rest pose.
    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Hips")};
    Asset::MeshAssetUVE mesh;
    mesh.joints = {Asset::MeshJointUVE{0U, Math::Matrix4x4UVE::IdentityUVE(), "Hips"},
                   Asset::MeshJointUVE{1U, Math::Matrix4x4UVE::IdentityUVE(), "Tail"}};

    SkinComponentUVE skin;
    ASSERT_TRUE(TryBuildSkinFromMeshJointsUVE(mesh, skeleton, skin));
    ASSERT_EQ(skin.boneBinds.size(), 2U);
    EXPECT_EQ(skin.boneBinds[0].boneIndex, 0);
    EXPECT_EQ(skin.boneBinds[1].boneIndex, -1);
    EXPECT_EQ(skin.boneBinds[1].boneName, "Tail");
}

TEST(SkinBindingUVETest, ABindingNothingCanBeMadeOfLeavesTheSkinAlone) {
    SkinComponentUVE untouched;
    untouched.boneBinds = {SkinBoneBindUVE{"Original", 0, Math::Matrix4x4UVE::IdentityUVE()}};
    untouched.bindCount = 1U;

    Skeleton3DComponentUVE skeleton;
    skeleton.bones = {MakeBoneUVE("Hips")};

    // No joints to bind.
    Asset::MeshAssetUVE jointless;
    SkinComponentUVE skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJointsUVE(jointless, skeleton, skin));
    EXPECT_EQ(skin, untouched);

    // No bones to bind to.
    Asset::MeshAssetUVE mesh;
    mesh.joints = {Asset::MeshJointUVE{0U, Math::Matrix4x4UVE::IdentityUVE(), "Hips"}};
    skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJointsUVE(mesh, Skeleton3DComponentUVE{}, skin));
    EXPECT_EQ(skin, untouched);

    // Every name misses: a Skin of unbound entries would look configured and draw nothing.
    Asset::MeshAssetUVE unmatched;
    unmatched.joints = {Asset::MeshJointUVE{0U, Math::Matrix4x4UVE::IdentityUVE(), "Tail"}};
    skin = untouched;
    EXPECT_FALSE(TryBuildSkinFromMeshJointsUVE(unmatched, skeleton, skin));
    EXPECT_EQ(skin, untouched);
}

} // namespace
} // namespace UVE::Scene::Tests
