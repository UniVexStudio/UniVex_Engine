// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch_uve.h"

#include "uve/asset/gltf_skin_converter_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/skeleton_asset_uve.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPathUVE(const char* const name) {
    return ::UVE::Tests::ScratchRootUVE() / name;
}

void AppendU32LittleEndianUVE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

void AppendU16LittleEndianUVE(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendFloatLittleEndianUVE(std::vector<std::byte>& bytes, const float value) {
    AppendU32LittleEndianUVE(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteFloatLittleEndianUVE(std::vector<std::byte>& bytes, const std::size_t offset, const float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset + shift / 8U] = std::byte{static_cast<unsigned char>((bits >> shift) & 0xFFU)};
    }
}

void AppendColumnMajorTranslationUVE(std::vector<std::byte>& bytes, const float x, const float y,
                                     const float z) {
    AppendFloatLittleEndianUVE(bytes, 1.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 1.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, 1.0F);
    AppendFloatLittleEndianUVE(bytes, 0.0F);
    AppendFloatLittleEndianUVE(bytes, x);
    AppendFloatLittleEndianUVE(bytes, y);
    AppendFloatLittleEndianUVE(bytes, z);
    AppendFloatLittleEndianUVE(bytes, 1.0F);
}

GltfSkeletonUVE MakeTwoJointSkeletonUVE() {
    GltfJointUVE root;
    root.sourceNodeIndex = 0U;
    root.name = "Root";
    root.parentIndex = -1;
    GltfJointUVE child;
    child.sourceNodeIndex = 1U;
    child.name = "Child";
    child.parentIndex = 0;
    child.translation = Math::Vector3UVE{0.0F, 1.0F, 0.0F};
    GltfSkeletonUVE skeleton;
    skeleton.joints = {root, child};
    skeleton.skinCount = 1U;
    return skeleton;
}

struct HandBuiltSkinUVE final {
    std::vector<std::byte> ibmBytes;
    std::vector<std::byte> jointBytes;
    std::vector<std::byte> weightBytes;
    GltfSkinSourceUVE source;
};

// Two joints (root node 0, child node 1) with the skin order REVERSED relative to the
// hierarchy, so every assertion exercises the skin-order to skeleton-order mapping: skin
// position 0 is the child (inverse bind translation 4,5,6), skin position 1 is the root
// (translation 1,2,3). The single vertex references skin joints {0,1} with weights {2,2}.
HandBuiltSkinUVE MakeHandBuiltSkinUVE(const GltfComponentTypeUVE jointType) {
    HandBuiltSkinUVE built;
    AppendColumnMajorTranslationUVE(built.ibmBytes, 4.0F, 5.0F, 6.0F);
    AppendColumnMajorTranslationUVE(built.ibmBytes, 1.0F, 2.0F, 3.0F);
    const std::uint32_t skinJoints[4U] = {0U, 1U, 0U, 0U};
    for (const std::uint32_t skinJoint : skinJoints) {
        switch (jointType) {
        case GltfComponentTypeUVE::UnsignedByte:
            built.jointBytes.push_back(std::byte{static_cast<unsigned char>(skinJoint)});
            break;
        case GltfComponentTypeUVE::UnsignedShort:
            AppendU16LittleEndianUVE(built.jointBytes, static_cast<std::uint16_t>(skinJoint));
            break;
        case GltfComponentTypeUVE::UnsignedInt:
            AppendU32LittleEndianUVE(built.jointBytes, skinJoint);
            break;
        case GltfComponentTypeUVE::Float:
            AppendFloatLittleEndianUVE(built.jointBytes, static_cast<float>(skinJoint));
            break;
        }
    }
    AppendFloatLittleEndianUVE(built.weightBytes, 2.0F);
    AppendFloatLittleEndianUVE(built.weightBytes, 2.0F);
    AppendFloatLittleEndianUVE(built.weightBytes, 0.0F);
    AppendFloatLittleEndianUVE(built.weightBytes, 0.0F);

    built.source.skeletonId = "Rig";
    built.source.skeleton = MakeTwoJointSkeletonUVE();
    built.source.skinJointNodes = {1U, 0U};
    built.source.inverseBindMatrices =
        GltfAccessorViewUVE{std::span<const std::byte>{built.ibmBytes}, 0U, 2U, 0U,
                            GltfComponentTypeUVE::Float};
    built.source.joints0 = GltfAccessorViewUVE{std::span<const std::byte>{built.jointBytes}, 0U, 1U, 0U,
                                               jointType};
    built.source.weights0 =
        GltfAccessorViewUVE{std::span<const std::byte>{built.weightBytes}, 0U, 1U, 0U,
                            GltfComponentTypeUVE::Float};
    built.source.vertexCount = 1U;
    return built;
}

std::vector<std::byte> MakeDocumentBufferUVE() {
    std::vector<std::byte> buffer;
    AppendColumnMajorTranslationUVE(buffer, 4.0F, 5.0F, 6.0F);
    AppendColumnMajorTranslationUVE(buffer, 1.0F, 2.0F, 3.0F);
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    buffer.push_back(std::byte{0U});
    buffer.push_back(std::byte{1U});
    buffer.push_back(std::byte{0U});
    buffer.push_back(std::byte{0U});
    AppendFloatLittleEndianUVE(buffer, 2.0F);
    AppendFloatLittleEndianUVE(buffer, 2.0F);
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    AppendFloatLittleEndianUVE(buffer, 0.0F);
    return buffer;
}

constexpr const char* kSkinnedDocumentUVE = R"json({
  "asset": {"version": "2.0"},
  "nodes": [
    {"name": "Root", "children": [1]},
    {"name": "Child", "translation": [0.0, 1.0, 0.0]}
  ],
  "skins": [{"name": "Rig", "joints": [1, 0], "inverseBindMatrices": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 1, "JOINTS_0": 2, "WEIGHTS_0": 3}}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 1, "componentType": 5126, "count": 1, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5121, "count": 1, "type": "VEC4"},
    {"bufferView": 3, "componentType": 5126, "count": 1, "type": "VEC4"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 128},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12},
    {"buffer": 0, "byteOffset": 140, "byteLength": 4},
    {"buffer": 0, "byteOffset": 144, "byteLength": 16}
  ],
  "buffers": [{"byteLength": 160}]
})json";

} // namespace

