// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "asset_cooker.h"
#include <filesystem>
#include <fstream>
#include <gtest/gtest.h>
#include <nlohmann/json.hpp>
#include "Support/test_scratch.h"
#include "asset_database.h"
#include "texture_asset.h"
#include "texture_compression.h"
#include "uve_file_envelope.h"
#include "editor_project_package.h"
#include "particle_system_asset.h"
namespace UVE::Pack::Tests {
namespace {
class AssetCookerTest : public ::testing::Test {
protected:
    std::filesystem::path root, output;
    Platform::EditorProjectPackage package;
    Asset::AssetGuid guid;
    void SetUp() override {
        root=::UVE::Tests::MakeTestCaseDirectory("cook-source");
        output=::UVE::Tests::ScratchPath("cooked-output");
        std::filesystem::create_directories(root/"content");
        package.revision=1; package.projectId="cook-test"; package.displayName="Cook Test";
        package.productMetadata.name="Cook Test"; package.productMetadata.shortName="cook-test";
        package.contentRoot="content"; package.startupScenePath="main.uvscene";
        package.settingsPath="project.uvsettings";package.assetDatabasePath=".uvassetdb";
        ASSERT_TRUE(Platform::EditorProjectPackageCodec::Save(root/"game.uvproject",package).IsAccepted());
        const std::string scene="{\"entities\":[]}";
        const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
        ASSERT_TRUE(Asset::WriteUveFile(root/"content/main.uvscene",Asset::AssetKind::Scene,{bytes,bytes+scene.size()}));
        Asset::TextureAsset texture; texture.width=2048;texture.height=4;
        texture.pixels.resize(2048*4*4,std::byte{255});
        ASSERT_TRUE(Asset::SaveTextureAsset(texture,root/"content/hero.uvtex"));
        Asset::AssetDatabase db;guid=db.Register("content/hero.uvtex"); ASSERT_TRUE(db.Save(root/".uvassetdb"));
        std::ofstream(root/"content/authoring.png")<<"not runtime content";
        std::ofstream(root/"content/player.uvs")<<"var speed = 1\n";
    }
    void TearDown() override {std::filesystem::remove_all(root);std::filesystem::remove_all(output);}
};
TEST_F(AssetCookerTest, AndroidProfileBoundsTexturesPreservesGuidAndRelocatesWithoutSources) {
    const auto cooked=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    ASSERT_TRUE(cooked.success)<<cooked.message;
    EXPECT_FALSE(std::filesystem::exists(output/"content/authoring.png"));
    EXPECT_TRUE(std::filesystem::exists(output/"content/player.uvs"));
    Asset::TextureAsset texture;
    ASSERT_TRUE(Asset::LoadTextureAsset(output/"content/hero.uvtex",texture));
    EXPECT_LE(texture.width,1024U); EXPECT_LE(texture.height,1024U);
    std::filesystem::remove_all(root);
    Asset::AssetDatabase db;ASSERT_TRUE(db.Load(output/".uvassetdb"));
    ASSERT_TRUE(db.RootPaths(output));EXPECT_TRUE(db.HasGuid(guid));
    EXPECT_EQ(db.Resolve(guid),std::filesystem::canonical(output/"content/hero.uvtex"));
    EXPECT_TRUE(Asset::LoadTextureAsset(db.Resolve(guid),texture));
}
TEST_F(AssetCookerTest, RepeatCookingProducesIdenticalCatalogAndTextureBytes) {
    const auto other=::UVE::Tests::ScratchPath("cook-second");
    ASSERT_TRUE(CookProjectAssets(root/"game.uvproject",output,CookTarget::Windows).success);
    ASSERT_TRUE(CookProjectAssets(root/"game.uvproject",other,CookTarget::Windows).success);
    for(const auto& p:{".uvassetdb","content/hero.uvtex","cooked.json"}) {
        std::ifstream a(output/p,std::ios::binary),b(other/p,std::ios::binary);
        const std::string x{std::istreambuf_iterator<char>(a),{}},y{std::istreambuf_iterator<char>(b),{}};
        EXPECT_EQ(x,y)<<p;
    }
    std::filesystem::remove_all(other);
}
TEST_F(AssetCookerTest, CorruptRegisteredContentDoesNotPublishOrDeleteExistingStaging) {
    std::ofstream(root/"content/hero.uvtex",std::ios::trunc)<<"bad";
    const auto stage=std::filesystem::path(output.string()+".cooking");
    std::filesystem::create_directories(stage);std::ofstream(stage/"keep")<<"preexisting";
    auto result=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    EXPECT_FALSE(result.success);EXPECT_TRUE(std::filesystem::exists(stage/"keep"));
    std::filesystem::remove_all(stage);
    result=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(output));EXPECT_FALSE(std::filesystem::exists(stage));
}
TEST_F(AssetCookerTest, MissingRegistryEntryAndEscapingSymlinkFailClosed) {
    std::filesystem::remove(root/"content/hero.uvtex");
    EXPECT_FALSE(CookProjectAssets(root/"game.uvproject",output,CookTarget::Windows).success);
    Asset::TextureAsset v;v.width=1;v.height=1;v.pixels.resize(4);
    ASSERT_TRUE(Asset::SaveTextureAsset(v,root/"content/hero.uvtex"));
    std::error_code error;
    std::filesystem::create_symlink(root/"game.uvproject",root/"content/escape.uvs",error);
    if(error) GTEST_SKIP()<<"Symlink creation unavailable";
    EXPECT_FALSE(CookProjectAssets(root/"game.uvproject",output,CookTarget::Windows).success);
}
TEST_F(AssetCookerTest, MissingSceneGuidAndMalformedRegistryAreRejected) {
    const std::string scene="{\"entities\":[{\"components\":{\"PrimitiveMeshComponent\":{\"materialGuid\":999}}}]}";
    const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
    ASSERT_TRUE(Asset::WriteUveFile(root/"content/main.uvscene",Asset::AssetKind::Scene,{bytes,bytes+scene.size()}));
    auto result=CookProjectAssets(root/"game.uvproject",output,CookTarget::Windows);
    EXPECT_FALSE(result.success);EXPECT_NE(result.message.find("Missing asset GUID"),std::string::npos);
    std::ofstream(root/".uvassetdb",std::ios::trunc)<<"{\"not-a-guid\":\"content/hero.uvtex\"}";
    result=CookProjectAssets(root/"game.uvproject",output,CookTarget::Windows);
    EXPECT_FALSE(result.success);EXPECT_NE(result.message.find("database GUID"),std::string::npos);
}
TEST_F(AssetCookerTest, OutputInsideContentIsRejectedBeforeCreatingStaging) {
    auto result=CookProjectAssets(root/"game.uvproject",root/"content/build",CookTarget::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(root/"content/build.cooking"));
    std::error_code error;
    std::filesystem::create_directory_symlink(root/"content",root/"content-alias",error);
    if(error) GTEST_SKIP()<<"Directory symlink unavailable";
    result=CookProjectAssets(root/"game.uvproject",root/"content-alias/build",CookTarget::Android);
    EXPECT_FALSE(result.success);EXPECT_FALSE(std::filesystem::exists(root/"content/build.cooking"));
}
TEST_F(AssetCookerTest, OmitsRegisteredAuthoringSourcesButRejectsRuntimeReferencesToThem) {
    Asset::AssetDatabase db;ASSERT_TRUE(db.Load(root/".uvassetdb"));
    const auto sourceGuid=db.Register("content/authoring.png");ASSERT_TRUE(db.Save());
    const auto cooked=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    ASSERT_TRUE(cooked.success)<<cooked.message;
    Asset::AssetDatabase shipped;ASSERT_TRUE(shipped.Load(output/".uvassetdb"));
    EXPECT_TRUE(shipped.HasGuid(guid));EXPECT_FALSE(shipped.HasGuid(sourceGuid));
    EXPECT_FALSE(std::filesystem::exists(output/"content/authoring.png"));
    std::filesystem::remove_all(output);
    const auto scene=nlohmann::json{{"entities",nlohmann::json::array({
        {{"components",{{"PrimitiveMeshComponent",{{"materialGuid",sourceGuid.value}}}}}}})}}.dump();
    const auto* bytes=reinterpret_cast<const std::byte*>(scene.data());
    ASSERT_TRUE(Asset::WriteUveFile(root/"content/main.uvscene",Asset::AssetKind::Scene,{bytes,bytes+scene.size()}));
    const auto rejected=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    EXPECT_FALSE(rejected.success);EXPECT_NE(rejected.message.find("authoring source"),std::string::npos);
}
TEST_F(AssetCookerTest, ExistingDanglingStagingSymlinkIsNeverOwnedOrRemoved) {
    const auto stage=std::filesystem::path(output.string()+".cooking");
    std::error_code error;
    std::filesystem::create_symlink(root/"missing-target",stage,error);
    if(error) GTEST_SKIP()<<"Symlink creation unavailable";
    const auto result=CookProjectAssets(root/"game.uvproject",output,CookTarget::Android);
    EXPECT_FALSE(result.success);EXPECT_TRUE(std::filesystem::is_symlink(std::filesystem::symlink_status(stage)));
    std::filesystem::remove(stage);
}
TEST_F(AssetCookerTest, RootedCatalogRefusesTraversalWithoutChangingRecords) {
    std::ofstream(root/"bad-db")<<"{\"0000000000000001\":\"../outside\"}";
    Asset::AssetDatabase db; ASSERT_TRUE(db.Load(root/"bad-db"));
    EXPECT_FALSE(db.RootPaths(root));EXPECT_EQ(db.Resolve({1}),std::filesystem::path("../outside"));
}
TEST_F(AssetCookerTest, ParticleSystemAssetsAndDependenciesAreValidatedDuringCook) {
    Asset::ParticleSystemAsset sys;
    Asset::ParticleEmitterAsset em;
    em.name = "Sparks";
    em.emitterUpdate.push_back(Asset::MakeDefaultParticleModule(1U, Asset::ParticleModuleType::SpawnRate));
    em.particleSpawn.push_back(Asset::MakeDefaultParticleModule(2U, Asset::ParticleModuleType::ShapePoint));
    auto sprite = Asset::MakeDefaultParticleModule(3U, Asset::ParticleModuleType::SpriteRenderer);
    sprite.strings["Texture"] = "hero.uvtex";
    em.render.push_back(sprite);
    sys.emitters.push_back(em);
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(sys, root/"content/VFX_Sparks.uvfx"));

