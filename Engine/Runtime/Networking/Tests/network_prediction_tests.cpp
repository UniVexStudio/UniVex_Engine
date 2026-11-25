// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_prediction.h"

#include "network_conditions.h"
#include "character_prediction.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Velocity-carry integrator: the input pushes, the velocity decays, the position follows.
// Nonlinear in application order (early inputs decay more), so an exactly-matching trajectory
// proves the server applied every tick once, in order - any skip, dup, or swap shows.
[[nodiscard]] PredictionStepper CarryStepper(const float gain = 20.0F) {
    return [gain](const PredictedState& state, const PredictedInput& input,
                  const float dt) {
        PredictedState next = state;
        next.velocity = state.velocity * 0.9F + input.move * (gain * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictedInput Push(const float x) {
    PredictedInput input;
    input.move.x = x;
    return input;
}

[[nodiscard]] std::vector<std::uint8_t> Tagged(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const char c : text) {
        datagram.push_back(static_cast<std::uint8_t>(c));
    }
    return datagram;
}

struct PredictionHarness final {
    LoopbackTransportPair link;
    NetworkPrediction client;
    NetworkPrediction server;

    void UpdateBoth(const std::uint64_t nowMs) {
        client.Update(nowMs);
        server.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return client.Session().State() == SessionState::Established &&
               server.Session().State() == SessionState::Established;
    }
};

[[nodiscard]] PredictionHarness MakePrediction(PredictionStepper clientStepper,
                                                     PredictionStepper serverStepper,
                                                     PredictedState initial = PredictedState{}) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkPrediction client(std::move(sessionA), SessionRole::Client,
                                std::move(clientStepper), initial);
    NetworkPrediction server(std::move(sessionB), SessionRole::Server,
                                std::move(serverStepper), initial);
    return PredictionHarness{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkPredictionTest, Client_PredictsLocallyWithNoSession) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());

    // Ticks are 1-based and never need the server: three offline ticks simulate and queue,
    // sending nothing.
    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        EXPECT_EQ(harness.client.Tick(input), n);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.client.LocalState(), expect);
    EXPECT_EQ(harness.client.Stats().inputsSent, 0U);
    EXPECT_EQ(harness.client.LastAckedTick(), 0U);
}

TEST(NetworkPredictionTest, Tick_OnServerAnswersZero) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.server.Tick(Push(1.0F)), kInvalidPredictionTick);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 0U);
    EXPECT_EQ(harness.server.LastAppliedTick(), 0U);
}

TEST(NetworkPredictionTest, Server_AppliesInputsInOrderAndAcks) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));

    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(input), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_EQ(harness.server.Stats().inputsApplied, 5U);
    EXPECT_EQ(harness.server.LastAppliedTick(), 5U);
    EXPECT_EQ(harness.server.LocalState(), expect);
    EXPECT_EQ(harness.client.LastAckedTick(), 5U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), expect);
    EXPECT_EQ(harness.client.Stats().inputsSent, 5U);
    EXPECT_EQ(harness.server.Stats().statesSent, 5U);
}

