// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/config/config_manager_uve.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

#include "uve/logging/log_sink_uve.h"
#include "uve/logging/logger_uve.h"

namespace UVE::Config::Tests {
namespace {

/// Writes `contents` to `path`, overwriting any existing file. Used to set
/// up JSON fixture files for LoadUVE() tests.
void WriteFixtureFileUVE(const std::filesystem::path& path, std::string_view contents) {
    std::ofstream file(path);
    ASSERT_TRUE(file.is_open());
    file << contents;
}

TEST(ConfigManagerUVETest, LoadUVE_NonexistentPath_ReturnsFalseAndStartsWithAnEmptyDocument) {
    const std::filesystem::path missingPath = "uve_config_tests_nonexistent.uvsettings";
    std::filesystem::remove(missingPath);
    ConfigManagerUVE config;
    config.SetIntUVE("window.width", 1600);
    EXPECT_FALSE(config.LoadUVE(missingPath));
    EXPECT_EQ(config.GetStringUVE("editor.theme", "light"), "light");
    EXPECT_EQ(config.GetIntUVE("window.width", 1280), 1280);
    std::filesystem::remove(missingPath);
}

TEST(ConfigManagerUVETest, LoadUVE_ValidFixture_ReadsAllFourScalarTypesAcrossNestedPaths) {
    const std::filesystem::path fixturePath = "uve_config_tests_valid.uvsettings";
    WriteFixtureFileUVE(fixturePath, R"({
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

    ConfigManagerUVE config;
    ASSERT_TRUE(config.LoadUVE(fixturePath));

    EXPECT_EQ(config.GetStringUVE("editor.theme", "light"), "dark");
    EXPECT_EQ(config.GetIntUVE("window.width", 0), 1920);
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("window.scale", 0.0), 1.5);
    EXPECT_TRUE(config.GetBoolUVE("server.enabled", false));
    EXPECT_EQ(config.GetIntUVE("version", 0), 1);

    std::filesystem::remove(fixturePath);
}

TEST(ConfigManagerUVETest, GetXUVE_MissingKey_ReturnsDefault) {
    ConfigManagerUVE config;
    EXPECT_EQ(config.GetStringUVE("does.not.exist", "fallback"), "fallback");
    EXPECT_EQ(config.GetIntUVE("does.not.exist", 7), 7);
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("does.not.exist", 2.5), 2.5);
    EXPECT_FALSE(config.GetBoolUVE("does.not.exist", false));
}

TEST(ConfigManagerUVETest, GetXUVE_WrongType_ReturnsDefault) {
    ConfigManagerUVE config;
    config.SetStringUVE("editor.theme", "dark");

    EXPECT_EQ(config.GetIntUVE("editor.theme", 42), 42);
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("editor.theme", 1.5), 1.5);
    EXPECT_TRUE(config.GetBoolUVE("editor.theme", true));
}

TEST(ConfigManagerUVETest, SetAndGetXUVE_RoundTripsEachScalarType) {
    ConfigManagerUVE config;
    config.SetStringUVE("editor.theme", "dark");
    config.SetIntUVE("window.width", 1600);
    config.SetDoubleUVE("window.scale", 1.25);
    config.SetBoolUVE("server.enabled", true);

    EXPECT_EQ(config.GetStringUVE("editor.theme", ""), "dark");
    EXPECT_EQ(config.GetIntUVE("window.width", 0), 1600);
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("window.scale", 0.0), 1.25);
    EXPECT_TRUE(config.GetBoolUVE("server.enabled", false));
}

