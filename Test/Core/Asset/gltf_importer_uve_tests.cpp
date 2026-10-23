// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/asset/asset_importer_uve.h"

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_image_fixtures_uve.h"

#include "uve/asset/asset_database_uve.h"
#include "uve/asset/asset_import_sidecar_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/asset/texture_asset_uve.h"

namespace UVE::Asset::Tests {
namespace {

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
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    AppendU32LittleEndianUVE(bytes, bits);
}

void AppendVector3UVE(std::vector<std::byte>& bytes, const float x, const float y, const float z) {
    AppendFloatLittleEndianUVE(bytes, x);
    AppendFloatLittleEndianUVE(bytes, y);
    AppendFloatLittleEndianUVE(bytes, z);
}

[[nodiscard]] std::vector<std::byte> MakeTriangleBufferUVE() {
    std::vector<std::byte> bytes;
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 1.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 1.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        AppendFloatLittleEndianUVE(bytes, value);
    }
    AppendU16LittleEndianUVE(bytes, 0U);
    AppendU16LittleEndianUVE(bytes, 1U);
    AppendU16LittleEndianUVE(bytes, 2U);
    return bytes;
}

[[nodiscard]] std::string MakeGltfJsonUVE(const std::string_view uri) {
    const std::string bufferUri = uri.empty() ? std::string{} : "\"uri\":\"" + std::string(uri) + "\",\n    ";
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{" + bufferUri + "\"byteLength\": 102}],\n"
           "  \"bufferViews\": [\n"
           "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},\n"
           "    {\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}\n"
           "  ],\n"
           "  \"accessors\": [\n"
           "    {\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}\n"
           "  ],\n"
           "  \"meshes\": [{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4}]}]\n"
           "}\n";
}

/// A triangle plus two blend shapes. Layout, all float32 unless noted:
///   [0,36)   positions   [36,72) normals   [72,96) texcoords   [96,102) u16 indices
///   [104,140) "jawOpen" displacements      [140,176) "blink" displacements
/// 104 rather than 102 so the second block starts on a 4-byte boundary, which is what glTF
/// requires of a float accessor; the two padding bytes are part of the file.
[[nodiscard]] std::vector<std::byte> MakeTriangleWithMorphTargetsBufferUVE() {
    std::vector<std::byte> bytes;
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 1.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 1.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        AppendFloatLittleEndianUVE(bytes, value);
    }
    AppendU16LittleEndianUVE(bytes, 0U);
    AppendU16LittleEndianUVE(bytes, 1U);
    AppendU16LittleEndianUVE(bytes, 2U);
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{0U});
    // "jawOpen": only the third vertex moves.
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, -0.5F, 0.0F);
    // "blink": no vertex moves at all.
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3UVE(bytes, 0.0F, 0.0F, 0.0F);
    return bytes;
}

