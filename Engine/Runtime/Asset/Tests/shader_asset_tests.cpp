// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "shader_asset.h"

#include <algorithm>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "asset_database.h"
#include "asset_handle.h"
#include "asset_manager.h"
#include "uve_file_envelope.h"
#include "log_sink.h"
#include "logger.h"
#include "event_system.h"
#include "thread_pool.h"

namespace UVE::Asset::Tests {
namespace {

[[nodiscard]] ShaderAsset MakeTestShader() {
    ShaderAsset shader;
    shader.stage = ShaderStageKind::Fragment;
    shader.sourceCode = "void main() { }";
    shader.entryPointName = "main";
    return shader;
}

TEST(ShaderAssetTest, SaveThenLoad_RoundTripsFieldExact) {
    const std::filesystem::path path = "uve_shader_asset_tests_round_trip.uvshader";
    std::filesystem::remove(path);
    const ShaderAsset original = MakeTestShader();
    ASSERT_TRUE(SaveShaderAsset(original, path));

    ShaderAsset loaded;
    ASSERT_TRUE(LoadShaderAsset(path, loaded));

    EXPECT_EQ(loaded.stage, original.stage);
    EXPECT_EQ(loaded.sourceCode, original.sourceCode);
    EXPECT_EQ(loaded.entryPointName, original.entryPointName);

    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, LoadShaderAsset_WrongAssetKind_FailsCleanlyAndLogsError) {
    const std::filesystem::path path = "uve_shader_asset_tests_wrong_kind.uvblob";
    std::filesystem::remove(path);
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Blob, {}));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ShaderAsset shader;
    EXPECT_FALSE(LoadShaderAsset(path, shader));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("not a shader file");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, LoadShaderAsset_MissingFile_ReturnsFalse) {
    const std::filesystem::path path = "uve_shader_asset_tests_nonexistent.uvshader";
    std::filesystem::remove(path);

    ShaderAsset shader;
    EXPECT_FALSE(LoadShaderAsset(path, shader));
}

TEST(ShaderAssetTest, LoadShaderAsset_EmptySourceCode_FailsAndLogsError) {
    const std::filesystem::path path = "uve_shader_asset_tests_empty_source.uvshader";
    std::filesystem::remove(path);

    ShaderAsset invalidShader = MakeTestShader();
    invalidShader.sourceCode.clear();
    ASSERT_TRUE(SaveShaderAsset(invalidShader, path));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ShaderAsset loaded;
    EXPECT_FALSE(LoadShaderAsset(path, loaded));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("empty source code");
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, LoadShaderAsset_UnknownStageFailsBeforePublication) {
    const std::filesystem::path path = "uve_shader_asset_tests_unknown_stage.uvshader";
    std::filesystem::remove(path);
    const std::string invalidPayload =
        R"({"stage":255,"sourceCode":"void main() { }","entryPointName":"main"})";
    const auto* const payloadBytes = reinterpret_cast<const std::byte*>(invalidPayload.data());
    ASSERT_TRUE(WriteUveFile(path, AssetKind::Shader,
                                std::vector<std::byte>(payloadBytes, payloadBytes + invalidPayload.size())));

    ShaderAsset loaded = MakeTestShader();
    const ShaderAsset original = loaded;
    EXPECT_FALSE(LoadShaderAsset(path, loaded));
    EXPECT_EQ(loaded.stage, original.stage);
    EXPECT_EQ(loaded.sourceCode, original.sourceCode);
    EXPECT_EQ(loaded.entryPointName, original.entryPointName);
    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, SaveShaderAsset_UnknownStagePreservesExistingEnvelope) {
    const std::filesystem::path path = "uve_shader_asset_tests_unknown_stage_save.uvshader";
    std::filesystem::remove(path);
    const ShaderAsset original = MakeTestShader();
    ASSERT_TRUE(SaveShaderAsset(original, path));

    ShaderAsset invalid = original;
    invalid.stage = static_cast<ShaderStageKind>(0xFFU);
    EXPECT_FALSE(SaveShaderAsset(invalid, path));

    ShaderAsset loaded;
    ASSERT_TRUE(LoadShaderAsset(path, loaded));
    EXPECT_EQ(loaded.stage, original.stage);
    EXPECT_EQ(loaded.sourceCode, original.sourceCode);
    EXPECT_EQ(loaded.entryPointName, original.entryPointName);
    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, LoadShaderAsset_MalformedJson_FailsAndLogsError) {
    const std::filesystem::path path = "uve_shader_asset_tests_malformed_json.uvshader";
    std::filesystem::remove(path);
    const std::string garbage = "{ not valid json";
    const auto* const garbageBytes = reinterpret_cast<const std::byte*>(garbage.data());
    ASSERT_TRUE(
        WriteUveFile(path, AssetKind::Shader, std::vector<std::byte>(garbageBytes, garbageBytes + garbage.size())));

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ShaderAsset shader;
    EXPECT_FALSE(LoadShaderAsset(path, shader));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error;
        });
    EXPECT_TRUE(foundError);

    logger.Shutdown();
    std::filesystem::remove(path);
}

TEST(ShaderAssetTest, EndToEnd_RegisterLoaderThenLoad_ReachesLoadedWithMatchingData) {
    const std::filesystem::path path = "uve_shader_asset_tests_end_to_end.uvshader";
    std::filesystem::remove(path);
    const ShaderAsset original = MakeTestShader();
    ASSERT_TRUE(SaveShaderAsset(original, path));

    Threading::ThreadPool threadPool(2);
    Events::EventSystem eventSystem;
    AssetDatabase assetDatabase;
    AssetManager assetManager(threadPool, eventSystem);
    assetManager.RegisterLoader<ShaderAsset>(&LoadShaderAsset);

    const AssetGuid guid = assetDatabase.Register(path);
    const AssetHandle<ShaderAsset> handle = assetManager.Load<ShaderAsset>(guid, assetDatabase);

    bool ready = false;
    for (int iteration = 0; iteration < 200000 && !ready; ++iteration) {
        ready = handle.IsReady() || handle.HasFailed();
        if (!ready) {
            std::this_thread::yield();
        }
    }
    ASSERT_TRUE(ready);
    ASSERT_TRUE(handle.IsReady());
    const ShaderAsset* const loaded = handle.TryGet();
    ASSERT_NE(loaded, nullptr);
    EXPECT_EQ(loaded->sourceCode, original.sourceCode);

    std::filesystem::remove(path);
}

} // namespace
} // namespace UVE::Asset::Tests