TEST(NetworkPredictionTest, Mispredict_SnapsAndReplaysAgainstAServerWall) {
    // The server's world has a wall at x = 5 the client's prediction knows nothing about:
    // every tick past it disagrees, every disagreement snaps back, and the client never runs
    // away - then the input stops and both sides settle to the same pose.
    PredictionStepper walled = [](const PredictedState& state,
                                     const PredictedInput& input, const float dt) {
        PredictedState next = CarryStepper(60.0F)(state, input, dt);
        if (next.position.x > 5.0F) {
            next.position.x = 5.0F;
            if (next.velocity.x > 0.0F) {
                next.velocity.x = 0.0F;
            }
        }
        return next;
    };
    PredictionHarness harness = MakePrediction(CarryStepper(60.0F), std::move(walled));
    ASSERT_TRUE(harness.Establish(1000U));

    float farthest = 0.0F;
    for (std::uint64_t n = 1U; n <= 40U; ++n) {
        ASSERT_EQ(harness.client.Tick(Push(1.0F)), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
        farthest = std::max(farthest, harness.client.LocalState().position.x);
    }
    EXPECT_GT(harness.client.Stats().reconciles, 0U);
    // Snapped back every tick: the overshoot past the wall is bounded by the unacked window,
    // never the forty ticks of input behind it.
    EXPECT_LE(farthest, 5.5F);
    const std::uint64_t reconciles = harness.client.Stats().reconciles;

    for (std::uint64_t n = 41U; n <= 50U; ++n) {
        ASSERT_EQ(harness.client.Tick(PredictedInput{}), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
    EXPECT_EQ(harness.client.Stats().reconciles, reconciles);
    EXPECT_EQ(harness.client.LocalState(), harness.server.LocalState());
    EXPECT_EQ(harness.server.LocalState().position.x, 5.0F);
}

TEST(NetworkPredictionTest, LossyWire_RecoversEveryTick) {
    // The engine's own loss simulator on the client's uplink, starting mid-stream: every
    // third datagram dies, the triple repeat covers it, and all twelve ticks still apply in
    // order with zero reconciles.
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    std::uint64_t fakeMs = 1000U;
    TransportClock clock = [&fakeMs]() { return fakeMs; };
    auto lossyEnd = std::make_unique<SimulatedTransport>(
        std::make_unique<LoopbackEndpoint>(pair.a), clock, SimulatedTransportConfig{});
    SimulatedTransport* const lossy = lossyEnd.get();
    NetworkSession sessionA(SessionRole::Client, std::move(lossyEnd));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkPrediction client(std::move(sessionA), SessionRole::Client, CarryStepper());
    NetworkPrediction server(std::move(sessionB), SessionRole::Server, CarryStepper());

    std::uint64_t nowMs = 1000U;
    ASSERT_TRUE(client.Session().Connect(nowMs));
    for (int i = 0; i < 4; ++i) {
        client.Update(nowMs);
        server.Update(nowMs);
    }
    ASSERT_EQ(client.Session().State(), SessionState::Established);
    ASSERT_EQ(server.Session().State(), SessionState::Established);

    SimulatedTransportConfig loss;
    loss.outboundDropPeriod = 3U;
    lossy->SetConfig(loss);

    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 12U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(client.Tick(input), n);
        client.Update(nowMs);
        server.Update(nowMs);
        client.Update(nowMs);
        server.Update(nowMs);
        nowMs += 16U;
        fakeMs = nowMs;
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_GT(lossy->Stats().outboundDropped, 0U);
    EXPECT_EQ(server.Stats().inputsApplied, 12U);
    EXPECT_EQ(server.LastAppliedTick(), 12U);
    EXPECT_EQ(server.LocalState(), expect);
    EXPECT_EQ(client.LastAckedTick(), 12U);
    EXPECT_EQ(client.Stats().reconciles, 0U);
    EXPECT_EQ(client.LocalState(), expect);
}

TEST(NetworkPredictionTest, DuplicateInput_ResendsLastState) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.Tick(Push(0.1F)), 1U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.client.Tick(Push(0.2F)), 2U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.server.LastAppliedTick(), 2U);
    ASSERT_EQ(harness.client.LastAckedTick(), 2U);

    // Tick 3's message goes astray (stolen, never delivered); tick 4's window carries both
    // across, and the stray copy replayed after that reads as the duplicate it now is.
    ASSERT_EQ(harness.client.Tick(Push(0.3F)), 3U);
    const std::optional<std::vector<std::uint8_t>> stray = harness.link.b.TryReceive();
    ASSERT_TRUE(stray.has_value());
    ASSERT_EQ(harness.client.Tick(Push(0.4F)), 4U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.server.LastAppliedTick(), 4U);
    const std::uint64_t statesSent = harness.server.Stats().statesSent;

    ASSERT_TRUE(harness.link.a.Send(*stray));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.server.Stats().inputsIgnoredDuplicate, 1U);
    EXPECT_EQ(harness.server.Stats().statesSent, statesSent + 1U);
    EXPECT_EQ(harness.client.Stats().statesIgnoredStale, 1U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
}

TEST(NetworkPredictionTest, Reconnect_ResumesMidStream) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    constexpr std::uint64_t kToken = 0x0102030405060708U;
    harness.client.Session().SetReconnectConfig(SessionReconnectConfig{50U, 20U});
    ASSERT_TRUE(harness.client.Session().Connect(1000U, kToken));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    ASSERT_EQ(harness.client.Session().State(), SessionState::Established);

    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(input), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }
    ASSERT_EQ(harness.server.LastAppliedTick(), 3U);

    // Ticks 4-5 simulate while established and their messages die in the outage below;
    // then the wire goes quiet past the silence timeout and the client redials on its own.
    for (std::uint64_t n = 4U; n <= 5U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(input), n);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.client.LocalState(), expect);

    // The outage swallows ticks 4-5's messages mid-wire: the silence past the timeout is real,
    // and the unacked inputs survive in history for the resumed session to drain.
    while (harness.link.b.TryReceive().has_value()) {
    }

    std::uint64_t nowMs = 1000U + kDefaultSessionTimeoutMs;
    harness.client.Update(nowMs);
    harness.server.Update(nowMs);
    ASSERT_EQ(harness.client.Session().State(), SessionState::Reconnecting);
    ASSERT_EQ(harness.server.Session().State(), SessionState::Disconnected);

    bool resumed = false;
    for (int i = 0; i < 2000 && !resumed; ++i) {
        harness.UpdateBoth(nowMs);
        ++nowMs;
        resumed = harness.client.Session().State() == SessionState::Established;
    }
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(harness.server.Session().DidResume());
    EXPECT_TRUE(harness.client.Session().DidResume());

    // The backlog drains through the resumed session and the stream continues mid-numbering:
    // nothing lost, nothing replayed twice, nothing to reconcile.
    for (std::uint64_t n = 6U; n <= 8U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(input), n);
        harness.UpdateBoth(nowMs);
        harness.UpdateBoth(nowMs);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.server.Stats().inputsApplied, 8U);
    EXPECT_EQ(harness.server.LastAppliedTick(), 8U);
    EXPECT_EQ(harness.server.LocalState(), expect);
    EXPECT_EQ(harness.client.LastAckedTick(), 8U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), expect);
    // Three sends before the outage, two swallowed by it, three after the resume.
    EXPECT_EQ(harness.client.Stats().inputsSent, 8U);
}

