// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_conditions.h"

#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

[[nodiscard]] std::vector<std::uint8_t> Bytes(const std::string& text) {
    return std::vector<std::uint8_t>(text.begin(), text.end());
}

struct ConditionsHarness final {
    LoopbackTransportPair link;
    LoopbackEndpoint rawB;
    // Shared (not a plain field): the simulator's clock lambda outlives Make's locals, so it
    // must not capture them by reference.
    std::shared_ptr<std::uint64_t> nowMs;
    SimulatedTransport sim;

    void Advance(const std::uint64_t timeMs) { *nowMs = timeMs; }

    [[nodiscard]] static ConditionsHarness Make(SimulatedTransportConfig config) {
        LoopbackTransportPair pair = CreateLoopbackTransportPair(64U);
        LoopbackEndpoint rawB = pair.b;
        auto nowMs = std::make_shared<std::uint64_t>(1000U);
        TransportClock clock = [nowMs]() { return *nowMs; };
        SimulatedTransport sim(std::make_unique<LoopbackEndpoint>(pair.a), std::move(clock),
                                  config);
        return ConditionsHarness{std::move(pair), rawB, nowMs, std::move(sim)};
    }
};

TEST(SimulatedTransportTest, Passthrough_DefaultConfigIsInvisible) {
    ConditionsHarness harness = ConditionsHarness::Make(SimulatedTransportConfig{});

    ASSERT_TRUE(harness.sim.Send(Bytes("a-to-b")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b-to-a")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("a-to-b")));
    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("b-to-a")));
    EXPECT_EQ(harness.sim.Stats(), SimulatedTransportStats{});
}

TEST(SimulatedTransportTest, OutboundDrop_EveryNthReportsSuccess) {
    SimulatedTransportConfig config;
    config.outboundDropPeriod = 2U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    EXPECT_TRUE(harness.sim.Send(Bytes("m1")));
    EXPECT_TRUE(harness.sim.Send(Bytes("m2-dropped")));
    EXPECT_TRUE(harness.sim.Send(Bytes("m3")));
    EXPECT_TRUE(harness.sim.Send(Bytes("m4-dropped")));

    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("m1")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("m3")));
    EXPECT_FALSE(harness.rawB.TryReceive().has_value());
    EXPECT_EQ(harness.sim.Stats().outboundDropped, 2U);
}

TEST(SimulatedTransportTest, InboundDrop) {
    SimulatedTransportConfig config;
    config.inboundDropPeriod = 3U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    ASSERT_TRUE(harness.rawB.Send(Bytes("b1")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b2")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b3-dropped")));

    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("b1")));
    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("b2")));
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_EQ(harness.sim.Stats().inboundDropped, 1U);
}

TEST(SimulatedTransportTest, OutboundDelay_HeldUntilDue) {
    SimulatedTransportConfig config;
    config.outboundDelayMs = 100U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    ASSERT_TRUE(harness.sim.Send(Bytes("held")));
    EXPECT_FALSE(harness.rawB.TryReceive().has_value());

    harness.Advance(1099U);
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_FALSE(harness.rawB.TryReceive().has_value());

    harness.Advance(1100U);
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("held")));
}

TEST(SimulatedTransportTest, InboundDelay_HeldUntilDue) {
    SimulatedTransportConfig config;
    config.inboundDelayMs = 100U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    ASSERT_TRUE(harness.rawB.Send(Bytes("held")));
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_FALSE(harness.sim.TryReceive().has_value());

    harness.Advance(1100U);
    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("held")));
}

TEST(SimulatedTransportTest, DelayPreservesFifoOrder) {
    SimulatedTransportConfig config;
    config.outboundDelayMs = 50U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    ASSERT_TRUE(harness.sim.Send(Bytes("first")));
    ASSERT_TRUE(harness.sim.Send(Bytes("second")));
    ASSERT_TRUE(harness.sim.Send(Bytes("third")));

    harness.Advance(1050U);
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("first")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("second")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("third")));
}

TEST(SimulatedTransportTest, DelayQueueFull_ReportsFalse) {
    SimulatedTransportConfig config;
    config.outboundDelayMs = 10000U;
    config.maxPendingDatagrams = 1U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    EXPECT_TRUE(harness.sim.Send(Bytes("first")));
    EXPECT_FALSE(harness.sim.Send(Bytes("second")));
    EXPECT_EQ(harness.sim.Stats().outboundDroppedFull, 1U);

    harness.Advance(11000U);
    EXPECT_FALSE(harness.sim.TryReceive().has_value());
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("first")));
    EXPECT_FALSE(harness.rawB.TryReceive().has_value());
}

TEST(SimulatedTransportTest, SetConfig_Midstream) {
    ConditionsHarness harness = ConditionsHarness::Make(SimulatedTransportConfig{});

    ASSERT_TRUE(harness.sim.Send(Bytes("before")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("before")));

    SimulatedTransportConfig blackout;
    blackout.outboundDropPeriod = 1U;
    harness.sim.SetConfig(blackout);
    EXPECT_TRUE(harness.sim.Send(Bytes("lost")));
    EXPECT_FALSE(harness.rawB.TryReceive().has_value());
    EXPECT_EQ(harness.sim.Stats().outboundDropped, 1U);

    harness.sim.SetConfig(SimulatedTransportConfig{});
    ASSERT_TRUE(harness.sim.Send(Bytes("after")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("after")));
}

TEST(SimulatedTransportTest, Stats_MixedExchange) {
    SimulatedTransportConfig config;
    config.outboundDropPeriod = 2U;
    config.inboundDropPeriod = 3U;
    ConditionsHarness harness = ConditionsHarness::Make(config);

    ASSERT_TRUE(harness.sim.Send(Bytes("m1")));
    ASSERT_TRUE(harness.sim.Send(Bytes("m2-dropped")));
    ASSERT_TRUE(harness.sim.Send(Bytes("m3")));
    ASSERT_TRUE(harness.sim.Send(Bytes("m4-dropped")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b1")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b2")));
    ASSERT_TRUE(harness.rawB.Send(Bytes("b3-dropped")));

    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("m1")));
    EXPECT_EQ(harness.rawB.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("m3")));
    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("b1")));
    EXPECT_EQ(harness.sim.TryReceive(), std::optional<std::vector<std::uint8_t>>(Bytes("b2")));
    EXPECT_EQ(harness.sim.Stats(), (SimulatedTransportStats{2U, 0U, 1U, 0U}));
}

} // namespace
} // namespace UVE::Network::Tests
