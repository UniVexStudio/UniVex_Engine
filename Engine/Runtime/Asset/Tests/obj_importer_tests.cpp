// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_importer.h"

#include <filesystem>
#include <fstream>
#include <string_view>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures.h"

#include "asset_database.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "texture_asset.h"
#include "color.h"

namespace UVE::Asset::Tests {
namespace {

void WriteObjFixture(const std::filesystem::path& path, const std::string_view source) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(source.data(), static_cast<std::streamsize>(source.size()));
    ASSERT_TRUE(output.good());
}

[[nodiscard]] MeshAsset MakeExistingMesh() {
    MeshAsset mesh;
    mesh.vertices = {
        MeshVertex{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F},
        MeshVertex{Math::Vector3{5.0F, 5.0F, 6.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 1.0F, 0.0F},
        MeshVertex{Math::Vector3{4.0F, 6.0F, 6.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F, 1.0F},
    };
    mesh.indices = {0U, 1U, 2U};
    mesh.localBounds = Math::Aabb{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{5.0F, 6.0F, 6.0F}};
    return mesh;
}

TEST(ObjImporterTest, Import_ValidObjPublishesUveModelAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_triangle.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_triangle.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixture(sourcePath, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destinationPath);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.localBounds,
              (Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}}));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

// An OBJ carries its look in the MTL it names, not in the object that later draws it. Importing the
// geometry alone is what made every OBJ arrive grey with its textures sitting unused next to it.
TEST(ObjImporterTest, Import_ObjWithMtl_PublishesItsMaterialsAndTextures) {
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
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    ASSERT_FALSE(png.empty());
    textureOutput.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    textureOutput.close();

    WriteObjFixture(libraryPath,
                       "newmtl RedPaint\n"
                       "Kd 0.9 0.2 0.1\n"
                       "map_Kd uve_obj_importer_tests_diffuse.png\n"
                       "newmtl GreenPaint\n"
                       "Kd 0.1 0.8 0.2\n");
    WriteObjFixture(sourcePath,
                       "mtllib uve_obj_importer_tests_material.mtl\n"
                       "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                       "usemtl GreenPaint\n"
                       "usemtl RedPaint\n"
                       "f 1 2 3\n");

    AssetImporter importer;
    AssetDatabase database;
    ASSERT_NE(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    // First-use order: the material the author switched to first is the object's default look.
    // GreenPaint only names a material here (no face uses it before the switch to RedPaint), so
    // this file has ONE drawn material and therefore no surface table.
    ASSERT_TRUE(mesh.surfaces.empty());
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_EQ(database.Resolve(mesh.materialGuids[0]), greenPath);
    EXPECT_EQ(database.Resolve(mesh.materialGuids[1]), redPath);

    MaterialAsset green;
    ASSERT_TRUE(LoadMaterialAsset(greenPath, green));
    EXPECT_EQ(green.albedoTexture, kInvalidAssetGuid);
    // MTL colours are display-authored; the material stores linear, so the assertion goes through
    // the same conversion the converter documents.
    EXPECT_NEAR(green.albedoColor.g, Math::DisplayToLinear(0.8F), 0.0001F);

    MaterialAsset red;
    ASSERT_TRUE(LoadMaterialAsset(redPath, red));
    ASSERT_NE(red.albedoTexture, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(red.albedoTexture), texturePath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);

    for (const auto& path : {sourcePath, libraryPath, textureSourcePath, destinationPath, greenPath, redPath,
                             texturePath}) {
        std::filesystem::remove(path);
    }
}

// An OBJ that paints its faces with more than one material publishes those runs as surfaces, one
// per usemtl run, so the object draws each part with the material the author named for it.
TEST(ObjImporterTest, Import_MultipleUsemtlRuns_PublishOneSurfacePerRun) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_surfaces.obj";
    const std::filesystem::path libraryPath = "uve_obj_importer_tests_surfaces.mtl";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_surfaces.uvmodel";
    const std::filesystem::path greenPath = "uve_obj_importer_tests_surfaces.Green.uvmat";
    const std::filesystem::path redPath = "uve_obj_importer_tests_surfaces.Red.uvmat";
    for (const auto& path : {sourcePath, libraryPath, destinationPath, greenPath, redPath}) {
        std::filesystem::remove(path);
    }
    WriteObjFixture(libraryPath,
                       "newmtl Green\n"
                       "Kd 0.1 0.8 0.2\n"
                       "newmtl Red\n"
                       "Kd 0.9 0.2 0.1\n");
    WriteObjFixture(sourcePath,
                       "mtllib uve_obj_importer_tests_surfaces.mtl\n"
                       "v 0 0 0\nv 1 0 0\nv 0 1 0\n"
                       "v 2 0 0\nv 3 0 0\nv 2 1 0\n"
                       "v 4 0 0\nv 5 0 0\nv 4 1 0\n"
                       "usemtl Green\nf 1 2 3\n"
                       "usemtl Red\nf 4 5 6\n"
                       "usemtl Green\nf 7 8 9\n");

    AssetImporter importer;
    AssetDatabase database;
    ASSERT_NE(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    ASSERT_EQ(mesh.surfaces.size(), 3U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurface{0U, 3U, 0U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurface{3U, 3U, 1U}));
    // The third run returns to the first material's slot rather than publishing it twice.
    EXPECT_EQ(mesh.surfaces[2], (MeshSurface{6U, 3U, 0U}));
    EXPECT_EQ(database.Resolve(mesh.materialGuids[0]), greenPath);
    EXPECT_EQ(database.Resolve(mesh.materialGuids[1]), redPath);

    for (const auto& path : {sourcePath, libraryPath, destinationPath, greenPath, redPath}) {
        std::filesystem::remove(path);
    }
}

TEST(ObjImporterTest, Import_InvalidObjPreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_invalid.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_existing.uvmodel";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixture(sourcePath, "v 0 0 0\nf 1 2 3\n");
    const MeshAsset original = MakeExistingMesh();
    ASSERT_TRUE(SaveMeshAsset(original, destinationPath));

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_TRUE(database.Resolve(guid).empty());
    MeshAsset retained;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, retained));
    EXPECT_EQ(retained.indices, original.indices);
    EXPECT_EQ(retained.localBounds, original.localBounds);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

TEST(ObjImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_obj_importer_tests_wrong_destination.obj";
    const std::filesystem::path destinationPath = "uve_obj_importer_tests_wrong_destination.uvmodel.tmp";
    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
    WriteObjFixture(sourcePath, "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n");

    AssetImporter importer;
    AssetDatabase database;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
