// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "uve/pack/asset_cooker_uve.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "Support/test_scratch_uve.h"
#include "uve/asset/asset_database_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/asset/texture_compression_uve.h"
#include "uve/asset/uve_file_envelope_uve.h"
#include "uve/platform/editor_project_package_uve.h"
namespace UVE::Pack::Tests {
namespace {
class AssetCookerUVETest : public ::testing::Test {
protected:
    std::filesystem::path root, output;
    Platform::EditorProjectPackageUVE package;
    Asset::AssetGuidUVE guid;
    void SetUp() override {
        root=::UVE::Tests::MakeTestCaseDirectoryUVE("cook-source");
        output=::UVE::Tests::ScratchPathUVE("cooked-output");
        std::filesystem::create_directories(root/"content");
        package.revision=1; package.projectId="cook-test"; package.displayName="Cook Test";
        package.productMetadata.name="Cook Test"; package.productMetadata.shortName="cook-test";
        package.contentRoot="content"; package.startupScenePath="main.uvscene";
        package.settingsPath="project.uvsettings";package.assetDatabasePath=".uvassetdb";
        ASSERT_TRUE(Platform::EditorProjectPackageCodecUVE::SaveUVE(root/"game.uvproject",package).IsAcceptedUVE());
        const std::string scene="{\"entities\":[]}";
        const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
        ASSERT_TRUE(Asset::WriteUveFileUVE(root/"content/main.uvscene",Asset::AssetKindUVE::Scene,{bytes,bytes+scene.size()}));
        Asset::TextureAssetUVE texture; texture.width=2048;texture.height=4;
        texture.pixels.resize(2048*4*4,std::byte{255});
        ASSERT_TRUE(Asset::SaveTextureAssetUVE(texture,root/"content/hero.uvtex"));
        Asset::AssetDatabaseUVE db;guid=db.RegisterUVE("content/hero.uvtex"); ASSERT_TRUE(db.SaveUVE(root/".uvassetdb"));
        std::ofstream(root/"content/authoring.png")<<"not runtime content";
        std::ofstream(root/"content/player.uvs")<<"var speed = 1\n";
    }
    void TearDown() override {std::filesystem::remove_all(root);std::filesystem::remove_all(output);}
};
TEST_F(AssetCookerUVETest, AndroidProfileBoundsTexturesPreservesGuidAndRelocatesWithoutSources) {
    const auto cooked=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Android);
    ASSERT_TRUE(cooked.success)<<cooked.message;
    EXPECT_FALSE(std::filesystem::exists(output/"content/authoring.png"));
    EXPECT_TRUE(std::filesystem::exists(output/"content/player.uvs"));
    Asset::TextureAssetUVE texture;
    ASSERT_TRUE(Asset::LoadTextureAssetUVE(output/"content/hero.uvtex",texture));
    EXPECT_LE(texture.width,1024U); EXPECT_LE(texture.height,1024U);
    std::filesystem::remove_all(root);
    Asset::AssetDatabaseUVE db;ASSERT_TRUE(db.LoadUVE(output/".uvassetdb"));
    ASSERT_TRUE(db.RootPathsUVE(output));EXPECT_TRUE(db.HasGuidUVE(guid));
    EXPECT_EQ(db.ResolveUVE(guid),std::filesystem::canonical(output/"content/hero.uvtex"));
    EXPECT_TRUE(Asset::LoadTextureAssetUVE(db.ResolveUVE(guid),texture));
}
TEST_F(AssetCookerUVETest, RepeatCookingProducesIdenticalCatalogAndTextureBytes) {
    const auto other=::UVE::Tests::ScratchPathUVE("cook-second");
    ASSERT_TRUE(CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Windows).success);
    ASSERT_TRUE(CookProjectAssetsUVE(root/"game.uvproject",other,CookTargetUVE::Windows).success);
    for(const auto& p:{".uvassetdb","content/hero.uvtex","cooked.json"}) {
        std::ifstream a(output/p,std::ios::binary),b(other/p,std::ios::binary);
        const std::string x{std::istreambuf_iterator<char>(a),{}},y{std::istreambuf_iterator<char>(b),{}};
        EXPECT_EQ(x,y)<<p;
    }
    std::filesystem::remove_all(other);
}
TEST_F(AssetCookerUVETest, CorruptRegisteredContentDoesNotPublishOrDeleteExistingStaging) {
    std::ofstream(root/"content/hero.uvtex",std::ios::trunc)<<"bad";
    const auto stage=std::filesystem::path(output.string()+".cooking");
    std::filesystem::create_directories(stage);std::ofstream(stage/"keep")<<"preexisting";
    auto result=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Android);
    EXPECT_FALSE(result.success);EXPECT_TRUE(std::filesystem::exists(stage/"keep"));
    std::filesystem::remove_all(stage);
    result=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(output));EXPECT_FALSE(std::filesystem::exists(stage));
}
TEST_F(AssetCookerUVETest, MissingRegistryEntryAndEscapingSymlinkFailClosed) {
    std::filesystem::remove(root/"content/hero.uvtex");
    EXPECT_FALSE(CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Windows).success);
    Asset::TextureAssetUVE v;v.width=1;v.height=1;v.pixels.resize(4);
    ASSERT_TRUE(Asset::SaveTextureAssetUVE(v,root/"content/hero.uvtex"));
    std::error_code error;
    std::filesystem::create_symlink(root/"game.uvproject",root/"content/escape.uvs",error);
    if(error) GTEST_SKIP()<<"Symlink creation unavailable";
    EXPECT_FALSE(CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Windows).success);
}
TEST_F(AssetCookerUVETest, MissingSceneGuidAndMalformedRegistryAreRejected) {
    const std::string scene="{\"entities\":[{\"components\":{\"PrimitiveMeshComponentUVE\":{\"materialGuid\":999}}}]}";
    const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
    ASSERT_TRUE(Asset::WriteUveFileUVE(root/"content/main.uvscene",Asset::AssetKindUVE::Scene,{bytes,bytes+scene.size()}));
    auto result=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Windows);
    EXPECT_FALSE(result.success);EXPECT_NE(result.message.find("Missing asset GUID"),std::string::npos);
    std::ofstream(root/".uvassetdb",std::ios::trunc)<<"{\"not-a-guid\":\"content/hero.uvtex\"}";
    result=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Windows);
    EXPECT_FALSE(result.success);EXPECT_NE(result.message.find("database GUID"),std::string::npos);
}
TEST_F(AssetCookerUVETest, OutputInsideContentIsRejectedBeforeCreatingStaging) {
    auto result=CookProjectAssetsUVE(root/"game.uvproject",root/"content/build",CookTargetUVE::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(root/"content/build.cooking"));
    std::error_code error;
    std::filesystem::create_directory_symlink(root/"content",root/"content-alias",error);
    if(error) GTEST_SKIP()<<"Directory symlink unavailable";
    result=CookProjectAssetsUVE(root/"game.uvproject",root/"content-alias/build",CookTargetUVE::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(root/"content/build.cooking"));
}
TEST_F(AssetCookerUVETest, OmitsRegisteredAuthoringSourcesButRejectsRuntimeReferencesToThem) {
    Asset::AssetDatabaseUVE db;ASSERT_TRUE(db.LoadUVE(root/".uvassetdb"));
    const auto sourceGuid=db.RegisterUVE("content/authoring.png");ASSERT_TRUE(db.SaveUVE());
    const auto cooked=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Android);
    ASSERT_TRUE(cooked.success)<<cooked.message;
    Asset::AssetDatabaseUVE shipped;ASSERT_TRUE(shipped.LoadUVE(output/".uvassetdb"));
    EXPECT_TRUE(shipped.HasGuidUVE(guid));EXPECT_FALSE(shipped.HasGuidUVE(sourceGuid));
    EXPECT_FALSE(std::filesystem::exists(output/"content/authoring.png"));
    std::filesystem::remove_all(output);
    const auto scene=nlohmann::json{{"entities",nlohmann::json::array({
        {{"components",{{"PrimitiveMeshComponentUVE",{{"materialGuid",sourceGuid.value}}}}}}})}}.dump();
    const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
    ASSERT_TRUE(Asset::WriteUveFileUVE(root/"content/main.uvscene",Asset::AssetKindUVE::Scene,{bytes,bytes+scene.size()}));
    const auto rejected=CookProjectAssetsUVE(root/"game.uvproject",output,CookTargetUVE::Android);
    EXPECT_FALSE(rejected.success);EXPECT_NE(rejected.message.find("authoring source"),std::string::npos);
}
TEST_F(AssetCookerUVETest, RootedCatalogRefusesTraversalWithoutChangingRecords) {
    std::ofstream(root/"bad-db")<<"{\"0000000000000001\":\"../outside\"}";
    Asset::AssetDatabaseUVE db; ASSERT_TRUE(db.LoadUVE(root/"bad-db"));
    EXPECT_FALSE(db.RootPathsUVE(root));EXPECT_EQ(db.ResolveUVE({1}),std::filesystem::path("../outside"));
}
} // namespace
} // namespace UVE::Pack::Tests
