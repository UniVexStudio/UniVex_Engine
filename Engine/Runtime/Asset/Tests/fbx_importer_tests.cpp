// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// FBX import publication tests (Foundation WP7). The fixtures are the same ASCII FBX 7.4 shape
// the converter tests use: one floor quad plus materials. What is under test here is the step
// after conversion - one `.uvmat` per material beside the `.uvmodel`, texture references that
// resolve ONLY to textures somebody actually imported, and GUIDs that survive a re-import.

#include "asset_importer.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "skeleton_asset.h"

namespace UVE::Asset::Tests {
namespace {

constexpr std::string_view kScratchDirectory = "uve_fbx_importer_tests";

[[nodiscard]] std::string MakeFbx(const std::string_view objects, const std::string_view connections) {
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

constexpr std::string_view kFloorGeometry = R"(	Geometry: 1000, "Geometry::Floor", "Mesh" {
		Vertices: *12 {
			a: 0,0,0,100,0,0,100,100,0,0,100,0
		}
		PolygonVertexIndex: *4 {
			a: 0,1,2,-4
		}
		GeometryVersion: 124
	}
	Model: 2000, "Model::Floor", "Mesh" {
		Version: 232
	}
)";
constexpr std::string_view kFloorConnections = "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n";

constexpr std::string_view kTwoMaterials = R"(	Material: 3500, "Material::Red Paint", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",1.0,0.0,0.0
		}
	}
	Material: 3501, "Material::Blue", "" {
		Version: 102
		ShadingModel: "lambert"
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.0,0.0,1.0
			P: "TransparencyFactor", "Number", "", "A",1.0
		}
	}
)";

// Two triangles that do not share a corner, with one material each - and the polygons written in
// the order Blue, Red, Blue, so the published table's order comes from first use rather than from
// the file's material list.
constexpr std::string_view kTwoMaterialFaces = R"(	Geometry: 1000, "Geometry::TwoFaces", "Mesh" {
		Vertices: *27 {
			a: 0,0,0,100,0,0,100,100,0,200,0,0,300,0,0,300,100,0,400,0,0,500,0,0,500,100,0
		}
		PolygonVertexIndex: *9 {
			a: 0,1,-3,3,4,-6,6,7,-9
		}
		LayerElementMaterial: 0 {
			Version: 101
			Name: ""
			MappingInformationType: "ByPolygon"
			ReferenceInformationType: "IndexToDirect"
			Materials: *3 {
				a: 1,0,1
			}
		}
		GeometryVersion: 124
	}
	Model: 2000, "Model::TwoFaces", "Mesh" {
		Version: 232
	}
)";
constexpr std::string_view kTwoMaterialFacesConnections =
    "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n\tC: \"OO\",3500,1000\n\tC: \"OO\",3501,1000\n";

constexpr std::string_view kTexturedMaterial = R"(	Material: 3100, "Material::BrushedSteel", "" {
		Version: 102
		ShadingModel: "phong"
		MultiLayer: 0
		Properties70:  {
			P: "DiffuseColor", "Color", "", "A",0.1,0.2,0.3
		}
	}
	Texture: 3200, "Texture::SteelDiffuse", "" {
		Type: "File"
		FileName: "C:/assets/textures/steel_diffuse.png"
		RelativeFilename: "textures/steel_diffuse.png"
	}
)";
constexpr std::string_view kTexturedConnections = "\tC: \"OP\",3200,3100,\"DiffuseColor\"\n";

/// A floor skinned to one bone: the shape a rigged character imports as. The bone sits two metres up
/// the file's own up axis, which is +Z here, so a skeleton read from the published asset can be told
/// apart from one guessed at - and the axis conversion is exercised rather than assumed.
constexpr std::string_view kSkinnedFloor = R"(	Model: 3000, "Model::Hips", "LimbNode" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",0,0,200
		}
	}
	NodeAttribute: 3100, "NodeAttribute::Hips", "LimbNode" {
		TypeFlags: "Skeleton"
	}
	Deformer: 4000, "Deformer::Skin", "Skin" {
		Version: 101
	}
	Deformer: 4100, "SubDeformer::Cluster", "Cluster" {
		Version: 100
		Indexes: *4 {
			a: 0,1,2,3
		}
		Weights: *4 {
			a: 1,1,1,1
		}
		Transform: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
		TransformLink: *16 {
			a: 1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1
		}
	}
)";
constexpr std::string_view kSkinnedFloorConnections =
    "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n\tC: \"OO\",4000,1000\n\tC: \"OO\",4100,4000\n"
    "\tC: \"OO\",3000,4100\n\tC: \"OO\",3100,3000\n\tC: \"OO\",3000,0\n";

class FbxImporterTest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kScratchDirectory);
        ASSERT_TRUE(std::filesystem::create_directories(kScratchDirectory));
    }
    void TearDown() override { std::filesystem::remove_all(kScratchDirectory); }

    [[nodiscard]] static std::filesystem::path Scratch(const std::string_view name) {
        return std::filesystem::path(kScratchDirectory) / name;
    }

    static void WriteFixture(const std::filesystem::path& path, const std::string& text) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(output.good());
    }

    [[nodiscard]] static std::vector<std::filesystem::path> PublishedMaterials() {
        std::vector<std::filesystem::path> materials;
        for (const auto& entry : std::filesystem::directory_iterator(kScratchDirectory)) {
            if (entry.path().extension() == ".uvmat") {
                materials.push_back(entry.path());
            }
        }
        return materials;
    }

    AssetImporter importer;
    AssetDatabase database;
};