TEST(NetworkPredictionTest, Malformed_ForeignFramesIgnored) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.Tick(Push(0.1F)), 1U);

    // Steal tick 1's real input message off the wire to mutate: no codec duplicated here.
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    // Server-ward: a bad prediction kind (byte 1 - byte 0 is the session tag), a truncation,
    // a trailing byte, and session-Data junk.
    std::vector<std::uint8_t> badKind = *wire;
    badKind[1U] = 0x09U;
    ASSERT_TRUE(harness.link.a.Send(badKind));
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>(wire->begin(), wire->begin() + 4)));
    std::vector<std::uint8_t> trailing = *wire;
    trailing.push_back(0xFFU);
    ASSERT_TRUE(harness.link.a.Send(trailing));
    ASSERT_TRUE(harness.link.a.Send(
        Tagged(static_cast<std::uint8_t>(SessionMessageTag::Data), "junk")));

    // Client-ward: the pristine input message (the wrong direction on a client) plus a
    // truncated state.
    ASSERT_TRUE(harness.link.b.Send(*wire));
    ASSERT_TRUE(harness.link.b.Send(
        Tagged(static_cast<std::uint8_t>(SessionMessageTag::Data), std::string("\x02oops"))));

    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.server.Stats().framesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.client.Stats().framesIgnoredMalformed, 2U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 0U);
    EXPECT_EQ(harness.client.Stats().statesReceived, 0U);
}

TEST(NetworkPredictionTest, GapTimeout_SkipsUnfillableWaits) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));

    // Seventy ticks with no server pump: the client's history holds 7-70 and the wire holds
    // seventy messages - then every message dies, and one more tick leaves the server a gap
    // starting at 1 that no repeat can ever fill.
    for (std::uint64_t n = 1U; n <= 70U; ++n) {
        ASSERT_EQ(harness.client.Tick(Push(0.05F * static_cast<float>(n))), n);
        harness.client.Update(1000U);
    }
    while (harness.link.b.TryReceive().has_value()) {
    }
    ASSERT_EQ(harness.client.Tick(Push(3.55F)), 71U);

    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 0U);
    EXPECT_EQ(harness.server.Stats().inputBufferOverruns, 0U);

    // Half a second of no progress on the gap: the stream jumps past it (tick 71's window
    // is the oldest survivors, 8-10) and applies what it holds instead of waiting forever.
    harness.server.Update(1000U + kPredictionGapTimeoutMs);
    EXPECT_EQ(harness.server.Stats().inputBufferOverruns, 1U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 3U);
    EXPECT_EQ(harness.server.LastAppliedTick(), 10U);
}

