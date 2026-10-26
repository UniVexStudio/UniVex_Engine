// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// FBX material conversion tests. The fixtures are ASCII FBX 7.4 files written the way the mesh
// converter's own tests write them, so the reader is exercised through the vendored ufbx parser
// rather than through hand-built structs. Every expected value was taken from what the parser
// actually reports for these files; the assertions below are the contract, not a snapshot.

#include "uve/asset/fbx_mesh_converter_uve.h"

#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures_uve.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::string MakeFbxUVE(const std::string_view objects, const std::string_view connections) {
    std::string text = R"(; FBX 7.4.0 project file
FBXHeaderExtension:  {
	FBXHeaderVersion: 1003
	FBXVersion: 7400
}
GlobalSettings:  {
	Version: 1000
	Properties70:  {
		P: "UpAxis", "int", "Integer", "",2
		P: "UpAxisSign", "int", "Integer", "",1
		P: "FrontAxis", "int", "Integer", "",1
		P: "FrontAxisSign", "int", "Integer", "",-1
		P: "CoordAxis", "int", "Integer", "",0
		P: "CoordAxisSign", "int", "Integer", "",1
		P: "UnitScaleFactor", "double", "Number", "",1
	}
}
Objects:  {
)";
    text += objects;
    text += "}\nConnections:  {\n";
    text += connections;
    text += "}\n";
    return text;
}

[[nodiscard]] std::span<const std::byte> BytesUVE(const std::string& text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

constexpr std::string_view kLambertMaterialUVE = R"(	Material: 3000, "Material::PaintedMetal", "" {
		Version: 102
		ShadingModel: "lambert"
		MultiLayer: 0
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.25,0.5,0.75
			P: "TransparencyFactor", "Number", "", "A",0.5
		}
	}
)";

// A classic phong material, the shape most DCC tools write: PBR factors come out of the FBX
// properties ufbx maps for this shading model (DiffuseColor, Shininess, EmissiveColor), while a
// base-color map is referenced by file and a normal map is a texture the file does not name.
[[nodiscard]] std::string MakeTexturedPhongMaterialFbxUVE() {
    return MakeFbxUVE(R"(	Material: 3100, "Material::BrushedSteel", "" {
		Version: 102
		ShadingModel: "phong"
		MultiLayer: 0
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.1,0.2,0.3
			P: "Shininess", "Number", "", "A",40
			P: "EmissiveColor", "Color", "", "A",0.05,0.06,0.07
		}
	}
	Texture: 3200, "Texture::SteelDiffuse", "" {
		Type: "File"
		FileName: "C:/assets/textures/steel_diffuse.png"
		RelativeFilename: "textures/steel_diffuse.png"
	}
	Texture: 3201, "Texture::SteelNoise", "" {
		Type: "Procedural"
	}
)",
                     "\tC: \"OP\",3200,3100,\"DiffuseColor\"\n"
                     "\tC: \"OP\",3201,3100,\"NormalMap\"\n");
}


TEST(FbxMaterialConverterUVETest, AnEmptyOrMalformedFileFailsWithoutPublishingMaterials) {
    EXPECT_FALSE(ReadFbxMaterialsUVE({}).has_value());
    const std::string garbage = "not an fbx file at all";
    EXPECT_FALSE(ReadFbxMaterialsUVE(BytesUVE(garbage)).has_value());
}

TEST(FbxMaterialConverterUVETest, AFileWithoutMaterialsIsAnEmptyListNotAFailure) {
    const std::string text = MakeFbxUVE(R"(	Model: 2000, "Model::Bare", "Mesh" {
		Version: 232
	}
)",
                                        "\tC: \"OO\",2000,0\n");
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    EXPECT_TRUE(materials->empty());
}

