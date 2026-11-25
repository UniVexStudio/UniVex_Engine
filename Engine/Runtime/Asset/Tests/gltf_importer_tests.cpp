// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "asset_importer.h"

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

#include "Support/test_image_fixtures.h"

#include "asset_database.h"
#include "asset_import_sidecar.h"
#include "material_asset.h"
#include "mesh_asset.h"
#include "texture_asset.h"

namespace UVE::Asset::Tests {
namespace {

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
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    AppendU32LittleEndian(bytes, bits);
}

void AppendVector3(std::vector<std::byte>& bytes, const float x, const float y, const float z) {
    AppendFloatLittleEndian(bytes, x);
    AppendFloatLittleEndian(bytes, y);
    AppendFloatLittleEndian(bytes, z);
}

[[nodiscard]] std::vector<std::byte> MakeTriangleBuffer() {
    std::vector<std::byte> bytes;
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        AppendFloatLittleEndian(bytes, value);
    }
    AppendU16LittleEndian(bytes, 0U);
    AppendU16LittleEndian(bytes, 1U);
    AppendU16LittleEndian(bytes, 2U);
    return bytes;
}

[[nodiscard]] std::string MakeGltfJson(const std::string_view uri) {
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
[[nodiscard]] std::vector<std::byte> MakeTriangleWithMorphTargetsBuffer() {
    std::vector<std::byte> bytes;
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 1.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 1.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    for (const float value : {0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 1.0F}) {
        AppendFloatLittleEndian(bytes, value);
    }
    AppendU16LittleEndian(bytes, 0U);
    AppendU16LittleEndian(bytes, 1U);
    AppendU16LittleEndian(bytes, 2U);
    bytes.push_back(std::byte{0U});
    bytes.push_back(std::byte{0U});
    // "jawOpen": only the third vertex moves.
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, -0.5F, 0.0F);
    // "blink": no vertex moves at all.
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    AppendVector3(bytes, 0.0F, 0.0F, 0.0F);
    return bytes;
}

