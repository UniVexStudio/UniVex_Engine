// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
//
// FBX import publication tests (Foundation WP7). The fixtures are the same ASCII FBX 7.4 shape
// the converter tests use: one floor quad plus materials. What is under test here is the step
// after conversion - one `.uvmat` per material beside the `.uvmodel`, texture references that
// resolve ONLY to textures somebody actually imported, and GUIDs that survive a re-import.

#include "uve/asset/asset_importer_uve.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/skeleton_asset_uve.h"

namespace UVE::Asset::Tests {
namespace {

constexpr std::string_view kScratchDirectoryUVE = "uve_fbx_importer_tests";

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

constexpr std::string_view kFloorGeometryUVE = R"(	Geometry: 1000, "Geometry::Floor", "Mesh" {
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
constexpr std::string_view kFloorConnectionsUVE = "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n";

constexpr std::string_view kTwoMaterialsUVE = R"(	Material: 3500, "Material::Red Paint", "" {
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
constexpr std::string_view kTwoMaterialFacesUVE = R"(	Geometry: 1000, "Geometry::TwoFaces", "Mesh" {
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
constexpr std::string_view kTwoMaterialFacesConnectionsUVE =
    "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n\tC: \"OO\",3500,1000\n\tC: \"OO\",3501,1000\n";

constexpr std::string_view kTexturedMaterialUVE = R"(	Material: 3100, "Material::BrushedSteel", "" {
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
constexpr std::string_view kTexturedConnectionsUVE = "\tC: \"OP\",3200,3100,\"DiffuseColor\"\n";

/// A floor skinned to one bone: the shape a rigged character imports as. The bone sits two metres up
/// the file's own up axis, which is +Z here, so a skeleton read from the published asset can be told
/// apart from one guessed at - and the axis conversion is exercised rather than assumed.
constexpr std::string_view kSkinnedFloorUVE = R"(	Model: 3000, "Model::Hips", "LimbNode" {
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
constexpr std::string_view kSkinnedFloorConnectionsUVE =
    "\tC: \"OO\",1000,2000\n\tC: \"OO\",2000,0\n\tC: \"OO\",4000,1000\n\tC: \"OO\",4100,4000\n"
    "\tC: \"OO\",3000,4100\n\tC: \"OO\",3100,3000\n\tC: \"OO\",3000,0\n";

class FbxImporterUVETest : public ::testing::Test {
protected:
    void SetUp() override {
        std::filesystem::remove_all(kScratchDirectoryUVE);
        ASSERT_TRUE(std::filesystem::create_directories(kScratchDirectoryUVE));
    }
    void TearDown() override { std::filesystem::remove_all(kScratchDirectoryUVE); }

    [[nodiscard]] static std::filesystem::path ScratchUVE(const std::string_view name) {
        return std::filesystem::path(kScratchDirectoryUVE) / name;
    }

    static void WriteFixtureUVE(const std::filesystem::path& path, const std::string& text) {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        ASSERT_TRUE(output.is_open());
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        ASSERT_TRUE(output.good());
    }

    [[nodiscard]] static std::vector<std::filesystem::path> PublishedMaterialsUVE() {
        std::vector<std::filesystem::path> materials;
        for (const auto& entry : std::filesystem::directory_iterator(kScratchDirectoryUVE)) {
            if (entry.path().extension() == ".uvmat") {
                materials.push_back(entry.path());
            }
        }
        return materials;
    }

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
};

TEST_F(FbxImporterUVETest, FbxImportPublishesOneUvmatPerMaterial) {
    const std::filesystem::path source = ScratchUVE("crate.fbx");
    const std::filesystem::path destination = ScratchUVE("crate.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kTwoMaterialsUVE),
                                       kFloorConnectionsUVE));

    const AssetGuidUVE modelGuid = importer.ImportUVE(source, destination, database);
    ASSERT_NE(modelGuid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(modelGuid), destination);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U) << "one GUID per FBX material, in file order";
    EXPECT_NE(mesh.materialGuids[0], kInvalidAssetGuidUVE);
    EXPECT_NE(mesh.materialGuids[1], kInvalidAssetGuidUVE);
    EXPECT_NE(mesh.materialGuids[0], mesh.materialGuids[1]);

    const std::filesystem::path redPath = database.ResolveUVE(mesh.materialGuids[0]);
    const std::filesystem::path bluePath = database.ResolveUVE(mesh.materialGuids[1]);
    EXPECT_EQ(redPath.filename(), "crate.Red_Paint.uvmat") << "<model-stem>.<sanitised-name>.uvmat";
    EXPECT_EQ(bluePath.filename(), "crate.Blue.uvmat");
    EXPECT_EQ(PublishedMaterialsUVE().size(), 2U);

