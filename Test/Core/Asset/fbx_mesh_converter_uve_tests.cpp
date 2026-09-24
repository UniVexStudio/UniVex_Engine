// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/asset/fbx_mesh_converter_uve.h"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_importer_uve.h"
#include "uve/asset/mesh_asset_uve.h"

namespace UVE::Asset::Tests {
namespace {

// An ASCII FBX 7.4 file authored the way many DCC tools write them: Z up, and in centimetres
// (UnitScaleFactor 1 means one file unit is one centimetre).
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

// A one-metre square lying on the ground (the XY plane, in a Z-up file), in centimetres.
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

[[nodiscard]] std::span<const std::byte> BytesUVE(const std::string& text) {
    return std::as_bytes(std::span<const char>(text.data(), text.size()));
}

[[nodiscard]] bool NearUVE(const float value, const float expected) {
    return std::abs(value - expected) < 1.0e-5F;
}

TEST(FbxMeshConverterUVETest, AZUpCentimetreFileArrivesInMetresStandingOnY) {
    const std::string fbx = MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(fbx), mesh));

    // One quad: two triangles over four shared corners.
    EXPECT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices.size(), 6U);
    // 100 cm is one metre, and the Z-up ground plane is the Y-up ground plane.
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.x - mesh.localBounds.min.x, 1.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.y - mesh.localBounds.min.y, 0.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.z - mesh.localBounds.min.z, 1.0F));
    for (const MeshVertexUVE& vertex : mesh.vertices) {
        // No normals in the file: generated, and facing up.
        EXPECT_TRUE(NearUVE(vertex.normal.y, 1.0F)) << vertex.normal.x << ", " << vertex.normal.y;
        EXPECT_TRUE(std::isfinite(vertex.tangent.x) && std::isfinite(vertex.tangentHandedness));
    }
}

TEST(FbxMeshConverterUVETest, EveryInstanceIsMergedWhereItsNodePlacesItAndAMirroredOneStaysFacingOut) {
    const std::string objects = std::string(kFloorGeometryUVE) + R"(	Model: 2001, "Model::Mirrored", "Mesh" {
		Version: 232
		Properties70:  {
			P: "Lcl Translation", "Lcl Translation", "", "A",300,0,0
			P: "Lcl Scaling", "Lcl Scaling", "", "A",-1,1,1
		}
	}
)";
    const std::string fbx =
        MakeFbxUVE(objects, std::string(kFloorConnectionsUVE) + "\tC: \"OO\",1000,2001\n\tC: \"OO\",2001,0\n");
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(fbx), mesh));

    EXPECT_EQ(mesh.vertices.size(), 8U);
    EXPECT_EQ(mesh.indices.size(), 12U);
    // The second copy sits 3 m away, flipped back over it: 0..1 m and 2..3 m along X.
    EXPECT_TRUE(NearUVE(mesh.localBounds.min.x, 0.0F));
    EXPECT_TRUE(NearUVE(mesh.localBounds.max.x, 3.0F));
    // Each triangle's winding agrees with its normals, the mirrored copy's included.
    for (std::size_t first = 0U; first < mesh.indices.size(); first += 3U) {
        const Math::Vector3UVE& a = mesh.vertices[mesh.indices[first]].position;
        const Math::Vector3UVE& b = mesh.vertices[mesh.indices[first + 1U]].position;
        const Math::Vector3UVE& c = mesh.vertices[mesh.indices[first + 2U]].position;
        const float faceY = ((b.z - a.z) * (c.x - a.x)) - ((b.x - a.x) * (c.z - a.z));
        const float normalY = mesh.vertices[mesh.indices[first]].normal.y;
        EXPECT_GT(faceY * normalY, 0.0F) << "triangle " << first / 3U << " is inside out";
    }
}

TEST(FbxMeshConverterUVETest, ASkinIsWhatMakesItRigged) {
    const std::string objects = std::string(kFloorGeometryUVE) + R"(	Model: 3000, "Model::Bone", "LimbNode" {
		Version: 232
	}
	NodeAttribute: 3100, "NodeAttribute::Bone", "LimbNode" {
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
    const std::string rigged = MakeFbxUVE(
        objects, std::string(kFloorConnectionsUVE) +
                     "\tC: \"OO\",4000,1000\n\tC: \"OO\",4100,4000\n\tC: \"OO\",3000,4100\n\tC: \"OO\",3100,3000\n"
                     "\tC: \"OO\",3000,0\n");
    EXPECT_TRUE(FbxSourceHasSkinUVE(BytesUVE(rigged)));
    EXPECT_FALSE(FbxSourceHasSkinUVE(BytesUVE(MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE))));

    // The mesh itself still imports, in its bind pose.
    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertFbxMeshUVE(BytesUVE(rigged), mesh));
    EXPECT_EQ(mesh.vertices.size(), 4U);
    EXPECT_FALSE(mesh.IsSkinnedUVE());
}

TEST(FbxMeshConverterUVETest, WhatIsNotAnFbxWithTrianglesIsRefusedAndLeavesTheMeshAlone) {
    MeshAssetUVE mesh;
    mesh.indices = {7U};
    EXPECT_FALSE(ConvertFbxMeshUVE({}, mesh));
    const std::string garbage = "definitely not an fbx file";
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(garbage), mesh));
    // OBJ has its own importer; the FBX path does not quietly take it.
    const std::string obj = "v 0 0 0\nv 1 0 0\nv 0 1 0\nf 1 2 3\n";
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(obj), mesh));
    // A valid FBX with nothing to draw.
    const std::string empty = MakeFbxUVE("", "");
    EXPECT_FALSE(ConvertFbxMeshUVE(BytesUVE(empty), mesh));
    EXPECT_FALSE(FbxSourceHasSkinUVE(BytesUVE(garbage)));
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{7U}));
}

TEST(FbxMeshConverterUVETest, TheImporterPublishesAUveModelAndRegistersIt) {
    const std::filesystem::path sourcePath = "uve_fbx_importer_tests_floor.fbx";
    const std::filesystem::path destinationPath = "uve_fbx_importer_tests_floor.uvemodel";
    std::filesystem::remove(destinationPath);
    {
        const std::string fbx = MakeFbxUVE(kFloorGeometryUVE, kFloorConnectionsUVE);
        std::ofstream output(sourcePath, std::ios::binary | std::ios::trunc);
        output.write(fbx.data(), static_cast<std::streamsize>(fbx.size()));
        ASSERT_TRUE(output.good());
    }

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destinationPath);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    EXPECT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices.size(), 6U);

    // A destination that is not a .uvemodel is refused before anything is written.
    EXPECT_EQ(importer.ImportUVE(sourcePath, "uve_fbx_importer_tests_floor.uvetex", database), kInvalidAssetGuidUVE);

    std::filesystem::remove(sourcePath);
    std::filesystem::remove(destinationPath);
}

} // namespace
} // namespace UVE::Asset::Tests