TEST(ConfigManagerUVETest, BatchReadReturnsEveryScalarTypeAndMarksMissingOrObjectPaths) {
    ConfigManagerUVE config;
    config.SetStringUVE("sample.label", "hello");
    config.SetIntUVE("sample.count", 42);
    config.SetDoubleUVE("sample.ratio", 0.75);
    config.SetBoolUVE("sample.enabled", true);

    const std::vector<std::optional<ConfigScalarValueUVE>> values =
        config.GetValuesUVE({"sample.label", "sample.count", "sample.ratio", "sample.enabled", "missing", "sample"});
    ASSERT_EQ(values.size(), 6U);
    ASSERT_TRUE(values[0].has_value());
    ASSERT_TRUE(values[1].has_value());
    ASSERT_TRUE(values[2].has_value());
    ASSERT_TRUE(values[3].has_value());
    EXPECT_EQ(std::get<std::string>(*values[0]), "hello");
    EXPECT_EQ(std::get<std::int64_t>(*values[1]), 42);
    EXPECT_DOUBLE_EQ(std::get<double>(*values[2]), 0.75);
    EXPECT_TRUE(std::get<bool>(*values[3]));
    EXPECT_FALSE(values[4].has_value());
    EXPECT_FALSE(values[5].has_value());
}

TEST(ConfigManagerUVETest, BatchReadReturnsNulloptForJsonContainersAndNull) {
    const std::filesystem::path fixturePath = "uve_config_tests_batch_values.uvsettings";
    WriteFixtureFileUVE(fixturePath, R"({
        "sample": {
            "object": {"child": 1},
            "array": [1, 2],
            "nullValue": null
        }
    })");

    ConfigManagerUVE config;
    ASSERT_TRUE(config.LoadUVE(fixturePath));
    const std::vector<std::optional<ConfigScalarValueUVE>> values = config.GetValuesUVE(
        {"sample.object", "sample.array", "sample.nullValue", "sample.missing"});
    ASSERT_EQ(values.size(), 4U);
    for (const auto& value : values) {
        EXPECT_FALSE(value.has_value());
    }
    std::filesystem::remove(fixturePath);
}

TEST(ConfigManagerUVETest, AtomicMutationsCommitSetsAndRemovalsTogetherOrNotAtAll) {
    ConfigManagerUVE config;
    config.SetStringUVE("palette.name", "old");
    config.SetStringUVE("palette.stale", "remove me");

    const std::vector<ConfigMutationUVE> mutations{
        {"palette.name", ConfigScalarValueUVE{std::string("new")}},
        {"palette.red", ConfigScalarValueUVE{0.25}},
        {"palette.stale", std::nullopt},
    };
    ASSERT_TRUE(config.ApplyMutationsUVE(mutations));
    EXPECT_EQ(config.GetStringUVE("palette.name", ""), "new");
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("palette.red", 0.0), 0.25);
    EXPECT_FALSE(config.HasKeyUVE("palette.stale"));

    const std::vector<ConfigMutationUVE> malformed{
        {"palette.name", ConfigScalarValueUVE{std::string("must not commit")}},
        {"palette..invalid", ConfigScalarValueUVE{std::int64_t{1}}},
    };
    EXPECT_FALSE(config.ApplyMutationsUVE(malformed));
    EXPECT_EQ(config.GetStringUVE("palette.name", ""), "new");
    EXPECT_FALSE(config.HasKeyUVE("palette..invalid"));

    const std::vector<ConfigMutationUVE> nonFinite{
        {"palette.name", ConfigScalarValueUVE{std::string("must not commit")}},
        {"palette.red", ConfigScalarValueUVE{std::numeric_limits<double>::infinity()}},
    };
    EXPECT_FALSE(config.ApplyMutationsUVE(nonFinite));
    EXPECT_EQ(config.GetStringUVE("palette.name", ""), "new");
    EXPECT_DOUBLE_EQ(config.GetDoubleUVE("palette.red", 0.0), 0.25);
    EXPECT_TRUE(config.ApplyMutationsUVE({}));
}