[[nodiscard]] std::string MakeGltfJsonWithMorphTargets(const std::string_view uri) {
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

/// Two triangles, each its own glTF primitive with its own accessors and material - the shape a
/// DCC tool exports for a model whose paint and glass are different material slots.
/// Layout: [0,36) positions 0  [36,72) positions 1  [72,108) normals 0  [108,144) normals 1
///         [144,168) uvs 0      [168,192) uvs 1      [192,198) indices 0  [198,204) indices 1
[[nodiscard]] std::vector<std::byte> MakeTwoPrimitiveBuffer() {
    std::vector<std::byte> bytes;
    for (const float value : {0.0F, 0.0F, 0.0F, 1.0F, 0.0F, 0.0F, 0.0F, 1.0F, 0.0F,
                              2.0F, 0.0F, 0.0F, 3.0F, 0.0F, 0.0F, 2.0F, 1.0F, 0.0F}) {
        AppendFloatLittleEndian(bytes, value);
    }
    for (int vertex = 0; vertex < 6; ++vertex) {
        AppendVector3(bytes, 0.0F, 0.0F, 1.0F);
    }
    for (int vertex = 0; vertex < 6; ++vertex) {
        AppendFloatLittleEndian(bytes, vertex % 2 == 0 ? 0.0F : 1.0F);
        AppendFloatLittleEndian(bytes, 0.0F);
    }
    for (const std::uint16_t index : {std::uint16_t{0U}, std::uint16_t{1U}, std::uint16_t{2U},
                                      std::uint16_t{0U}, std::uint16_t{1U}, std::uint16_t{2U}}) {
        AppendU16LittleEndian(bytes, index);
    }
    return bytes;
}

[[nodiscard]] std::string MakeTwoPrimitiveGltfJson(const std::string_view uri) {
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{\"uri\":\"" + std::string(uri) + "\",\"byteLength\": 204}],\n"
           "  \"bufferViews\": [\n"
           "    {\"buffer\":0,\"byteOffset\":0,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":36,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":72,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":108,\"byteLength\":36},\n"
           "    {\"buffer\":0,\"byteOffset\":144,\"byteLength\":24},\n"
           "    {\"buffer\":0,\"byteOffset\":168,\"byteLength\":24},\n"
           "    {\"buffer\":0,\"byteOffset\":192,\"byteLength\":6},\n"
           "    {\"buffer\":0,\"byteOffset\":198,\"byteLength\":6}\n"
           "  ],\n"
           "  \"accessors\": [\n"
           "    {\"bufferView\":0,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[0,0,0],\"max\":[1,1,0]},\n"
           "    {\"bufferView\":1,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\",\"min\":[2,0,0],\"max\":[3,1,0]},\n"
           "    {\"bufferView\":2,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":3,\"componentType\":5126,\"count\":3,\"type\":\"VEC3\"},\n"
           "    {\"bufferView\":4,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":5,\"componentType\":5126,\"count\":3,\"type\":\"VEC2\"},\n"
           "    {\"bufferView\":6,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"},\n"
           "    {\"bufferView\":7,\"componentType\":5123,\"count\":3,\"type\":\"SCALAR\"}\n"
           "  ],\n"
           "  \"materials\": [\n"
           "    {\"name\":\"paint\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[1,0,0,1]}},\n"
           "    {\"name\":\"glass\",\"pbrMetallicRoughness\":{\"baseColorFactor\":[0,0,1,1]}}\n"
           "  ],\n"
           "  \"meshes\": [{\"primitives\":[\n"
           "    {\"attributes\":{\"POSITION\":0,\"NORMAL\":2,\"TEXCOORD_0\":4},\"indices\":6,\"mode\":4,\"material\":0},\n"
           "    {\"attributes\":{\"POSITION\":1,\"NORMAL\":3,\"TEXCOORD_0\":5},\"indices\":7,\"mode\":4,\"material\":1}\n"
           "  ]}]\n"
           "}\n";
}

void WriteBytes(const std::filesystem::path& path, const std::vector<std::byte>& bytes) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    if (!bytes.empty()) {
        output.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    }
    ASSERT_TRUE(output.good());
}

void WriteText(const std::filesystem::path& path, const std::string_view text) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    ASSERT_TRUE(output.is_open());
    output.write(text.data(), static_cast<std::streamsize>(text.size()));
    ASSERT_TRUE(output.good());
}

void WriteGlb(const std::filesystem::path& path, const std::string& json,
                 const std::vector<std::byte>& binary) {
    std::vector<std::byte> bytes;
    std::vector<std::byte> jsonBytes(reinterpret_cast<const std::byte*>(json.data()),
                                     reinterpret_cast<const std::byte*>(json.data() + json.size()));
    while (jsonBytes.size() % 4U != 0U) jsonBytes.push_back(std::byte{' '});
    AppendU32LittleEndian(bytes, 0x46546C67U);
    AppendU32LittleEndian(bytes, 2U);
    AppendU32LittleEndian(bytes, static_cast<std::uint32_t>(12U + 8U + jsonBytes.size() + 8U + binary.size()));
    AppendU32LittleEndian(bytes, static_cast<std::uint32_t>(jsonBytes.size()));
    AppendU32LittleEndian(bytes, 0x4E4F534AU);
    bytes.insert(bytes.end(), jsonBytes.begin(), jsonBytes.end());
    AppendU32LittleEndian(bytes, static_cast<std::uint32_t>(binary.size()));
    AppendU32LittleEndian(bytes, 0x004E4942U);
    bytes.insert(bytes.end(), binary.begin(), binary.end());
    WriteBytes(path, bytes);
}

void RemoveFiles(const std::initializer_list<std::filesystem::path>& paths) {
    for (const auto& path : paths) std::filesystem::remove(path);
}

