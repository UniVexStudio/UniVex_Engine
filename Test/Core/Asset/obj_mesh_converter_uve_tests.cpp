// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/obj_mesh_converter_uve.h"

#include <string_view>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_ExplicitAttributesPublishesTriangleMesh) {
    constexpr std::string_view source = R"OBJ(
# one textured triangle
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 0
vt 1 0
vt 0 1
vn 0 0 1
f 1/1/1 2/2/1 3/3/1
)OBJ";

    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.vertices[0].position, (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(mesh.vertices[1].position, (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(mesh.vertices[2].position, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
    EXPECT_FLOAT_EQ(mesh.vertices[1].u, 1.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[2].v, 1.0F);
    EXPECT_EQ(mesh.vertices[0].normal, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(mesh.localBounds,
              (Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}}));
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_AcceptsFiniteHomogeneousPositionWeight) {
    constexpr std::string_view source = R"OBJ(
v 0 0 0 1
v 2 0 0 2
v 0 2 0 2
f 1 2 3
)OBJ";

    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.vertices[1].position, (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(mesh.vertices[2].position, (Math::Vector3UVE{0.0F, 1.0F, 0.0F}));
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_RejectsTrailingTexcoordCoordinateAtomically) {
    MeshAssetUVE original;
    original.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};
    MeshAssetUVE output = original;

    constexpr std::string_view invalidSource = R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
vt 0 1 0.5
f 1/1 2/1 3/1
)OBJ";
    EXPECT_FALSE(ConvertObjMeshUVE(invalidSource, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
    EXPECT_EQ(output.localBounds, original.localBounds);
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_RejectsTrailingNormalCoordinateAtomically) {
    MeshAssetUVE original;
    original.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};
    MeshAssetUVE output = original;

    constexpr std::string_view invalidSource = R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
vn 0 0 1 0
f 1//1 2//1 3//1
)OBJ";
    EXPECT_FALSE(ConvertObjMeshUVE(invalidSource, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
    EXPECT_EQ(output.localBounds, original.localBounds);
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_RejectsTrailingPositionCoordinateAtomically) {
    MeshAssetUVE original;
    original.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};
    MeshAssetUVE output = original;

    constexpr std::string_view invalidSource = R"OBJ(
v 0 0 0 1 99
v 1 0 0
v 0 1 0
f 1 2 3
)OBJ";
    EXPECT_FALSE(ConvertObjMeshUVE(invalidSource, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
    EXPECT_EQ(output.localBounds, original.localBounds);
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_NegativeIndexQuadFanComputesFaceNormals) {
    constexpr std::string_view source = R"OBJ(
v 0 0 0
v 1 0 0
v 1 1 0
v 0 1 0
f -4 -3 -2 -1
)OBJ";

    MeshAssetUVE mesh;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh));
    ASSERT_EQ(mesh.vertices.size(), 6U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U, 3U, 4U, 5U}));
    for (const MeshVertexUVE& vertex : mesh.vertices) {
        EXPECT_EQ(vertex.normal, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
        EXPECT_NEAR(Math::LengthUVE(vertex.tangent), 1.0F, 0.0001F);
    }
    EXPECT_EQ(mesh.localBounds,
              (Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}}));
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_InvalidInputPreservesExistingOutput) {
    MeshAssetUVE original;
    original.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};
    MeshAssetUVE output = original;

    constexpr std::string_view invalidSource = "v 0 0 0\nf 1 2 3\n";
    EXPECT_FALSE(ConvertObjMeshUVE(invalidSource, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
    EXPECT_EQ(output.localBounds, original.localBounds);
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_DegenerateOrNonFiniteInputFails) {
    MeshAssetUVE mesh;
    EXPECT_FALSE(ConvertObjMeshUVE("v nan 0 0\nf 1 1 1\n", mesh));
    EXPECT_FALSE(ConvertObjMeshUVE("v 0 0 0\nv 1 0 0\nv 2 0 0\nf 1 2 3\n", mesh));
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_RejectsFiniteNormalAndEdgeOverflowWithoutPublishing) {
    MeshAssetUVE original;
    original.vertices = {
        MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F, 0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};

    MeshAssetUVE normalOverflow = original;
    EXPECT_FALSE(ConvertObjMeshUVE(
        "v 0 0 0\nv 1 0 0\nv 0 1 0\nvn 3.402823466e38 3.402823466e38 0\nf 1/1 2/1 3/1\n",
        normalOverflow));
    EXPECT_EQ(normalOverflow.vertices.size(), original.vertices.size());
    EXPECT_EQ(normalOverflow.vertices[0].position, original.vertices[0].position);

    MeshAssetUVE edgeOverflow = original;
    EXPECT_FALSE(ConvertObjMeshUVE(
        "v -3.402823466e38 0 0\nv 3.402823466e38 0 0\nv 0 1 0\nf 1 2 3\n",
        edgeOverflow));
    EXPECT_EQ(edgeOverflow.vertices.size(), original.vertices.size());
    EXPECT_EQ(edgeOverflow.vertices[0].position, original.vertices[0].position);
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_MaterialRunsTileTheIndexBufferInFileOrder) {
    // The look of an OBJ is a sequence of usemtl runs, not a set of names: a face painted green,
    // then red, then green again draws in that order, so the table keeps three ranges and the
    // middle name repeating is not merged away.
    constexpr std::string_view source = R"OBJ(
mtllib looks.mtl
v 0 0 0
v 1 0 0
v 0 1 0
v 2 0 0
v 3 0 0
v 2 1 0
v 4 0 0
v 5 0 0
v 4 1 0
usemtl Green Paint
f 1 2 3
usemtl Red
f 4 5 6
usemtl Green Paint
f 7 8 9
)OBJ";

    MeshAssetUVE mesh;
    std::vector<ObjSurfaceRangeUVE> surfaces;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh, &surfaces));
    ASSERT_EQ(mesh.indices.size(), 9U);
    ASSERT_EQ(surfaces.size(), 3U);
    EXPECT_EQ(surfaces[0].firstIndex, 0U);
    EXPECT_EQ(surfaces[0].indexCount, 3U);
    EXPECT_EQ(surfaces[0].materialName, "Green Paint");
    EXPECT_EQ(surfaces[1].firstIndex, 3U);
    EXPECT_EQ(surfaces[1].indexCount, 3U);
    EXPECT_EQ(surfaces[1].materialName, "Red");
    EXPECT_EQ(surfaces[2].firstIndex, 6U);
    EXPECT_EQ(surfaces[2].indexCount, 3U);
    EXPECT_EQ(surfaces[2].materialName, "Green Paint");
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_ASingleMaterialIsTheWholeBufferAndNotARange) {
    // One run covers everything, which is exactly what an empty surface table already means: a
    // table with one entry would make every single-material model take the per-surface path.
    constexpr std::string_view source = R"OBJ(
v 0 0 0
v 1 0 0
v 0 1 0
v 1 1 0
usemtl OnlyOne
f 1 2 3
f 2 4 3
)OBJ";

    MeshAssetUVE mesh;
    std::vector<ObjSurfaceRangeUVE> surfaces;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh, &surfaces));
    EXPECT_TRUE(surfaces.empty());
    EXPECT_TRUE(mesh.surfaces.empty());
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_NamesASwitchNoFaceUsesAreNotSurfaces) {
    // usemtl lines before the first face are how OBJ files declare what comes next; a name that
    // never paints a triangle is not a surface, and cannot become a zero-length range.
    constexpr std::string_view source = R"OBJ(
usemtl Unused
v 0 0 0
v 1 0 0
v 0 1 0
usemtl Used
f 1 2 3
)OBJ";

    MeshAssetUVE mesh;
    std::vector<ObjSurfaceRangeUVE> surfaces;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh, &surfaces));
    EXPECT_TRUE(surfaces.empty());
}