    Asset::AssetDatabase db;
    ASSERT_TRUE(db.Load(root/".uvassetdb"));
    const auto vfxGuid = db.Register("content/VFX_Sparks.uvfx");
    ASSERT_TRUE(db.Save());

    const auto cooked = CookProjectAssets(root/"game.uvproject", output, CookTarget::Windows);
    ASSERT_TRUE(cooked.success) << cooked.message;
    EXPECT_TRUE(std::filesystem::exists(output/"content/VFX_Sparks.uvfx"));
    Asset::AssetDatabase shipped;
    ASSERT_TRUE(shipped.Load(output/".uvassetdb"));
    EXPECT_TRUE(shipped.HasGuid(vfxGuid));
    std::filesystem::remove_all(output);

    const auto sourceGuid = db.Register("content/authoring.png");
    ASSERT_TRUE(db.Save());
    sys.emitters[0].render[0].strings["Texture"] = std::to_string(sourceGuid.value);
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(sys, root/"content/VFX_Sparks.uvfx"));
    const auto rejectedSource = CookProjectAssets(root/"game.uvproject", output, CookTarget::Windows);
    EXPECT_FALSE(rejectedSource.success);
    EXPECT_NE(rejectedSource.message.find("authoring source"), std::string::npos);

    sys.emitters[0].render[0].strings["Texture"] = "999999999999";
    ASSERT_TRUE(Asset::SaveParticleSystemAsset(sys, root/"content/VFX_Sparks.uvfx"));
    const auto rejectedMissing = CookProjectAssets(root/"game.uvproject", output, CookTarget::Windows);
    EXPECT_FALSE(rejectedMissing.success);
    EXPECT_NE(rejectedMissing.message.find("Missing asset GUID"), std::string::npos);
}
TEST_F(AssetCookerTest, CooksAuthoredMaterialsEntitiesAndHonorsConfiguredMaxTextureSize) {
    const std::string entityJson = "{\"entities\":[]}";
    const auto* entityBytes = reinterpret_cast<const std::byte*>(entityJson.data());
    ASSERT_TRUE(Asset::WriteUveFile(root/"content/hero.uventity", Asset::AssetKind::Prefab,
                                       {entityBytes, entityBytes + entityJson.size()}));
    Asset::TextureAsset largeTex;
    largeTex.width = 4096U;
    largeTex.height = 4U;
    largeTex.pixels.resize(4096U * 4U * 4U, std::byte{255});
    ASSERT_TRUE(Asset::SaveTextureAsset(largeTex, root/"content/hero.uvtex"));
    std::ofstream(root/"project.uvsettings") << "{\"rendering\":{\"textures\":{\"maxSizePerTier\":4096}}}\n";

    Asset::AssetDatabase db;
    ASSERT_TRUE(db.Load(root/".uvassetdb"));
    const auto entityGuid = db.Register("content/hero.uventity");
    ASSERT_TRUE(db.Save());

    const auto cooked = CookProjectAssets(root/"game.uvproject", output, CookTarget::Windows);
    ASSERT_TRUE(cooked.success) << cooked.message;
    EXPECT_TRUE(std::filesystem::exists(output/"content/hero.uventity"));
    Asset::AssetDatabase shipped;
    ASSERT_TRUE(shipped.Load(output/".uvassetdb"));
    EXPECT_TRUE(shipped.HasGuid(entityGuid));
    Asset::TextureAsset cookedTex;
    ASSERT_TRUE(Asset::LoadTextureAsset(output/"content/hero.uvtex", cookedTex));
    EXPECT_EQ(cookedTex.width, 4096U);
}
} // namespace
} // namespace UVE::Pack::Tests