TEST(GltfImporterTest, Import_ValidExternalBufferPublishesUveModelAndRegistersGuid) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_triangle.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_triangle.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_triangle.uvmodel";
    RemoveFiles({sourcePath, bufferPath, destinationPath});
    const auto binary = MakeTriangleBuffer();
    WriteBytes(bufferPath, binary);
    WriteText(sourcePath, MakeGltfJson(bufferPath.filename().string()));

    AssetImporter importer;
    AssetDatabase database;
    const auto classification = importer.ClassifySource(sourcePath);
    EXPECT_EQ(classification.kind, AssetImportSourceKind::RawModel);
    EXPECT_TRUE(classification.importerRegistered);
    EXPECT_TRUE(classification.requiresFormatSpecificParser);
    EXPECT_EQ(classification.diagnostic, "format-specific parser is registered");
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(guid), destinationPath);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.vertices.size(), 3U);
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    EXPECT_EQ(mesh.vertices[0].normal, (Math::Vector3{0.0F, 0.0F, 1.0F}));
    EXPECT_EQ(mesh.localBounds,
              (Math::Aabb{Math::Vector3{0.0F, 0.0F, 0.0F}, Math::Vector3{1.0F, 1.0F, 0.0F}}));
    RemoveFiles({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterTest, Import_ImportsMorphTargetsNamesAndDefaultWeights) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_morph.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_morph.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_morph.uvmodel";
    RemoveFiles({sourcePath, bufferPath, destinationPath});
    WriteBytes(bufferPath, MakeTriangleWithMorphTargetsBuffer());
    WriteText(sourcePath, MakeGltfJsonWithMorphTargets(bufferPath.filename().string()));

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.morphTargets.size(), 2U);
    EXPECT_EQ(mesh.morphTargets[0].name, "jawOpen");
    EXPECT_EQ(mesh.morphTargets[1].name, "blink");
    // "jawOpen" keeps only the one vertex it actually moves; "blink" moves nothing and keeps an
    // empty delta list so its weight slot keeps its index.
    ASSERT_EQ(mesh.morphTargets[0].deltas.size(), 1U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].vertexIndex, 2U);
    EXPECT_EQ(mesh.morphTargets[0].deltas[0].positionDelta, (Math::Vector3{0.0F, -0.5F, 0.0F}));
    EXPECT_TRUE(mesh.morphTargets[1].deltas.empty());
    EXPECT_EQ(mesh.defaultMorphWeights, (std::vector<float>{0.5F, 0.0F}));

    // And the weights actually drive the geometry through the eval path the runtime uses.
    std::vector<MeshVertex> morphed;
    const std::array<float, 2U> weights{1.0F, 0.0F};
    ASSERT_TRUE(AccumulateMorphTargets(mesh.vertices, mesh.morphTargets, weights, morphed));
    EXPECT_EQ(morphed[2].position, (Math::Vector3{0.0F, 0.5F, 0.0F}));
    RemoveFiles({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterTest, Import_MorphTargetImportOffSkipsShapesEntirely) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_morph_off.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_morph_off.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_morph_off.uvmodel";
    RemoveFiles({sourcePath, bufferPath, destinationPath});
    WriteBytes(bufferPath, MakeTriangleWithMorphTargetsBuffer());
    WriteText(sourcePath, MakeGltfJsonWithMorphTargets(bufferPath.filename().string()));

    MeshImportSettings settings;
    settings.importMorphTargets = false;
    AssetImporter importer;
    AssetDatabase database;
    ASSERT_NE(importer.Import(sourcePath, destinationPath, database, settings), kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    // The setting is honoured, not merely recorded: the shapes are absent from the asset, so the
    // base mesh is all anyone downstream can see.
    EXPECT_TRUE(mesh.morphTargets.empty());
    EXPECT_TRUE(mesh.defaultMorphWeights.empty());
    EXPECT_EQ(mesh.vertices.size(), 3U);
    RemoveFiles({sourcePath, bufferPath, destinationPath});
}

TEST(GltfImporterTest, Import_ValidGlbPublishesUveModelAndRegistersBothExtensions) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_triangle.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_triangle_glb.uvmodel";
    RemoveFiles({sourcePath, destinationPath});
    const auto binary = MakeTriangleBuffer();
    WriteGlb(sourcePath, MakeGltfJson(""), binary);

    AssetImporter importer;
    AssetDatabase database;
    const auto glbClassification = importer.ClassifySource(sourcePath);
    EXPECT_TRUE(glbClassification.importerRegistered);
    EXPECT_EQ(glbClassification.diagnostic, "format-specific parser is registered");
    const auto gltfClassification = importer.ClassifySource("fixture.gltf");
    EXPECT_TRUE(gltfClassification.importerRegistered);
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    ASSERT_NE(guid, kInvalidAssetGuid);
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    EXPECT_EQ(mesh.indices, (std::vector<std::uint32_t>{0U, 1U, 2U}));
    RemoveFiles({sourcePath, destinationPath});
}