// The production stepper (character_prediction_uve.h): the real character intent plus
// kinematic integration, with the jump's liftoff packed into the prediction.
[[nodiscard]] PredictionStepper MoverStepper() {
    return Scene::MakeCharacterPredictionStepper(Scene::CharacterControllerComponent{},
                                                    -9.81F);
}

TEST(NetworkPredictionTest, RealMover_JumpReconcilesAgainstAServerFloor) {
    // Production end to end: both sides step the real character intent plus kinematic
    // integration, but only the server knows the floor (y = 0, landing grounded). The client
    // jumps and arcs cleanly - the prediction carries the liftoff, so the flight agrees -
    // then falls through the floor it cannot see, and every server truth past the crossing
    // snaps it back until both sides rest on the floor together.
    PredictionStepper floored = [](const PredictedState& state,
                                      const PredictedInput& input, const float dt) {
        PredictedState next = MoverStepper()(state, input, dt);
        if (next.position.y <= 0.0F) {
            next.position.y = 0.0F;
            if (next.velocity.y < 0.0F) {
                next.velocity.y = 0.0F;
            }
            next.grounded = true;
        } else {
            next.grounded = false;
        }
        return next;
    };
    PredictedState spawn;
    spawn.grounded = true;
    PredictionHarness harness =
        MakePrediction(MoverStepper(), std::move(floored), spawn);
    ASSERT_TRUE(harness.Establish(1000U));

    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        ASSERT_EQ(harness.client.Tick(PredictedInput{}), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
    // Standing agreement needs no correction: both sides rest grounded at y = 0.
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), harness.server.LocalState());

    PredictedInput jump;
    jump.jumpPressed = true;
    ASSERT_EQ(harness.client.Tick(jump), 5U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    // The liftoff agrees: the flight only diverges where the floor is.
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), harness.server.LocalState());

    for (std::uint64_t n = 6U; n <= 60U; ++n) {
        ASSERT_EQ(harness.client.Tick(Push(1.0F)), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
    // Still arcing at tick 60 (about 0.85 m up): identical steppers, identical inputs, no
    // floor crossed yet, nothing to correct.
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    for (std::uint64_t n = 61U; n <= 80U; ++n) {
        ASSERT_EQ(harness.client.Tick(PredictedInput{}), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }

    // The landing crossed the floor the client cannot see: corrected every tick since, and
    // resting together now.
    EXPECT_GT(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.server.LocalState().position.y, 0.0F);
    EXPECT_TRUE(harness.server.LocalState().grounded);
    EXPECT_EQ(harness.client.LocalState(), harness.server.LocalState());
}

TEST(NetworkPredictionTest, EmptyStepper_StandsStill) {
    PredictionHarness harness =
        MakePrediction(PredictionStepper{}, PredictionStepper{});
    ASSERT_TRUE(harness.Establish(1000U));
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(harness.client.Tick(Push(1.0F)), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
    EXPECT_EQ(harness.server.Stats().inputsApplied, 3U);
    EXPECT_EQ(harness.client.LastAckedTick(), 3U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), PredictedState{});
    EXPECT_EQ(harness.server.LocalState(), PredictedState{});
}


TEST(NetworkPredictionTest, MultiPawn_TwoPawnsInterleaveIndependently) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    // Pawn 1 spawns apart, seeded alike on both sides.
    PredictedState seed;
    seed.position = Math::Vector3{10.0F, 0.0F, 0.0F};
    ASSERT_TRUE(harness.client.SeedPawn(1U, seed));
    ASSERT_TRUE(harness.server.SeedPawn(1U, seed));

    PredictedState expect0;
    PredictedState expect1 = seed;
    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        const PredictedInput input0 = Push(0.05F * static_cast<float>(n));
        const PredictedInput input1 = Push(-0.1F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(0U, input0), n);
        ASSERT_EQ(harness.client.Tick(1U, input1), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
        expect0 = CarryStepper()(expect0, input0, 1.0F / 60.0F);
        expect1 = CarryStepper()(expect1, input1, 1.0F / 60.0F);
    }
    // Per-pawn numbering, ordering, and acks: two streams, one session, no crosstalk.
    EXPECT_EQ(harness.server.LastAppliedTick(0U), 4U);
    EXPECT_EQ(harness.server.LastAppliedTick(1U), 4U);
    EXPECT_EQ(harness.server.LocalState(0U), expect0);
    EXPECT_EQ(harness.server.LocalState(1U), expect1);
    EXPECT_EQ(harness.client.LastAckedTick(0U), 4U);
    EXPECT_EQ(harness.client.LastAckedTick(1U), 4U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(0U), expect0);
    EXPECT_EQ(harness.client.LocalState(1U), expect1);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 8U);
}