    MaterialAssetUVE red;
    ASSERT_TRUE(LoadMaterialAssetUVE(redPath, red));
    EXPECT_FLOAT_EQ(red.albedoColor.r, 1.0F);
    EXPECT_FALSE(red.isTransparent);
    EXPECT_EQ(red.albedoTexture, kInvalidAssetGuidUVE);
    MaterialAssetUVE blue;
    ASSERT_TRUE(LoadMaterialAssetUVE(bluePath, blue));
    EXPECT_FLOAT_EQ(blue.albedoColor.b, 1.0F);
    EXPECT_TRUE(blue.isTransparent);
}

TEST_F(FbxImporterUVETest, FbxFacesGroupedByMaterialPublishSlotsTheModelCanDraw) {
    // The converter reports file material positions; the importer is the only place that knows
    // which of those became a slot, because it is the one publishing them. Blue is file material 1
    // and Red is 0, so a table that skipped that remap would shade the model the wrong way round.
    const std::filesystem::path source = ScratchUVE("two_faces.fbx");
    const std::filesystem::path destination = ScratchUVE("two_faces.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kTwoMaterialFacesUVE) + std::string(kTwoMaterialsUVE),
                                       kTwoMaterialFacesConnectionsUVE));

    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[0]).filename(), "two_faces.Red_Paint.uvmat");
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[1]).filename(), "two_faces.Blue.uvmat");

    ASSERT_EQ(mesh.surfaces.size(), 2U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurfaceUVE{0U, 6U, 1U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurfaceUVE{6U, 3U, 0U}));
}

TEST_F(FbxImporterUVETest, FbxImportWithoutMaterialsPublishesOnlyTheModel) {
    const std::filesystem::path source = ScratchUVE("plain.fbx");
    const std::filesystem::path destination = ScratchUVE("plain.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE));
    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    EXPECT_TRUE(mesh.materialGuids.empty());
    EXPECT_TRUE(PublishedMaterialsUVE().empty());
}

TEST_F(FbxImporterUVETest, FbxImportResolvesSiblingTextureGuid) {
    const std::filesystem::path source = ScratchUVE("steel.fbx");
    const std::filesystem::path destination = ScratchUVE("steel.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kTexturedMaterialUVE),
                                       std::string(kFloorConnectionsUVE) + std::string(kTexturedConnectionsUVE)));
    // The texture was imported earlier (its published .uvtex sits beside the model and is
    // registered); a same-stem texture elsewhere must not be confused with it.
    const AssetGuidUVE elsewhere = database.RegisterUVE("some_other_folder/steel_diffuse.uvtex");
    const AssetGuidUVE sibling = database.RegisterUVE(ScratchUVE("steel_diffuse.uvtex"));
    ASSERT_NE(sibling, kInvalidAssetGuidUVE);
    ASSERT_NE(sibling, elsewhere);

    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAssetUVE material;
    ASSERT_TRUE(LoadMaterialAssetUVE(database.ResolveUVE(mesh.materialGuids[0]), material));
    EXPECT_EQ(material.albedoTexture, sibling) << "the authored relative filename resolves to the imported sibling";
    EXPECT_EQ(material.normalTexture, kInvalidAssetGuidUVE);
}

TEST_F(FbxImporterUVETest, FbxImportLeavesUnresolvedTextureEmptyWithDiagnostic) {
    const std::filesystem::path source = ScratchUVE("steel.fbx");
    const std::filesystem::path destination = ScratchUVE("steel.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kTexturedMaterialUVE),
                                       std::string(kFloorConnectionsUVE) + std::string(kTexturedConnectionsUVE)));
    const std::size_t registeredBefore = database.GetRegisteredAssetsUVE().size();
    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE)
        << "an unresolved texture is a diagnostic, not an import failure";
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAssetUVE material;
    ASSERT_TRUE(LoadMaterialAssetUVE(database.ResolveUVE(mesh.materialGuids[0]), material));
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuidUVE) << "never a fabricated GUID";
    EXPECT_EQ(database.GetRegisteredAssetsUVE().size(), registeredBefore + 2U)
        << "exactly the model and its material were registered - no texture record was invented";
    for (const AssetRecordUVE& record : database.GetRegisteredAssetsUVE()) {
        EXPECT_NE(record.path.extension(), ".uvtex");
    }
}