TEST(GltfSkinConverterUVETest, ConvertsSkinWithReversedSkinOrder) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    GltfSkinResultUVE result;
    ASSERT_TRUE(ConvertGltfSkinUVE(built.source, result));

    EXPECT_EQ(result.skeleton.skeletonId, "Rig");
    ASSERT_EQ(result.skeleton.joints.size(), 2U);
    EXPECT_EQ(result.skeleton.joints[0].name, "Root");
    EXPECT_EQ(result.skeleton.joints[0].parent, -1);
    EXPECT_EQ(result.skeleton.joints[1].name, "Child");
    EXPECT_EQ(result.skeleton.joints[1].parent, 0);
    EXPECT_TRUE(IsSkeletonAssetValidUVE(result.skeleton));

    ASSERT_EQ(result.joints.size(), 2U);
    EXPECT_EQ(result.joints[0].parentIndex, kInvalidJointParentUVE);
    EXPECT_EQ(result.joints[1].parentIndex, 0U);
    // Skin position 0 is the child: its matrix (translation 4,5,6) lands on skeleton joint 1,
    // transposed from glTF column-major to row-major.
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[0][3], 4.0F);
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[1][3], 5.0F);
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[2][3], 6.0F);
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[3][3], 1.0F);
    EXPECT_FLOAT_EQ(result.joints[0].inverseBindMatrix.m[0][3], 1.0F);
    EXPECT_FLOAT_EQ(result.joints[0].inverseBindMatrix.m[1][3], 2.0F);
    EXPECT_FLOAT_EQ(result.joints[0].inverseBindMatrix.m[2][3], 3.0F);

    ASSERT_EQ(result.influences.size(), 1U);
    const MeshSkinningInfluenceUVE& influence = result.influences[0];
    EXPECT_EQ(influence.joints[0], 1U);
    EXPECT_EQ(influence.joints[1], 0U);
    EXPECT_FLOAT_EQ(influence.weights[0], 0.5F);
    EXPECT_FLOAT_EQ(influence.weights[1], 0.5F);
    EXPECT_FLOAT_EQ(influence.weights[2], 0.0F);
    EXPECT_FLOAT_EQ(influence.weights[3], 0.0F);
    EXPECT_TRUE(IsSkinningInfluenceNormalizedUVE(influence));

    MeshAssetUVE mesh;
    mesh.vertices.resize(1U);
    mesh.skinningInfluences = result.influences;
    mesh.joints = result.joints;
    EXPECT_TRUE(IsMeshSkinningDataValidUVE(mesh));
}

TEST(GltfSkinConverterUVETest, AcceptsUnsignedShortAndUnsignedIntJoints) {
    for (const GltfComponentTypeUVE jointType :
         {GltfComponentTypeUVE::UnsignedShort, GltfComponentTypeUVE::UnsignedInt}) {
        HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(jointType);
        GltfSkinResultUVE result;
        ASSERT_TRUE(ConvertGltfSkinUVE(built.source, result));
        ASSERT_EQ(result.influences.size(), 1U);
        EXPECT_EQ(result.influences[0].joints[0], 1U);
        EXPECT_EQ(result.influences[0].joints[1], 0U);
        EXPECT_FLOAT_EQ(result.influences[0].weights[0], 0.5F);
        EXPECT_FLOAT_EQ(result.influences[0].weights[1], 0.5F);
    }
}

