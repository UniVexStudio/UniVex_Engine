// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "gltf_mesh_converter.h"

#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Asset::Tests {
namespace {

void AppendU16LittleEndian(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendFloatLittleEndian(std::vector<std::byte>& bytes, const float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((bits >> shift) & 0xFFU)});
    }
}

void AppendVector3(std::vector<std::byte>& bytes, const float x, const float y, const float z) {
    AppendFloatLittleEndian(bytes, x);
    AppendFloatLittleEndian(bytes, y);
    AppendFloatLittleEndian(bytes, z);
}

GltfAccessorView MakeAccessor(const std::vector<std::byte>& bytes, const std::uint64_t count,
                                    const GltfComponentType componentType) {
    return GltfAccessorView{std::span<const std::byte>{bytes.data(), bytes.size()}, 0U, count, 0U, componentType};
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_ExplicitAttributesAndU16Indices) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        AppendVector3(bytes, 2.0F, 2.0F, 2.0F);
        return bytes;
    }();
    const std::vector<std::byte> normals = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
        return bytes;
    }();
    const std::vector<std::byte> texcoords = [&] {
        std::vector<std::byte> bytes;
        AppendFloatLittleEndian(bytes, 0.0F); AppendFloatLittleEndian(bytes, 0.0F);
        AppendFloatLittleEndian(bytes, 1.0F); AppendFloatLittleEndian(bytes, 0.0F);
        AppendFloatLittleEndian(bytes, 0.0F); AppendFloatLittleEndian(bytes, 1.0F);
        AppendFloatLittleEndian(bytes, 2.0F); AppendFloatLittleEndian(bytes, 2.0F);
        return bytes;
    }();
    const std::vector<std::byte> indices = [&] {
        std::vector<std::byte> bytes;
        AppendU16LittleEndian(bytes, 0U); AppendU16LittleEndian(bytes, 1U); AppendU16LittleEndian(bytes, 2U);
        return bytes;
    }();

    const GltfPrimitiveSource source{
        MakeAccessor(positions, 4U, GltfComponentType::Float),
        MakeAccessor(normals, 4U, GltfComponentType::Float),
        MakeAccessor(texcoords, 4U, GltfComponentType::Float),
        MakeAccessor(indices, 3U, GltfComponentType::UnsignedShort),
        4U,
    };
    MeshAsset mesh;
    ASSERT_TRUE(ConvertGltfPrimitive(source, mesh));
    ASSERT_EQ(mesh.vertices.size(), 4U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.vertices[1].position, (Math::Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(mesh.vertices[0].normal, (Math::Vector3{0.0F, 0.0F, 1.0F}));
    EXPECT_FLOAT_EQ(mesh.vertices[1].u, 1.0F);
    EXPECT_FLOAT_EQ(mesh.vertices[2].v, 1.0F);
    EXPECT_EQ(mesh.localBounds,
              (Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{2.0F, 2.0F, 2.0F}}));
    EXPECT_NEAR(Math::Length(mesh.vertices[0].tangent), 1.0F, 0.0001F);
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_MissingNormalsAccumulatesFaceNormal) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    MeshAsset mesh;
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    ASSERT_TRUE(ConvertGltfPrimitive(source, mesh));
    for (const MeshVertex& vertex : mesh.vertices) {
        EXPECT_EQ(vertex.normal, (Math::Vector3{0.0F, 0.0F, 1.0F}));
    }
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_MissingIndicesGeneratesSequentialIndices) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    MeshAsset mesh;
    ASSERT_TRUE(ConvertGltfPrimitive(source, mesh));
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_NonTrianglesModeRejects) {
    const std::vector<std::byte> positions(36U);
    MeshAsset mesh;
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 5U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, mesh));
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_NonFinitePositionPreservesOutput) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, std::numeric_limits<float>::quiet_NaN(), 1.0F, 0.0F);
        return bytes;
    }();
    MeshAsset output;
    output.vertices = {MeshVertex{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F,
                                     0.0F}};
    output.indices = {0U};
    output.localBounds = Math::Aabb{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{4.0F, 5.0F, 6.0F}};
    const MeshAsset original = output;
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
    EXPECT_EQ(output.localBounds, original.localBounds);
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_OverflowedFiniteNormalPreservesOutput) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> normals = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, std::numeric_limits<float>::max(), 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    MeshAsset output;
    output.vertices = {MeshVertex{Math::Vector3{7.0F, 8.0F, 9.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F,
                                     0.0F}};
    output.indices = {0U};
    const MeshAsset original = output;
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float),
                                        MakeAccessor(normals, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, 4U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_OverflowedGeneratedNormalAccumulationPreservesOutput) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0e19F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 3.0e19F, 0.0F);
        AppendVector3(bytes, 1.0e19F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 3.0e19F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> indices = [&] {
        std::vector<std::byte> bytes;
        for (const std::uint16_t index : {std::uint16_t{0U}, std::uint16_t{1U}, std::uint16_t{2U},
                                           std::uint16_t{0U}, std::uint16_t{3U}, std::uint16_t{4U}}) {
            AppendU16LittleEndian(bytes, index);
        }
        return bytes;
    }();
    MeshAsset output;
    output.vertices = {MeshVertex{Math::Vector3{7.0F, 8.0F, 9.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F,
                                     0.0F}};
    output.indices = {0U};
    const MeshAsset original = output;
    const GltfPrimitiveSource source{MakeAccessor(positions, 5U, GltfComponentType::Float), std::nullopt,
                                        MakeAccessor(indices, 6U, GltfComponentType::UnsignedShort), std::nullopt,
                                        4U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_OutOfBoundsIndexPreservesOutput) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> indices = [&] {
        std::vector<std::byte> bytes;
        AppendU16LittleEndian(bytes, 0U); AppendU16LittleEndian(bytes, 1U); AppendU16LittleEndian(bytes, 3U);
        return bytes;
    }();
    MeshAsset output;
    output.vertices = {MeshVertex{Math::Vector3{7.0F, 8.0F, 9.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F,
                                     0.0F}};
    output.indices = {0U};
    const MeshAsset original = output;
    const GltfPrimitiveSource source{
        MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt, std::nullopt,
        MakeAccessor(indices, 3U, GltfComponentType::UnsignedShort), 4U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, output));
    EXPECT_EQ(output.vertices.size(), original.vertices.size());
    EXPECT_EQ(output.vertices[0].position, original.vertices[0].position);
    EXPECT_EQ(output.indices, original.indices);
}

TEST(GltfPrimitiveConverterTest, ConvertGltfPrimitive_WrongPositionComponentTypeRejects) {
    const std::vector<std::byte> positions(12U);
    MeshAsset mesh;
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::UnsignedShort),
                                        std::nullopt, std::nullopt, std::nullopt, 4U};
    EXPECT_FALSE(ConvertGltfPrimitive(source, mesh));
}

} // namespace
/// A base triangle plus two targets: "jawOpen" moves only vertex 2 (and bends its normal), and
/// "empty" is the degenerate case an exporter still emits - a target whose displacements are all
/// exactly zero, which must cost nothing after import.
TEST(GltfMorphTargetConverterTest, ConvertGltfPrimitive_BuildsSparseTargetsAndKeepsNames) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> indices = [&] {
        std::vector<std::byte> bytes;
        AppendU16LittleEndian(bytes, 0U);
        AppendU16LittleEndian(bytes, 1U);
        AppendU16LittleEndian(bytes, 2U);
        return bytes;
    }();
    const std::vector<std::byte> jawPositions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, -0.5F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> jawNormals = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, -1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> zeroPositions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        return bytes;
    }();

    const GltfPrimitiveSource source{
        MakeAccessor(positions, 3U, GltfComponentType::Float),
        std::nullopt,
        std::nullopt,
        MakeAccessor(indices, 3U, GltfComponentType::UnsignedShort),
        4U,
    };
    GltfMorphTargetSet morphSet;
    morphSet.targets.push_back(GltfMorphTargetSource{
        MakeAccessor(jawPositions, 3U, GltfComponentType::Float),
        MakeAccessor(jawNormals, 3U, GltfComponentType::Float), std::nullopt});
    morphSet.targets.push_back(GltfMorphTargetSource{
        MakeAccessor(zeroPositions, 3U, GltfComponentType::Float), std::nullopt, std::nullopt});
    morphSet.names = {"jawOpen", "unused"};
    morphSet.defaultWeights = {0.25F, 0.0F};

    MeshAsset mesh;
    ASSERT_TRUE(ConvertGltfPrimitive(source, morphSet, mesh));
    ASSERT_EQ(mesh.morphTargets.size(), 2U);
    EXPECT_EQ(mesh.morphTargets[0].name, "jawOpen");
    // Only the one moved vertex is stored: the two all-zero rows are dropped, which is what makes
    // a sparse shape cost a fraction of a dense one.
    ASSERT_EQ(mesh.morphTargets[0].deltas.size(), 1U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].vertexIndex, 2U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].positionDelta, (Math::Vector3{0.0F, -0.5F, 0.0F}));
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].normalDelta, (Math::Vector3{0.0F, -1.0F, 0.0F}));
    // A target that moves nothing survives as a nameable, zero-cost shape rather than being
    // silently deleted - deleting it would renumber every weight after it.
    EXPECT_EQ(mesh.morphTargets[1].name, "unused");
    EXPECT_TRUE(mesh.morphTargets[1].deltas.empty());
    EXPECT_EQ(mesh.defaultMorphWeights, (std::vector<float>{0.25F, 0.0F}));
    EXPECT_TRUE(IsMeshMorphTargetDataValid(mesh));
}