TEST(FbxMaterialConverterUVETest, ClassicDiffuseAndTransparencyMapToThePbrMaterial) {
    const std::string text = MakeFbxUVE(kLambertMaterialUVE, "");
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 1U);
    const FbxMaterialUVE& material = materials->front();
    EXPECT_EQ(material.name, "PaintedMetal");
    EXPECT_FLOAT_EQ(material.material.albedoColor.r, 0.25F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.g, 0.5F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.b, 0.75F);
    EXPECT_TRUE(material.material.isTransparent) << "TransparencyFactor 0.5 is a blending material";
    EXPECT_FLOAT_EQ(material.material.alphaCutoff, 0.0F) << "FBX has no cutoff to map";
    EXPECT_TRUE(material.albedo.file.empty());
    EXPECT_TRUE(material.albedo.embedded.empty());
    EXPECT_FALSE(material.albedo.procedural);
    EXPECT_TRUE(material.normal.file.empty());
    EXPECT_FALSE(material.normal.procedural);
    EXPECT_TRUE(IsMaterialAssetValidUVE(material.material));
}

TEST(FbxMaterialConverterUVETest, PbrFactorsAndFileTextureReferencesSurviveTheConversion) {
    const std::string text = MakeTexturedPhongMaterialFbxUVE();
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 1U);
    const FbxMaterialUVE& material = materials->front();
    EXPECT_EQ(material.name, "BrushedSteel");
    EXPECT_FLOAT_EQ(material.material.albedoColor.r, 0.1F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.g, 0.2F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.b, 0.3F);
    EXPECT_FLOAT_EQ(material.material.emissiveColor.r, 0.05F);
    EXPECT_FLOAT_EQ(material.material.emissiveColor.g, 0.06F);
    EXPECT_FLOAT_EQ(material.material.emissiveEnergy, 1.0F) << "no authored emission factor means 1";
    // Roughness comes from the parser's Shininess -> roughness derivation, not from a guess here.
    EXPECT_GT(material.material.roughness, 0.0F);
    EXPECT_LT(material.material.roughness, 1.0F);
    EXPECT_FLOAT_EQ(material.material.metallic, 0.0F) << "a classic material has no metalness";
    EXPECT_FALSE(material.material.isTransparent);
    // The reference is the file's own relative filename; no GUID is invented here.
    EXPECT_EQ(material.albedo.file, "textures/steel_diffuse.png");
    EXPECT_FALSE(material.albedo.HasImageUVE()) << "a file reference carries no bytes of its own";
    EXPECT_TRUE(material.normal.file.empty());
    EXPECT_TRUE(material.normal.procedural) << "the procedural texture has neither a name nor bytes";
    EXPECT_EQ(material.material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(material.material.normalTexture, kInvalidAssetGuidUVE);
    EXPECT_TRUE(IsMaterialAssetValidUVE(material.material));
}

TEST(FbxMaterialConverterUVETest, AlbedoChannelsOutsideTheValidatedRangeAreClampedAndReported) {
    const std::string text = MakeFbxUVE(R"(	Material: 3400, "Material::Overbright", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",-0.5,2.0,0.25
		}
	}
)",
                                        "");
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 1U);
    const FbxMaterialUVE& material = materials->front();
    EXPECT_TRUE(material.albedoClamped);
    EXPECT_FLOAT_EQ(material.material.albedoColor.r, 0.0F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.g, 1.0F);
    EXPECT_FLOAT_EQ(material.material.albedoColor.b, 0.25F);
    EXPECT_TRUE(IsMaterialAssetValidUVE(material.material));
}

TEST(FbxMaterialConverterUVETest, SeveralMaterialsKeepFileOrderAndIndependentValues) {
    std::string objects;
    objects += R"(	Material: 3500, "Material::Red", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1.0,0.0,0.0
		}
	}
)";
    objects += R"(	Material: 3501, "Material::Blue", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.0,0.0,1.0
			P: "TransparencyFactor", "Number", "", "A",1.0
		}
	}
)";
    const std::string text = MakeFbxUVE(objects, "");
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 2U);
    EXPECT_EQ((*materials)[0].name, "Red");
    EXPECT_EQ((*materials)[1].name, "Blue");
    EXPECT_FLOAT_EQ((*materials)[0].material.albedoColor.r, 1.0F);
    EXPECT_FLOAT_EQ((*materials)[1].material.albedoColor.b, 1.0F);
    EXPECT_FALSE((*materials)[0].material.isTransparent);
    EXPECT_TRUE((*materials)[1].material.isTransparent)
        << "TransparencyFactor 1 is fully transparent in FBX, so the material blends";
}


