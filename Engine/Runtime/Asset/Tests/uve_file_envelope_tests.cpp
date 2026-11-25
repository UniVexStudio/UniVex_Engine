// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve_file_envelope.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] std::vector<std::byte> MakePayload(std::string_view text) {
    const auto* const bytes = reinterpret_cast<const std::byte*>(text.data());
    return std::vector<std::byte>(bytes, bytes + text.size());
}

TEST(UveFileEnvelopeTest, IsUveFilePayloadSizeValid_EnforcesMaximumWithoutAllocation) {
    EXPECT_TRUE(IsUveFilePayloadSizeValid(kMaximumUveFilePayloadBytes));
    EXPECT_FALSE(IsUveFilePayloadSizeValid(kMaximumUveFilePayloadBytes + 1U));
    EXPECT_FALSE(IsUveFilePayloadSizeValid(std::numeric_limits<std::size_t>::max()));
}

TEST(UveFileEnvelopeTest, WriteThenRead_RoundTripsPayloadAndAssetKind) {
    const std::filesystem::path path = "uve_file_envelope_tests_roundtrip.uvblob";
    std::filesystem::remove(path);

    const std::vector<std::byte> payload = MakePayload("hello universe");
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, payload));

    const auto result = ReadUveFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.assetType, AssetKind::Blob);
    EXPECT_EQ(result->first.compressionMethod, 0U);
    EXPECT_EQ(result->second, payload);

    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, WriteThenRead_EmptyPayload_RoundTrips) {
    const std::filesystem::path path = "uve_file_envelope_tests_empty.uvblob";
    std::filesystem::remove(path);

    ASSERT_TRUE(WriteUveFile(path, AssetKind::Bundle, {}));
    const auto result = ReadUveFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.assetType, AssetKind::Bundle);
    EXPECT_TRUE(result->second.empty());

    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, WriteThenRead_SaveAssetKind_RoundTrips) {
    const std::filesystem::path path = "uve_file_envelope_tests_save.uvsave";
    std::filesystem::remove(path);

    const std::vector<std::byte> payload = MakePayload("save payload bytes");
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Save, payload));

    const auto result = ReadUveFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.assetType, AssetKind::Save);
    EXPECT_EQ(result->second, payload);

    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, ReadUveFile_MissingFile_ReturnsNulloptAndLogsError) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const auto result = ReadUveFile("uve_file_envelope_tests_nonexistent.uvblob");
    EXPECT_FALSE(result.has_value());

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
}

TEST(UveFileEnvelopeTest, ReadUveFile_BadMagic_ReturnsNulloptAndLogsError) {
    const std::filesystem::path path = "uve_file_envelope_tests_bad_magic.uvblob";
    {
        std::ofstream file(path, std::ios::binary);
        file << "NOT A VALID UVE FILE";
    }

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const auto result = ReadUveFile(path);
    EXPECT_FALSE(result.has_value());

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("bad magic");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, WriteUveFile_InvalidAssetKindPreservesExistingDestination) {
    const std::filesystem::path path = "uve_file_envelope_tests_invalid_write_kind.uvblob";
    std::filesystem::remove(path);
    const std::vector<std::byte> originalPayload = MakePayload("original");
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, originalPayload));

    EXPECT_FALSE(WriteUveFile(path, static_cast<AssetKind>(0xFFFFFFFFU), MakePayload("replacement")));
    const auto result = ReadUveFile(path);
    ASSERT_TRUE(result.has_value());
    EXPECT_EQ(result->first.assetType, AssetKind::Blob);
    EXPECT_EQ(result->second, originalPayload);
    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, ReadUveFile_InvalidAssetKind_ReturnsNullopt) {
    const std::filesystem::path path = "uve_file_envelope_tests_bad_asset_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("data")));

    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        const std::uint32_t invalidAssetKind = 0xFFFFFFFFU;
        file.seekp(8);
        file.write(reinterpret_cast<const char*>(&invalidAssetKind), sizeof(invalidAssetKind));
    }

    EXPECT_FALSE(ReadUveFile(path).has_value());
    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, ReadUveFile_UnsupportedCompression_ReturnsNulloptAndLogsError) {
    const std::filesystem::path path = "uve_file_envelope_tests_bad_compression.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, MakePayload("data")));

    // Corrupt the compressionMethod field (bytes [12, 16) - magic[4] + version[4] + assetType[4])
    // to a never-implemented value, simulating a future/unsupported file.
    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        const std::uint32_t badCompression = 99;
        file.seekp(12);
        file.write(reinterpret_cast<const char*>(&badCompression), sizeof(badCompression));
    }

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    const auto result = ReadUveFile(path);
    EXPECT_FALSE(result.has_value());

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("compression");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(UveFileEnvelopeTest, ReadUveFile_OversizedDeclaredPayload_ReturnsNulloptWithoutAllocating) {
    const std::filesystem::path path = "uve_file_envelope_tests_oversized_payload.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));

    {
        std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
        ASSERT_TRUE(file.is_open());
        const std::uint64_t oversizedPayload = std::numeric_limits<std::uint64_t>::max();
        file.seekp(16);
        file.write(reinterpret_cast<const char*>(&oversizedPayload), sizeof(oversizedPayload));
    }

    EXPECT_FALSE(ReadUveFile(path).has_value());
    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
