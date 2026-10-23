// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/mtl_material_converter_uve.h"

#include <string_view>

#include <gtest/gtest.h>

#include "uve/asset/asset_guid_uve.h"

namespace UVE::Asset::Tests {
namespace {

TEST(MtlMaterialConverterUVETest, ConvertMtlMaterialUVE_MapsBoundedPbrProperties) {
    constexpr std::string_view source = R"MTL(
newmtl PaintedMetal
Kd 0.2 0.4 0.6
Ka 0.1 0.1 0.1
Ks 0.8 0.6 0.4
Ke 1.0 2.0 3.0
Ns 500
Ni 1.5
d 0.5
illum 2
map_Kd -s 1 1 1 -o 0 0 0 -clamp on albedo.png
)MTL";

    MaterialAssetUVE material;
    ASSERT_TRUE(ConvertMtlMaterialUVE(source, material));
    EXPECT_NEAR(material.albedoColor.r, 0.0331048F, 1e-6F);
    EXPECT_NEAR(material.albedoColor.g, 0.1328683F, 1e-6F);
    EXPECT_NEAR(material.albedoColor.b, 0.3185468F, 1e-6F);
    EXPECT_NEAR(material.emissiveColor.r, 1.0F, 1e-6F);
    EXPECT_NEAR(material.emissiveColor.g, 4.9538458F, 1e-4F);
    EXPECT_NEAR(material.emissiveColor.b, 12.8298333F, 1e-3F);
    EXPECT_FLOAT_EQ(material.metallic, 0.6F);
    EXPECT_FLOAT_EQ(material.roughness, 0.5F);
    EXPECT_TRUE(material.isTransparent);
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.aoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.vertexShader, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.fragmentShader, kInvalidAssetGuidUVE);
}

TEST(MtlMaterialConverterUVETest, ConvertMtlMaterialUVE_MapReferencesRemainUnresolved) {
    constexpr std::string_view source = "newmtl Neutral\nmap_Ka normal.png\nmap_Bump -clamp off normal.png\n";

    MaterialAssetUVE material;
    ASSERT_TRUE(ConvertMtlMaterialUVE(source, material));
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.aoTexture, kInvalidAssetGuidUVE);
}

TEST(MtlMaterialConverterUVETest, ConvertMtlMaterialUVE_InvalidInputPreservesExistingOutput) {
    MaterialAssetUVE original;
    original.albedoColor = Math::ColorUVE{0.3F, 0.2F, 0.1F};
    original.metallic = 0.8F;
    original.isTransparent = true;
    MaterialAssetUVE output = original;

    constexpr std::string_view invalidSource = "newmtl Broken\nKd 1 0\n";
    EXPECT_FALSE(ConvertMtlMaterialUVE(invalidSource, output));
    EXPECT_EQ(output.albedoColor, original.albedoColor);
    EXPECT_FLOAT_EQ(output.metallic, original.metallic);
    EXPECT_EQ(output.isTransparent, original.isTransparent);
}

TEST(MtlMaterialConverterUVETest, ConvertMtlMaterialUVE_RejectsInvalidDissolveAndMultipleMaterials) {
    MaterialAssetUVE material;
    EXPECT_FALSE(ConvertMtlMaterialUVE("newmtl Broken\nd 1.5\n", material));
    EXPECT_FALSE(ConvertMtlMaterialUVE("newmtl First\nnewmtl Second\n", material));
    EXPECT_FALSE(ConvertMtlMaterialUVE("Kd 1 1 1\n", material));
}

// A scene library is one file with many materials; the single-material converter refuses those by
// design, so the library converter splits them and keeps the per-block validation intact.
TEST(MtlMaterialConverterUVETest, ConvertMtlLibraryUVE_SplitsBlocksAndKeepsTheirTextureNames) {
    constexpr std::string_view source = R"MTL(
# exported by a DCC tool
newmtl RedPaint
Kd 0.9 0.2 0.1
map_Kd paint/red.png

newmtl GreenPaint
Kd 0.1 0.8 0.2
map_Kd paint/green.png
map_Bump paint/green_normal.jpg
bump paint/legacy_normal.png
)MTL";

    MtlLibraryUVE library;
    ASSERT_TRUE(ConvertMtlLibraryUVE(source, library));
    ASSERT_EQ(library.materials.size(), 2U);
    EXPECT_EQ(library.skippedMaterials, 0U);
    EXPECT_FALSE(library.truncated);

    EXPECT_EQ(library.materials[0].name, "RedPaint");
    EXPECT_EQ(library.materials[0].albedoTextureFile, "paint/red.png");
    EXPECT_TRUE(library.materials[0].normalTextureFile.empty());
    EXPECT_NEAR(library.materials[0].material.albedoColor.r, 0.7874123F, 1e-5F);

    EXPECT_EQ(library.materials[1].name, "GreenPaint");
    EXPECT_EQ(library.materials[1].albedoTextureFile, "paint/green.png");
    // The first normal reference wins; the older `bump` spelling is the same slot, and it is not
    // allowed to overwrite what the author wrote first.
    EXPECT_EQ(library.materials[1].normalTextureFile, "paint/green_normal.jpg");
}

TEST(MtlMaterialConverterUVETest, ConvertMtlLibraryUVE_DropsOnlyTheBlockThatFails) {
    constexpr std::string_view source = R"MTL(
newmtl Good
Kd 0.5 0.5 0.5
newmtl Broken
Kd 1 0
newmtl AlsoGood
Ke 0.1 0.1 0.1
)MTL";

    MtlLibraryUVE library;
    ASSERT_TRUE(ConvertMtlLibraryUVE(source, library));
    ASSERT_EQ(library.materials.size(), 2U);
    EXPECT_EQ(library.skippedMaterials, 1U);
    EXPECT_EQ(library.materials[0].name, "Good");
    EXPECT_EQ(library.materials[1].name, "AlsoGood");
}

TEST(MtlMaterialConverterUVETest, ConvertMtlLibraryUVE_RejectsUnreadableSources) {
    MtlLibraryUVE library;
    EXPECT_FALSE(ConvertMtlLibraryUVE("", library));
    std::string tooLarge(kMaximumMtlLibrarySourceBytesUVE + 1U, 'x');
    EXPECT_FALSE(ConvertMtlLibraryUVE(tooLarge, library));
    EXPECT_FALSE(ConvertMtlLibraryUVE(std::string_view{"newmtl A\0B\n", 11U}, library));
}

} // namespace
} // namespace UVE::Asset::Tests