TEST_F(FbxImporterUVETest, FbxImportIsIdempotentAcrossReimport) {
    const std::filesystem::path source = ScratchUVE("crate.fbx");
    const std::filesystem::path destination = ScratchUVE("crate.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kTwoMaterialsUVE),
                                       kFloorConnectionsUVE));
    const AssetGuidUVE firstModel = importer.ImportUVE(source, destination, database);
    ASSERT_NE(firstModel, kInvalidAssetGuidUVE);
    MeshAssetUVE firstMesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, firstMesh));
    const std::size_t registeredAfterFirst = database.GetRegisteredAssetsUVE().size();

    const AssetGuidUVE secondModel = importer.ImportUVE(source, destination, database);
    EXPECT_EQ(secondModel, firstModel);
    MeshAssetUVE secondMesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, secondMesh));
    EXPECT_EQ(secondMesh.materialGuids, firstMesh.materialGuids) << "same paths, same GUIDs";
    EXPECT_EQ(database.GetRegisteredAssetsUVE().size(), registeredAfterFirst) << "no duplicate records";
    EXPECT_EQ(PublishedMaterialsUVE().size(), 2U) << "no duplicate files";
}

} // namespace
TEST_F(FbxImporterUVETest, ARiggedModelPublishesItsRigBesideIt) {
    // Unreal splits a Skeletal Mesh from its Skeleton for a reason: one rig, shared. The published
    // `.uvskel` has to name the same joints, in the same order, with the same rest pose the model
    // carries - otherwise a clip made for one and played on the other finds nothing.
    const std::filesystem::path source = ScratchUVE("hero.fbx");
    const std::filesystem::path destination = ScratchUVE("hero.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kSkinnedFloorUVE),
                                       std::string(kFloorConnectionsUVE) + std::string(kSkinnedFloorConnectionsUVE)));

    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);

    const std::filesystem::path skeletonPath = ScratchUVE("hero.uvskel");
    ASSERT_TRUE(std::filesystem::is_regular_file(skeletonPath)) << "a rigged model publishes its rig";

    SkeletonAssetUVE skeleton;
    ASSERT_TRUE(LoadSkeletonAssetUVE(skeletonPath, skeleton));
    EXPECT_EQ(skeleton.skeletonId, "hero");
    ASSERT_EQ(skeleton.joints.size(), 1U);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destination, mesh));
    ASSERT_EQ(mesh.joints.size(), skeleton.joints.size()) << "the rig and the model cannot disagree on bones";
    EXPECT_EQ(mesh.joints[0].name, skeleton.joints[0].name);
    EXPECT_EQ(skeleton.joints[0].parent, -1);
    EXPECT_NEAR(skeleton.joints[0].position.y, 2.0F, 1.0e-3F) << "the bone's own rest position, in metres";

    // Registered, so a clip or another mesh can point at it by GUID without a path.
    const AssetGuidUVE skeletonGuid = database.RegisterUVE(skeletonPath);
    ASSERT_NE(skeletonGuid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(skeletonGuid), skeletonPath);
}

TEST_F(FbxImporterUVETest, AStaticModelPublishesNoRig) {
    // Nothing to share: a static mesh has no bones, and writing an empty skeleton asset would only
    // teach the content browser to offer a rig with nothing in it.
    const std::filesystem::path source = ScratchUVE("crate.fbx");
    const std::filesystem::path destination = ScratchUVE("crate.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE));

    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    EXPECT_FALSE(std::filesystem::exists(ScratchUVE("crate.uvskel")));
}

TEST_F(FbxImporterUVETest, ARigsReimportKeepsTheSameSkeletonFile) {
    // The rig is rewritten in place, not duplicated, so its GUID survives - the property every clip
    // that points at it depends on.
    const std::filesystem::path source = ScratchUVE("hero.fbx");
    const std::filesystem::path destination = ScratchUVE("hero.uvmodel");
    WriteFixtureUVE(source, MakeFbxUVE(std::string(kFloorGeometryUVE) + std::string(kSkinnedFloorUVE),
                                       std::string(kFloorConnectionsUVE) + std::string(kSkinnedFloorConnectionsUVE)));

    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    const AssetGuidUVE first = database.RegisterUVE(ScratchUVE("hero.uvskel"));
    ASSERT_NE(importer.ImportUVE(source, destination, database), kInvalidAssetGuidUVE);
    const AssetGuidUVE second = database.RegisterUVE(ScratchUVE("hero.uvskel"));
    EXPECT_EQ(first, second);

    std::size_t skeletonCount = 0U;
    for (const auto& entry : std::filesystem::directory_iterator(kScratchDirectoryUVE)) {
        if (entry.path().extension() == ".uvskel") {
            ++skeletonCount;
        }
    }
    EXPECT_EQ(skeletonCount, 1U) << "one rig per model, however many times it is imported";
}

} // namespace UVE::Asset::Tests
