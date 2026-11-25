// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "command_line.h"

#include <atomic>
#include <iterator>
#include <string>
#include <thread>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::CommandLine::Tests {
namespace {

TEST(CommandLineTest, EmptyArgs_NoFlagsPresent) {
    const CommandLine commandLine(std::vector<std::string>{});
    EXPECT_FALSE(commandLine.HasFlag("project"));
    EXPECT_FALSE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("project", "default"), "default");
}

TEST(CommandLineTest, FlagWithValue_IsCapturedUnderNormalizedName) {
    const CommandLine commandLine(std::vector<std::string>{"--project", "MyGame"});
    EXPECT_TRUE(commandLine.HasFlag("project"));
    EXPECT_EQ(commandLine.GetValue("project", "default"), "MyGame");
}

TEST(CommandLineTest, RawDashDashPrefixedQuery_DoesNotMatch) {
    const CommandLine commandLine(std::vector<std::string>{"--project", "MyGame", "--server"});

    EXPECT_TRUE(commandLine.HasFlag("project"));
    EXPECT_EQ(commandLine.GetValue("project", ""), "MyGame");
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("server", "default"), "default");

    // Proves the normalization layer actually strips "--" rather than being a no-op.
    EXPECT_FALSE(commandLine.HasFlag("--project"));
    EXPECT_FALSE(commandLine.HasFlag("--server"));
    EXPECT_EQ(commandLine.GetValue("--project", "default"), "default");
}

TEST(CommandLineTest, AbsentFlag_HasFlagFalseAndGetValueReturnsDefault) {
    const CommandLine commandLine(std::vector<std::string>{"--project", "MyGame"});
    EXPECT_FALSE(commandLine.HasFlag("build"));
    EXPECT_EQ(commandLine.GetValue("build", "fallback"), "fallback");
}

TEST(CommandLineTest, PresenceOnlyFlag_AsLastToken_HasFlagTrueValueIsDefault) {
    const CommandLine commandLine(std::vector<std::string>{"--server"});
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("server", "unset"), "unset");
}

TEST(CommandLineTest, PresenceOnlyFlag_FollowedByAnotherFlag_BothRecognizedIndependently) {
    const CommandLine commandLine(std::vector<std::string>{"--server", "--verbose"});
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("server", "unset"), "unset");
    EXPECT_TRUE(commandLine.HasFlag("verbose"));
    EXPECT_EQ(commandLine.GetValue("verbose", "unset"), "unset");
}

TEST(CommandLineTest, OptionalValueDistinguishesAbsentPresenceOnlyAndEmptyValues) {
    const CommandLine commandLine(std::vector<std::string>{"--server", "--empty", ""});

    EXPECT_FALSE(commandLine.GetOptionalValue("missing").has_value());
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_FALSE(commandLine.GetOptionalValue("server").has_value());
    ASSERT_TRUE(commandLine.GetOptionalValue("empty").has_value());
    EXPECT_TRUE(commandLine.GetOptionalValue("empty")->empty());
}

TEST(CommandLineTest, MixedFlags_HubStyleInvocation_ParsesAllCorrectly) {
    const CommandLine commandLine(
        std::vector<std::string>{"--project", "/path/to/project", "--build", "linux", "--server"});
    EXPECT_EQ(commandLine.GetValue("project", ""), "/path/to/project");
    EXPECT_EQ(commandLine.GetValue("build", ""), "linux");
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("server", "unset"), "unset");
}

TEST(CommandLineTest, ArgcArgvConstructor_SkipsProgramPath_MatchesVectorConstructor) {
    const char* rawArgs[] = {"/usr/bin/uve_runtime", "--project", "MyGame", "--server"};
    const int argc = static_cast<int>(std::size(rawArgs));
    const CommandLine commandLine(argc, const_cast<char**>(rawArgs));

    EXPECT_TRUE(commandLine.HasFlag("project"));
    EXPECT_EQ(commandLine.GetValue("project", ""), "MyGame");
    EXPECT_TRUE(commandLine.HasFlag("server"));
    EXPECT_EQ(commandLine.GetValue("server", "unset"), "unset");
}

TEST(CommandLineTest, ArgcArgvConstructor_ArgcOne_NoArgumentsBeyondProgramPath) {
    const char* rawArgs[] = {"/usr/bin/uve_runtime"};
    const int argc = static_cast<int>(std::size(rawArgs));
    const CommandLine commandLine(argc, const_cast<char**>(rawArgs));

    EXPECT_FALSE(commandLine.HasFlag("project"));
}

TEST(CommandLineTest, ConcurrentReads_FromManyThreads_NoDataRacesOrCrashes) {
    const CommandLine commandLine(
        std::vector<std::string>{"--project", "/path/to/project", "--build", "linux", "--server"});

    constexpr int kThreadCount = 8;
    constexpr int kIterationsPerThread = 500;
    std::atomic<int> mismatchCount{0};
    std::vector<std::thread> threads;
    threads.reserve(kThreadCount);

    for (int threadIndex = 0; threadIndex < kThreadCount; ++threadIndex) {
        threads.emplace_back([&commandLine, &mismatchCount] {
            for (int iteration = 0; iteration < kIterationsPerThread; ++iteration) {
                if (!commandLine.HasFlag("project") ||
                    commandLine.GetValue("project", "") != "/path/to/project" ||
                    !commandLine.HasFlag("server") ||
                    commandLine.GetValue("server", "unset") != "unset") {
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
} // namespace UVE::CommandLine::Tests