[[nodiscard]] std::string MakeGltfJsonWithMorphTargetsUVE(const std::string_view uri) {
    const std::string bufferUri = uri.empty() ? std::string{} : "\"uri\":\"" + std::string(uri) + "\",\n    ";
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{" + bufferUri + "\"byteLength\": 176}],\n"
           "  \"bufferViews\": [\n"
           "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},\n"
           "    {\"buffer\":0,\"byteOffset\":96,\"byteLength\":6},\n"
           "    {\"buffer\":0,\"byteOffset\":104,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":140,\"byteLength\":36}\n"
           "  ],\n"
           "  \"accessors\": [\n"
           "    {\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},\n"
           "    {\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":5,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"}\n"
           "  ],\n"
           "  \"meshes\": [{\n"
           "    \"weights\": [0.5, 0.0],\n"
           "    \"extras\": {\"targetNames\": [\"jawOpen\", \"blink\"]},\n"
           "    \"primitives\":[{\n"
           "      \"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\n"
           "      \"indices\":3,\"mode\":4,\n"
           "      \"targets\":[{\"POSITION\":4},{\"POSITION\":5}]\n"
           "    }]\n"
           "  }]\n"
           "}\n";
}

/// Two triangles in one buffer, each its own glTF primitive with its own material - the shape a
/// DCC tool exports for a model whose paint and glass are different material slots.
/// Layout: [0,72) positions (6 vec3)  [72,144) normals (6 vec3)  [144,192) uvs (6 vec2)
///         [192,204) indices (6 u16: two triangles).
[[nodiscard]] std::vector<std::byte> MakeTwoPrimitiveBufferUVE() {
    std::vector<std::byte> bytes;
    for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                              2.0F, 0.0F, 0.0F, 3.0F, 0.0F, 0.0F, 2.0F, 1.0F, 0.0F}) {
        AppendFloatLittleEndianUVE(bytes, value);
    }
    for (int vertex = 0; vertex < 6; ++vertex) {
        AppendVector3UVE(bytes, 0.0F, 0.0F, 1.0F);
    }
    for (int vertex = 0; vertex < 6; ++vertex) {
        AppendFloatLittleEndianUVE(bytes, vertex % 2 == 0 ? 0.0F : 1.0F);
        AppendFloatLittleEndianUVE(bytes, 0.0F);
    }
    for (const std::uint16_t index : {std::uint16_t{0U}, std::uint16_t{1U}, std::uint16_t{2U},
                                      std::uint16_t{3U}, std::uint16_t{4U}, std::uint16_t{5U}}) {
        AppendU16LittleEndianUVE(bytes, index);
    }
    return bytes;
}

[[nodiscard]] std::string MakeTwoPrimitiveGltfJsonUVE(const std::string_view uri) {
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{\"uri\":\"" + std::string(uri) + "\",\"byteLength\": 204}],\n"
           "  \"bufferViews\": [\n"
           "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":72},\n"
           "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":72},\n"
           "    {\"buffer\":0,\"byteOffset\":144,\"byteLength\":48},\n"
           "    {\"buffer\":0,\"byteOffset\":192,\"byteLength\":6},\n"
           "    {\"buffer\":0,\"byteOffset\":198,\"byteLength\":6}\n"
           "  ],\n"
           "  \"accessors\": [\n"
           "    {\"bufferView\":0,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":1,\"componentType\":5126,\"count\":6,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":2,\"componentType\":5126,\"count\":6,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},\n"
           "    {\"bufferView\":4,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}\n"
           "  ],\n"
           "  \"materials\": [\n"
           "    {\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},\n"
           "    {\"name\":\"glass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}}\n"
           "  ],\n"
           "  \"meshes\": [{\"primitives\":[\n"
           "    {\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":3,\"mode\":4,\"material\":0},\n"
           "    {\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},\"indices\":4,\"mode\":4,\"material\":1}\n"
           "  ]}]\n"
           "}\n";
}

void WriteBytesUVE(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ASSERT_TRUE(output.good());
}

void WriteTextUVE(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    ASSERT_TRUE(output.good());
}

void WriteGlbUVE(const std::filesystem::path& path, const std::string& json,
                 const std::vector<std::byte>& binary) {
    std::vector<std::byte> bytes;
    std::vector<std::byte> jsonBytes(reinterpret_cast<const std::byte*>(json.data()),
                                     reinterpret_cast<const std::byte*>(json.data() + json.size()));
    while (jsonBytes.size() % 4U != 0U) jsonBytes.push_back(std::byte{' '});
    AppendU32LittleEndianUVE(bytes, 0x46546C67U);
    AppendU32LittleEndianUVE(bytes, 2U);
    AppendU32LittleEndianUVE(bytes, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    AppendU32LittleEndianUVE(bytes, static_cast<std::uint32_t>(jsonBytes.size()));
    AppendU32LittleEndianUVE(bytes, 0x4E4F534AU);
    bytes.insert(bytes.end(), jsonBytes.begin(), jsonBytes.end());
    AppendU32LittleEndianUVE(bytes, static_cast<std::uint32_t>(binary.size()));
    AppendU32LittleEndianUVE(bytes, 0x004E4942U);
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    WriteBytesUVE(path, bytes);
}

void RemoveFilesUVE(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) std::filesystem::remove(path);
}

