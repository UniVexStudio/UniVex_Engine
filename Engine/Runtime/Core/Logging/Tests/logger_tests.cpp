// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "logger.h"

#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "Support/test_scratch.h"

#include "log_sink.h"
#include "logging_macros.h"

namespace UVE::Debug::Tests {
namespace {

class LoggerTest : public ::testing::Test {
protected:
    void SetUp() override {
        logger.Init(LogLevel::Trace);
        auto memorySink = std::make_unique<MemorySink>();
        memorySinkPtr = memorySink.get();
        logger.AddSink(std::move(memorySink));
    }

    void TearDown() override { logger.Shutdown(); }

    Logger logger;
    MemorySink* memorySinkPtr = nullptr;
};

TEST_F(LoggerTest, ActiveInstance_SetDuringInit) {
    EXPECT_EQ(Logger::GetActiveInstance(), &logger);
}

TEST_F(LoggerTest, ActiveInstance_ClearedAfterShutdown) {
    logger.Shutdown();
    EXPECT_EQ(Logger::GetActiveInstance(), nullptr);
}

TEST_F(LoggerTest, MinLevel_FiltersLowerSeverity) {
    logger.SetMinLevel(LogLevel::Warning);
    logger.LogFormatted(LogLevel::Info, "", __FILE__, __LINE__, "info message");
    EXPECT_TRUE(memorySinkPtr->GetMessages().empty());

    logger.LogFormatted(LogLevel::Error, "", __FILE__, __LINE__, "error message");
    EXPECT_EQ(memorySinkPtr->GetMessages().size(), 1U);
}

TEST_F(LoggerTest, AllLevels_RoundTripCorrectly) {
    constexpr LogLevel kLevels[] = {LogLevel::Trace,   LogLevel::Debug, LogLevel::Info,
                                        LogLevel::Warning, LogLevel::Error, LogLevel::Fatal};
    for (const LogLevel level : kLevels) {
        memorySinkPtr->Clear();
        logger.LogFormatted(level, "", __FILE__, __LINE__, "message");
        ASSERT_EQ(memorySinkPtr->GetMessages().size(), 1U);
        EXPECT_EQ(memorySinkPtr->GetMessages().front().level, level);
    }
}

TEST_F(LoggerTest, MultipleSinks_AllReceiveSameMessage) {
    auto secondSink = std::make_unique<MemorySink>();
    MemorySink* secondSinkPtr = secondSink.get();
    logger.AddSink(std::move(secondSink));

    logger.LogFormatted(LogLevel::Info, "", __FILE__, __LINE__, "hello");

    ASSERT_EQ(memorySinkPtr->GetMessages().size(), 1U);
    ASSERT_EQ(secondSinkPtr->GetMessages().size(), 1U);
    EXPECT_EQ(memorySinkPtr->GetMessages().front().message, "hello");
    EXPECT_EQ(secondSinkPtr->GetMessages().front().message, "hello");
}

TEST_F(LoggerTest, MemorySink_ClearRemovesMessages) {
    logger.LogFormatted(LogLevel::Info, "", __FILE__, __LINE__, "hello");
    ASSERT_FALSE(memorySinkPtr->GetMessages().empty());
    memorySinkPtr->Clear();
    EXPECT_TRUE(memorySinkPtr->GetMessages().empty());
}

TEST_F(LoggerTest, FileSink_WritesLineToFile) {
    const std::filesystem::path tempPath =
        ::UVE::Tests::ScratchRoot() / "uve_logger_test_file_sink.log";
    std::filesystem::remove(tempPath);

    {
        Logger fileLogger;
        fileLogger.Init(LogLevel::Trace);
        fileLogger.AddSink(std::make_unique<FileSink>(tempPath));
        fileLogger.LogFormatted(LogLevel::Info, "", __FILE__, __LINE__, "file sink message");
        fileLogger.Shutdown();
    }

    ASSERT_TRUE(std::filesystem::exists(tempPath));
    std::ifstream fileStream(tempPath);
    std::stringstream contents;
    contents << fileStream.rdbuf();
    EXPECT_NE(contents.str().find("file sink message"), std::string::npos);

    std::filesystem::remove(tempPath);
}

TEST_F(LoggerTest, FatalLevel_FlushesAllSinks) {
    class FlushCountingSink final : public ILogSink {
    public:
        void Write(const LogMessage&) override {}
        void Flush() override { ++flushCount; }
        int flushCount = 0;
    };

    auto flushSink = std::make_unique<FlushCountingSink>();
    FlushCountingSink* flushSinkPtr = flushSink.get();
    logger.AddSink(std::move(flushSink));

    logger.LogFormatted(LogLevel::Fatal, "", __FILE__, __LINE__, "fatal message");
    EXPECT_GT(flushSinkPtr->flushCount, 0);
}

TEST_F(LoggerTest, MacroFamily_ProducesCorrectLevelsAndMessages) {
    UVE_TRACE("trace {}", 1);
    UVE_INFO("info {}", 2);
    UVE_WARNING("warning {}", 3);
    UVE_ERROR("error {}", 4);
    UVE_FATAL("fatal {}", 5);

    const std::vector<LogMessage> messages = memorySinkPtr->GetMessages();
    ASSERT_EQ(messages.size(), 5U);
    EXPECT_EQ(messages[0].level, LogLevel::Trace);
    EXPECT_EQ(messages[1].level, LogLevel::Info);
    EXPECT_EQ(messages[2].level, LogLevel::Warning);
    EXPECT_EQ(messages[3].level, LogLevel::Error);
    EXPECT_EQ(messages[4].level, LogLevel::Fatal);
    EXPECT_EQ(messages[0].message, "trace 1");
    EXPECT_EQ(messages[4].message, "fatal 5");
}

TEST_F(LoggerTest, FormattedMessage_SubstitutesArgumentsCorrectly) {
    UVE_INFO("value={} name={}", 42, "abc");
    ASSERT_EQ(memorySinkPtr->GetMessages().size(), 1U);
    EXPECT_EQ(memorySinkPtr->GetMessages().front().message, "value=42 name=abc");
}

} // namespace
} // namespace UVE::Debug::Tests