TEST(ConfigManagerUVETest, BatchReadIsOneSnapshotWhileAtomicWritersReplaceRelatedValues) {
    ConfigManagerUVE config;
    ASSERT_TRUE(config.ApplyMutationsUVE({
        {"pair.left", ConfigScalarValueUVE{std::int64_t{0}}},
        {"pair.right", ConfigScalarValueUVE{std::int64_t{0}}},
    }));

    constexpr std::int64_t kIterations = 50000;
    std::atomic<bool> start{false};
    std::atomic<bool> finished{false};
    std::atomic<bool> snapshotsConsistent{true};
    std::thread writer([&] {
        while (!start.load(std::memory_order_acquire)) {
            std::this_thread::yield();
        }
        for (std::int64_t value = 1; value <= kIterations; ++value) {
            if (!config.ApplyMutationsUVE({
                    {"pair.left", ConfigScalarValueUVE{value}},
                    {"pair.right", ConfigScalarValueUVE{-value}},
                })) {
                snapshotsConsistent.store(false, std::memory_order_release);
                break;
            }
            if ((value % 32) == 0) {
                std::this_thread::yield();
            }
        }
        finished.store(true, std::memory_order_release);
    });

    start.store(true, std::memory_order_release);
    std::size_t snapshotsRead = 0U;
    do {
        const std::vector<std::optional<ConfigScalarValueUVE>> values =
            config.GetValuesUVE({"pair.left", "pair.right"});
        if (values.size() != 2U || !values[0] || !values[1]) {
            snapshotsConsistent.store(false, std::memory_order_release);
        } else {
            const auto* left = std::get_if<std::int64_t>(&*values[0]);
            const auto* right = std::get_if<std::int64_t>(&*values[1]);
            if (left == nullptr || right == nullptr || *left != -*right) {
                snapshotsConsistent.store(false, std::memory_order_release);
            }
        }
        ++snapshotsRead;
    } while (!finished.load(std::memory_order_acquire));
    writer.join();

    EXPECT_GT(snapshotsRead, 0U);
    EXPECT_TRUE(snapshotsConsistent.load(std::memory_order_acquire));
}

TEST(ConfigManagerUVETest, ClearAllUVE_EmptiesTheDocumentAndKeepsTheSavePath) {
    const std::filesystem::path savePath = "uve_config_tests_clear_all.uvsettings";
    std::filesystem::remove(savePath);

    ConfigManagerUVE config;
    config.SetIntUVE("editor.width", 1280);
    ASSERT_TRUE(config.SaveUVE(savePath));
    config.SetStringUVE("editor.theme", "dark");

    config.ClearAllUVE();

    EXPECT_FALSE(config.HasKeyUVE("editor.width"));
    EXPECT_FALSE(config.HasKeyUVE("editor.theme"));
    ASSERT_TRUE(config.SaveUVE());

    ConfigManagerUVE reloaded;
    ASSERT_TRUE(reloaded.LoadUVE(savePath));
    EXPECT_FALSE(reloaded.HasKeyUVE("editor.width"));
    EXPECT_FALSE(reloaded.HasKeyUVE("editor.theme"));
    std::filesystem::remove(savePath);
}

TEST(ConfigManagerUVETest, SetStringUVE_OnBrandNewNestedPath_CreatesIntermediateObjects) {
    ConfigManagerUVE config;
    EXPECT_FALSE(config.HasKeyUVE("a.b.c"));

    config.SetStringUVE("a.b.c", "leaf");

    EXPECT_TRUE(config.HasKeyUVE("a.b.c"));
    EXPECT_EQ(config.GetStringUVE("a.b.c", ""), "leaf");
}

TEST(ConfigManagerUVETest, SaveThenLoad_RoundTripsThroughDisk) {
    const std::filesystem::path savePath = "uve_config_tests_roundtrip.uvsettings";
    std::filesystem::remove(savePath);

    {
        ConfigManagerUVE writer;
        writer.SetStringUVE("editor.theme", "dark");
        writer.SetIntUVE("window.width", 1440);
        writer.SetDoubleUVE("window.scale", 2.0);
        writer.SetBoolUVE("server.enabled", true);
        ASSERT_TRUE(writer.SaveUVE(savePath));
    }

    ConfigManagerUVE reader;
    ASSERT_TRUE(reader.LoadUVE(savePath));
    EXPECT_EQ(reader.GetStringUVE("editor.theme", ""), "dark");
    EXPECT_EQ(reader.GetIntUVE("window.width", 0), 1440);
    EXPECT_DOUBLE_EQ(reader.GetDoubleUVE("window.scale", 0.0), 2.0);
    EXPECT_TRUE(reader.GetBoolUVE("server.enabled", false));

    std::filesystem::remove(savePath);
}

