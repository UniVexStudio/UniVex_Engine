// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_prediction_uve.h"

#include "uve/network/network_conditions_uve.h"
#include "uve/objects/3d/character_prediction_uve.h"

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
[[nodiscard]] PredictionStepperUVE CarryStepperUVE(const float gain = 20.0F) {
    return [gain](const PredictedStateUVE& state, const PredictedInputUVE& input,
                  const float dt) {
        PredictedStateUVE next = state;
        next.velocity = state.velocity * 0.9F + input.move * (gain * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictedInputUVE PushUVE(const float x) {
    PredictedInputUVE input;
    input.move.x = x;
    return input;
}

[[nodiscard]] std::vector<std::uint8_t> TaggedUVE(const std::uint8_t tag, const std::string& text) {
    std::vector<std::uint8_t> datagram{tag};
    for (const char c : text) {
        datagram.push_back(static_cast<std::uint8_t>(c));
    }
    return datagram;
}

struct PredictionHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkPredictionUVE client;
    NetworkPredictionUVE server;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return client.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] PredictionHarnessUVE MakePredictionUVE(PredictionStepperUVE clientStepper,
                                                     PredictionStepperUVE serverStepper,
                                                     PredictedStateUVE initial = PredictedStateUVE{}) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkPredictionUVE client(std::move(sessionA), SessionRoleUVE::Client,
                                std::move(clientStepper), initial);
    NetworkPredictionUVE server(std::move(sessionB), SessionRoleUVE::Server,
                                std::move(serverStepper), initial);
    return PredictionHarnessUVE{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkPredictionUVETest, Client_PredictsLocallyWithNoSession) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());

    // Ticks are 1-based and never need the server: three offline ticks simulate and queue,
    // sending nothing.
    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        EXPECT_EQ(harness.client.TickUVE(input), n);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.client.LocalStateUVE(), expect);
    EXPECT_EQ(harness.client.StatsUVE().inputsSent, 0U);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 0U);
}

TEST(NetworkPredictionUVETest, Tick_OnServerAnswersZero) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.server.TickUVE(PushUVE(1.0F)), kInvalidPredictionTickUVE);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 0U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), 0U);
}

TEST(NetworkPredictionUVETest, Server_AppliesInputsInOrderAndAcks) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 5U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(input), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 5U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), 5U);
    EXPECT_EQ(harness.server.LocalStateUVE(), expect);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 5U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), expect);
    EXPECT_EQ(harness.client.StatsUVE().inputsSent, 5U);
    EXPECT_EQ(harness.server.StatsUVE().statesSent, 5U);
}

TEST(NetworkPredictionUVETest, Mispredict_SnapsAndReplaysAgainstAServerWall) {
    // The server's world has a wall at x = 5 the client's prediction knows nothing about:
    // every tick past it disagrees, every disagreement snaps back, and the client never runs
    // away - then the input stops and both sides settle to the same pose.
    PredictionStepperUVE walled = [](const PredictedStateUVE& state,
                                     const PredictedInputUVE& input, const float dt) {
        PredictedStateUVE next = CarryStepperUVE(60.0F)(state, input, dt);
        if (next.position.x > 5.0F) {
            next.position.x = 5.0F;
            if (next.velocity.x > 0.0F) {
                next.velocity.x = 0.0F;
            }
        }
        return next;
    };
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(60.0F), std::move(walled));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    float farthest = 0.0F;
    for (std::uint64_t n = 1U; n <= 40U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PushUVE(1.0F)), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
        farthest = std::max(farthest, harness.client.LocalStateUVE().position.x);
    }
    EXPECT_GT(harness.client.StatsUVE().reconciles, 0U);
    // Snapped back every tick: the overshoot past the wall is bounded by the unacked window,
    // never the forty ticks of input behind it.
    EXPECT_LE(farthest, 5.5F);
    const std::uint64_t reconciles = harness.client.StatsUVE().reconciles;

    for (std::uint64_t n = 41U; n <= 50U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PredictedInputUVE{}), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
    EXPECT_EQ(harness.client.StatsUVE().reconciles, reconciles);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.server.LocalStateUVE());
    EXPECT_EQ(harness.server.LocalStateUVE().position.x, 5.0F);
}

