// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "interest_fanout_sample_uve.h"

namespace UVE::Samples::Tests {
namespace {

using CountArrayUVE = std::array<std::uint64_t, kInterestFanoutEntitiesUVE>;

void ExpectArraysEqualUVE(const CountArrayUVE& actual, const CountArrayUVE& expected,
                           const std::string& name) {
    for (std::uint64_t slot = 0U; slot < kInterestFanoutEntitiesUVE; ++slot) {
        EXPECT_EQ(actual[slot], expected[slot]) << name << " slot " << slot;
    }
}

TEST(InterestFanoutSampleUVE, FullStoryPasses) {
    const InterestFanoutSampleResultUVE result = RunInterestFanoutSampleUVE();
    for (const std::string& failure : result.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stats.ticks, 100U);
    // A: everything every tick, except phased walker 25 (12 sends, first at tick 8).
    ExpectArraysEqualUVE(result.stats.receivedA, {100U, 100U, 100U, 100U, 100U, 12U},
                         "receivedA");
    ExpectArraysEqualUVE(result.stats.firstSeenA, {1U, 1U, 1U, 1U, 1U, 8U}, "firstSeenA");
    ExpectArraysEqualUVE(result.stats.lastSeenA, {100U, 100U, 100U, 100U, 100U, 96U},
                         "lastSeenA");
    // B: the top two every tick, nothing else, ever.
    ExpectArraysEqualUVE(result.stats.receivedB, {100U, 100U, 0U, 0U, 0U, 0U}, "receivedB");
    ExpectArraysEqualUVE(result.stats.firstSeenB, {1U, 1U, 0U, 0U, 0U, 0U}, "firstSeenB");
    ExpectArraysEqualUVE(result.stats.lastSeenB, {100U, 100U, 0U, 0U, 0U, 0U}, "lastSeenB");
    // C: the formation walks past the window - reverse discovery, dwell, exits in order.
    ExpectArraysEqualUVE(result.stats.receivedC, {25U, 25U, 25U, 25U, 25U, 3U}, "receivedC");
    ExpectArraysEqualUVE(result.stats.firstSeenC, {68U, 64U, 60U, 56U, 52U, 51U},
                         "firstSeenC");
    ExpectArraysEqualUVE(result.stats.lastSeenC, {92U, 88U, 84U, 80U, 76U, 67U}, "lastSeenC");
    EXPECT_EQ(result.stats.fanoutRefreshes, 300U);
    EXPECT_EQ(result.stats.fanoutSelected, 840U);
    EXPECT_EQ(result.stats.fanoutBudgetDropped, 312U);
    EXPECT_EQ(result.stats.skippedA, 88U);
    EXPECT_EQ(result.stats.skippedB, 400U);
    EXPECT_EQ(result.stats.skippedC, 472U);
    EXPECT_EQ(result.stats.profilerSpans, 100U);
    EXPECT_EQ(result.stats.profilerCounters, 320U);
}

TEST(InterestFanoutSampleUVE, RunsAreDeterministic) {
    InterestFanoutSampleConfigUVE config;
    config.ticks = 100U;
    const InterestFanoutSampleResultUVE first = RunInterestFanoutSampleUVE(config);
    const InterestFanoutSampleResultUVE second = RunInterestFanoutSampleUVE(config);
    for (const std::string& failure : first.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(first.success);
    EXPECT_TRUE(second.success);
    EXPECT_EQ(first.stats.ticks, second.stats.ticks);
    ExpectArraysEqualUVE(first.stats.receivedA, second.stats.receivedA, "receivedA");
    ExpectArraysEqualUVE(first.stats.receivedB, second.stats.receivedB, "receivedB");
    ExpectArraysEqualUVE(first.stats.receivedC, second.stats.receivedC, "receivedC");
    ExpectArraysEqualUVE(first.stats.firstSeenA, second.stats.firstSeenA, "firstSeenA");
    ExpectArraysEqualUVE(first.stats.firstSeenB, second.stats.firstSeenB, "firstSeenB");
    ExpectArraysEqualUVE(first.stats.firstSeenC, second.stats.firstSeenC, "firstSeenC");
    ExpectArraysEqualUVE(first.stats.lastSeenA, second.stats.lastSeenA, "lastSeenA");
    ExpectArraysEqualUVE(first.stats.lastSeenB, second.stats.lastSeenB, "lastSeenB");
    ExpectArraysEqualUVE(first.stats.lastSeenC, second.stats.lastSeenC, "lastSeenC");
    EXPECT_EQ(first.stats.fanoutRefreshes, second.stats.fanoutRefreshes);
    EXPECT_EQ(first.stats.fanoutSelected, second.stats.fanoutSelected);
    EXPECT_EQ(first.stats.fanoutBudgetDropped, second.stats.fanoutBudgetDropped);
    EXPECT_EQ(first.stats.skippedA, second.stats.skippedA);
    EXPECT_EQ(first.stats.skippedB, second.stats.skippedB);
    EXPECT_EQ(first.stats.skippedC, second.stats.skippedC);
    EXPECT_EQ(first.stats.profilerSpans, second.stats.profilerSpans);
    EXPECT_EQ(first.stats.profilerCounters, second.stats.profilerCounters);
}

TEST(InterestFanoutSampleUVE, LogSinkSeesTheStory) {
    InterestFanoutSampleConfigUVE config;
    config.ticks = 100U;
    std::vector<std::string> lines;
    config.log = [&](const std::string& line) { lines.push_back(line); };
    const InterestFanoutSampleResultUVE result = RunInterestFanoutSampleUVE(config);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(lines.empty());
    EXPECT_NE(lines.back().find("violation"), std::string::npos);
}

} // namespace
} // namespace UVE::Samples::Tests
