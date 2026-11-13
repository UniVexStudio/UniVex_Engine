// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include <gtest/gtest.h>

#include "animation_clip_asset.h"
#include "mesh_asset.h"
#include "model_animations.h"

#include <filesystem>
#include <string>

namespace UVE::Asset {
namespace {

[[nodiscard]] MeshAssetUVE TriangleUVE() {
    MeshAssetUVE mesh;
    mesh.vertices = {
        MeshVertexUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{1.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{0.0F, 1.0F, 0.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0, 1, 2};
    mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}};
    return mesh;
}

[[nodiscard]] AnimationClipAssetUVE TakeUVE(const std::string& name, const double seconds) {
    AnimationClipAssetUVE clip;
    clip.clipId = name;
    clip.durationSeconds = seconds;
    AnimationAssetBoneTrackUVE track;
    track.bone = "Hips";
    for (int i = 0; i <= 4; ++i) {
        AnimationAssetSampleUVE sample;
        sample.timeSeconds = seconds * i / 4.0;
        sample.pose.position = Math::Vector3UVE{0.0F, static_cast<float>(i) * 0.1F, 0.0F};
        track.samples.push_back(sample);
    }
    clip.bones.push_back(track);
    return clip;
}

TEST(ModelAnimationsUVETest, TakesLiveInTheModelAndReadByTheirPath) {
    const std::filesystem::path dir = std::filesystem::temp_directory_path() / "uve_model_takes_test";
    std::filesystem::create_directories(dir);
    const std::filesystem::path model = dir / "Hero.uvmodel";
    ASSERT_TRUE(SaveMeshAssetUVE(TriangleUVE(), model));
    EXPECT_TRUE(ListModelAnimationsUVE(model).empty()) << "a model saved before takes has none";

    ASSERT_TRUE(SaveModelAnimationsUVE(model, {TakeUVE("Walk", 1.0), TakeUVE("Run", 0.5)}));
    const std::vector<ModelAnimationEntryUVE> takes = ListModelAnimationsUVE(model);
    ASSERT_EQ(takes.size(), 2U);
    EXPECT_EQ(takes[0].name, "Walk");
    EXPECT_EQ(takes[1].name, "Run");
    EXPECT_DOUBLE_EQ(takes[1].durationSeconds, 0.5);

    // A static mesh followed by takes still reads as itself.
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(model, mesh));
    EXPECT_EQ(mesh.vertices.size(), 3U);
    EXPECT_TRUE(mesh.joints.empty());

    // "Hero.uvmodel#Run" is a clip like any file.
    AnimationClipAssetUVE run;
    ASSERT_TRUE(LoadAnimationClipAssetUVE(MakeEmbeddedClipPathUVE(model, "Run"), run));
    EXPECT_EQ(run.clipId, "Run");
    ASSERT_EQ(run.bones.size(), 1U);
    EXPECT_EQ(run.bones[0].samples.size(), 5U);
    AnimationClipAssetUVE missing;
    EXPECT_FALSE(LoadAnimationClipAssetUVE(MakeEmbeddedClipPathUVE(model, "Jump"), missing));

    // The mesh written again keeps its takes; replacing them replaces them.
    ASSERT_TRUE(SaveMeshAssetUVE(TriangleUVE(), model));
    EXPECT_EQ(ListModelAnimationsUVE(model).size(), 2U);
    ASSERT_TRUE(LoadAnimationClipAssetUVE(MakeEmbeddedClipPathUVE(model, "Walk"), run));
    ASSERT_TRUE(SaveModelAnimationsUVE(model, {TakeUVE("Idle", 2.0)}));
    ASSERT_EQ(ListModelAnimationsUVE(model).size(), 1U);
    EXPECT_EQ(ListModelAnimationsUVE(model)[0].name, "Idle");
    ASSERT_TRUE(SaveModelAnimationsUVE(model, {}));
    EXPECT_TRUE(ListModelAnimationsUVE(model).empty());
    ASSERT_TRUE(LoadMeshAssetUVE(model, mesh));

    std::filesystem::path file;
    std::string take;
    EXPECT_TRUE(SplitEmbeddedClipPathUVE(MakeEmbeddedClipPathUVE(model, "A Take"), file, take));
    EXPECT_EQ(take, "A Take");
    EXPECT_EQ(file, model);
    EXPECT_FALSE(SplitEmbeddedClipPathUVE(dir / "Walk.uvanim", file, take));
    std::filesystem::remove_all(dir);
}

} // namespace
} // namespace UVE::Asset

TEST(ModelAnimationsUVETest, DisplayNameReadsModelAndTake) {
    EXPECT_EQ(UVE::Asset::AssetDisplayNameUVE("Imported/Hero.fbx.uvmodel#Walk"), "Hero / Walk");
    EXPECT_EQ(UVE::Asset::AssetDisplayNameUVE("Imported/Jogging.fbx.uvmodel#Jogging"), "Jogging");
    EXPECT_EQ(UVE::Asset::AssetDisplayNameUVE("Content/Run.uvanim"), "Run");
}
