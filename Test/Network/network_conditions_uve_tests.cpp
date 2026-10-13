// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_conditions_uve.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> BytesUVE(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct ConditionsHarnessUVE final {
    LoopbackTransportPairUVE link;
    LoopbackEndpointUVE rawB;
    // Shared (not a plain field): the simulator's clock lambda outlives MakeUVE's locals, so it
    // must not capture them by reference.
    std::shared_ptr<std::uint64_t> nowMs;
    SimulatedTransportUVE sim;

    void AdvanceUVE(const std::uint64_t timeMs) { *nowMs = timeMs; }

    [[nodiscard]] static ConditionsHarnessUVE MakeUVE(SimulatedTransportConfigUVE config) {
        LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(64U);
        LoopbackEndpointUVE rawB = pair.b;
        auto nowMs = std::make_shared<std::uint64_t>(1000U);
        TransportClockUVE clock = [nowMs]() { return *nowMs; };
        SimulatedTransportUVE sim(std::make_unique<LoopbackEndpointUVE>(pair.a), std::move(clock),
                                  config);
        return ConditionsHarnessUVE{std::move(pair), rawB, nowMs, std::move(sim)};
    }
};

TEST(SimulatedTransportUVETest, Passthrough_DefaultConfigIsInvisible) {
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(SimulatedTransportConfigUVE{});

    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("a-to-b")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b-to-a")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("a-to-b")));
    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("b-to-a")));
    EXPECT_EQ(harness.sim.StatsUVE(), SimulatedTransportStatsUVE{});
}

TEST(SimulatedTransportUVETest, OutboundDrop_EveryNthReportsSuccess) {
    SimulatedTransportConfigUVE config;
    config.outboundDropPeriod = 2U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("m1")));
    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("m2-dropped")));
    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("m3")));
    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("m4-dropped")));

    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("m1")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("m3")));
    EXPECT_FALSE(harness.rawB.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.sim.StatsUVE().outboundDropped, 2U);
}

TEST(SimulatedTransportUVETest, InboundDrop) {
    SimulatedTransportConfigUVE config;
    config.inboundDropPeriod = 3U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b1")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b2")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b3-dropped")));

    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("b1")));
    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("b2")));
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.sim.StatsUVE().inboundDropped, 1U);
}

TEST(SimulatedTransportUVETest, OutboundDelay_HeldUntilDue) {
    SimulatedTransportConfigUVE config;
    config.outboundDelayMs = 100U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("held")));
    EXPECT_FALSE(harness.rawB.TryReceiveUVE().has_value());

    harness.AdvanceUVE(1099U);
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_FALSE(harness.rawB.TryReceiveUVE().has_value());

    harness.AdvanceUVE(1100U);
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("held")));
}

TEST(SimulatedTransportUVETest, InboundDelay_HeldUntilDue) {
    SimulatedTransportConfigUVE config;
    config.inboundDelayMs = 100U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("held")));
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());

    harness.AdvanceUVE(1100U);
    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("held")));
}

TEST(SimulatedTransportUVETest, DelayPreservesFifoOrder) {
    SimulatedTransportConfigUVE config;
    config.outboundDelayMs = 50U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("first")));
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("second")));
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("third")));

    harness.AdvanceUVE(1050U);
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("first")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("second")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("third")));
}

TEST(SimulatedTransportUVETest, DelayQueueFull_ReportsFalse) {
    SimulatedTransportConfigUVE config;
    config.outboundDelayMs = 10000U;
    config.maxPendingDatagrams = 1U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("first")));
    EXPECT_FALSE(harness.sim.SendUVE(BytesUVE("second")));
    EXPECT_EQ(harness.sim.StatsUVE().outboundDroppedFull, 1U);

    harness.AdvanceUVE(11000U);
    EXPECT_FALSE(harness.sim.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("first")));
    EXPECT_FALSE(harness.rawB.TryReceiveUVE().has_value());
}

TEST(SimulatedTransportUVETest, SetConfig_Midstream) {
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(SimulatedTransportConfigUVE{});

    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("before")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("before")));

    SimulatedTransportConfigUVE blackout;
    blackout.outboundDropPeriod = 1U;
    harness.sim.SetConfigUVE(blackout);
    EXPECT_TRUE(harness.sim.SendUVE(BytesUVE("lost")));
    EXPECT_FALSE(harness.rawB.TryReceiveUVE().has_value());
    EXPECT_EQ(harness.sim.StatsUVE().outboundDropped, 1U);

    harness.sim.SetConfigUVE(SimulatedTransportConfigUVE{});
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("after")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("after")));
}

TEST(SimulatedTransportUVETest, Stats_MixedExchange) {
    SimulatedTransportConfigUVE config;
    config.outboundDropPeriod = 2U;
    config.inboundDropPeriod = 3U;
    ConditionsHarnessUVE harness = ConditionsHarnessUVE::MakeUVE(config);

    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("m1")));
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("m2-dropped")));
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("m3")));
    ASSERT_TRUE(harness.sim.SendUVE(BytesUVE("m4-dropped")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b1")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b2")));
    ASSERT_TRUE(harness.rawB.SendUVE(BytesUVE("b3-dropped")));

    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("m1")));
    EXPECT_EQ(harness.rawB.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("m3")));
    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("b1")));
    EXPECT_EQ(harness.sim.TryReceiveUVE(), std::optional<std::vector<std::uint8_t>>(BytesUVE("b2")));
    EXPECT_EQ(harness.sim.StatsUVE(), (SimulatedTransportStatsUVE{2U, 0U, 1U, 0U}));
}

} // namespace
} // namespace UVE::Network::Tests