/// The triangle layout every fixture in this file uses, followed by an image blob at a 4-byte
/// boundary: 102 bytes of geometry, two pad bytes, then whatever the test appends.
constexpr std::size_t kGeometryBytes = 104U;

[[nodiscard]] std::string MakeGltfJsonWithMaterial(const std::string_view bufferUri,
                                                     const std::size_t imageByteLength) {
    const std::string bufferUriJson =
        bufferUri.empty() ? std::string{} : "\"uri\":\"" + std::string(bufferUri) + "\",\n    ";
    return "{\n"
           "  \"asset\": {\"version\": \"2.0\"},\n"
           "  \"buffers\": [{" + bufferUriJson + "\"byteLength\": " +
           std::to_string(kGeometryBytes + imageByteLength) +
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
[[nodiscard]] std::vector<std::byte> MakePaddedTriangleBuffer() {
    std::vector<std::byte> bytes = MakeTriangleBuffer();
    while (bytes.size() < kGeometryBytes) {
        bytes.push_back(std::byte{0U});
    }
    return bytes;
}

TEST(GltfImporterTest, Import_GlbWithBakedMaterial_PublishesMaterialAndTextureAsAssets) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_material.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_material.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_material.Brick.uvmat";
    const std::filesystem::path texturePath = "uve_gltf_importer_tests_material.brick.uvtex";
    RemoveFiles({sourcePath, destinationPath, materialPath, texturePath});

    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    ASSERT_FALSE(png.empty());
    std::vector<std::byte> binary = MakePaddedTriangleBuffer();
    binary.insert(binary.end(), png.begin(), png.end());
    WriteGlb(sourcePath, MakeGltfJsonWithMaterial("", png.size()), binary);

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuid);

    // The mesh references the material the file carried, so a load of the model alone is enough:
    // nothing has to be assigned by hand before it renders textured.
    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    EXPECT_EQ(database.Resolve(mesh.materialGuids[0]), materialPath);

    MaterialAsset material;
    ASSERT_TRUE(LoadMaterialAsset(materialPath, material));
    EXPECT_FLOAT_EQ(material.albedoColor.r, 0.8F);
    EXPECT_FLOAT_EQ(material.albedoColor.g, 0.5F);
    EXPECT_FLOAT_EQ(material.albedoColor.b, 0.25F);
    EXPECT_FLOAT_EQ(material.albedoAlpha, 1.0F);
    EXPECT_FLOAT_EQ(material.metallic, 0.25F);
    EXPECT_FLOAT_EQ(material.roughness, 0.75F);
    EXPECT_FALSE(material.isTransparent);

    // The embedded image became a real texture asset whose pixels survive the round trip.
    ASSERT_NE(material.albedoTexture, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(material.albedoTexture), texturePath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    EXPECT_EQ(texture.height, 1U);
    EXPECT_EQ(texture.colorSpace, TextureAssetColorSpace::Srgb);
    ASSERT_GE(texture.pixels.size(), 8U);
    EXPECT_EQ(texture.pixels[0], std::byte{0xFF});
    EXPECT_EQ(texture.pixels[3], std::byte{0xFF});

    RemoveFiles({sourcePath, destinationPath, materialPath, texturePath});
}

