// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "mtl_metadata.h"
#include <string>
#include <gtest/gtest.h>
namespace UVE::Asset::Tests {
TEST(MtlMetadataTest, ValidateMtlTextureReference_AcceptsBoundedRelativePaths) {
    EXPECT_TRUE(ValidateMtlTextureReference("textures/brick.png"));
    EXPECT_TRUE(ValidateMtlTextureReference("brick.png"));
    EXPECT_TRUE(ValidateMtlTextureReference(std::string(kMaximumMtlTextureReferenceBytes, 'a')));
}

TEST(MtlMetadataTest, ValidateMtlTextureReference_RejectsUnsafeReferences) {
    EXPECT_FALSE(ValidateMtlTextureReference(""));
    EXPECT_FALSE(ValidateMtlTextureReference("/textures/brick.png"));
    EXPECT_FALSE(ValidateMtlTextureReference("C:/textures/brick.png"));
    EXPECT_FALSE(ValidateMtlTextureReference("textures/../brick.png"));
    EXPECT_FALSE(ValidateMtlTextureReference("textures//brick.png"));
    EXPECT_FALSE(ValidateMtlTextureReference(std::string(kMaximumMtlTextureReferenceBytes + 1U, 'a')));
    EXPECT_FALSE(ValidateMtlTextureReference(std::string("brick\0.png", 9U)));
}

TEST(MtlMetadataTest, ParseMtlMaterialProperty_CopiesScalarVectorAndTextureFacts) {
    MtlMaterialProperty property;
    ASSERT_TRUE(ParseMtlMaterialProperty("Ns 32.5", property));
    EXPECT_EQ(property.kind, MtlMaterialPropertyKind::Scalar);
    EXPECT_FLOAT_EQ(property.scalarValue, 32.5F);
    ASSERT_TRUE(ParseMtlMaterialProperty("Kd 1.0 0.5 0.25", property));
    EXPECT_EQ(property.kind, MtlMaterialPropertyKind::Vector3);
    EXPECT_FLOAT_EQ(property.vectorValue[0], 1.0F);
    EXPECT_FLOAT_EQ(property.vectorValue[1], 0.5F);
    EXPECT_FLOAT_EQ(property.vectorValue[2], 0.25F);
    ASSERT_TRUE(ParseMtlMaterialProperty("map_Kd textures/brick.png", property));
    EXPECT_EQ(property.kind, MtlMaterialPropertyKind::TextureReference);
    EXPECT_EQ(property.textureReference, "textures/brick.png");
}

TEST(MtlMetadataTest, ParseMtlMaterialProperty_RejectsInvalidAndExtraTokensAtomically) {
    const MtlMaterialProperty original{MtlMaterialPropertyKind::Scalar, 7.0F, {1.0F, 2.0F, 3.0F}, "stable.png"};
    MtlMaterialProperty property = original;
    EXPECT_FALSE(ParseMtlMaterialProperty("Kd 1 0", property));
    EXPECT_EQ(property, original);
    EXPECT_FALSE(ParseMtlMaterialProperty("Ns nan", property));
    EXPECT_EQ(property, original);
    EXPECT_FALSE(ParseMtlMaterialProperty("map_Kd ../brick.png", property));
    EXPECT_EQ(property, original);
    EXPECT_FALSE(ParseMtlMaterialProperty("map_Kd -s 1 1 1 brick.png", property));
    EXPECT_EQ(property, original);
    EXPECT_FALSE(ParseMtlMaterialProperty("Kd 1 0 0 extra", property));
    EXPECT_EQ(property, original);
}

TEST(MtlMetadataTest, ParseMtlTextureMap_ParsesBoundedOptionsAndDefaults) {
    MtlTextureMap map;
    ASSERT_TRUE(ParseMtlTextureMap("map_Kd -s 2 3 4 -o -1 0.5 0 -clamp on textures/brick.png", map));
    EXPECT_EQ(map.textureReference, "textures/brick.png");
    EXPECT_EQ(map.scale, (std::array<float, 3U>{2.0F, 3.0F, 4.0F}));
    EXPECT_EQ(map.offset, (std::array<float, 3U>{-1.0F, 0.5F, 0.0F}));
    EXPECT_TRUE(map.clamp);
    ASSERT_TRUE(ParseMtlTextureMap("map_Bump brick_n.png", map));
    EXPECT_EQ(map.textureReference, "brick_n.png");
    EXPECT_EQ(map.scale, (std::array<float, 3U>{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(map.offset, (std::array<float, 3U>{0.0F, 0.0F, 0.0F}));
    EXPECT_FALSE(map.clamp);
}

TEST(MtlMetadataTest, ParseMtlTextureMap_RejectsInvalidOptionsAtomically) {
    const MtlTextureMap original{"stable.png", {1.0F, 1.0F, 1.0F}, {0.0F, 0.0F, 0.0F}, false};
    MtlTextureMap map = original;
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd -s 1 2 textures/brick.png", map));
    EXPECT_EQ(map, original);
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd -clamp maybe brick.png", map));
    EXPECT_EQ(map, original);
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd -s nan 1 1 brick.png", map));
    EXPECT_EQ(map, original);
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd ../brick.png", map));
    EXPECT_EQ(map, original);
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd brick.png other.png", map));
    EXPECT_EQ(map, original);
    EXPECT_FALSE(ParseMtlTextureMap("map_Kd -unknown brick.png", map));
    EXPECT_EQ(map, original);
}

TEST(MtlMetadataTest, ParseMtlMetadata_CountsMaterialPropertiesAndMaps) {
    const auto metadata = ParseMtlMetadata("newmtl Brick\nKd 1 0.5 0.2\nNs 32\nmap_Kd brick.png\nmap_Bump brick_n.png\nillum 2\n");
    ASSERT_TRUE(metadata.has_value()); EXPECT_EQ(metadata->materialCount,1U); EXPECT_EQ(metadata->textureMapCount,2U);
    EXPECT_EQ(metadata->vectorPropertyCount,1U); EXPECT_EQ(metadata->scalarPropertyCount,2U); EXPECT_EQ(metadata->ignoredStatementCount,0U);
}
TEST(MtlMetadataTest, ParseMtlMetadata_CountsIgnoredDirectives) {
    const auto metadata = ParseMtlMetadata("# comment\nnewmtl A\nfoo custom\n"); ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->materialCount,1U); EXPECT_EQ(metadata->ignoredStatementCount,1U);
}
TEST(MtlMetadataTest, ParseMtlMetadata_RejectsMissingRequiredTokens) {
    EXPECT_FALSE(ParseMtlMetadata("newmtl\n").has_value()); EXPECT_FALSE(ParseMtlMetadata("Kd 1 0\n").has_value());
    EXPECT_FALSE(ParseMtlMetadata("map_Kd\n").has_value());
}

TEST(MtlMetadataTest, ParseMtlMetadata_RejectsTrailingTokensOnRecognizedDeclarations) {
    EXPECT_FALSE(ParseMtlMetadata("newmtl Brick extra\n").has_value());
    EXPECT_FALSE(ParseMtlMetadata("newmtl Brick\nKd 1 0.5 0.2 extra\n").has_value());
    EXPECT_FALSE(ParseMtlMetadata("newmtl Brick\nNs 32 extra\n").has_value());
    EXPECT_FALSE(ParseMtlMetadata("newmtl Brick\nmap_Kd brick.png other.png\n").has_value());
}
}