TEST(GltfImporterUVETest, ImportUVE_ValidExternalBufferPublishesUveModelAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_triangle.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_triangle.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_triangle.uvmodel";
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
    const auto binary = MakeTriangleBufferUVE();
    WriteBytesUVE(bufferPath, binary);
    WriteTextUVE(sourcePath, MakeGltfJsonUVE(bufferPath.filename().string()));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const auto classification = importer.ClassifySourceUVE(sourcePath);
    EXPECT_EQ(classification.kind, AssetImportSourceKindUVE::RawModel);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    EXPECT_EQ(classification.diagnostic, "format-specific parser is registered");
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(guid), destinationPath);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.vertices[0].normal, (Math::Vector3UVE{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(mesh.localBounds,
              (Math::AabbUVE{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 0.0F}}));
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterUVETest, ImportUVE_ImportsMorphTargetsNamesAndDefaultWeights) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_morph.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_morph.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_morph.uvmodel";
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
    WriteBytesUVE(bufferPath, MakeTriangleWithMorphTargetsBufferUVE());
    WriteTextUVE(sourcePath, MakeGltfJsonWithMorphTargetsUVE(bufferPath.filename().string()));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.morphTargets.size(), 2U);
    EXPECT_EQ(mesh.morphTargets[0].name, "jawOpen");
    EXPECT_EQ(mesh.morphTargets[1].name, "blink");
    // "jawOpen" keeps only the one vertex it actually moves; "blink" moves nothing and keeps an
    // empty delta list so its weight slot keeps its index.
    ASSERT_EQ(mesh.morphTargets[0].deltas.size(), 1U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].vertexIndex, 2U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].positionDelta, (Math::Vector3UVE{0.0F, -0.5F, 0.0F}));
    EXPECT_TRUE(mesh.morphTargets[1].deltas.empty());
    EXPECT_EQ(mesh.defaultMorphWeights, (std::vector<float>{0.5F, 0.0F}));

    // And the weights actually drive the geometry through the eval path the runtime uses.
    std::vector<MeshVertexUVE> morphed;
    const std::array<float, 2U> weights{1.0F, 0.0F};
    ASSERT_TRUE(AccumulateMorphTargetsUVE(mesh.vertices, mesh.morphTargets, weights, morphed));
    EXPECT_EQ(morphed[2].position, (Math::Vector3UVE{0.0F, 0.5F, 0.0F}));
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterUVETest, ImportUVE_MorphTargetImportOffSkipsShapesEntirely) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_morph_off.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_morph_off.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_morph_off.uvmodel";
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
    WriteBytesUVE(bufferPath, MakeTriangleWithMorphTargetsBufferUVE());
    WriteTextUVE(sourcePath, MakeGltfJsonWithMorphTargetsUVE(bufferPath.filename().string()));

    MeshImportSettingsUVE settings;
    settings.importMorphTargets = false;
    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(sourcePath, destinationPath, database, settings), kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    // The setting is honoured, not merely recorded: the shapes are absent from the asset, so the
    // base mesh is all anyone downstream can see.
    EXPECT_TRUE(mesh.morphTargets.empty());
    EXPECT_TRUE(mesh.defaultMorphWeights.empty());
    EXPECT_EQ(mesh.vertices.size(), 3U);
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterUVETest, ImportUVE_ValidGlbPublishesUveModelAndRegistersBothExtensions) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_triangle.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_triangle_glb.uvmodel";
    RemoveFilesUVE({sourcePath, destinationPath});
    const auto binary = MakeTriangleBufferUVE();
    WriteGlbUVE(sourcePath, MakeGltfJsonUVE(""), binary);

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const auto glbClassification = importer.ClassifySourceUVE(sourcePath);
    EXPECT_TRUE(glbClassification.importerRegistered);
    EXPECT_EQ(glbClassification.diagnostic, "format-specific parser is registered");
    const auto gltfClassification = importer.ClassifySourceUVE("fixture.gltf");
    EXPECT_TRUE(gltfClassification.importerRegistered);
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuidUVE);
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    RemoveFilesUVE({sourcePath, destinationPath});
}

