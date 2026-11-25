// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "gltf_skin_converter.h"
#include "mesh_asset.h"
#include "skeleton_asset.h"

namespace UVE::Asset::Tests {
namespace {

std::filesystem::path TestPath(const char* const name) {
    return ::UVE::Tests::ScratchRoot() / name;
}

void AppendU32LittleEndian(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
    }
}

void AppendU16LittleEndian(std::vector<std::byte>& bytes, const std::uint16_t value) {
    bytes.push_back(std::byte{static_cast<unsigned char>(value & 0xFFU)});
    bytes.push_back(std::byte{static_cast<unsigned char>((value >> 8U) & 0xFFU)});
}

void AppendFloatLittleEndian(std::vector<std::byte>& bytes, const float value) {
    AppendU32LittleEndian(bytes, std::bit_cast<std::uint32_t>(value));
}

void WriteFloatLittleEndian(std::vector<std::byte>& bytes, const std::size_t offset, const float value) {
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) {
        bytes[offset + shift / 8U] = std::byte{static_cast<unsigned char>((bits >> shift) & 0xFFU)};
    }
}

void AppendColumnMajorTranslation(std::vector<std::byte>& bytes, const float x, const float y,
                                     const float z) {
    AppendFloatLittleEndian(bytes, 1.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 1.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, 1.0F);
    AppendFloatLittleEndian(bytes, 0.0F);
    AppendFloatLittleEndian(bytes, x);
    AppendFloatLittleEndian(bytes, y);
    AppendFloatLittleEndian(bytes, z);
    AppendFloatLittleEndian(bytes, 1.0F);
}

GltfSkeleton MakeTwoJointSkeleton() {
    GltfJoint root;
    root.sourceNodeIndex = 0U;
    root.name = "Root";
    root.parentIndex = -1;
    GltfJoint child;
    child.sourceNodeIndex = 1U;
    child.name = "Child";
    child.parentIndex = 0;
    child.translation = Math::Vector3{0.0F, 1.0F, 0.0F};
    GltfSkeleton skeleton;
    skeleton.joints = {root, child};
    skeleton.skinCount = 1U;
    return skeleton;
}

struct HandBuiltSkin final {
    std::vector<std::byte> ibmBytes;
    std::vector<std::byte> jointBytes;
    std::vector<std::byte> weightBytes;
    GltfSkinSource source;
};

// Two joints (root node 0, child node 1) with the skin order REVERSED relative to the
// hierarchy, so every assertion exercises the skin-order to skeleton-order mapping: skin
// position 0 is the child (inverse bind translation 4,5,6), skin position 1 is the root
// (translation 1,2,3). The single vertex references skin joints {0,1} with weights {2,2}.
HandBuiltSkin MakeHandBuiltSkin(const GltfComponentType jointType) {
    HandBuiltSkin built;
    AppendColumnMajorTranslation(built.ibmBytes, 4.0F, 5.0F, 6.0F);
    AppendColumnMajorTranslation(built.ibmBytes, 1.0F, 2.0F, 3.0F);
    const std::uint32_t skinJoints[4U] = {0U, 1U, 0U, 0U};
    for (const std::uint32_t skinJoint : skinJoints) {
        switch (jointType) {
        case GltfComponentType::UnsignedByte:
            built.jointBytes.push_back(std::byte{static_cast<unsigned char>(skinJoint)});
            break;
        case GltfComponentType::UnsignedShort:
            AppendU16LittleEndian(built.jointBytes, static_cast<std::uint16_t>(skinJoint));
            break;
        case GltfComponentType::UnsignedInt:
            AppendU32LittleEndian(built.jointBytes, skinJoint);
            break;
        case GltfComponentType::Float:
            AppendFloatLittleEndian(built.jointBytes, static_cast<float>(skinJoint));
            break;
        }
    }
    AppendFloatLittleEndian(built.weightBytes, 2.0F);
    AppendFloatLittleEndian(built.weightBytes, 2.0F);
    AppendFloatLittleEndian(built.weightBytes, 0.0F);
    AppendFloatLittleEndian(built.weightBytes, 0.0F);

    built.source.skeletonId = "Rig";
    built.source.skeleton = MakeTwoJointSkeleton();
    built.source.skinJointNodes = {1U, 0U};
    built.source.inverseBindMatrices =
        GltfAccessorView{std::span<const std::byte>{built.ibmBytes}, 0U, 2U, 0U,
                            GltfComponentType::Float};
    built.source.joints0 = GltfAccessorView{std::span<const std::byte>{built.jointBytes}, 0U, 1U, 0U,
                                               jointType};
    built.source.weights0 =
        GltfAccessorView{std::span<const std::byte>{built.weightBytes}, 0U, 1U, 0U,
                            GltfComponentType::Float};
    built.source.vertexCount = 1U;
    return built;
}

