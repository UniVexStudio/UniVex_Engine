// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "packet_capture_sample_uve.h"

namespace UVE::Samples::Tests {
namespace {

TEST(PacketCaptureSampleUVE, FullStoryPasses) {
    const PacketCaptureSampleResultUVE result = RunPacketCaptureSampleUVE();
    for (const std::string& failure : result.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.stats.pushed, 12U);
    EXPECT_EQ(result.stats.size, 8U);
    EXPECT_EQ(result.stats.drops, 4U);

    EXPECT_EQ(result.stats.malformed, 2U);
    EXPECT_EQ(result.stats.inbound + result.stats.outbound, 8U);
    EXPECT_EQ(result.stats.inbound, 6U);
    EXPECT_EQ(result.stats.outbound, 2U);
    EXPECT_EQ(result.stats.replication, 2U);
    EXPECT_EQ(result.stats.rpc, 2U);
    EXPECT_EQ(result.stats.shots, 2U);
    EXPECT_EQ(result.stats.fireHits, 2U);
    EXPECT_EQ(result.stats.hexdumpBytes, 38U);
}

TEST(PacketCaptureSampleUVE, RunsAreDeterministic) {
    const PacketCaptureSampleResultUVE first = RunPacketCaptureSampleUVE();
    const PacketCaptureSampleResultUVE second = RunPacketCaptureSampleUVE();
    for (const std::string& failure : first.failures) {
        ADD_FAILURE() << failure;
    }
    EXPECT_TRUE(first.success);
    EXPECT_TRUE(second.success);
    EXPECT_EQ(first.stats.pushed, second.stats.pushed);
    EXPECT_EQ(first.stats.size, second.stats.size);
    EXPECT_EQ(first.stats.drops, second.stats.drops);
    EXPECT_EQ(first.stats.malformed, second.stats.malformed);
    EXPECT_EQ(first.stats.inbound, second.stats.inbound);
    EXPECT_EQ(first.stats.outbound, second.stats.outbound);
    EXPECT_EQ(first.stats.replication, second.stats.replication);
    EXPECT_EQ(first.stats.rpc, second.stats.rpc);
    EXPECT_EQ(first.stats.shots, second.stats.shots);
    EXPECT_EQ(first.stats.fireHits, second.stats.fireHits);
    EXPECT_EQ(first.stats.hexdumpBytes, second.stats.hexdumpBytes);
}

TEST(PacketCaptureSampleUVE, LogSinkSeesTheStory) {
    PacketCaptureSampleConfigUVE config;
    std::vector<std::string> lines;
    config.log = [&](const std::string& line) { lines.push_back(line); };
    const PacketCaptureSampleResultUVE result = RunPacketCaptureSampleUVE(config);
    EXPECT_TRUE(result.success);
    ASSERT_GE(lines.size(), 10U);
    bool sawFire = false;
    bool sawHexdump = false;
    for (const std::string& line : lines) {
        if (line.find("fire pawn") != std::string::npos) {
            sawFire = true;
        }
        if (line.rfind("00000000:", 0U) == 0U) {
            sawHexdump = true;
        }
    }
    EXPECT_TRUE(sawFire);
    EXPECT_TRUE(sawHexdump);
    bool sawVerdict = false;
    for (const std::string& line : lines) {
        if (line.find("violation") != std::string::npos) {
            sawVerdict = true;
        }
    }
    EXPECT_TRUE(sawVerdict);
}

} // namespace
} // namespace UVE::Samples::Tests