TEST(NetworkPredictionTest, MultiPawn_GapOnOnePawnDoesNotHoldTheOther) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));

    // Seventy pawn-1 ticks with no server pump, then every message dies: the server holds a
    // pawn-1 gap starting at 1 that no repeat can ever fill.
    for (std::uint64_t n = 1U; n <= 70U; ++n) {
        ASSERT_EQ(harness.client.Tick(1U, Push(0.01F)), n);
        harness.client.Update(1000U);
    }
    while (harness.link.b.TryReceive().has_value()) {
    }
    ASSERT_EQ(harness.client.Tick(1U, Push(0.01F)), 71U);

    // Pawn 0 starts fresh while pawn 1 is gapped: its stream numbers from 1 and applies at
    // once - the stall is pawn 1's alone.
    ASSERT_EQ(harness.client.Tick(0U, Push(0.05F)), 1U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.server.LastAppliedTick(0U), 1U);
    EXPECT_EQ(harness.server.LastAppliedTick(1U), 0U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 1U);

    // Half a second of no progress on pawn 1's gap jumps only that pawn (tick 71's window
    // carries the oldest survivors, 8-10) while pawn 0 keeps flowing.
    ASSERT_EQ(harness.client.Tick(0U, Push(0.05F)), 2U);
    harness.server.Update(1000U + kPredictionGapTimeoutMs);
    harness.UpdateBoth(1000U + kPredictionGapTimeoutMs);
    harness.UpdateBoth(1000U + kPredictionGapTimeoutMs);
    EXPECT_EQ(harness.server.Stats().inputBufferOverruns, 1U);
    EXPECT_EQ(harness.server.LastAppliedTick(1U), 10U);
    EXPECT_EQ(harness.server.LastAppliedTick(0U), 2U);
}

TEST(NetworkPredictionTest, MultiPawn_PawnZeroCallsMatchThePrimary) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    // The v1 calls are exactly pawn 0: numbering, states, and acks agree either way.
    ASSERT_EQ(harness.client.Tick(Push(0.1F)), 1U);
    ASSERT_EQ(harness.client.Tick(0U, Push(0.2F)), 2U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.client.LocalState(), harness.client.LocalState(0U));
    EXPECT_EQ(harness.client.LastAckedTick(), harness.client.LastAckedTick(0U));
    EXPECT_EQ(harness.server.LastAppliedTick(), harness.server.LastAppliedTick(0U));
    EXPECT_EQ(harness.client.LastAckedTick(0U), 2U);
    // Untouched pawns read default: no stream, no numbering, no surprise.
    EXPECT_EQ(harness.client.LocalState(7U), PredictedState{});
    EXPECT_EQ(harness.client.LastAckedTick(7U), 0U);
    EXPECT_EQ(harness.server.LastAppliedTick(7U), 0U);
}

TEST(NetworkPredictionTest, MultiPawn_OverCapPawnsRefuse) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    // Sixteen live pawns is the table: pawns 0-15 tick and drain one wave each (a window
    // only carries three, so each tick's input applies before the next pawn's is sent), then
    // the 17th answers 0 and steps nothing.
    for (std::uint32_t raw = 0U; raw < 16U; ++raw) {
        const PredictionPawn pawn = static_cast<PredictionPawn>(raw);
        ASSERT_EQ(harness.client.Tick(pawn, Push(0.01F)), 1U);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
    }
    EXPECT_EQ(harness.client.Tick(16U, Push(0.01F)), kInvalidPredictionTick);
    EXPECT_EQ(harness.client.LocalState(16U), PredictedState{});
    EXPECT_EQ(harness.server.Stats().inputsApplied, 16U);
    EXPECT_EQ(harness.client.LastAckedTick(15U), 1U);

    // Past the cap on the wire: a live message re-addressed to pawn 16 refuses - counted,
    // never buffered, never applied.
    ASSERT_EQ(harness.client.Tick(Push(0.02F)), 2U);
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    std::vector<std::uint8_t> mutant = *wire;
    mutant[11U] = static_cast<std::uint8_t>(16U); // tag, kind, count, tick, then the pawn.
    ASSERT_TRUE(harness.link.a.Send(mutant));
    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.Stats().inputsRefusedOverPawnCap, 1U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 16U);
}