TEST(GltfImporterTest, Import_MaterialWithUndecodableImage_StillPublishesGeometryAndMaterial) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_bad_image.glb";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_bad_image.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_bad_image.Brick.uvmat";
    RemoveFiles({sourcePath, destinationPath, materialPath});

    // The view points at geometry bytes, which are not an image at all. A texture that cannot be
    // produced must not cost the author the mesh, the material factors or the PBR numbers.
    std::vector<std::byte> binary = MakePaddedTriangleBuffer();
    binary.insert(binary.end(), 16U, std::byte{0x5A});
    WriteGlb(sourcePath, MakeGltfJsonWithMaterial("", 16U), binary);

    AssetImporter importer;
    AssetDatabase database;
    ASSERT_NE(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    ASSERT_EQ(mesh.materialGuids.size(), 1U);
    MaterialAsset material;
    ASSERT_TRUE(LoadMaterialAsset(materialPath, material));
    EXPECT_EQ(material.albedoTexture, kInvalidAssetGuid);
    EXPECT_FLOAT_EQ(material.metallic, 0.25F);
    RemoveFiles({sourcePath, destinationPath, materialPath});
}

TEST(GltfImporterTest, Import_MaterialSamplingExternalPng_ImportsThatTextureToo) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_external_texture.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_external_texture.bin";
    const std::filesystem::path textureSourcePath = "uve_gltf_importer_tests_paint.png";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_external_texture.uvmodel";
    const std::filesystem::path materialPath = "uve_gltf_importer_tests_external_texture.Paint.uvmat";
    const std::filesystem::path texturePath = "uve_gltf_importer_tests_paint.uvtex";
    RemoveFiles({sourcePath, bufferPath, textureSourcePath, destinationPath, materialPath, texturePath});

    const std::vector<std::byte> png = UVE::Tests::MakePngTwoByOneRgba8();
    ASSERT_FALSE(png.empty());
    WriteBytes(textureSourcePath, png);
    WriteBytes(bufferPath, MakeTriangleBuffer());
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
    WriteText(sourcePath, json);

    AssetImporter importer;
    AssetDatabase database;
    ASSERT_NE(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);

    MaterialAsset material;
    ASSERT_TRUE(LoadMaterialAsset(materialPath, material));
    ASSERT_NE(material.albedoTexture, kInvalidAssetGuid);
    EXPECT_EQ(database.Resolve(material.albedoTexture), texturePath);
    TextureAsset texture;
    ASSERT_TRUE(LoadTextureAsset(texturePath, texture));
    EXPECT_EQ(texture.width, 2U);
    // The source PNG stays where the author put it; the importer publishes the .uvtex beside the
    // model, which is where every texture reference in the project is expected to live.
    EXPECT_TRUE(std::filesystem::is_regular_file(textureSourcePath));
    RemoveFiles({sourcePath, bufferPath, textureSourcePath, destinationPath, materialPath, texturePath});
}