/// The triangle layout every fixture in this file uses, followed by an image blob at a 4-byte
/// boundary: 102 bytes of geometry, two pad bytes, then whatever the test appends.
constexpr std::size_t kGeometryBytesUVE = 104U;

[[nodiscard]] std::string MakeGltfJsonWithMaterialUVE(const std::string_view bufferUri,
                                                     const std::size_t imageByteLength) {
    const std::string bufferUriJson =
        bufferUri.empty() ? std::string{} : "\"uri\":\"" + std::string(bufferUri) + "\",\n    ";
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{" + bufferUriJson + "\"byteLength\": " +
           std::to_string(kGeometryBytesUVE + imageByteLength) +
           "}],\n"
           "  \"bufferViews\": [\n"
           "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},\n"
           "    {\"buffer\":0,\"byteOffset\":96,\"byteLength\":6},\n"
           "    {\"buffer\":0,\"byteOffset\":104,\"byteLength\":" + std::to_string(imageByteLength) +
           "}\n"
           "  ],\n"
           "  \"accessors\": [\n"
           "    {\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}\n"
           "  ],\n"
           "  \"images\": [{\"bufferView\":4,\"mimeType\":\"image/png\",\"name\":\"brick\"}],\n"
           "  \"textures\": [{\"source\":0}],\n"
           "  \"materials\": [{\n"
           "    \"name\": \"Brick\",\n"
           "    \"pbrMetallicRoughness\": {\n"
           "      \"baseColorFactor\": [0.8, 0.5, 0.25, 1.0],\n"
           "      \"baseColorTexture\": {\"index\": 0},\n"
           "      \"metallicFactor\": 0.25,\n"
           "      \"roughnessFactor\": 0.75\n"
           "    }\n"
           "  }],\n"
           "  \"meshes\": [{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},"
           "\"indices\":3,\"mode\":4,\"material\":0}]}]\n"
           "}\n";
}

/// The geometry blob the material fixtures embed their image into, padded to the 4-byte boundary
/// the JSON above declares.
[[nodiscard]] std::vector<std::byte> MakePaddedTriangleBufferUVE() {
    std::vector<std::byte> bytes = MakeTriangleBufferUVE();
    while (bytes.size() < kGeometryBytesUVE) {
        bytes.push_back(std::byte{0U});
    }
    return bytes;
}

TEST(GltfImporterUVETest, ImportUVE_GlbWithBakedMaterial_PublishesMaterialAndTextureAsAssets) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_material.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_material.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_material.Brick.uvmat";
    const std::filesystem::path texturePath = "uve_gltf_importer_tests_material.brick.uvtex";
    RemoveFilesUVE({sourcePath, destinationPath, materialPath, texturePath});

    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    ASSERT_FALSE(png.empty());
    std::vector<std::byte> binary = MakePaddedTriangleBufferUVE();
    binary.insert(binary.end(), png.begin(), png.end());
    WriteGlbUVE(sourcePath, MakeGltfJsonWithMaterialUVE("", png.size()), binary);

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuidUVE);

    // The mesh references the material the file carried, so a load of the model alone is enough:
    // nothing has to be assigned by hand before it renders textured.
    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    EXPECT_EQ(database.ResolveUVE(mesh.materialGuids[0]), materialPath);

    MaterialAssetUVE material;
    ASSERT_TRUE(LoadMaterialAssetUVE(materialPath, material));
    EXPECT_FLOAT_EQ(material.albedoColor.r, 0.8F);
    EXPECT_FLOAT_EQ(material.albedoColor.g, 0.5F);
    EXPECT_FLOAT_EQ(material.albedoColor.b, 0.25F);
    EXPECT_FLOAT_EQ(material.albedoAlpha, 1.0F);
    EXPECT_FLOAT_EQ(material.metallic, 0.25F);
    EXPECT_FLOAT_EQ(material.roughness, 0.75F);
    EXPECT_FALSE(material.isTransparent);

    // The embedded image became a real texture asset whose pixels survive the round trip.
    ASSERT_NE(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(material.albedoTexture), texturePath);
    TextureAssetUVE texture;
    ASSERT_TRUE(LoadTextureAssetUVE(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpaceUVE::Srgb);
    ASSERT_GE(texture.pixels.size(), 8U);
    EXPECT_EQ(texture.pixels[0], std::byte{0xFF});
    EXPECT_EQ(texture.pixels[3], std::byte{0xFF});

    RemoveFilesUVE({sourcePath, destinationPath, materialPath, texturePath});
}