std::vector<std::byte> MakeDocumentBuffer() {
    std::vector<std::byte> buffer;
    AppendColumnMajorTranslation(buffer, 4.0F, 5.0F, 6.0F);
    AppendColumnMajorTranslation(buffer, 1.0F, 2.0F, 3.0F);
    AppendFloatLittleEndian(buffer, 0.0F);
    AppendFloatLittleEndian(buffer, 0.0F);
    AppendFloatLittleEndian(buffer, 0.0F);
    buffer.push_back(std::byte{0U});
    buffer.push_back(std::byte{1U});
    buffer.push_back(std::byte{0U});
    buffer.push_back(std::byte{0U});
    AppendFloatLittleEndian(buffer, 2.0F);
    AppendFloatLittleEndian(buffer, 2.0F);
    AppendFloatLittleEndian(buffer, 0.0F);
    AppendFloatLittleEndian(buffer, 0.0F);
    return buffer;
}

constexpr const char* kSkinnedDocument = R"json({
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

TEST(GltfSkinConverterTest, ConvertsSkinWithReversedSkinOrder) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    GltfSkinResult result;
    ASSERT_TRUE(ConvertGltfSkin(built.source, result));

    EXPECT_EQ(result.skeleton.skeletonId, "Rig");
    ASSERT_EQ(result.skeleton.joints.size(), 2U);
    EXPECT_EQ(result.skeleton.joints[0].name, "Root");
    EXPECT_EQ(result.skeleton.joints[0].parent, -1);
    EXPECT_EQ(result.skeleton.joints[1].name, "Child");
    EXPECT_EQ(result.skeleton.joints[1].parent, 0);
    EXPECT_TRUE(IsSkeletonAssetValid(result.skeleton));

    ASSERT_EQ(result.joints.size(), 2U);
    EXPECT_EQ(result.joints[0].parentIndex, kInvalidJointParent);
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
    const MeshSkinningInfluence& influence = result.influences[0];
    EXPECT_EQ(influence.joints[0], 1U);
    EXPECT_EQ(influence.joints[1], 0U);
    EXPECT_FLOAT_EQ(influence.weights[0], 0.5F);
    EXPECT_FLOAT_EQ(influence.weights[1], 0.5F);
    EXPECT_FLOAT_EQ(influence.weights[2], 0.0F);
    EXPECT_FLOAT_EQ(influence.weights[3], 0.0F);
    EXPECT_TRUE(IsSkinningInfluenceNormalized(influence));

    MeshAsset mesh;
    mesh.vertices.resize(1U);
    mesh.skinningInfluences = result.influences;
    mesh.joints = result.joints;
    EXPECT_TRUE(IsMeshSkinningDataValid(mesh));
}

TEST(GltfSkinConverterTest, AcceptsUnsignedShortAndUnsignedIntJoints) {
    for (const GltfComponentType jointType :
         {GltfComponentType::UnsignedShort, GltfComponentType::UnsignedInt}) {
        HandBuiltSkin built = MakeHandBuiltSkin(jointType);
        GltfSkinResult result;
        ASSERT_TRUE(ConvertGltfSkin(built.source, result));
        ASSERT_EQ(result.influences.size(), 1U);
        EXPECT_EQ(result.influences[0].joints[0], 1U);
        EXPECT_EQ(result.influences[0].joints[1], 0U);
        EXPECT_FLOAT_EQ(result.influences[0].weights[0], 0.5F);
        EXPECT_FLOAT_EQ(result.influences[0].weights[1], 0.5F);
    }
}

