// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/asset_importer_uve.h"

#include <filesystem>
#include <fstream>
#include <string_view>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures_uve.h"

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"
#include "uve/math/color_uve.h"

namespace UVE::Asset::Tests {
namespace {

void WriteObjFixtureUVE(const std::filesystem::path& path, const std::string_view source) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    ASSERT_TRUE(output.good());
}

[[nodiscard]] MeshAssetUVE MakeExistingMeshUVE() {
    MeshAssetUVE mesh;
    mesh.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{5.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertexUVE{Math::Vector3UVE{4.0F, 6.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{5.0F, 6.0F, 6.0F}};
    return mesh;
}

TEST(ObjImporterUVETest, ImportUVE_ValidObjPublishesUveModelAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_triangle.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_triangle.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixtureUVE(sourcePath, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destinationPath);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.localBounds,
              (Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}}));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

// An OBJ carries its look in the MTL it names, not in the object that later draws it. Importing the
// geometry alone is what made every OBJ arrive grey with its textures sitting unused next to it.
TEST(ObjImporterUVETest, ImportUVE_ObjWithMtl_PublishesItsMaterialsAndTextures) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_material.obj";
    const std::filesystem::path libraryPath = "uve_obj_importer_tests_material.mtl";
    const std::filesystem::path textureSourcePath = "uve_obj_importer_tests_diffuse.png";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_material.uvmodel";
    const std::filesystem::path greenPath = "uve_obj_importer_tests_material.GreenPaint.uvmat";
    const std::filesystem::path redPath = "uve_obj_importer_tests_material.RedPaint.uvmat";
    const std::filesystem::path texturePath = "uve_obj_importer_tests_diffuse.uvtex";
    for (const auto& path : {sourcePath, libraryPath, textureSourcePath, destinationPath, greenPath, redPath,
                             texturePath}) {
        std::filesystem::remove(path);
    }
    std::ofstream textureOutput(textureSourcePath, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(textureOutput.is_open());
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    ASSERT_FALSE(png.empty());
    textureOutput.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    textureOutput.close();

    WriteObjFixtureUVE(libraryPath,
                       "newmtl RedPaint\n"
                       "Kd 0.9 0.2 0.1\n"
                       "map_Kd uve_obj_importer_tests_diffuse.png\n"
                       "newmtl GreenPaint\n"
                       "Kd 0.1 0.8 0.2\n");
    WriteObjFixtureUVE(sourcePath,
                       "mtllib uve_obj_importer_tests_material.mtl\n"
                       "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                       "usemtl GreenPaint\n"
                       "usemtl RedPaint\n"
                       "f 1 2 3\n");

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    // First-use order: the material the author switched to first is the object's default look.
    // GreenPaint only names a material here (no face uses it before the switch to RedPaint), so
    // this file has ONE drawn material and therefore no surface table.
    ASSERT_TRUE(mesh.surfaces.empty());
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[0]), greenPath);
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[1]), redPath);

    MaterialAssetUVE green;
    ASSERT_TRUE(LoadMaterialAssetUVE(greenPath, green));
    EXPECT_EQ(green.albedoTexture, kInvalidAssetGuidUVE);
    // MTL colours are display-authored; the material stores linear, so the assertion goes through
    // the same conversion the converter documents.
    EXPECT_NEAR(green.albedoColor.g, Math::DisplayToLinearUVE(0.8F), 0.0001F);

    MaterialAssetUVE red;
    ASSERT_TRUE(LoadMaterialAssetUVE(redPath, red));
    ASSERT_NE(red.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(red.albedoTexture), texturePath);
    TextureAssetUVE texture;
    ASSERT_TRUE(LoadTextureAssetUVE(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpaceUVE::Srgb);

    for (const auto& path : {sourcePath, libraryPath, textureSourcePath, destinationPath, greenPath, redPath,
                             texturePath}) {
        std::filesystem::remove(path);
    }
}

// An OBJ that paints its faces with more than one material publishes those runs as surfaces, one
// per usemtl run, so the object draws each part with the material the author named for it.
TEST(ObjImporterUVETest, ImportUVE_MultipleUsemtlRuns_PublishOneSurfacePerRun) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_surfaces.obj";
    const std::filesystem::path libraryPath = "uve_obj_importer_tests_surfaces.mtl";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_surfaces.uvmodel";
    const std::filesystem::path greenPath = "uve_obj_importer_tests_surfaces.Green.uvmat";
    const std::filesystem::path redPath = "uve_obj_importer_tests_surfaces.Red.uvmat";
    for (const auto& path : {sourcePath, libraryPath, destinationPath, greenPath, redPath}) {
        std::filesystem::remove(path);
    }
    WriteObjFixtureUVE(libraryPath,
                       "newmtl Green\n"
                       "Kd 0.1 0.8 0.2\n"
                       "newmtl Red\n"
                       "Kd 0.9 0.2 0.1\n");
    WriteObjFixtureUVE(sourcePath,
                       "mtllib uve_obj_importer_tests_surfaces.mtl\n"
                       "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                       "v 2 0 0\nv 3 0 0\nv 2 1 0\n"
                       "v 4 0 0\nv 5 0 0\nv 4 1 0\n"
                       "usemtl Green\nf 1 2 3\n"
                       "usemtl Red\nf 4 5 6\n"
                       "usemtl Green\nf 7 8 9\n");

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    ASSERT_EQ(mesh.surfaces.size(), 3U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurfaceUVE{0U, 3U, 0U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurfaceUVE{3U, 3U, 1U}));
    // The third run returns to the first material's slot rather than publishing it twice.
    EXPECT_EQ(mesh.surfaces[2], (MeshSurfaceUVE{6U, 3U, 0U}));
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[0]), greenPath);
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[1]), redPath);

    for (const auto& path : {sourcePath, libraryPath, destinationPath, greenPath, redPath}) {
        std::filesystem::remove(path);
    }
}

TEST(ObjImporterUVETest, ImportUVE_InvalidObjPreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_invalid.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_existing.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixtureUVE(sourcePath, "v 0 0 0\nf 1 2 3\n");
    const MeshAssetUVE original = MakeExistingMeshUVE();
    ASSERT_TRUE(SaveMeshAssetUVE(original, destinationPath));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuidUVE);
    EXPECT_TRUE(database.ResolveUVE(guid).empty());
    MeshAssetUVE retained;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, retained));
    EXPECT_EQ(retained.indices, original.indices);
    EXPECT_EQ(retained.localBounds, original.localBounds);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(ObjImporterUVETest, ImportUVE_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_wrong_destination.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_wrong_destination.uvmodel.tmp";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixtureUVE(sourcePath, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    EXPECT_EQ(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