TEST(GltfImporterUVETest, ImportUVE_MaterialWithUndecodableImage_StillPublishesGeometryAndMaterial) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_bad_image.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_bad_image.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_bad_image.Brick.uvmat";
    RemoveFilesUVE({sourcePath, destinationPath, materialPath});

    // The view points at geometry bytes, which are not an image at all. A texture that cannot be
    // produced must not cost the author the mesh, the material factors or the PBR numbers.
    std::vector<std::byte> binary = MakePaddedTriangleBufferUVE();
    binary.insert(binary.end(), 16U, std::byte{0x5A});
    WriteGlbUVE(sourcePath, MakeGltfJsonWithMaterialUVE("", 16U), binary);

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAssetUVE material;
    ASSERT_TRUE(LoadMaterialAssetUVE(materialPath, material));
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_FLOAT_EQ(material.metallic, 0.25F);
    RemoveFilesUVE({sourcePath, destinationPath, materialPath});
}

TEST(GltfImporterUVETest, ImportUVE_MaterialSamplingExternalPng_ImportsThatTextureToo) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_external_texture.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_external_texture.bin";
    const std::filesystem::path textureSourcePath = "uve_gltf_importer_tests_paint.png";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_external_texture.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_external_texture.Paint.uvmat";
    const std::filesystem::path texturePath = "uve_gltf_importer_tests_paint.uvtex";
    RemoveFilesUVE({sourcePath, bufferPath, textureSourcePath, destinationPath, materialPath, texturePath});

    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8UVE();
    ASSERT_FALSE(png.empty());
    WriteBytesUVE(textureSourcePath, png);
    WriteBytesUVE(bufferPath, MakeTriangleBufferUVE());
    const std::string json =
        "{\n"
        "  \"asset\": {\"version\": \"2.0\"},\n"
        "  \"buffers\": [{\"uri\":\"" + bufferPath.filename().string() + "\",\"byteLength\": 102}],\n"
        "  \"bufferViews\": [\n"
        "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},\n"
        "    {\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},\n"
        "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":24},\n"
        "    {\"buffer\":0,\"byteOffset\":96,\"byteLength\":6}\n"
        "  ],\n"
        "  \"accessors\": [\n"
        "    {\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
        "    {\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
        "    {\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
        "    {\"bufferView\":3,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}\n"
        "  ],\n"
        "  \"images\": [{\"uri\":\"" + textureSourcePath.filename().string() + "\"}],\n"
        "  \"textures\": [{\"source\":0}],\n"
        "  \"materials\": [{\"name\":\"Paint\",\"pbrMetallicRoughness\":{\"baseColorTexture\":{\"index\":0}}}],\n"
        "  \"meshes\": [{\"primitives\":[{\"attributes\":{\"POSITION\":0,\"NORMAL\":1,\"TEXCOORD_0\":2},"
        "\"indices\":3,\"mode\":4,\"material\":0}]}]\n"
        "}\n";
    WriteTextUVE(sourcePath, json);

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    ASSERT_NE(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);

    MaterialAssetUVE material;
    ASSERT_TRUE(LoadMaterialAssetUVE(materialPath, material));
    ASSERT_NE(material.albedoTexture, kInvalidAssetGuidUVE);
    EXPECT_EQ(database.ResolveUVE(material.albedoTexture), texturePath);
    TextureAssetUVE texture;
    ASSERT_TRUE(LoadTextureAssetUVE(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    // The source PNG stays where the author put it; the importer publishes the .uvtex beside the
    // model, which is where every texture reference in the project is expected to live.
    EXPECT_TRUE(std::filesystem::is_regular_file(textureSourcePath));
    RemoveFilesUVE({sourcePath, bufferPath, textureSourcePath, destinationPath, materialPath, texturePath});
}

TEST(GltfImporterUVETest, ImportUVE_TwoPrimitives_BecomeTwoSurfacesWithTheirOwnMaterials) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_two_primitives.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_two_primitives.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_two_primitives.uvmodel";
    const std::filesystem::path paintPath = "uve_gltf_importer_tests_two_primitives_paint.uvmat";
    const std::filesystem::path glassPath = "uve_gltf_importer_tests_two_primitives_glass.uvmat";
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath, paintPath, glassPath});
    WriteBytesUVE(bufferPath, MakeTwoPrimitiveBufferUVE());
    WriteTextUVE(sourcePath, MakeTwoPrimitiveGltfJsonUVE(bufferPath.filename().string()));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuidUVE);

    MeshAssetUVE mesh;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, mesh));
    // Both primitives are merged into one vertex/index buffer...
    ASSERT_EQ(mesh.vertices.size(), 6U);
    ASSERT_EQ(mesh.indices.size(), 6U);
    // ...and the split is preserved as two ranges that tile it, each naming its own material.
    ASSERT_EQ(mesh.surfaces.size(), 2U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurfaceUVE{0U, 3U, 0U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurfaceUVE{3U, 3U, 1U}));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_NE(mesh.materialGuids[0], kInvalidAssetGuidUVE);
    EXPECT_NE(mesh.materialGuids[1], kInvalidAssetGuidUVE);
    EXPECT_NE(mesh.materialGuids[0], mesh.materialGuids[1]);
    EXPECT_EQ(mesh.SurfaceMaterialGuidUVE(0U), mesh.materialGuids[0]);
    EXPECT_EQ(mesh.SurfaceMaterialGuidUVE(1U), mesh.materialGuids[1]);
    // The second primitive's indices are re-based into the merged vertex array, so its triangles
    // reference its own three vertices rather than the first primitive's.
    EXPECT_EQ(mesh.indices[3], 3U);
    EXPECT_EQ(mesh.indices[4], 4U);
    EXPECT_EQ(mesh.indices[5], 5U);
    RemoveFilesUVE({sourcePath, bufferPath, destinationPath, paintPath, glassPath});
}