TEST(NetworkPredictionUVETest, LossyWire_RecoversEveryTick) {
    // The engine's own loss simulator on the client's uplink, starting mid-stream: every
    // third datagram dies, the triple repeat covers it, and all twelve ticks still apply in
    // order with zero reconciles.
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    std::uint64_t fakeMs = 1000U;
    TransportClockUVE clock = [&fakeMs]() { return fakeMs; };
    auto lossyEnd = std::make_unique<SimulatedTransportUVE>(
        std::make_unique<LoopbackEndpointUVE>(pair.a), clock, SimulatedTransportConfigUVE{});
    SimulatedTransportUVE* const lossy = lossyEnd.get();
    NetworkSessionUVE sessionA(SessionRoleUVE::Client, std::move(lossyEnd));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkPredictionUVE client(std::move(sessionA), SessionRoleUVE::Client, CarryStepperUVE());
    NetworkPredictionUVE server(std::move(sessionB), SessionRoleUVE::Server, CarryStepperUVE());

    std::uint64_t nowMs = 1000U;
    ASSERT_TRUE(client.SessionUVE().ConnectUVE(nowMs));
    for (int i = 0; i < 4; ++i) {
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
    }
    ASSERT_EQ(client.SessionUVE().StateUVE(), SessionStateUVE::Established);
    ASSERT_EQ(server.SessionUVE().StateUVE(), SessionStateUVE::Established);

    SimulatedTransportConfigUVE loss;
    loss.outboundDropPeriod = 3U;
    lossy->SetConfigUVE(loss);

    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 12U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(client.TickUVE(input), n);
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        nowMs += 16U;
        fakeMs = nowMs;
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_GT(lossy->StatsUVE().outboundDropped, 0U);
    EXPECT_EQ(server.StatsUVE().inputsApplied, 12U);
    EXPECT_EQ(server.LastAppliedTickUVE(), 12U);
    EXPECT_EQ(server.LocalStateUVE(), expect);
    EXPECT_EQ(client.LastAckedTickUVE(), 12U);
    EXPECT_EQ(client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(client.LocalStateUVE(), expect);
}

TEST(NetworkPredictionUVETest, DuplicateInput_ResendsLastState) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.1F)), 1U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.2F)), 2U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.server.LastAppliedTickUVE(), 2U);
    ASSERT_EQ(harness.client.LastAckedTickUVE(), 2U);

    // Tick 3's message goes astray (stolen, never delivered); tick 4's window carries both
    // across, and the stray copy replayed after that reads as the duplicate it now is.
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.3F)), 3U);
    const std::optional<std::vector<std::uint8_t>> stray = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(stray.has_value());
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.4F)), 4U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.server.LastAppliedTickUVE(), 4U);
    const std::uint64_t statesSent = harness.server.StatsUVE().statesSent;

    ASSERT_TRUE(harness.link.a.SendUVE(*stray));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.server.StatsUVE().inputsIgnoredDuplicate, 1U);
    EXPECT_EQ(harness.server.StatsUVE().statesSent, statesSent + 1U);
    EXPECT_EQ(harness.client.StatsUVE().statesIgnoredStale, 1U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
}

