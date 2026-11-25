// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "config_manager.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "log_sink.h"
#include "logger.h"

namespace UVE::Config::Tests {
namespace {

/// Writes `contents` to `path`, overwriting any existing file. Used to set
/// up JSON fixture files for Load() tests.
void WriteFixtureFile(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

TEST(ConfigManagerTest, Load_NonexistentPath_ReturnsFalseAndStartsWithAnEmptyDocument) {
    const std::filesystem::path missingPath = "uve_config_tests_nonexistent.uvsettings";
    std::filesystem::remove(missingPath);
    ConfigManager config;
    config.SetInt("window.width", 1600);
    EXPECT_FALSE(config.Load(missingPath));
    EXPECT_EQ(config.GetString("editor.theme", "light"), "light");
    EXPECT_EQ(config.GetInt("window.width", 1280), 1280);
    std::filesystem::remove(missingPath);
}

TEST(ConfigManagerTest, Load_ValidFixture_ReadsAllFourScalarTypesAcrossNestedPaths) {
    const std::filesystem::path fixturePath = "uve_config_tests_valid.uvsettings";
    WriteFixtureFile(fixturePath, R"({
        "version": 1,
        "editor": {
            "theme": "dark"
        },
        "window": {
            "width": 1920,
            "scale": 1.5
        },
        "server": {
            "enabled": true
        }
    })");

    ConfigManager config;
    ASSERT_TRUE(config.Load(fixturePath));

    EXPECT_EQ(config.GetString("editor.theme", "light"), "dark");
    EXPECT_EQ(config.GetInt("window.width", 0), 1920);
    EXPECT_DOUBLE_EQ(config.GetDouble("window.scale", 0.0), 1.5);
    EXPECT_TRUE(config.GetBool("server.enabled", false));
    EXPECT_EQ(config.GetInt("version", 0), 1);

    std::filesystem::remove(fixturePath);
}

TEST(ConfigManagerTest, GetX_MissingKey_ReturnsDefault) {
    ConfigManager config;
    EXPECT_EQ(config.GetString("does.not.exist", "fallback"), "fallback");
    EXPECT_EQ(config.GetInt("does.not.exist", 7), 7);
    EXPECT_DOUBLE_EQ(config.GetDouble("does.not.exist", 2.5), 2.5);
    EXPECT_FALSE(config.GetBool("does.not.exist", false));
}

TEST(ConfigManagerTest, GetX_WrongType_ReturnsDefault) {
    ConfigManager config;
    config.SetString("editor.theme", "dark");

    EXPECT_EQ(config.GetInt("editor.theme", 42), 42);
    EXPECT_DOUBLE_EQ(config.GetDouble("editor.theme", 1.5), 1.5);
    EXPECT_TRUE(config.GetBool("editor.theme", true));
}

TEST(ConfigManagerTest, SetAndGetX_RoundTripsEachScalarType) {
    ConfigManager config;
    config.SetString("editor.theme", "dark");
    config.SetInt("window.width", 1600);
    config.SetDouble("window.scale", 1.25);
    config.SetBool("server.enabled", true);

    EXPECT_EQ(config.GetString("editor.theme", ""), "dark");
    EXPECT_EQ(config.GetInt("window.width", 0), 1600);
    EXPECT_DOUBLE_EQ(config.GetDouble("window.scale", 0.0), 1.25);
    EXPECT_TRUE(config.GetBool("server.enabled", false));
}

TEST(ConfigManagerTest, ClearAll_EmptiesTheDocumentAndKeepsTheSavePath) {
    const std::filesystem::path savePath = "uve_config_tests_clear_all.uvsettings";
    std::filesystem::remove(savePath);

    ConfigManager config;
    config.SetInt("editor.width", 1280);
    ASSERT_TRUE(config.Save(savePath));
    config.SetString("editor.theme", "dark");

    config.ClearAll();

    EXPECT_FALSE(config.HasKey("editor.width"));
    EXPECT_FALSE(config.HasKey("editor.theme"));
    ASSERT_TRUE(config.Save());

    ConfigManager reloaded;
    ASSERT_TRUE(reloaded.Load(savePath));
    EXPECT_FALSE(reloaded.HasKey("editor.width"));
    EXPECT_FALSE(reloaded.HasKey("editor.theme"));
    std::filesystem::remove(savePath);
}

TEST(ConfigManagerTest, SetString_OnBrandNewNestedPath_CreatesIntermediateObjects) {
    ConfigManager config;
    EXPECT_FALSE(config.HasKey("a.b.c"));

    config.SetString("a.b.c", "leaf");

    EXPECT_TRUE(config.HasKey("a.b.c"));
    EXPECT_EQ(config.GetString("a.b.c", ""), "leaf");
}

TEST(ConfigManagerTest, SaveThenLoad_RoundTripsThroughDisk) {
    const std::filesystem::path savePath = "uve_config_tests_roundtrip.uvsettings";
    std::filesystem::remove(savePath);

    {
        ConfigManager writer;
        writer.SetString("editor.theme", "dark");
        writer.SetInt("window.width", 1440);
        writer.SetDouble("window.scale", 2.0);
        writer.SetBool("server.enabled", true);
        ASSERT_TRUE(writer.Save(savePath));
    }

    ConfigManager reader;
    ASSERT_TRUE(reader.Load(savePath));
    EXPECT_EQ(reader.GetString("editor.theme", ""), "dark");
    EXPECT_EQ(reader.GetInt("window.width", 0), 1440);
    EXPECT_DOUBLE_EQ(reader.GetDouble("window.scale", 0.0), 2.0);
    EXPECT_TRUE(reader.GetBool("server.enabled", false));

    std::filesystem::remove(savePath);
}

TEST(ConfigManagerTest, Save_NoArg_WritesToMostRecentlyLoadedPath) {
    const std::filesystem::path savePath = "uve_config_tests_save_no_arg.uvsettings";
    std::filesystem::remove(savePath);

    ConfigManager config;
    config.Load(savePath); // fails (file doesn't exist yet) but remembers savePath
    config.SetString("editor.theme", "dark");
    ASSERT_TRUE(config.Save());

    ConfigManager reader;
    ASSERT_TRUE(reader.Load(savePath));
    EXPECT_EQ(reader.GetString("editor.theme", ""), "dark");

    std::filesystem::remove(savePath);
}

TEST(ConfigManagerTest, Load_MalformedJson_ReturnsFalseAndLogsError) {
    const std::filesystem::path fixturePath = "uve_config_tests_malformed.uvsettings";
    WriteFixtureFile(fixturePath, "{ not valid json");

    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ConfigManager config;
    config.SetInt("keep.value", 42);
    EXPECT_FALSE(config.Load(fixturePath));
    EXPECT_EQ(config.GetInt("keep.value", 0), 42);

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundParseError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Error &&
                   message.message.contains("failed to parse");
        });
    EXPECT_TRUE(foundParseError);