TEST(ObjMeshConverterUVETest, ConvertObjMeshUVE_TooManyRunsDropTheTableRatherThanHalfOfIt) {
    // Past the format's surface cap there is no table that could be saved with the mesh, so the
    // conversion keeps the whole-buffer draw instead of publishing a truncated split - every
    // triangle still draws, with the object's material.
    std::string source;
    for (std::size_t triangle = 0U; triangle <= kMaximumMeshSurfacesUVE; ++triangle) {
        const float offset = static_cast<float>(triangle) * 4.0F;
        source += "v " + std::to_string(offset) + " 0 0\n";
        source += "v " + std::to_string(offset + 1.0F) + " 0 0\n";
        source += "v " + std::to_string(offset) + " 1 0\n";
        source += "usemtl m" + std::to_string(triangle) + "\n";
        const std::size_t first = triangle * 3U;
        source += "f " + std::to_string(first + 1U) + " " + std::to_string(first + 2U) + " " +
                  std::to_string(first + 3U) + "\n";
    }

    MeshAssetUVE mesh;
    std::vector<ObjSurfaceRangeUVE> surfaces;
    ASSERT_TRUE(ConvertObjMeshUVE(source, mesh, &surfaces));
    EXPECT_EQ(mesh.indices.size(), (kMaximumMeshSurfacesUVE + 1U) * 3U);
    EXPECT_TRUE(surfaces.empty());
}

} // namespace
} // namespace UVE::Asset::Tests