TEST(NetworkPredictionUVETest, Reconnect_ResumesMidStream) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    constexpr std::uint64_t kToken = 0x0102030405060708U;
    harness.client.SessionUVE().SetReconnectConfigUVE(SessionReconnectConfigUVE{50U, 20U});
    ASSERT_TRUE(harness.client.SessionUVE().ConnectUVE(1000U, kToken));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    ASSERT_EQ(harness.client.SessionUVE().StateUVE(), SessionStateUVE::Established);

    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(input), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }
    ASSERT_EQ(harness.server.LastAppliedTickUVE(), 3U);

    // Ticks 4-5 simulate while established and their messages die in the outage below;
    // then the wire goes quiet past the silence timeout and the client redials on its own.
    for (std::uint64_t n = 4U; n <= 5U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(input), n);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.client.LocalStateUVE(), expect);

    // The outage swallows ticks 4-5's messages mid-wire: the silence past the timeout is real,
    // and the unacked inputs survive in history for the resumed session to drain.
    while (harness.link.b.TryReceiveUVE().has_value()) {
    }

    std::uint64_t nowMs = 1000U + kDefaultSessionTimeoutMsUVE;
    harness.client.UpdateUVE(nowMs);
    harness.server.UpdateUVE(nowMs);
    ASSERT_EQ(harness.client.SessionUVE().StateUVE(), SessionStateUVE::Reconnecting);
    ASSERT_EQ(harness.server.SessionUVE().StateUVE(), SessionStateUVE::Disconnected);

    bool resumed = false;
    for (int i = 0; i < 2000 && !resumed; ++i) {
        harness.UpdateBothUVE(nowMs);
        ++nowMs;
        resumed = harness.client.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
    ASSERT_TRUE(resumed);
    EXPECT_TRUE(harness.server.SessionUVE().DidResumeUVE());
    EXPECT_TRUE(harness.client.SessionUVE().DidResumeUVE());

    // The backlog drains through the resumed session and the stream continues mid-numbering:
    // nothing lost, nothing replayed twice, nothing to reconcile.
    for (std::uint64_t n = 6U; n <= 8U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(input), n);
        harness.UpdateBothUVE(nowMs);
        harness.UpdateBothUVE(nowMs);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 8U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), 8U);
    EXPECT_EQ(harness.server.LocalStateUVE(), expect);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 8U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), expect);
    // Three sends before the outage, two swallowed by it, three after the resume.
    EXPECT_EQ(harness.client.StatsUVE().inputsSent, 8U);
}

TEST(NetworkPredictionUVETest, Malformed_ForeignFramesIgnored) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.1F)), 1U);

    // Steal tick 1's real input message off the wire to mutate: no codec duplicated here.
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());

    // Server-ward: a bad prediction kind (byte 1 - byte 0 is the session tag), a truncation,
    // a trailing byte, and session-Data junk.
    std::vector<std::uint8_t> badKind = *wire;
    badKind[1U] = 0x09U;
    ASSERT_TRUE(harness.link.a.SendUVE(badKind));
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>(wire->begin(), wire->begin() + 4)));
    std::vector<std::uint8_t> trailing = *wire;
    trailing.push_back(0xFFU);
    ASSERT_TRUE(harness.link.a.SendUVE(trailing));
    ASSERT_TRUE(harness.link.a.SendUVE(
        TaggedUVE(static_cast<std::uint8_t>(SessionMessageTagUVE::Data), "junk")));

    // Client-ward: the pristine input message (the wrong direction on a client) plus a
    // truncated state.
    ASSERT_TRUE(harness.link.b.SendUVE(*wire));
    ASSERT_TRUE(harness.link.b.SendUVE(
        TaggedUVE(static_cast<std::uint8_t>(SessionMessageTagUVE::Data), std::string("\x02oops"))));

    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.server.StatsUVE().framesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.client.StatsUVE().framesIgnoredMalformed, 2U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 0U);
    EXPECT_EQ(harness.client.StatsUVE().statesReceived, 0U);
}

TEST(NetworkPredictionUVETest, GapTimeout_SkipsUnfillableWaits) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Seventy ticks with no server pump: the client's history holds 7-70 and the wire holds
    // seventy messages - then every message dies, and one more tick leaves the server a gap
    // starting at 1 that no repeat can ever fill.
    for (std::uint64_t n = 1U; n <= 70U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PushUVE(0.05F * static_cast<float>(n))), n);
        harness.client.UpdateUVE(1000U);
    }
    while (harness.link.b.TryReceiveUVE().has_value()) {
    }
    ASSERT_EQ(harness.client.TickUVE(PushUVE(3.55F)), 71U);

    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 0U);
    EXPECT_EQ(harness.server.StatsUVE().inputBufferOverruns, 0U);

    // Half a second of no progress on the gap: the stream jumps past it (tick 71's window
    // is the oldest survivors, 8-10) and applies what it holds instead of waiting forever.
    harness.server.UpdateUVE(1000U + kPredictionGapTimeoutMsUVE);
    EXPECT_EQ(harness.server.StatsUVE().inputBufferOverruns, 1U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 3U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), 10U);
}

// The production stepper (character_prediction_uve.h): the real character intent plus
// kinematic integration, with the jump's liftoff packed into the prediction.
[[nodiscard]] PredictionStepperUVE MoverStepperUVE() {
    return Scene::MakeCharacterPredictionStepperUVE(Scene::CharacterControllerComponentUVE{},
                                                    -9.81F);
}