TEST_F(FbxImporterTest, FbxImportPublishesOneUvmatPerMaterial) {
    const std::filesystem::path source = Scratch("crate.fbx");
    const std::filesystem::path destination = Scratch("crate.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kTwoMaterials),
                                       kFloorConnections));

    const AssetGuid modelGuid = importer.Import(source, destination, database);
    ASSERT_NE(modelGuid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(modelGuid), destination);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U) << "one GUID per FBX material, in file order";
    EXPECT_NE(mesh.materialGuids[0], kInvalidAssetGuid);
    EXPECT_NE(mesh.materialGuids[1], kInvalidAssetGuid);
    EXPECT_NE(mesh.materialGuids[0], mesh.materialGuids[1]);

    const std::filesystem::path redPath = database.Resolve(mesh.materialGuids[0]);
    const std::filesystem::path bluePath = database.Resolve(mesh.materialGuids[1]);
    EXPECT_EQ(redPath.filename(), "crate.Red_Paint.uvmat") << "<model-stem>.<sanitised-name>.uvmat";
    EXPECT_EQ(bluePath.filename(), "crate.Blue.uvmat");
    EXPECT_EQ(PublishedMaterials().size(), 2U);

    MaterialAsset red;
    ASSERT_TRUE(LoadMaterialAsset(redPath, red));
    EXPECT_FLOAT_EQ(red.albedoColor.r, 1.0F);
    EXPECT_FALSE(red.isTransparent);
    EXPECT_EQ(red.albedoTexture, kInvalidAssetGuid);
    MaterialAsset blue;
    ASSERT_TRUE(LoadMaterialAsset(bluePath, blue));
    EXPECT_FLOAT_EQ(blue.albedoColor.b, 1.0F);
    EXPECT_TRUE(blue.isTransparent);
}

TEST_F(FbxImporterTest, FbxFacesGroupedByMaterialPublishSlotsTheModelCanDraw) {
    // The converter reports file material positions; the importer is the only place that knows
    // which of those became a slot, because it is the one publishing them. Blue is file material 1
    // and Red is 0, so a table that skipped that remap would shade the model the wrong way round.
    const std::filesystem::path source = Scratch("two_faces.fbx");
    const std::filesystem::path destination = Scratch("two_faces.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kTwoMaterialFaces) + std::string(kTwoMaterials),
                                       kTwoMaterialFacesConnections));

    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_EQ(database.Resolve(mesh.materialGuids[0]).filename(), "two_faces.Red_Paint.uvmat");
    EXPECT_EQ(database.Resolve(mesh.materialGuids[1]).filename(), "two_faces.Blue.uvmat");

    ASSERT_EQ(mesh.surfaces.size(), 2U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurface{0U, 6U, 1U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurface{6U, 3U, 0U}));
}

TEST_F(FbxImporterTest, FbxImportWithoutMaterialsPublishesOnlyTheModel) {
    const std::filesystem::path source = Scratch("plain.fbx");
    const std::filesystem::path destination = Scratch("plain.uvmodel");
    WriteFixture(source, MakeFbx(kFloorGeometry, kFloorConnections));
    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    EXPECT_TRUE(mesh.materialGuids.empty());
    EXPECT_TRUE(PublishedMaterials().empty());
}

TEST_F(FbxImporterTest, FbxImportResolvesSiblingTextureGuid) {
    const std::filesystem::path source = Scratch("steel.fbx");
    const std::filesystem::path destination = Scratch("steel.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kTexturedMaterial),
                                       std::string(kFloorConnections) + std::string(kTexturedConnections)));
    // The texture was imported earlier (its published .uvtex sits beside the model and is
    // registered); a same-stem texture elsewhere must not be confused with it.
    const AssetGuid elsewhere = database.Register("some_other_folder/steel_diffuse.uvtex");
    const AssetGuid sibling = database.Register(Scratch("steel_diffuse.uvtex"));
    ASSERT_NE(sibling, kInvalidAssetGuid);
    ASSERT_NE(sibling, elsewhere);

    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAsset material;
    ASSERT_TRUE(LoadMaterialAsset(database.Resolve(mesh.materialGuids[0]), material));
    EXPECT_EQ(material.albedoTexture, sibling) << "the authored relative filename resolves to the imported sibling";
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuid);
}

