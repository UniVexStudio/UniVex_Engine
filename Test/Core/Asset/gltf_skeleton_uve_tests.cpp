// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <cmath>
#include <string>

#include <gtest/gtest.h>

#include "uve/asset/gltf_skeleton_uve.h"

namespace UVE::Asset {
namespace {

// A Blender-style export: an Armature node that is not a bone, holding Hips > Spine > Head, with
// the skin listing the joints child-first to prove the reader orders them parent-first.
constexpr const char* kArmatureGltfUVE = R"({
  "asset":{"version":"2.0"},
  "nodes":[
    {"name":"Armature","children":[1]},
    {"name":"Hips","translation":[0,1,0],"children":[2]},
    {"name":"Spine","translation":[0,0.5,0],"rotation":[0,0,0.70710677,0.70710677],"children":[3]},
    {"name":"Head","matrix":[2,0,0,0, 0,2,0,0, 0,0,2,0, 1,2,3,1]}
  ],
  "skins":[{"joints":[3,2,1]}]
})";

TEST(GltfSkeletonUVETest, ParseGltfSkeletonUVE_OrdersJointsParentFirstAndSkipsNonBoneAncestors) {
    const std::optional<GltfSkeletonUVE> skeleton = ParseGltfSkeletonUVE(kArmatureGltfUVE, 256U);
    ASSERT_TRUE(skeleton.has_value());
    ASSERT_EQ(skeleton->joints.size(), 3U);
    EXPECT_EQ(skeleton->skinCount, 1U);
    EXPECT_EQ(skeleton->joints[0].name, "Hips");
    EXPECT_EQ(skeleton->joints[0].parentIndex, -1); // the Armature object is not a bone
    EXPECT_EQ(skeleton->joints[1].name, "Spine");
    EXPECT_EQ(skeleton->joints[1].parentIndex, 0);
    EXPECT_EQ(skeleton->joints[2].name, "Head");
    EXPECT_EQ(skeleton->joints[2].parentIndex, 1);
    EXPECT_FLOAT_EQ(skeleton->joints[0].translation.y, 1.0F);
    EXPECT_NEAR(skeleton->joints[1].rotation.z, 0.70710677F, 1.0e-6F);
    // A matrix is split into TRS.
    EXPECT_FLOAT_EQ(skeleton->joints[2].translation.z, 3.0F);
    EXPECT_FLOAT_EQ(skeleton->joints[2].scale.x, 2.0F);
    EXPECT_NEAR(skeleton->joints[2].rotation.w, 1.0F, 1.0e-6F);
}

TEST(GltfSkeletonUVETest, ParseGltfSkeletonUVE_MakesDuplicateBoneNamesUnique) {
    const std::optional<GltfSkeletonUVE> skeleton = ParseGltfSkeletonUVE(
        R"({"asset":{"version":"2.0"},"nodes":[{"name":"Bone","children":[1]},{"name":"Bone"}],"skins":[{"joints":[0,1]}]})",
        256U);
    ASSERT_TRUE(skeleton.has_value());
    EXPECT_EQ(skeleton->joints[0].name, "Bone");
    EXPECT_EQ(skeleton->joints[1].name, "Bone_1");
}

TEST(GltfSkeletonUVETest, ParseGltfSkeletonUVE_RejectsWhatItCannotRepresent) {
    // No skin: a static mesh has no skeleton.
    EXPECT_FALSE(ParseGltfSkeletonUVE(R"({"asset":{"version":"2.0"},"nodes":[{}]})", 256U).has_value());
    // A joint naming a node that does not exist.
    EXPECT_FALSE(ParseGltfSkeletonUVE(R"({"asset":{"version":"2.0"},"nodes":[{}],"skins":[{"joints":[4]}]})", 256U)
                     .has_value());
    // Over the bone budget.
    EXPECT_FALSE(ParseGltfSkeletonUVE(kArmatureGltfUVE, 2U).has_value());
    // A cycle in the hierarchy.
    EXPECT_FALSE(ParseGltfSkeletonUVE(
                     R"({"asset":{"version":"2.0"},"nodes":[{"children":[1]},{"children":[0]}],"skins":[{"joints":[0,1]}]})",
                     256U)
                     .has_value());
    EXPECT_FALSE(ParseGltfSkeletonUVE("not json", 256U).has_value());
}

} // namespace
} // namespace UVE::Asset