    logger.Shutdown();
    std::filesystem::remove(fixturePath);
}

TEST(ConfigManagerTest, Load_MissingFile_LogsWarning) {
    Debug::Logger logger;
    logger.Init(Debug::LogLevel::Trace);
    auto memorySink = std::make_unique<Debug::MemorySink>();
    Debug::MemorySink* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ConfigManager config;
    EXPECT_FALSE(config.Load("uve_config_tests_still_nonexistent.uvsettings"));

    const std::vector<Debug::LogMessage> messages = memorySinkPtr->GetMessages();
    const bool foundWarning =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessage& message) {
            return message.level == Debug::LogLevel::Warning &&
                   message.message.contains("not found");
        });
    EXPECT_TRUE(foundWarning);

    logger.Shutdown();
}

TEST(ConfigManagerTest, HasKey_TrueForLeafFalseForMissingAndForIntermediateObject) {
    ConfigManager config;
    config.SetString("editor.theme", "dark");

    EXPECT_TRUE(config.HasKey("editor.theme"));
    EXPECT_FALSE(config.HasKey("editor.missing"));
    EXPECT_FALSE(config.HasKey("editor")); // reaches an intermediate object, not a leaf
}

TEST(ConfigManagerTest, ConcurrentReads_FromManyThreads_NoDataRacesOrCrashes) {
    ConfigManager config;
    config.SetString("editor.theme", "dark");
    config.SetInt("window.width", 1920);

    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 500;
    std::atomic<int> mismatchCount{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        threads.emplace_back([&config, &mismatchCount] {
            for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
                if (config.GetString("editor.theme", "") != "dark" ||
                    config.GetInt("window.width", 0) != 1920 || !config.HasKey("editor.theme") ||
                    config.HasKey("editor.missing")) {
                    mismatchCount.fetch_add(1, std::memory_order_relaxed);
                }
            }
        });
    }
    for (std::thread& thread : threads) {
        thread.join();
    }

    EXPECT_EQ(mismatchCount.load(), 0);
}

} // namespace
} // namespace UVE::Config::Tests
