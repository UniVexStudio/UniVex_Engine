// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "model_animations.h"

#include <filesystem>
#include <string>

namespace UVE::Asset {
namespace {

[[nodiscard]] MeshAsset Triangle() {
    MeshAsset mesh;
    mesh.vertices = {
        MeshVertex{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{1.0F, 0.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{0.0F, 1.0F, 0.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0, 1, 2};
    mesh.localBounds = Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}};
    return mesh;
}

[[nodiscard]] AnimationClipAsset Take(const std::string& name, const double seconds) {
    AnimationClipAsset clip;
    clip.clipId = name;
    clip.durationSeconds = seconds;
    AnimationAssetBoneTrack track;
    track.bone = "Hips";
    for (int i = 0; i <= 4; ++i) {
        AnimationAssetSample sample;
        sample.timeSeconds = seconds * i / 4.0;
        sample.pose.position = Math::Vector3{0.0F, static_cast<float>(i) * 0.1F, 0.0F};
        track.samples.push_back(sample);
    }
    clip.bones.push_back(track);
    return clip;
}

TEST(ModelAnimationsTest, TakesLiveInTheModelAndReadByTheirPath) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uve_model_takes_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path model = dir / "Hero.uvmodel";
    ASSERT_TRUE(SaveMeshAsset(Triangle(), model));
    EXPECT_TRUE(ListModelAnimations(model).empty()) << "a model saved before takes has none";

    ASSERT_TRUE(SaveModelAnimations(model, {Take("Walk", 1.0), Take("Run", 0.5)}));
    const std::vector<ModelAnimationEntry> takes = ListModelAnimations(model);
    ASSERT_EQ(takes.size(), 2U);
    EXPECT_EQ(takes[0].name, "Walk");
    EXPECT_EQ(takes[1].name, "Run");
    EXPECT_DOUBLE_EQ(takes[1].durationSeconds, 0.5);

    // A static mesh followed by takes still reads as itself.
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(model, mesh));
    EXPECT_EQ(mesh.vertices.size(), 3U);
    EXPECT_TRUE(mesh.joints.empty());

    // "Hero.uvmodel#Run" is a clip like any file.
    AnimationClipAsset run;
    ASSERT_TRUE(LoadAnimationClipAsset(MakeEmbeddedClipPath(model, "Run"), run));
    EXPECT_EQ(run.clipId, "Run");
    ASSERT_EQ(run.bones.size(), 1U);
    EXPECT_EQ(run.bones[0].samples.size(), 5U);
    AnimationClipAsset missing;
    EXPECT_FALSE(LoadAnimationClipAsset(MakeEmbeddedClipPath(model, "Jump"), missing));

    // The mesh written again keeps its takes; replacing them replaces them.
    ASSERT_TRUE(SaveMeshAsset(Triangle(), model));
    EXPECT_EQ(ListModelAnimations(model).size(), 2U);
    ASSERT_TRUE(LoadAnimationClipAsset(MakeEmbeddedClipPath(model, "Walk"), run));
    ASSERT_TRUE(SaveModelAnimations(model, {Take("Idle", 2.0)}));
    ASSERT_EQ(ListModelAnimations(model).size(), 1U);
    EXPECT_EQ(ListModelAnimations(model)[0].name, "Idle");
    ASSERT_TRUE(SaveModelAnimations(model, {}));
    EXPECT_TRUE(ListModelAnimations(model).empty());
    ASSERT_TRUE(LoadMeshAsset(model, mesh));

    std::filesystem::path file;
    std::string take;
    EXPECT_TRUE(SplitEmbeddedClipPath(MakeEmbeddedClipPath(model, "A Take"), file, take));
    EXPECT_EQ(take, "A Take");
    EXPECT_EQ(file, model);
    EXPECT_FALSE(SplitEmbeddedClipPath(dir / "Walk.uvanim", file, take));
    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::Asset

TEST(ModelAnimationsTest, DisplayNameReadsModelAndTake) {
    EXPECT_EQ(UVE::Asset::AssetDisplayName("Imported/Hero.fbx.uvmodel#Walk"), "Hero / Walk");
    EXPECT_EQ(UVE::Asset::AssetDisplayName("Imported/Jogging.fbx.uvmodel#Jogging"), "Jogging");
    EXPECT_EQ(UVE::Asset::AssetDisplayName("Content/Run.uvanim"), "Run");
}