TEST(GltfImporterUVETest, ImportUVE_InvalidSourcePreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_invalid.gltf";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_existing.uvmodel";
    RemoveFilesUVE({sourcePath, destinationPath});
    WriteTextUVE(sourcePath, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[]}");
    MeshAssetUVE original;
    original.vertices = {MeshVertexUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{0.0F, 1.0F, 0.0F}, 0.0F,
                                       0.0F}};
    original.indices = {0U};
    original.localBounds = Math::AabbUVE{Math::Vector3UVE{4.0F, 5.0F, 6.0F}, Math::Vector3UVE{4.0F, 5.0F, 6.0F}};
    ASSERT_TRUE(SaveMeshAssetUVE(original, destinationPath));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    const AssetGuidUVE guid = importer.ImportUVE(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuidUVE);
    EXPECT_TRUE(database.ResolveUVE(guid).empty());
    MeshAssetUVE retained;
    ASSERT_TRUE(LoadMeshAssetUVE(destinationPath, retained));
    EXPECT_EQ(retained.indices, original.indices);
    EXPECT_EQ(retained.localBounds, original.localBounds);
    RemoveFilesUVE({sourcePath, destinationPath});
}

TEST(GltfImporterUVETest, ImportUVE_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_wrong_destination.gltf";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_wrong_destination.uvmat";
    RemoveFilesUVE({sourcePath, destinationPath});
    WriteTextUVE(sourcePath, MakeGltfJsonUVE("missing.bin"));

    AssetImporterUVE importer;
    AssetDatabaseUVE database;
    EXPECT_EQ(importer.ImportUVE(sourcePath, destinationPath, database), kInvalidAssetGuidUVE);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    RemoveFilesUVE({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Asset::Tests