TEST(NetworkPredictionUVETest, RealMover_JumpReconcilesAgainstAServerFloor) {
    // Production end to end: both sides step the real character intent plus kinematic
    // integration, but only the server knows the floor (y = 0, landing grounded). The client
    // jumps and arcs cleanly - the prediction carries the liftoff, so the flight agrees -
    // then falls through the floor it cannot see, and every server truth past the crossing
    // snaps it back until both sides rest on the floor together.
    PredictionStepperUVE floored = [](const PredictedStateUVE& state,
                                      const PredictedInputUVE& input, const float dt) {
        PredictedStateUVE next = MoverStepperUVE()(state, input, dt);
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
    PredictedStateUVE spawn;
    spawn.grounded = true;
    PredictionHarnessUVE harness =
        MakePredictionUVE(MoverStepperUVE(), std::move(floored), spawn);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PredictedInputUVE{}), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
    // Standing agreement needs no correction: both sides rest grounded at y = 0.
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.server.LocalStateUVE());

    PredictedInputUVE jump;
    jump.jumpPressed = true;
    ASSERT_EQ(harness.client.TickUVE(jump), 5U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    // The liftoff agrees: the flight only diverges where the floor is.
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.server.LocalStateUVE());

    for (std::uint64_t n = 6U; n <= 60U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PushUVE(1.0F)), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
    // Still arcing at tick 60 (about 0.85 m up): identical steppers, identical inputs, no
    // floor crossed yet, nothing to correct.
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    for (std::uint64_t n = 61U; n <= 80U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PredictedInputUVE{}), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }

    // The landing crossed the floor the client cannot see: corrected every tick since, and
    // resting together now.
    EXPECT_GT(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.server.LocalStateUVE().position.y, 0.0F);
    EXPECT_TRUE(harness.server.LocalStateUVE().grounded);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.server.LocalStateUVE());
}

TEST(NetworkPredictionUVETest, EmptyStepper_StandsStill) {
    PredictionHarnessUVE harness =
        MakePredictionUVE(PredictionStepperUVE{}, PredictionStepperUVE{});
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(PushUVE(1.0F)), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 3U);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 3U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), PredictedStateUVE{});
    EXPECT_EQ(harness.server.LocalStateUVE(), PredictedStateUVE{});
}


TEST(NetworkPredictionUVETest, MultiPawn_TwoPawnsInterleaveIndependently) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // Pawn 1 spawns apart, seeded alike on both sides.
    PredictedStateUVE seed;
    seed.position = Math::Vector3UVE{10.0F, 0.0F, 0.0F};
    ASSERT_TRUE(harness.client.SeedPawnUVE(1U, seed));
    ASSERT_TRUE(harness.server.SeedPawnUVE(1U, seed));

    PredictedStateUVE expect0;
    PredictedStateUVE expect1 = seed;
    for (std::uint64_t n = 1U; n <= 4U; ++n) {
        const PredictedInputUVE input0 = PushUVE(0.05F * static_cast<float>(n));
        const PredictedInputUVE input1 = PushUVE(-0.1F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(0U, input0), n);
        ASSERT_EQ(harness.client.TickUVE(1U, input1), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
        expect0 = CarryStepperUVE()(expect0, input0, 1.0F / 60.0F);
        expect1 = CarryStepperUVE()(expect1, input1, 1.0F / 60.0F);
    }
    // Per-pawn numbering, ordering, and acks: two streams, one session, no crosstalk.
    EXPECT_EQ(harness.server.LastAppliedTickUVE(0U), 4U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(1U), 4U);
    EXPECT_EQ(harness.server.LocalStateUVE(0U), expect0);
    EXPECT_EQ(harness.server.LocalStateUVE(1U), expect1);
    EXPECT_EQ(harness.client.LastAckedTickUVE(0U), 4U);
    EXPECT_EQ(harness.client.LastAckedTickUVE(1U), 4U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(0U), expect0);
    EXPECT_EQ(harness.client.LocalStateUVE(1U), expect1);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 8U);
}