TEST(ConfigManagerUVETest, SaveUVE_NoArg_WritesToMostRecentlyLoadedPath) {
    const std::filesystem::path savePath = "uve_config_tests_save_no_arg.uvsettings";
    std::filesystem::remove(savePath);

    ConfigManagerUVE config;
    config.LoadUVE(savePath); // fails (file doesn't exist yet) but remembers savePath
    config.SetStringUVE("editor.theme", "dark");
    ASSERT_TRUE(config.SaveUVE());

    ConfigManagerUVE reader;
    ASSERT_TRUE(reader.LoadUVE(savePath));
    EXPECT_EQ(reader.GetStringUVE("editor.theme", ""), "dark");

    std::filesystem::remove(savePath);
}

TEST(ConfigManagerUVETest, LoadUVE_MalformedJson_ReturnsFalseAndLogsError) {
    const std::filesystem::path fixturePath = "uve_config_tests_malformed.uvsettings";
    WriteFixtureFileUVE(fixturePath, "{ not valid json");

    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ConfigManagerUVE config;
    config.SetIntUVE("keep.value", 42);
    EXPECT_FALSE(config.LoadUVE(fixturePath));
    EXPECT_EQ(config.GetIntUVE("keep.value", 0), 42);

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundParseError =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Error &&
                   message.message.contains("failed to parse");
        });
    EXPECT_TRUE(foundParseError);

    logger.Shutdown();
    std::filesystem::remove(fixturePath);
}

TEST(ConfigManagerUVETest, LoadUVE_MissingFile_LogsWarning) {
    Debug::LoggerUVE logger;
    logger.Init(Debug::LogLevelUVE::Trace);
    auto memorySink = std::make_unique<Debug::MemorySinkUVE>();
    Debug::MemorySinkUVE* const memorySinkPtr = memorySink.get();
    logger.AddSink(std::move(memorySink));

    ConfigManagerUVE config;
    EXPECT_FALSE(config.LoadUVE("uve_config_tests_still_nonexistent.uvsettings"));

    const std::vector<Debug::LogMessageUVE> messages = memorySinkPtr->GetMessagesUVE();
    const bool foundWarning =
        std::any_of(messages.begin(), messages.end(), [](const Debug::LogMessageUVE& message) {
            return message.level == Debug::LogLevelUVE::Warning &&
                   message.message.contains("not found");
        });
    EXPECT_TRUE(foundWarning);

    logger.Shutdown();
}

TEST(ConfigManagerUVETest, HasKeyUVE_TrueForLeafFalseForMissingAndForIntermediateObject) {
    ConfigManagerUVE config;
    config.SetStringUVE("editor.theme", "dark");

    EXPECT_TRUE(config.HasKeyUVE("editor.theme"));
    EXPECT_FALSE(config.HasKeyUVE("editor.missing"));
    EXPECT_FALSE(config.HasKeyUVE("editor")); // reaches an intermediate object, not a leaf
}

TEST(ConfigManagerUVETest, ConcurrentReads_FromManyThreads_NoDataRacesOrCrashes) {
    ConfigManagerUVE config;
    config.SetStringUVE("editor.theme", "dark");
    config.SetIntUVE("window.width", 1920);

    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 500;
    std::atomic<int> mismatchCount{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        threads.emplace_back([&config, &mismatchCount] {
            for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
                if (config.GetStringUVE("editor.theme", "") != "dark" ||
                    config.GetIntUVE("window.width", 0) != 1920 || !config.HasKeyUVE("editor.theme") ||
                    config.HasKeyUVE("editor.missing")) {
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