TEST(NetworkPredictionTest, SeedPawn_BeforeFirstTickOnly) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    PredictedState seed;
    seed.position = Math::Vector3{5.0F, 0.0F, 0.0F};
    // A fresh pawn seeds on both sides; pawn 0 and a re-seed refuse, touching nothing.
    EXPECT_TRUE(harness.client.SeedPawn(3U, seed));
    EXPECT_TRUE(harness.server.SeedPawn(3U, seed));
    EXPECT_EQ(harness.client.LocalState(3U), seed);
    EXPECT_FALSE(harness.client.SeedPawn(3U, PredictedState{}));
    EXPECT_EQ(harness.client.LocalState(3U), seed);
    EXPECT_FALSE(harness.client.SeedPawn(0U, PredictedState{}));
    // Once the pawn runs - a tick here, an input landed there - seeding refuses.
    ASSERT_EQ(harness.client.Tick(4U, Push(0.1F)), 1U);
    EXPECT_FALSE(harness.client.SeedPawn(4U, seed));
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_FALSE(harness.server.SeedPawn(4U, seed));
    EXPECT_EQ(harness.server.LocalState(3U), seed);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
}

TEST(NetworkPredictionTest, VariableDt_MixedStepsReproduceExactly) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    // Three rates share one stream: the server steps the transmitted dts, so every tick
    // agrees with the client's manual replay - zero reconciles across the rate changes.
    const float dts[3] = {1.0F / 60.0F, 1.0F / 30.0F, 1.0F / 120.0F};
    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 6U; ++n) {
        const float dt = dts[(n - 1U) % 3U];
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.Tick(0U, input, dt), n);
        harness.UpdateBoth(1000U);
        harness.UpdateBoth(1000U);
        expect = CarryStepper()(expect, input, dt);
    }
    EXPECT_EQ(harness.server.LastAppliedTick(), 6U);
    EXPECT_EQ(harness.server.LocalState(), expect);
    EXPECT_EQ(harness.client.LastAckedTick(), 6U);
    EXPECT_EQ(harness.client.Stats().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalState(), expect);
}

TEST(NetworkPredictionTest, VariableDt_ReplayUsesStoredDts) {
    // The server's world has a wall at x = 5 the client cannot see: one tick runs past it,
    // the next backs away. The snap replays the backoff with the stored dt, so it lands
    // exactly on the server's truth - one reconcile, never two.
    PredictionStepper walled = [](const PredictedState& state,
                                     const PredictedInput& input, const float dt) {
        PredictedState next = CarryStepper()(state, input, dt);
        if (next.position.x > 5.0F) {
            next.position.x = 5.0F;
            next.velocity.x = 0.0F;
        }
        return next;
    };
    PredictionHarness harness = MakePrediction(CarryStepper(), walled);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.Tick(0U, Push(300.0F), 1.0F / 30.0F), 1U);
    ASSERT_EQ(harness.client.Tick(0U, Push(-300.0F), 1.0F / 30.0F), 2U);
    harness.UpdateBoth(1000U);
    harness.UpdateBoth(1000U);
    EXPECT_EQ(harness.client.Stats().reconciles, 1U);
    EXPECT_EQ(harness.client.LastAckedTick(), 2U);
    EXPECT_EQ(harness.client.LocalState(), harness.server.LocalState());
}

TEST(NetworkPredictionTest, VariableDt_BadDtRefused) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    // Zero, negative, infinite, NaN, and past-a-second: every bad dt answers 0, stepping
    // nothing, storing nothing, sending nothing - the stream stays exactly where it was.
    const float badDts[5] = {0.0F, -1.0F / 60.0F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN(), 1.5F};
    for (const float dt : badDts) {
        EXPECT_EQ(harness.client.Tick(0U, Push(1.0F), dt), kInvalidPredictionTick);
    }
    EXPECT_EQ(harness.client.Stats().inputsSent, 0U);
    EXPECT_EQ(harness.client.LocalState(), PredictedState{});
    // The next good tick numbers from 1: the refusals never touched the stream.
    EXPECT_EQ(harness.client.Tick(0U, Push(1.0F), 1.0F / 60.0F), 1U);
}

