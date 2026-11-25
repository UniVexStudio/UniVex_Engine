// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "gltf_metadata.h"
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include <gtest/gtest.h>
namespace UVE::Asset::Tests {
namespace {
void AppendU32LE(std::vector<std::byte>& bytes, const std::uint32_t value) {
    for (unsigned int shift = 0U; shift < 32U; shift += 8U) bytes.push_back(std::byte{static_cast<unsigned char>((value >> shift) & 0xFFU)});
}
TEST(GltfMetadataTest, ClassifyGltfResourceUri_AcceptsSafeRelativeAndDataUris) {
    EXPECT_EQ(ClassifyGltfResourceUri("textures/albedo.png"), GltfResourceUriKind::RelativePath);
    EXPECT_EQ(ClassifyGltfResourceUri("mesh\\lod0.bin"), GltfResourceUriKind::RelativePath);
    EXPECT_EQ(ClassifyGltfResourceUri("data:image/png;base64,AAAA"), GltfResourceUriKind::DataUri);
}

TEST(GltfMetadataTest, ClassifyGltfResourceUri_RejectsUnsafeAndUnboundedUris) {
    EXPECT_EQ(ClassifyGltfResourceUri(""), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("/textures/albedo.png"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("http://example.com/mesh.bin"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("textures/../mesh.bin"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("textures/%2e%2e/mesh.bin"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("textures//mesh.bin"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("textures/%ZZ/mesh.bin"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri("data:image/png;base64"), GltfResourceUriKind::Invalid);
    EXPECT_EQ(ClassifyGltfResourceUri(std::string("textures/mesh\0.bin", 18U)), GltfResourceUriKind::Invalid);
    const std::string oversized(kMaximumGltfResourceUriBytes + 1U, 'x');
    EXPECT_EQ(ClassifyGltfResourceUri(oversized), GltfResourceUriKind::Invalid);
}

TEST(GltfMetadataTest, DecodeGltfDataUri_DecodesPercentAndBase64Payloads) {
    std::vector<std::byte> decoded;
    ASSERT_TRUE(DecodeGltfDataUri("data:text/plain,hello%20world", decoded));
    const std::vector<std::byte> expectedText{std::byte{'h'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'},
                                               std::byte{'o'}, std::byte{' '}, std::byte{'w'}, std::byte{'o'},
                                               std::byte{'r'}, std::byte{'l'}, std::byte{'d'}};
    EXPECT_EQ(decoded, expectedText);
    ASSERT_TRUE(DecodeGltfDataUri("data:application/octet-stream;base64,SGVsbG8=", decoded));
    const std::vector<std::byte> expectedBase64{std::byte{'H'}, std::byte{'e'}, std::byte{'l'}, std::byte{'l'}, std::byte{'o'}};
    EXPECT_EQ(decoded, expectedBase64);
}

TEST(GltfMetadataTest, DecodeGltfDataUri_RejectsMalformedAndCappedPayloadsAtomically) {
    const std::vector<std::byte> original{std::byte{0xAA}, std::byte{0xBB}};
    std::vector<std::byte> decoded = original;
    EXPECT_FALSE(DecodeGltfDataUri("data:text/plain,hello%2", decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(DecodeGltfDataUri("data:application/octet-stream;base64,SGVsbG8", decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(DecodeGltfDataUri("data:application/octet-stream;base64,AB==", decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(DecodeGltfDataUri("data:application/octet-stream;base64,AAB=", decoded));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(DecodeGltfDataUri("data:text/plain,hello", decoded, 4U));
    EXPECT_EQ(decoded, original);
    EXPECT_FALSE(DecodeGltfDataUri("textures/mesh.bin", decoded));
    EXPECT_EQ(decoded, original);
    ASSERT_TRUE(DecodeGltfDataUri("data:,", decoded, 0U));
    EXPECT_TRUE(decoded.empty());
}

TEST(GltfMetadataTest, ResolveGltfResourceVirtualPath_ComposesBoundedVfsPaths) {
    std::string resolved = "unchanged";
    ASSERT_TRUE(ResolveGltfResourceVirtualPath("models/characters/hero.gltf", "textures/albedo.png", resolved));
    EXPECT_EQ(resolved, "models/characters/textures/albedo.png");
    ASSERT_TRUE(ResolveGltfResourceVirtualPath("scene.gltf", "mesh\\lod0.bin", resolved));
    EXPECT_EQ(resolved, "mesh/lod0.bin");
}

TEST(GltfMetadataTest, ResolveGltfResourceVirtualPath_RejectsInvalidInputsAtomically) {
    const std::string original = "stable/path.bin";
    std::string resolved = original;
    EXPECT_FALSE(ResolveGltfResourceVirtualPath("models/hero.gltf", "data:application/octet-stream,AAAA", resolved));
    EXPECT_EQ(resolved, original);
    EXPECT_FALSE(ResolveGltfResourceVirtualPath("models/../hero.gltf", "mesh.bin", resolved));
    EXPECT_EQ(resolved, original);
    EXPECT_FALSE(ResolveGltfResourceVirtualPath("/hero.gltf", "mesh.bin", resolved));
    EXPECT_EQ(resolved, original);
    const std::string oversizedCombined(kMaximumGltfResourceUriBytes - 6U, 'a');
    EXPECT_FALSE(ResolveGltfResourceVirtualPath("models/hero.gltf", oversizedCombined, resolved));
    EXPECT_EQ(resolved, original);
    const std::string oversizedResource(kMaximumGltfResourceUriBytes + 1U, 'b');
    EXPECT_FALSE(ResolveGltfResourceVirtualPath("hero.gltf", oversizedResource, resolved));
    EXPECT_EQ(resolved, original);
}

TEST(GltfMetadataTest, ValidateGltfAccessorSpan_AcceptsBoundedTightAndStridedSpans) {
    EXPECT_TRUE(ValidateGltfAccessorSpan(64U, 0U, 16U, 4U, 4U));
    EXPECT_TRUE(ValidateGltfAccessorSpan(64U, 4U, 4U, 12U, 8U));
    EXPECT_TRUE(ValidateGltfAccessorSpan(8U, 8U, 0U, 1U, 1U));
}

TEST(GltfMetadataTest, ValidateGltfAccessorSpan_RejectsInvalidAndOverflowingSpans) {
    EXPECT_FALSE(ValidateGltfAccessorSpan(64U, 65U, 1U, 4U, 4U));
    EXPECT_FALSE(ValidateGltfAccessorSpan(64U, 0U, 2U, 3U, 4U));
    EXPECT_FALSE(ValidateGltfAccessorSpan(64U, 60U, 2U, 4U, 4U));
    EXPECT_FALSE(ValidateGltfAccessorSpan(64U, 0U, 1'000'001U, 4U, 4U));
    EXPECT_TRUE(ValidateGltfAccessorSpan(64U, 0U, 2U, 4U, 4U, 2U));
    EXPECT_FALSE(ValidateGltfAccessorSpan(std::numeric_limits<std::uint64_t>::max(), 0U,
                                             std::numeric_limits<std::uint64_t>::max(),
                                             std::numeric_limits<std::uint64_t>::max(), 1U,
                                             std::numeric_limits<std::uint64_t>::max()));
}

TEST(GltfMetadataTest, ParseGltfMetadata_ReturnsCopiedJsonCounts) {
    const auto metadata = ParseGltfMetadata(R"({"asset":{"version":"2.0"},"nodes":[{}],"meshes":[{},{}],"materials":[{}],"images":[{}],"buffers":[{},{}]})");
    ASSERT_TRUE(metadata.has_value());
    EXPECT_EQ(metadata->container, GltfContainerKind::Json);
    EXPECT_EQ(metadata->objectCount, 1U); EXPECT_EQ(metadata->meshCount, 2U); EXPECT_EQ(metadata->materialCount, 1U);
    EXPECT_EQ(metadata->imageCount, 1U); EXPECT_EQ(metadata->bufferCount, 2U); EXPECT_FALSE(metadata->hasBinaryChunk);
    EXPECT_EQ(metadata->skinCount, 0U);
}
TEST(GltfMetadataTest, ParseGltfMetadata_CountsSkinsForRiggedModels) {
    const auto rigged = ParseGltfMetadata(R"({"asset":{"version":"2.0"},"meshes":[{}],"skins":[{"joints":[0]}]})");
    ASSERT_TRUE(rigged.has_value());
    EXPECT_EQ(rigged->skinCount, 1U);
    EXPECT_FALSE(ParseGltfMetadata(R"({"asset":{"version":"2.0"},"skins":{}})").has_value());
}
TEST(GltfMetadataTest, ParseGlbMetadata_ValidatesHeaderJsonChunkAndBinaryTail) {
    const std::string json = R"({"asset":{"version":"2.0"},"nodes":[{}],"meshes":[]})";
    std::vector<std::byte> bytes;
    AppendU32LE(bytes, 0x46546C67U); AppendU32LE(bytes, 2U);
    AppendU32LE(bytes, static_cast<std::uint32_t>(20U + json.size() + 8U));
    AppendU32LE(bytes, static_cast<std::uint32_t>(json.size())); AppendU32LE(bytes, 0x4E4F534AU);
    bytes.insert(bytes.end(), reinterpret_cast<const std::byte*>(json.data()), reinterpret_cast<const std::byte*>(json.data() + json.size()));
    AppendU32LE(bytes, 0U); AppendU32LE(bytes, 0x004E4942U);
    const auto metadata = ParseGlbMetadata(bytes);
    ASSERT_TRUE(metadata.has_value()); EXPECT_EQ(metadata->container, GltfContainerKind::Binary);
    EXPECT_EQ(metadata->objectCount, 1U); EXPECT_TRUE(metadata->hasBinaryChunk);
}
TEST(GltfMetadataTest, ParseGlbMetadata_RejectsTruncatedAndDuplicateBinaryChunks) {
    const std::string json = R"({"asset":{"version":"2.0"}})";

    std::vector<std::byte> truncated;
    AppendU32LE(truncated, 0x46546C67U); AppendU32LE(truncated, 2U);
    AppendU32LE(truncated, static_cast<std::uint32_t>(20U + json.size() + 8U + 2U));
    AppendU32LE(truncated, static_cast<std::uint32_t>(json.size())); AppendU32LE(truncated, 0x4E4F534AU);
    truncated.insert(truncated.end(), reinterpret_cast<const std::byte*>(json.data()),
                    reinterpret_cast<const std::byte*>(json.data() + json.size()));
    AppendU32LE(truncated, 4U); AppendU32LE(truncated, 0x004E4942U);
    truncated.insert(truncated.end(), {std::byte{0xAA}, std::byte{0xBB}});
    EXPECT_FALSE(ParseGlbMetadata(truncated).has_value());

    std::vector<std::byte> duplicate;
    AppendU32LE(duplicate, 0x46546C67U); AppendU32LE(duplicate, 2U);
    AppendU32LE(duplicate, static_cast<std::uint32_t>(20U + json.size() + 16U));
    AppendU32LE(duplicate, static_cast<std::uint32_t>(json.size())); AppendU32LE(duplicate, 0x4E4F534AU);
    duplicate.insert(duplicate.end(), reinterpret_cast<const std::byte*>(json.data()),
                     reinterpret_cast<const std::byte*>(json.data() + json.size()));
    AppendU32LE(duplicate, 0U); AppendU32LE(duplicate, 0x004E4942U);
    AppendU32LE(duplicate, 0U); AppendU32LE(duplicate, 0x004E4942U);
    EXPECT_FALSE(ParseGlbMetadata(duplicate).has_value());
}

TEST(GltfMetadataTest, ParseGltfMetadata_RejectsWrongVersionAndMalformedGlb) {
    EXPECT_FALSE(ParseGltfMetadata(R"({"asset":{"version":"1.0"}})").has_value());
    std::vector<std::byte> bad{std::byte{0}, std::byte{1}, std::byte{2}, std::byte{3}};
    EXPECT_FALSE(ParseGlbMetadata(bad).has_value());
}
} }