TEST(NetworkPredictionUVETest, MultiPawn_GapOnOnePawnDoesNotHoldTheOther) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Seventy pawn-1 ticks with no server pump, then every message dies: the server holds a
    // pawn-1 gap starting at 1 that no repeat can ever fill.
    for (std::uint64_t n = 1U; n <= 70U; ++n) {
        ASSERT_EQ(harness.client.TickUVE(1U, PushUVE(0.01F)), n);
        harness.client.UpdateUVE(1000U);
    }
    while (harness.link.b.TryReceiveUVE().has_value()) {
    }
    ASSERT_EQ(harness.client.TickUVE(1U, PushUVE(0.01F)), 71U);

    // Pawn 0 starts fresh while pawn 1 is gapped: its stream numbers from 1 and applies at
    // once - the stall is pawn 1's alone.
    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(0.05F)), 1U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(0U), 1U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(1U), 0U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 1U);

    // Half a second of no progress on pawn 1's gap jumps only that pawn (tick 71's window
    // carries the oldest survivors, 8-10) while pawn 0 keeps flowing.
    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(0.05F)), 2U);
    harness.server.UpdateUVE(1000U + kPredictionGapTimeoutMsUVE);
    harness.UpdateBothUVE(1000U + kPredictionGapTimeoutMsUVE);
    harness.UpdateBothUVE(1000U + kPredictionGapTimeoutMsUVE);
    EXPECT_EQ(harness.server.StatsUVE().inputBufferOverruns, 1U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(1U), 10U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(0U), 2U);
}

TEST(NetworkPredictionUVETest, MultiPawn_PawnZeroCallsMatchThePrimary) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // The v1 calls are exactly pawn 0: numbering, states, and acks agree either way.
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.1F)), 1U);
    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(0.2F)), 2U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.client.LocalStateUVE(0U));
    EXPECT_EQ(harness.client.LastAckedTickUVE(), harness.client.LastAckedTickUVE(0U));
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), harness.server.LastAppliedTickUVE(0U));
    EXPECT_EQ(harness.client.LastAckedTickUVE(0U), 2U);
    // Untouched pawns read default: no stream, no numbering, no surprise.
    EXPECT_EQ(harness.client.LocalStateUVE(7U), PredictedStateUVE{});
    EXPECT_EQ(harness.client.LastAckedTickUVE(7U), 0U);
    EXPECT_EQ(harness.server.LastAppliedTickUVE(7U), 0U);
}

TEST(NetworkPredictionUVETest, MultiPawn_OverCapPawnsRefuse) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // Sixteen live pawns is the table: pawns 0-15 tick and drain one wave each (a window
    // only carries three, so each tick's input applies before the next pawn's is sent), then
    // the 17th answers 0 and steps nothing.
    for (std::uint32_t raw = 0U; raw < 16U; ++raw) {
        const PredictionPawnUVE pawn = static_cast<PredictionPawnUVE>(raw);
        ASSERT_EQ(harness.client.TickUVE(pawn, PushUVE(0.01F)), 1U);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
    }
    EXPECT_EQ(harness.client.TickUVE(16U, PushUVE(0.01F)), kInvalidPredictionTickUVE);
    EXPECT_EQ(harness.client.LocalStateUVE(16U), PredictedStateUVE{});
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 16U);
    EXPECT_EQ(harness.client.LastAckedTickUVE(15U), 1U);

    // Past the cap on the wire: a live message re-addressed to pawn 16 refuses - counted,
    // never buffered, never applied.
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.02F)), 2U);
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    std::vector<std::uint8_t> mutant = *wire;
    mutant[11U] = static_cast<std::uint8_t>(16U); // tag, kind, count, tick, then the pawn.
    ASSERT_TRUE(harness.link.a.SendUVE(mutant));
    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StatsUVE().inputsRefusedOverPawnCap, 1U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 16U);
}

TEST(NetworkPredictionUVETest, SeedPawn_BeforeFirstTickOnly) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    PredictedStateUVE seed;
    seed.position = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    // A fresh pawn seeds on both sides; pawn 0 and a re-seed refuse, touching nothing.
    EXPECT_TRUE(harness.client.SeedPawnUVE(3U, seed));
    EXPECT_TRUE(harness.server.SeedPawnUVE(3U, seed));
    EXPECT_EQ(harness.client.LocalStateUVE(3U), seed);
    EXPECT_FALSE(harness.client.SeedPawnUVE(3U, PredictedStateUVE{}));
    EXPECT_EQ(harness.client.LocalStateUVE(3U), seed);
    EXPECT_FALSE(harness.client.SeedPawnUVE(0U, PredictedStateUVE{}));
    // Once the pawn runs - a tick here, an input landed there - seeding refuses.
    ASSERT_EQ(harness.client.TickUVE(4U, PushUVE(0.1F)), 1U);
    EXPECT_FALSE(harness.client.SeedPawnUVE(4U, seed));
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_FALSE(harness.server.SeedPawnUVE(4U, seed));
    EXPECT_EQ(harness.server.LocalStateUVE(3U), seed);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
}