/// An FBX that carries its texture inside itself, the way a Mixamo "With Skin" download does: a
/// `Video` element whose `Content` is the base64 image, connected to the `Texture` the material
/// names. Nothing outside the bytes handed in is opened, so a test that sees the image come back out
/// proves the whole embedded path rather than a file lookup.
[[nodiscard]] std::string MakeEmbeddedTextureFbxUVE(const std::string& base64Image) {
    std::string objects = R"(	Material: 3100, "Material::Agent", "" {
		Version: 102
		ShadingModel: "phong"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1,1,1
		}
	}
	Video: 4000, "Video::AgentDiffuse", "Clip" {
		Type: "Clip"
		UseMipMap: 0
		Filename: "agent_diffuse.png"
		RelativeFilename: "agent_diffuse.png"
		Content: , ")";
    objects += base64Image;
    objects += R"("
	}
	Texture: 4001, "Texture::AgentDiffuse", "" {
		Type: "File"
		FileName: "agent_diffuse.png"
		RelativeFilename: "agent_diffuse.png"
	}
)";
    return MakeFbxUVE(objects, "\tC: \"OO\",4000,4001\n"
                               "\tC: \"OP\",4001,3100,\"DiffuseColor\"\n");
}

TEST(FbxMaterialConverterUVETest, AnImageEmbeddedInTheFileComesOutAsTheBytesItCarried) {
    // The bytes are a real PNG and the assertion is byte-for-byte: the importer is going to decode
    // exactly these, so anything that re-encodes, truncates or reinterprets them would publish a
    // texture that is not the one the model was made with.
    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(MakeEmbeddedTextureFbxUVE(UVE::Tests::Base64EncodeUVE(png))));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 1U);
    const FbxMaterialUVE& material = materials->front();
    ASSERT_TRUE(material.albedo.HasImageUVE()) << "the file embeds this map";
    EXPECT_EQ(material.albedo.embedded, png);
    EXPECT_EQ(material.albedo.file, "agent_diffuse.png") << "the authored name survives alongside the bytes";
    EXPECT_FALSE(material.albedo.procedural);
    // Still side-effect free: reading the image does not invent an asset identity for it.
    EXPECT_EQ(material.material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_TRUE(IsMaterialAssetValidUVE(material.material));
}

TEST(FbxMaterialConverterUVETest, AMapWithoutEmbeddedBytesAndWithoutANameIsProcedural) {
    // A file texture the FBX neither names nor embeds cannot be followed anywhere, so it must be
    // reported as procedural rather than looking like a reference that merely failed to resolve.
    const std::string text = MakeFbxUVE(R"(	Material: 3600, "Material::Nameless", "" {
		Version: 102
		ShadingModel: "phong"
	}
	Texture: 3601, "Texture::Nameless", "" {
		Type: "File"
	}
)",
                                        "\tC: \"OP\",3601,3600,\"DiffuseColor\"\n");
    const auto materials = ReadFbxMaterialsUVE(BytesUVE(text));
    ASSERT_TRUE(materials.has_value());
    ASSERT_EQ(materials->size(), 1U);
    const FbxMaterialUVE& material = materials->front();
    EXPECT_FALSE(material.albedo.HasImageUVE());
    EXPECT_TRUE(material.albedo.file.empty());
    EXPECT_TRUE(material.albedo.procedural);
}

} // namespace
} // namespace UVE::Asset::Tests