TEST(GltfSkinConverterTest, MissingInverseBindMatricesMeansIdentity) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    built.source.inverseBindMatrices.reset();
    GltfSkinResult result;
    ASSERT_TRUE(ConvertGltfSkin(built.source, result));
    ASSERT_EQ(result.joints.size(), 2U);
    for (const MeshJoint& joint : result.joints) {
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[0][0], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[1][1], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[2][2], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[3][3], 1.0F);
        EXPECT_FLOAT_EQ(joint.inverseBindMatrix.m[0][3], 0.0F);
    }
}

TEST(GltfSkinConverterTest, RejectsVertexCountMismatch) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    built.source.vertexCount = 2U;
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsOutOfRangeJointIndex) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    built.jointBytes[0] = std::byte{7U};
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsFloatJoints) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    // A valid float-VEC4 span, so only the component-type gate can reject this.
    built.jointBytes.resize(16U);
    built.source.joints0.buffer = std::span<const std::byte>{built.jointBytes};
    built.source.joints0.componentType = GltfComponentType::Float;
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsNegativeWeight) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    WriteFloatLittleEndian(built.weightBytes, 0U, -1.0F);
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsZeroWeightSum) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    WriteFloatLittleEndian(built.weightBytes, 0U, 0.0F);
    WriteFloatLittleEndian(built.weightBytes, 4U, 0.0F);
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsDuplicateSkinNode) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    built.source.skinJointNodes = {0U, 0U};
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, RejectsSkinNodeOutsideHierarchy) {
    HandBuiltSkin built = MakeHandBuiltSkin(GltfComponentType::UnsignedByte);
    built.source.skinJointNodes = {1U, 5U};
    GltfSkinResult result;
    EXPECT_FALSE(ConvertGltfSkin(built.source, result));
}

TEST(GltfSkinConverterTest, DocumentParseConvertsAndRoundTripsSkeleton) {
    const std::vector<std::byte> buffer = MakeDocumentBuffer();
    const std::optional<GltfSkinSource> source = ParseGltfSkinSource(kSkinnedDocument, buffer);
    ASSERT_TRUE(source.has_value());
    EXPECT_EQ(source->skeletonId, "Rig");
    EXPECT_EQ(source->vertexCount, 1U);

    GltfSkinResult result;
    ASSERT_TRUE(ConvertGltfSkin(*source, result));
    ASSERT_EQ(result.influences.size(), 1U);
    EXPECT_EQ(result.influences[0].joints[0], 1U);
    EXPECT_EQ(result.influences[0].joints[1], 0U);
    EXPECT_FLOAT_EQ(result.influences[0].weights[0], 0.5F);
    ASSERT_EQ(result.joints.size(), 2U);
    EXPECT_FLOAT_EQ(result.joints[1].inverseBindMatrix.m[0][3], 4.0F);

    const std::filesystem::path path = TestPath("uve_gltf_skin_skeleton_round_trip.uvskel");
    std::filesystem::remove(path);
    ASSERT_TRUE(SaveSkeletonAsset(result.skeleton, path));
    SkeletonAsset reloaded;
    ASSERT_TRUE(LoadSkeletonAsset(path, reloaded));
    EXPECT_EQ(reloaded, result.skeleton);
    std::filesystem::remove(path);
}

TEST(GltfSkinConverterTest, RejectsDocumentMissingWeights) {
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
    const std::vector<std::byte> buffer = MakeDocumentBuffer();
    EXPECT_FALSE(ParseGltfSkinSource(kMissingWeights, buffer).has_value());
}

TEST(GltfSkinConverterTest, RejectsDocumentWithoutSkin) {
    constexpr const char* kNoSkin = R"json({
  "asset": {"version": "2.0"},
  "nodes": [{"name": "Root"}],
  "meshes": [{"primitives": [{"attributes": {"POSITION": 0}}]}],
  "accessors": [{"bufferView": 0, "componentType": 5126, "count": 1, "type": "VEC3"}],
  "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 12}],
  "buffers": [{"byteLength": 12}]
})json";
    const std::vector<std::byte> buffer(12U);
    EXPECT_FALSE(ParseGltfSkinSource(kNoSkin, buffer).has_value());
}

TEST(GltfSkinConverterTest, RejectsMalformedDocument) {
    const std::vector<std::byte> buffer(12U);
    EXPECT_FALSE(ParseGltfSkinSource("{not json", buffer).has_value());
}

} // namespace UVE::Asset::Tests