TEST(NetworkPredictionTest, VariableDt_MalformedDtOnWire) {
    PredictionHarness harness = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.client.Tick(Push(0.1F)), 1U);
    // Steal tick 1's real input message and poison its dt: tag, kind, count, tick, pawn,
    // then four dt bytes - NaN where a step used to be.
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    ASSERT_GE(wire->size(), 17U);
    std::vector<std::uint8_t> poisoned = *wire;
    poisoned[12U] = 0xFFU;
    poisoned[13U] = 0xFFU;
    poisoned[14U] = 0xFFU;
    poisoned[15U] = 0xFFU;
    ASSERT_TRUE(harness.link.a.Send(poisoned));
    harness.server.Update(1000U);
    EXPECT_EQ(harness.server.Stats().framesIgnoredMalformed, 1U);
    EXPECT_EQ(harness.server.Stats().inputsReceived, 0U);
    EXPECT_EQ(harness.server.Stats().inputsApplied, 0U);
}

// One relay leg: the relay's subscriber session (server role) facing an observer's session
// (client role) across a loopback pair. The relay owns the subscriber side once added, so
// the handshake pumps through the relay itself.
struct RelayLeg final {
    LoopbackTransportPair link;
    NetworkSession subscriber;
    PredictionObserver observer;
};

[[nodiscard]] RelayLeg MakeRelayLeg() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession subscriber(SessionRole::Server,
                                 std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession watcher(SessionRole::Client,
                              std::make_unique<LoopbackEndpoint>(pair.b));
    PredictionObserver observer(std::move(watcher));
    return RelayLeg{std::move(pair), std::move(subscriber), std::move(observer)};
}

TEST(NetworkPredictionTest, Relay_FansOutToTwoSubscribers) {
    PredictionHarness game = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(game.Establish(1000U));
    PredictionRelay relay;
    RelayLeg first = MakeRelayLeg();
    RelayLeg second = MakeRelayLeg();
    ASSERT_EQ(relay.AddSubscriber(std::move(first.subscriber)), 0U);
    ASSERT_EQ(relay.AddSubscriber(std::move(second.subscriber)), 1U);
    ASSERT_EQ(relay.SubscriberCount(), 2U);
    ASSERT_TRUE(first.observer.Session().Connect(1000U));
    ASSERT_TRUE(second.observer.Session().Connect(1000U));
    for (int i = 0; i < 4; ++i) {
        first.observer.Update(1000U);
        second.observer.Update(1000U);
        relay.Update(1000U);
    }

    // Three applied ticks published the app's way - newest applied tick plus the whole
    // authoritative state - reach both watchers.
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(game.client.Tick(Push(0.05F * static_cast<float>(n))), n);
        game.UpdateBoth(1000U);
        game.UpdateBoth(1000U);
        ASSERT_EQ(game.server.LastAppliedTick(), n);
        EXPECT_EQ(relay.Publish(game.server.LastAppliedTick(), 0U,
                                   game.server.LocalState()),
                  2U);
        first.observer.Update(1000U);
        second.observer.Update(1000U);
    }
    const std::optional<PredictedStateMessage> latestFirst =
        first.observer.Latest(0U);
    const std::optional<PredictedStateMessage> latestSecond =
        second.observer.Latest(0U);
    ASSERT_TRUE(latestFirst.has_value());
    ASSERT_TRUE(latestSecond.has_value());
    EXPECT_EQ(latestFirst->tick, 3U);
    EXPECT_EQ(latestFirst->state, game.server.LocalState());
    EXPECT_EQ(latestSecond->tick, 3U);
    EXPECT_EQ(latestSecond->state, game.server.LocalState());
    EXPECT_EQ(relay.Stats().statesRelayed, 6U);
    EXPECT_EQ(relay.Stats().sendsFailed, 0U);
}

