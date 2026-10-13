// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "dedicated_server_sample_uve.h"

namespace UVE::Samples::Tests {
namespace {

TEST(DedicatedServerSampleUVE, FullStoryPasses) {
    const DedicatedServerSampleResultUVE result = RunDedicatedServerSampleUVE();
    for (const std::string& failure : result.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stats.ticks, 600U);
    EXPECT_GT(result.stats.reconcilesAmy, 0U);
    EXPECT_GT(result.stats.reconcilesBob, 0U);
    EXPECT_GT(result.stats.reconcilesCat, 0U);
    EXPECT_EQ(result.stats.shoutsHeardAmy, 20U);
    EXPECT_EQ(result.stats.shoutsHeardBob, 20U);
    EXPECT_EQ(result.stats.shoutsHeardCat, 0U);
    EXPECT_GT(result.stats.snapshotsReceived, 0U);
    EXPECT_FLOAT_EQ(result.stats.restY, 0.0F);
    EXPECT_GT(result.stats.walkerX, 12.0F);
}

TEST(DedicatedServerSampleUVE, ShortRunsStayGreenAndDeterministic) {
    DedicatedServerSampleConfigUVE config;
    config.ticks = 60U;
    const DedicatedServerSampleResultUVE first = RunDedicatedServerSampleUVE(config);
    const DedicatedServerSampleResultUVE second = RunDedicatedServerSampleUVE(config);
    for (const std::string& failure : first.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(first.success);
    EXPECT_TRUE(second.success);
    EXPECT_EQ(first.stats.ticks, second.stats.ticks);
    EXPECT_EQ(first.stats.reconcilesAmy, second.stats.reconcilesAmy);
    EXPECT_EQ(first.stats.reconcilesBob, second.stats.reconcilesBob);
    EXPECT_EQ(first.stats.reconcilesCat, second.stats.reconcilesCat);
    EXPECT_EQ(first.stats.shoutsHeardAmy, second.stats.shoutsHeardAmy);
    EXPECT_EQ(first.stats.shoutsHeardBob, second.stats.shoutsHeardBob);
    EXPECT_EQ(first.stats.shoutsHeardCat, second.stats.shoutsHeardCat);
    EXPECT_EQ(first.stats.snapshotsReceived, second.stats.snapshotsReceived);
    EXPECT_FLOAT_EQ(first.stats.restY, second.stats.restY);
    EXPECT_FLOAT_EQ(first.stats.walkerX, second.stats.walkerX);
}

TEST(DedicatedServerSampleUVE, LogSinkSeesTheStory) {
    DedicatedServerSampleConfigUVE config;
    config.ticks = 60U;
    std::vector<std::string> lines;
    config.log = [&](const std::string& line) { lines.push_back(line); };
    const DedicatedServerSampleResultUVE result = RunDedicatedServerSampleUVE(config);
    EXPECT_TRUE(result.success);
    EXPECT_FALSE(lines.empty());
    EXPECT_NE(lines.back().find("violation"), std::string::npos);
}

} // namespace
} // namespace UVE::Samples::Tests