TEST(GltfSkinConverterUVETest, MissingInverseBindMatricesMeansIdentity) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    built.source.inverseBindMatrices.reset();
    GltfSkinResultUVE result;
    ASSERT_TRUE(ConvertGltfSkinUVE(built.source, result));
    ASSERT_EQ(result.joints.size(), 2U);
    for (const MeshJointUVE& joint : result.joints) {
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[0][0], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[1][1], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[2][2], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[3][3], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[0][3], 0.0F);
    }
}

TEST(GltfSkinConverterUVETest, RejectsVertexCountMismatch) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    built.source.vertexCount = 2U;
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsOutOfRangeJointIndex) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    built.jointBytes[0] = std::byte{7U};
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsFloatJoints) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    // A valid float-VEC4 span, so only the component-type gate can reject this.
    built.jointBytes.resize(16U);
    built.source.joints0.buffer = std::span<const std::byte>{built.jointBytes};
    built.source.joints0.componentType = GltfComponentTypeUVE::Float;
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsNegativeWeight) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    WriteFloatLittleEndianUVE(built.weightBytes, 0U, -1.0F);
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsZeroWeightSum) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    WriteFloatLittleEndianUVE(built.weightBytes, 0U, 0.0F);
    WriteFloatLittleEndianUVE(built.weightBytes, 4U, 0.0F);
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsDuplicateSkinNode) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    built.source.skinJointNodes = {0U, 0U};
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, RejectsSkinNodeOutsideHierarchy) {
    HandBuiltSkinUVE built = MakeHandBuiltSkinUVE(GltfComponentTypeUVE::UnsignedByte);
    built.source.skinJointNodes = {1U, 5U};
    GltfSkinResultUVE result;
    EXPECT_FALSE(ConvertGltfSkinUVE(built.source, result));
}

TEST(GltfSkinConverterUVETest, DocumentParseConvertsAndRoundTripsSkeleton) {
    const std::vector<std::byte> buffer = MakeDocumentBufferUVE();
    const std::optional<GltfSkinSourceUVE> source = ParseGltfSkinSourceUVE(kSkinnedDocumentUVE, buffer);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->skeletonId, "Rig");
    EXPECT_EQ(source->vertexCount, 1U);

    GltfSkinResultUVE result;
    ASSERT_TRUE(ConvertGltfSkinUVE(*source, result));
    ASSERT_EQ(result.influences.size(), 1U);
    EXPECT_EQ(result.influences[0].joints[0], 1U);
    EXPECT_EQ(result.influences[0].joints[1], 0U);
    EXPECT_FLOAT_EQ(result.influences[0].weights[0], 0.5F);
    ASSERT_EQ(result.joints.size(), 2U);
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[0][3], 4.0F);

    const std::filesystem::path path = TestPathUVE("uve_gltf_skin_skeleton_round_trip.uvskel");
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveSkeletonAssetUVE(result.skeleton, path));
    SkeletonAssetUVE reloaded;
    ASSERT_TRUE(LoadSkeletonAssetUVE(path, reloaded));
    EXPECT_EQ(reloaded, result.skeleton);
    std::filesystem::remove(path);
}

TEST(GltfSkinConverterUVETest, RejectsDocumentMissingWeights) {
    constexpr const char* kMissingWeights = R"json({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Root", "children": [1]}, {"name": "Child"}],
  "skins": [{"joints": [0, 1], "inverseBindMatrices": 0}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 1, "JOINTS_0": 2}}]}],
  "accessors": [
    {"bufferView": 0, "componentType": 5126, "count": 2, "type": "MAT4"},
    {"bufferView": 1, "componentType": 5126, "count": 1, "type": "VEC3"},
    {"bufferView": 2, "componentType": 5121, "count": 1, "type": "VEC4"}
  ],
  "bufferViews": [
    {"buffer": 0, "byteOffset": 0, "byteLength": 128},
    {"buffer": 0, "byteOffset": 128, "byteLength": 12},
    {"buffer": 0, "byteOffset": 140, "byteLength": 4}
  ],
  "buffers": [{"byteLength": 144}]
})json";
    const std::vector<std::byte> buffer = MakeDocumentBufferUVE();
    EXPECT_FALSE(ParseGltfSkinSourceUVE(kMissingWeights, buffer).has_value());
}

TEST(GltfSkinConverterUVETest, RejectsDocumentWithoutSkin) {
    constexpr const char* kNoSkin = R"json({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Root"}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC3"}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 12}],
  "buffers": [{"byteLength": 12}]
})json";
    const std::vector<std::byte> buffer(12U);
    EXPECT_FALSE(ParseGltfSkinSourceUVE(kNoSkin, buffer).has_value());
}

TEST(GltfSkinConverterUVETest, RejectsMalformedDocument) {
    const std::vector<std::byte> buffer(12U);
    EXPECT_FALSE(ParseGltfSkinSourceUVE("{not json", buffer).has_value());
}

} // namespace UVE::Asset::Tests