TEST(GltfMorphTargetConverterTest, ConvertGltfPrimitive_UnnamedTargetsGetDeterministicNames) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> moved = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.1F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        return bytes;
    }();
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    GltfMorphTargetSet morphSet;
    morphSet.targets.push_back(GltfMorphTargetSource{
        MakeAccessor(moved, 3U, GltfComponentType::Float), std::nullopt, std::nullopt});

    MeshAsset mesh;
    ASSERT_TRUE(ConvertGltfPrimitive(source, morphSet, mesh));
    ASSERT_EQ(mesh.morphTargets.size(), 1U);
    EXPECT_EQ(mesh.morphTargets[0].name, "target0");
}

TEST(GltfMorphTargetConverterTest, ConvertGltfPrimitive_RejectsTargetThatDisagreesWithBaseVertexCount) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> shortDeltas = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        return bytes;
    }();
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    GltfMorphTargetSet morphSet;
    morphSet.targets.push_back(GltfMorphTargetSource{
        MakeAccessor(shortDeltas, 1U, GltfComponentType::Float), std::nullopt, std::nullopt});

    MeshAsset mesh;
    EXPECT_FALSE(ConvertGltfPrimitive(source, morphSet, mesh));
    EXPECT_TRUE(mesh.morphTargets.empty());
}

TEST(GltfMorphTargetConverterTest, ConvertGltfPrimitive_RejectsWeightCountThatDoesNotMatchTargets) {
    const std::vector<std::byte> positions = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
        return bytes;
    }();
    const std::vector<std::byte> moved = [&] {
        std::vector<std::byte> bytes;
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.1F, 0.0F, 0.0F);
        AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
        return bytes;
    }();
    const GltfPrimitiveSource source{MakeAccessor(positions, 3U, GltfComponentType::Float), std::nullopt,
                                        std::nullopt, std::nullopt, 4U};
    GltfMorphTargetSet morphSet;
    morphSet.targets.push_back(GltfMorphTargetSource{
        MakeAccessor(moved, 3U, GltfComponentType::Float), std::nullopt, std::nullopt});
    morphSet.defaultWeights = {0.5F, 0.5F}; // two weights, one target

    MeshAsset mesh;
    EXPECT_FALSE(ConvertGltfPrimitive(source, morphSet, mesh));
}

} // namespace UVE::Asset::Tests