// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "lag_comp_server_sample_uve.h"

namespace UVE::Samples::Tests {
namespace {

TEST(LagCompServerSampleUVE, FullStoryPasses) {
    const LagCompServerSampleResultUVE result = RunLagCompServerSampleUVE();
    for (const std::string& failure : result.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stats.ticks, 500U);
    EXPECT_EQ(result.stats.shotsFired, 499U);
    EXPECT_EQ(result.stats.shotsNoView, 0U);
    EXPECT_GT(result.stats.shotLiveHits, 0U);
    EXPECT_GT(result.stats.shotRewoundHits, 0U);
    EXPECT_EQ(result.stats.shotSaves, 8U);
    EXPECT_EQ(result.stats.fracInterpolated, 499U);
    EXPECT_EQ(result.stats.raysFired, 499U);
    EXPECT_GT(result.stats.rayLiveHits, 0U);
    EXPECT_GT(result.stats.rayRewoundHits, 0U);
    EXPECT_EQ(result.stats.raySaves, 8U);
    EXPECT_EQ(result.stats.enteredEvents, 1U);
    EXPECT_EQ(result.stats.maxViewLag, 9U);
    EXPECT_FLOAT_EQ(result.stats.victimHealth, 9439.0F);
}

TEST(LagCompServerSampleUVE, RunsAreDeterministic) {
    LagCompServerSampleConfigUVE config;
    config.ticks = 500U;
    const LagCompServerSampleResultUVE first = RunLagCompServerSampleUVE(config);
    const LagCompServerSampleResultUVE second = RunLagCompServerSampleUVE(config);
    for (const std::string& failure : first.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(first.success);
    EXPECT_TRUE(second.success);
    EXPECT_EQ(first.stats.ticks, second.stats.ticks);
    EXPECT_EQ(first.stats.shotsFired, second.stats.shotsFired);
    EXPECT_EQ(first.stats.shotsNoView, second.stats.shotsNoView);
    EXPECT_EQ(first.stats.shotLiveHits, second.stats.shotLiveHits);
    EXPECT_EQ(first.stats.shotRewoundHits, second.stats.shotRewoundHits);
    EXPECT_EQ(first.stats.shotSaves, second.stats.shotSaves);
    EXPECT_EQ(first.stats.fracInterpolated, second.stats.fracInterpolated);
    EXPECT_EQ(first.stats.raysFired, second.stats.raysFired);
    EXPECT_EQ(first.stats.rayLiveHits, second.stats.rayLiveHits);
    EXPECT_EQ(first.stats.rayRewoundHits, second.stats.rayRewoundHits);
    EXPECT_EQ(first.stats.raySaves, second.stats.raySaves);
    EXPECT_EQ(first.stats.enteredEvents, second.stats.enteredEvents);
    EXPECT_EQ(first.stats.maxViewLag, second.stats.maxViewLag);
    EXPECT_FLOAT_EQ(first.stats.victimHealth, second.stats.victimHealth);
}

TEST(LagCompServerSampleUVE, LogSinkSeesTheStory) {
    LagCompServerSampleConfigUVE config;
    config.ticks = 500U;
    std::vector<std::string> lines;
    config.log = [&](const std::string& line) { lines.push_back(line); };
    const LagCompServerSampleResultUVE result = RunLagCompServerSampleUVE(config);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(lines.empty());
    EXPECT_NE(lines.back().find("violation"), std::string::npos);
}

} // namespace
} // namespace UVE::Samples::Tests