TEST(NetworkPredictionUVETest, VariableDt_MixedStepsReproduceExactly) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // Three rates share one stream: the server steps the transmitted dts, so every tick
    // agrees with the client's manual replay - zero reconciles across the rate changes.
    const float dts[3] = {1.0F / 60.0F, 1.0F / 30.0F, 1.0F / 120.0F};
    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 6U; ++n) {
        const float dt = dts[(n - 1U) % 3U];
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(harness.client.TickUVE(0U, input, dt), n);
        harness.UpdateBothUVE(1000U);
        harness.UpdateBothUVE(1000U);
        expect = CarryStepperUVE()(expect, input, dt);
    }
    EXPECT_EQ(harness.server.LastAppliedTickUVE(), 6U);
    EXPECT_EQ(harness.server.LocalStateUVE(), expect);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 6U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), expect);
}

TEST(NetworkPredictionUVETest, VariableDt_ReplayUsesStoredDts) {
    // The server's world has a wall at x = 5 the client cannot see: one tick runs past it,
    // the next backs away. The snap replays the backoff with the stored dt, so it lands
    // exactly on the server's truth - one reconcile, never two.
    PredictionStepperUVE walled = [](const PredictedStateUVE& state,
                                     const PredictedInputUVE& input, const float dt) {
        PredictedStateUVE next = CarryStepperUVE()(state, input, dt);
        if (next.position.x > 5.0F) {
            next.position.x = 5.0F;
            next.velocity.x = 0.0F;
        }
        return next;
    };
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), walled);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(300.0F), 1.0F / 30.0F), 1U);
    ASSERT_EQ(harness.client.TickUVE(0U, PushUVE(-300.0F), 1.0F / 30.0F), 2U);
    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(1000U);
    EXPECT_EQ(harness.client.StatsUVE().reconciles, 1U);
    EXPECT_EQ(harness.client.LastAckedTickUVE(), 2U);
    EXPECT_EQ(harness.client.LocalStateUVE(), harness.server.LocalStateUVE());
}

TEST(NetworkPredictionUVETest, VariableDt_BadDtRefused) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // Zero, negative, infinite, NaN, and past-a-second: every bad dt answers 0, stepping
    // nothing, storing nothing, sending nothing - the stream stays exactly where it was.
    const float badDts[5] = {0.0F, -1.0F / 60.0F, std::numeric_limits<float>::infinity(),
                             std::numeric_limits<float>::quiet_NaN(), 1.5F};
    for (const float dt : badDts) {
        EXPECT_EQ(harness.client.TickUVE(0U, PushUVE(1.0F), dt), kInvalidPredictionTickUVE);
    }
    EXPECT_EQ(harness.client.StatsUVE().inputsSent, 0U);
    EXPECT_EQ(harness.client.LocalStateUVE(), PredictedStateUVE{});
    // The next good tick numbers from 1: the refusals never touched the stream.
    EXPECT_EQ(harness.client.TickUVE(0U, PushUVE(1.0F), 1.0F / 60.0F), 1U);
}

TEST(NetworkPredictionUVETest, VariableDt_MalformedDtOnWire) {
    PredictionHarnessUVE harness = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.client.TickUVE(PushUVE(0.1F)), 1U);
    // Steal tick 1's real input message and poison its dt: tag, kind, count, tick, pawn,
    // then four dt bytes - NaN where a step used to be.
    const std::optional<std::vector<std::uint8_t>> wire = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    ASSERT_GE(wire->size(), 17U);
    std::vector<std::uint8_t> poisoned = *wire;
    poisoned[12U] = 0xFFU;
    poisoned[13U] = 0xFFU;
    poisoned[14U] = 0xFFU;
    poisoned[15U] = 0xFFU;
    ASSERT_TRUE(harness.link.a.SendUVE(poisoned));
    harness.server.UpdateUVE(1000U);
    EXPECT_EQ(harness.server.StatsUVE().framesIgnoredMalformed, 1U);
    EXPECT_EQ(harness.server.StatsUVE().inputsReceived, 0U);
    EXPECT_EQ(harness.server.StatsUVE().inputsApplied, 0U);
}