TEST_F(FbxImporterTest, FbxImportLeavesUnresolvedTextureEmptyWithDiagnostic) {
    const std::filesystem::path source = Scratch("steel.fbx");
    const std::filesystem::path destination = Scratch("steel.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kTexturedMaterial),
                                       std::string(kFloorConnections) + std::string(kTexturedConnections)));
    const std::size_t registeredBefore = database.GetRegisteredAssets().size();
    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid)
        << "an unresolved texture is a diagnostic, not an import failure";
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAsset material;
    ASSERT_TRUE(LoadMaterialAsset(database.Resolve(mesh.materialGuids[0]), material));
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuid) << "never a fabricated GUID";
    EXPECT_EQ(database.GetRegisteredAssets().size(), registeredBefore + 2U)
        << "exactly the model and its material were registered - no texture record was invented";
    for (const AssetRecord& record : database.GetRegisteredAssets()) {
        EXPECT_NE(record.path.extension(), ".uvtex");
    }
}

TEST_F(FbxImporterTest, FbxImportIsIdempotentAcrossReimport) {
    const std::filesystem::path source = Scratch("crate.fbx");
    const std::filesystem::path destination = Scratch("crate.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kTwoMaterials),
                                       kFloorConnections));
    const AssetGuid firstModel = importer.Import(source, destination, database);
    ASSERT_NE(firstModel, kInvalidAssetGuid);
    MeshAsset firstMesh;
    ASSERT_TRUE(LoadMeshAsset(destination, firstMesh));
    const std::size_t registeredAfterFirst = database.GetRegisteredAssets().size();

    const AssetGuid secondModel = importer.Import(source, destination, database);
    EXPECT_EQ(secondModel, firstModel);
    MeshAsset secondMesh;
    ASSERT_TRUE(LoadMeshAsset(destination, secondMesh));
    EXPECT_EQ(secondMesh.materialGuids, firstMesh.materialGuids) << "same paths, same GUIDs";
    EXPECT_EQ(database.GetRegisteredAssets().size(), registeredAfterFirst) << "no duplicate records";
    EXPECT_EQ(PublishedMaterials().size(), 2U) << "no duplicate files";
}

} // namespace
TEST_F(FbxImporterTest, ARiggedModelPublishesItsRigBesideIt) {
    // Engines split a Skeletal Mesh from its Skeleton for a reason: one rig, shared. The published
    // `.uvskel` has to name the same joints, in the same order, with the same rest pose the model
    // carries - otherwise a clip made for one and played on the other finds nothing.
    const std::filesystem::path source = Scratch("hero.fbx");
    const std::filesystem::path destination = Scratch("hero.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kSkinnedFloor),
                                       std::string(kFloorConnections) + std::string(kSkinnedFloorConnections)));

    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);

    const std::filesystem::path skeletonPath = Scratch("hero.uvskel");
    ASSERT_TRUE(std::filesystem::is_regular_file(skeletonPath)) << "a rigged model publishes its rig";

    SkeletonAsset skeleton;
    ASSERT_TRUE(LoadSkeletonAsset(skeletonPath, skeleton));
    EXPECT_EQ(skeleton.skeletonId, "hero");
    ASSERT_EQ(skeleton.joints.size(), 1U);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destination, mesh));
    ASSERT_EQ(mesh.joints.size(), skeleton.joints.size()) << "the rig and the model cannot disagree on bones";
    EXPECT_EQ(mesh.joints[0].name, skeleton.joints[0].name);
    EXPECT_EQ(skeleton.joints[0].parent, -1);
    EXPECT_NEAR(skeleton.joints[0].position.y, 2.0F, 1.0e-3F) << "the bone's own rest position, in metres";

    // Registered, so a clip or another mesh can point at it by GUID without a path.
    const AssetGuid skeletonGuid = database.Register(skeletonPath);
    ASSERT_NE(skeletonGuid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(skeletonGuid), skeletonPath);
}

TEST_F(FbxImporterTest, AStaticModelPublishesNoRig) {
    // Nothing to share: a static mesh has no bones, and writing an empty skeleton asset would only
    // teach the asset table to offer a rig with nothing in it.
    const std::filesystem::path source = Scratch("crate.fbx");
    const std::filesystem::path destination = Scratch("crate.uvmodel");
    WriteFixture(source, MakeFbx(kFloorGeometry, kFloorConnections));

    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(Scratch("crate.uvskel")));
}

TEST_F(FbxImporterTest, ARigsReimportKeepsTheSameSkeletonFile) {
    // The rig is rewritten in place, not duplicated, so its GUID survives - the property every clip
    // that points at it depends on.
    const std::filesystem::path source = Scratch("hero.fbx");
    const std::filesystem::path destination = Scratch("hero.uvmodel");
    WriteFixture(source, MakeFbx(std::string(kFloorGeometry) + std::string(kSkinnedFloor),
                                       std::string(kFloorConnections) + std::string(kSkinnedFloorConnections)));

    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    const AssetGuid first = database.Register(Scratch("hero.uvskel"));
    ASSERT_NE(importer.Import(source, destination, database), kInvalidAssetGuid);
    const AssetGuid second = database.Register(Scratch("hero.uvskel"));
    EXPECT_EQ(first, second);

    std::size_t skeletonCount = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(kScratchDirectory)) {
        if (entry.path().extension() == ".uvskel") {
            ++skeletonCount;
        }
    }
    EXPECT_EQ(skeletonCount, 1U) << "one rig per model, however many times it is imported";
}

} // namespace UVE::Asset::Tests