TEST(NetworkPredictionTest, Relay_LateSubscriberReceivesLiveOnly) {
    PredictionHarness game = MakePrediction(CarryStepper(), CarryStepper());
    ASSERT_TRUE(game.Establish(1000U));
    PredictionRelay relay;
    RelayLeg early = MakeRelayLeg();
    ASSERT_EQ(relay.AddSubscriber(std::move(early.subscriber)), 0U);
    ASSERT_TRUE(early.observer.Session().Connect(1000U));
    for (int i = 0; i < 4; ++i) {
        early.observer.Update(1000U);
        relay.Update(1000U);
    }

    // Ticks 1-2 publish to the early watcher only.
    for (std::uint64_t n = 1U; n <= 2U; ++n) {
        ASSERT_EQ(game.client.Tick(Push(0.05F * static_cast<float>(n))), n);
        game.UpdateBoth(1000U);
        game.UpdateBoth(1000U);
        EXPECT_EQ(relay.Publish(game.server.LastAppliedTick(), 0U,
                                   game.server.LocalState()),
                  1U);
        early.observer.Update(1000U);
    }
    // A second watcher joins mid-stream: its handshake sees no backlog.
    RelayLeg late = MakeRelayLeg();
    ASSERT_EQ(relay.AddSubscriber(std::move(late.subscriber)), 1U);
    ASSERT_TRUE(late.observer.Session().Connect(1000U));
    for (int i = 0; i < 4; ++i) {
        early.observer.Update(1000U);
        late.observer.Update(1000U);
        relay.Update(1000U);
    }
    EXPECT_FALSE(late.observer.Latest(0U).has_value());
    // Tick 3 reaches both: live from here on.
    ASSERT_EQ(game.client.Tick(Push(0.15F)), 3U);
    game.UpdateBoth(1000U);
    game.UpdateBoth(1000U);
    EXPECT_EQ(relay.Publish(game.server.LastAppliedTick(), 0U, game.server.LocalState()),
              2U);
    early.observer.Update(1000U);
    late.observer.Update(1000U);
    ASSERT_TRUE(late.observer.Latest(0U).has_value());
    EXPECT_EQ(late.observer.Latest(0U)->tick, 3U);
    EXPECT_EQ(early.observer.Latest(0U)->tick, 3U);
    EXPECT_EQ(relay.Stats().statesRelayed, 4U);
}

TEST(NetworkPredictionTest, Observer_TracksLatestPerPawnAndIgnoresStale) {
    PredictionRelay relay;
    RelayLeg leg = MakeRelayLeg();
    ASSERT_EQ(relay.AddSubscriber(std::move(leg.subscriber)), 0U);
    ASSERT_TRUE(leg.observer.Session().Connect(1000U));
    for (int i = 0; i < 4; ++i) {
        leg.observer.Update(1000U);
        relay.Update(1000U);
    }
    PredictedState first;
    first.position = Math::Vector3{1.0F, 0.0F, 0.0F};
    PredictedState second;
    second.position = Math::Vector3{2.0F, 0.0F, 0.0F};
    PredictedState other;
    other.position = Math::Vector3{9.0F, 0.0F, 0.0F};
    // Out of order on purpose: pawn 0's tick 5 lands before its tick 2, pawn 1 rides along.
    EXPECT_EQ(relay.Publish(5U, 0U, second), 1U);
    EXPECT_EQ(relay.Publish(2U, 0U, first), 1U);
    EXPECT_EQ(relay.Publish(1U, 1U, other), 1U);
    leg.observer.Update(1000U);
    // Newest per pawn wins; the stale tick counts but never lands.
    ASSERT_TRUE(leg.observer.Latest(0U).has_value());
    EXPECT_EQ(leg.observer.Latest(0U)->tick, 5U);
    EXPECT_EQ(leg.observer.Latest(0U)->state, second);
    ASSERT_TRUE(leg.observer.Latest(1U).has_value());
    EXPECT_EQ(leg.observer.Latest(1U)->state, other);
    EXPECT_EQ(leg.observer.Stats().statesReceived, 3U);
    EXPECT_EQ(leg.observer.Stats().statesIgnoredStale, 1U);
    EXPECT_FALSE(leg.observer.Latest(2U).has_value());
    // Removal drops the leg: publishes reach nobody, unknown indices refuse.
    EXPECT_TRUE(relay.RemoveSubscriber(0U));
    EXPECT_EQ(relay.SubscriberCount(), 0U);
    EXPECT_FALSE(relay.RemoveSubscriber(0U));
    EXPECT_EQ(relay.Publish(6U, 0U, second), 0U);
}

} // namespace
} // namespace UVE::Network::Tests