// One relay leg: the relay's subscriber session (server role) facing an observer's session
// (client role) across a loopback pair. The relay owns the subscriber side once added, so
// the handshake pumps through the relay itself.
struct RelayLegUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE subscriber;
    PredictionObserverUVE observer;
};

[[nodiscard]] RelayLegUVE MakeRelayLegUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE subscriber(SessionRoleUVE::Server,
                                 std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE watcher(SessionRoleUVE::Client,
                              std::make_unique<LoopbackEndpointUVE>(pair.b));
    PredictionObserverUVE observer(std::move(watcher));
    return RelayLegUVE{std::move(pair), std::move(subscriber), std::move(observer)};
}

TEST(NetworkPredictionUVETest, Relay_FansOutToTwoSubscribers) {
    PredictionHarnessUVE game = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(game.EstablishUVE(1000U));
    PredictionRelayUVE relay;
    RelayLegUVE first = MakeRelayLegUVE();
    RelayLegUVE second = MakeRelayLegUVE();
    ASSERT_EQ(relay.AddSubscriberUVE(std::move(first.subscriber)), 0U);
    ASSERT_EQ(relay.AddSubscriberUVE(std::move(second.subscriber)), 1U);
    ASSERT_EQ(relay.SubscriberCountUVE(), 2U);
    ASSERT_TRUE(first.observer.SessionUVE().ConnectUVE(1000U));
    ASSERT_TRUE(second.observer.SessionUVE().ConnectUVE(1000U));
    for (int i = 0; i < 4; ++i) {
        first.observer.UpdateUVE(1000U);
        second.observer.UpdateUVE(1000U);
        relay.UpdateUVE(1000U);
    }

    // Three applied ticks published the app's way - newest applied tick plus the whole
    // authoritative state - reach both watchers.
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        ASSERT_EQ(game.client.TickUVE(PushUVE(0.05F * static_cast<float>(n))), n);
        game.UpdateBothUVE(1000U);
        game.UpdateBothUVE(1000U);
        ASSERT_EQ(game.server.LastAppliedTickUVE(), n);
        EXPECT_EQ(relay.PublishUVE(game.server.LastAppliedTickUVE(), 0U,
                                   game.server.LocalStateUVE()),
                  2U);
        first.observer.UpdateUVE(1000U);
        second.observer.UpdateUVE(1000U);
    }
    const std::optional<PredictedStateMessageUVE> latestFirst =
        first.observer.LatestUVE(0U);
    const std::optional<PredictedStateMessageUVE> latestSecond =
        second.observer.LatestUVE(0U);
    ASSERT_TRUE(latestFirst.has_value());
    ASSERT_TRUE(latestSecond.has_value());
    EXPECT_EQ(latestFirst->tick, 3U);
    EXPECT_EQ(latestFirst->state, game.server.LocalStateUVE());
    EXPECT_EQ(latestSecond->tick, 3U);
    EXPECT_EQ(latestSecond->state, game.server.LocalStateUVE());
    EXPECT_EQ(relay.StatsUVE().statesRelayed, 6U);
    EXPECT_EQ(relay.StatsUVE().sendsFailed, 0U);
}