TEST(GltfImporterTest, Import_TwoPrimitives_BecomeTwoSurfacesWithTheirOwnMaterials) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_two_primitives.gltf";
    const std::filesystem::path bufferPath = "uve_gltf_importer_tests_two_primitives.bin";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_two_primitives.uvmodel";
    const std::filesystem::path paintPath = "uve_gltf_importer_tests_two_primitives_paint.uvmat";
    const std::filesystem::path glassPath = "uve_gltf_importer_tests_two_primitives_glass.uvmat";
    RemoveFiles({sourcePath, bufferPath, destinationPath, paintPath, glassPath});
    WriteBytes(bufferPath, MakeTwoPrimitiveBuffer());
    WriteText(sourcePath, MakeTwoPrimitiveGltfJson(bufferPath.filename().string()));

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);
    ASSERT_NE(guid, kInvalidAssetGuid);

    MeshAsset mesh;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, mesh));
    // Both primitives are merged into one vertex/index buffer...
    ASSERT_EQ(mesh.vertices.size(), 6U);
    ASSERT_EQ(mesh.indices.size(), 6U);
    // ...and the split is preserved as two ranges that tile it, each naming its own material.
    ASSERT_EQ(mesh.surfaces.size(), 2U);
    EXPECT_EQ(mesh.surfaces[0], (MeshSurface{0U, 3U, 0U}));
    EXPECT_EQ(mesh.surfaces[1], (MeshSurface{3U, 3U, 1U}));
    ASSERT_EQ(mesh.materialGuids.size(), 2U);
    EXPECT_NE(mesh.materialGuids[0], kInvalidAssetGuid);
    EXPECT_NE(mesh.materialGuids[1], kInvalidAssetGuid);
    EXPECT_NE(mesh.materialGuids[0], mesh.materialGuids[1]);
    EXPECT_EQ(mesh.SurfaceMaterialGuid(0U), mesh.materialGuids[0]);
    EXPECT_EQ(mesh.SurfaceMaterialGuid(1U), mesh.materialGuids[1]);
    // The second primitive's indices are re-based into the merged vertex array, so its triangles
    // reference its own three vertices rather than the first primitive's.
    EXPECT_EQ(mesh.indices[3], 3U);
    EXPECT_EQ(mesh.indices[4], 4U);
    EXPECT_EQ(mesh.indices[5], 5U);
    RemoveFiles({sourcePath, bufferPath, destinationPath, paintPath, glassPath});
}

TEST(GltfImporterTest, Import_InvalidSourcePreservesExistingDestinationAndDoesNotRegister) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_invalid.gltf";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_existing.uvmodel";
    RemoveFiles({sourcePath, destinationPath});
    WriteText(sourcePath, "{\"asset\":{\"version\":\"2.0\"},\"buffers\":[]}");
    MeshAsset original;
    original.vertices = {MeshVertex{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{0.0F, 1.0F, 0.0F}, 0.0F,
                                       0.0F}};
    original.indices = {0U};
    original.localBounds = Math::Aabb{Math::Vector3{4.0F, 5.0F, 6.0F}, Math::Vector3{4.0F, 5.0F, 6.0F}};
    ASSERT_TRUE(SaveMeshAsset(original, destinationPath));

    AssetImporter importer;
    AssetDatabase database;
    const AssetGuid guid = importer.Import(sourcePath, destinationPath, database);

    EXPECT_EQ(guid, kInvalidAssetGuid);
    EXPECT_TRUE(database.Resolve(guid).empty());
    MeshAsset retained;
    ASSERT_TRUE(LoadMeshAsset(destinationPath, retained));
    EXPECT_EQ(retained.indices, original.indices);
    EXPECT_EQ(retained.localBounds, original.localBounds);
    RemoveFiles({sourcePath, destinationPath});
}

TEST(GltfImporterTest, Import_WrongDestinationExtensionFailsBeforePublish) {
    const std::filesystem::path sourcePath = "uve_gltf_importer_tests_wrong_destination.gltf";
    const std::filesystem::path destinationPath = "uve_gltf_importer_tests_wrong_destination.uvmat";
    RemoveFiles({sourcePath, destinationPath});
    WriteText(sourcePath, MakeGltfJson("missing.bin"));

    AssetImporter importer;
    AssetDatabase database;
    EXPECT_EQ(importer.Import(sourcePath, destinationPath, database), kInvalidAssetGuid);
    EXPECT_FALSE(std::filesystem::exists(destinationPath));
    RemoveFiles({sourcePath, destinationPath});
}

} // namespace
} // namespace UVE::Asset::Tests