TEST(NetworkPredictionUVETest, Relay_LateSubscriberReceivesLiveOnly) {
    PredictionHarnessUVE game = MakePredictionUVE(CarryStepperUVE(), CarryStepperUVE());
    ASSERT_TRUE(game.EstablishUVE(1000U));
    PredictionRelayUVE relay;
    RelayLegUVE early = MakeRelayLegUVE();
    ASSERT_EQ(relay.AddSubscriberUVE(std::move(early.subscriber)), 0U);
    ASSERT_TRUE(early.observer.SessionUVE().ConnectUVE(1000U));
    for (int i = 0; i < 4; ++i) {
        early.observer.UpdateUVE(1000U);
        relay.UpdateUVE(1000U);
    }

    // Ticks 1-2 publish to the early watcher only.
    for (std::uint64_t n = 1U; n <= 2U; ++n) {
        ASSERT_EQ(game.client.TickUVE(PushUVE(0.05F * static_cast<float>(n))), n);
        game.UpdateBothUVE(1000U);
        game.UpdateBothUVE(1000U);
        EXPECT_EQ(relay.PublishUVE(game.server.LastAppliedTickUVE(), 0U,
                                   game.server.LocalStateUVE()),
                  1U);
        early.observer.UpdateUVE(1000U);
    }
    // A second watcher joins mid-stream: its handshake sees no backlog.
    RelayLegUVE late = MakeRelayLegUVE();
    ASSERT_EQ(relay.AddSubscriberUVE(std::move(late.subscriber)), 1U);
    ASSERT_TRUE(late.observer.SessionUVE().ConnectUVE(1000U));
    for (int i = 0; i < 4; ++i) {
        early.observer.UpdateUVE(1000U);
        late.observer.UpdateUVE(1000U);
        relay.UpdateUVE(1000U);
    }
    EXPECT_FALSE(late.observer.LatestUVE(0U).has_value());
    // Tick 3 reaches both: live from here on.
    ASSERT_EQ(game.client.TickUVE(PushUVE(0.15F)), 3U);
    game.UpdateBothUVE(1000U);
    game.UpdateBothUVE(1000U);
    EXPECT_EQ(relay.PublishUVE(game.server.LastAppliedTickUVE(), 0U, game.server.LocalStateUVE()),
              2U);
    early.observer.UpdateUVE(1000U);
    late.observer.UpdateUVE(1000U);
    ASSERT_TRUE(late.observer.LatestUVE(0U).has_value());
    EXPECT_EQ(late.observer.LatestUVE(0U)->tick, 3U);
    EXPECT_EQ(early.observer.LatestUVE(0U)->tick, 3U);
    EXPECT_EQ(relay.StatsUVE().statesRelayed, 4U);
}

TEST(NetworkPredictionUVETest, Observer_TracksLatestPerPawnAndIgnoresStale) {
    PredictionRelayUVE relay;
    RelayLegUVE leg = MakeRelayLegUVE();
    ASSERT_EQ(relay.AddSubscriberUVE(std::move(leg.subscriber)), 0U);
    ASSERT_TRUE(leg.observer.SessionUVE().ConnectUVE(1000U));
    for (int i = 0; i < 4; ++i) {
        leg.observer.UpdateUVE(1000U);
        relay.UpdateUVE(1000U);
    }
    PredictedStateUVE first;
    first.position = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    PredictedStateUVE second;
    second.position = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    PredictedStateUVE other;
    other.position = Math::Vector3UVE{9.0F, 0.0F, 0.0F};
    // Out of order on purpose: pawn 0's tick 5 lands before its tick 2, pawn 1 rides along.
    EXPECT_EQ(relay.PublishUVE(5U, 0U, second), 1U);
    EXPECT_EQ(relay.PublishUVE(2U, 0U, first), 1U);
    EXPECT_EQ(relay.PublishUVE(1U, 1U, other), 1U);
    leg.observer.UpdateUVE(1000U);
    // Newest per pawn wins; the stale tick counts but never lands.
    ASSERT_TRUE(leg.observer.LatestUVE(0U).has_value());
    EXPECT_EQ(leg.observer.LatestUVE(0U)->tick, 5U);
    EXPECT_EQ(leg.observer.LatestUVE(0U)->state, second);
    ASSERT_TRUE(leg.observer.LatestUVE(1U).has_value());
    EXPECT_EQ(leg.observer.LatestUVE(1U)->state, other);
    EXPECT_EQ(leg.observer.StatsUVE().statesReceived, 3U);
    EXPECT_EQ(leg.observer.StatsUVE().statesIgnoredStale, 1U);
    EXPECT_FALSE(leg.observer.LatestUVE(2U).has_value());
    // Removal drops the leg: publishes reach nobody, unknown indices refuse.
    EXPECT_TRUE(relay.RemoveSubscriberUVE(0U));
    EXPECT_EQ(relay.SubscriberCountUVE(), 0U);
    EXPECT_FALSE(relay.RemoveSubscriberUVE(0U));
    EXPECT_EQ(relay.PublishUVE(6U, 0U, second), 0U);
}

} // namespace
} // namespace UVE::Network::Tests
