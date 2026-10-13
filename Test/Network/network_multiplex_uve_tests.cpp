// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_prediction_uve.h"
#include "uve/network/network_replication_uve.h"
#include "uve/network/network_rpc_uve.h"
#include "uve/network/network_session_uve.h"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <utility>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

struct MultiplexHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE sessionA;
    NetworkSessionUVE sessionB;

    void UpdateSessionsUVE(const std::uint64_t nowMs) {
        sessionA.UpdateUVE(nowMs);
        sessionB.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!sessionA.ConnectUVE(nowMs)) {
            return false;
        }
        UpdateSessionsUVE(nowMs);
        UpdateSessionsUVE(nowMs);
        return sessionA.StateUVE() == SessionStateUVE::Established &&
               sessionB.StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] MultiplexHarnessUVE MakeMultiplexedSessionsUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    static_cast<void>(sessionA.EnableMultiplexingUVE());
    static_cast<void>(sessionB.EnableMultiplexingUVE());
    return MultiplexHarnessUVE{std::move(pair), std::move(sessionA), std::move(sessionB)};
}

// Velocity-carry integrator: the input pushes, the velocity decays, the position follows.
[[nodiscard]] PredictionStepperUVE CarryStepperUVE() {
    return [](const PredictedStateUVE& state, const PredictedInputUVE& input, const float dt) {
        PredictedStateUVE next = state;
        next.velocity = state.velocity * 0.9F + input.move * (20.0F * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictedInputUVE PushUVE(const float x) {
    PredictedInputUVE input;
    input.move.x = x;
    return input;
}

TEST(NetworkMultiplexUVETest, Shared_ReplicationSnapshotsAndPropertiesFlow) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkReplicatorUVE replA(harness.sessionA, 1U);
    NetworkReplicatorUVE replB(harness.sessionB, 1U);
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;
    ASSERT_TRUE(replA.RegisterEntityUVE(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3UVE& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntityUVE(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3UVE& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};
    UVScript::ValueUVE applied{std::int64_t{0}};
    int applyCalls = 0;
    ASSERT_TRUE(replB.RegisterPropertyUVE(
        7U, 3U, [&applied, &applyCalls](const UVScript::ValueUVE& value) {
            applied = value;
            ++applyCalls;
        }));
    ASSERT_TRUE(replA.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{83}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    replA.UpdateUVE(1100U);
    replB.UpdateUVE(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(applied, UVScript::ValueUVE{std::int64_t{83}});
    EXPECT_EQ(applyCalls, 1);
    replA.UpdateUVE(1200U);
    replB.UpdateUVE(1200U);
    EXPECT_EQ(positionsA[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(applyCalls, 1); // Clean and non-full: no re-ship.
    EXPECT_EQ(replA.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(replB.StatsUVE().propertyFramesSent, 0U);
    EXPECT_EQ(replB.StatsUVE().messagesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexUVETest, Shared_RpcCallCompletesOnChannelZero) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkRpcUVE rpcA(harness.sessionA, 0U);
    NetworkRpcUVE rpcB(harness.sessionB, 0U);
    ASSERT_TRUE(rpcB.RegisterMethodUVE(
        "add", [](const std::span<const UVScript::ValueUVE> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOkUVE(UVScript::ValueUVE{x + y});
        }));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneA;
    const RpcCallIdUVE callId = rpcA.CallUVE(
        "add", {UVScript::ValueUVE{std::int64_t{20}}, UVScript::ValueUVE{std::int64_t{22}}},
        [&doneA](const RpcCallIdUVE id, const RpcOutcomeUVE& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    rpcA.UpdateUVE(1100U);
    rpcB.UpdateUVE(1100U);
    rpcA.UpdateUVE(1100U);
    rpcB.UpdateUVE(1100U);

    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, UVScript::ValueUVE{std::int64_t{42}});
    EXPECT_EQ(rpcB.StatsUVE().callsReceived, 1U);
}

TEST(NetworkMultiplexUVETest, Shared_PredictionInputsAndStatesFlow) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkPredictionUVE predClient(harness.sessionA, 3U, SessionRoleUVE::Client,
                                    CarryStepperUVE());
    NetworkPredictionUVE predServer(harness.sessionB, 3U, SessionRoleUVE::Server,
                                    CarryStepperUVE());
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    PredictedStateUVE expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInputUVE input = PushUVE(0.05F * static_cast<float>(n));
        ASSERT_EQ(predClient.TickUVE(input), n);
        predClient.UpdateUVE(1000U);
        predServer.UpdateUVE(1000U);
        predClient.UpdateUVE(1000U);
        predServer.UpdateUVE(1000U);
        expect = CarryStepperUVE()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_EQ(predServer.StatsUVE().inputsApplied, 3U);
    EXPECT_EQ(predServer.LastAppliedTickUVE(), 3U);
    EXPECT_EQ(predServer.LocalStateUVE(), expect);
    EXPECT_EQ(predClient.LastAckedTickUVE(), 3U);
    EXPECT_EQ(predClient.StatsUVE().reconciles, 0U);
    EXPECT_EQ(predClient.StatsUVE().framesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexUVETest, Shared_AllThreeCoexistOnOneSession) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkReplicatorUVE replA(harness.sessionA, 1U);
    NetworkReplicatorUVE replB(harness.sessionB, 1U);
    NetworkRpcUVE rpcA(harness.sessionA, 2U);
    NetworkRpcUVE rpcB(harness.sessionB, 2U);
    NetworkPredictionUVE predClient(harness.sessionA, 3U, SessionRoleUVE::Client,
                                    CarryStepperUVE());
    NetworkPredictionUVE predServer(harness.sessionB, 3U, SessionRoleUVE::Server,
                                    CarryStepperUVE());
    const auto updateAll = [&](const std::uint64_t nowMs) {
        replA.UpdateUVE(nowMs);
        replB.UpdateUVE(nowMs);
        rpcA.UpdateUVE(nowMs);
        rpcB.UpdateUVE(nowMs);
        predClient.UpdateUVE(nowMs);
        predServer.UpdateUVE(nowMs);
    };

    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;
    ASSERT_TRUE(replA.RegisterEntityUVE(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3UVE& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntityUVE(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3UVE& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3UVE{4.0F, 5.0F, 6.0F};
    UVScript::ValueUVE applied{std::int64_t{0}};
    ASSERT_TRUE(replB.RegisterPropertyUVE(
        7U, 9U, [&applied](const UVScript::ValueUVE& value) { applied = value; }));
    ASSERT_TRUE(replA.SetPropertyUVE(7U, 9U, UVScript::ValueUVE{std::int64_t{7}}));
    ASSERT_TRUE(rpcB.RegisterMethodUVE(
        "add", [](const std::span<const UVScript::ValueUVE> args) {
            return RpcOkUVE(UVScript::ValueUVE{std::get<std::int64_t>(args[0]) +
                                               std::get<std::int64_t>(args[1])});
        }));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneA;
    const RpcCallIdUVE callId = rpcA.CallUVE(
        "add", {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{std::int64_t{2}}},
        [&doneA](const RpcCallIdUVE id, const RpcOutcomeUVE& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    updateAll(1100U);
    updateAll(1100U);
    for (std::uint64_t n = 1U; n <= 2U; ++n) {
        ASSERT_EQ(predClient.TickUVE(PushUVE(0.1F)), n);
        updateAll(1100U);
        updateAll(1100U);
    }

    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(applied, UVScript::ValueUVE{std::int64_t{7}});
    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, UVScript::ValueUVE{std::int64_t{3}});
    EXPECT_EQ(predClient.LastAckedTickUVE(), 2U);
    EXPECT_EQ(predServer.LastAppliedTickUVE(), 2U);
    EXPECT_EQ(replA.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(predClient.StatsUVE().framesIgnoredMalformed, 0U);
    EXPECT_EQ(predServer.StatsUVE().framesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexUVETest, Shared_ChannelMismatchIsSilence) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkReplicatorUVE replA(harness.sessionA, 1U);
    NetworkReplicatorUVE replB(harness.sessionB, 2U);
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;
    ASSERT_TRUE(replA.RegisterEntityUVE(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3UVE& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntityUVE(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3UVE& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3UVE{1.0F, 1.0F, 1.0F};
    positionsB[7U] = Math::Vector3UVE{2.0F, 2.0F, 2.0F};
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    replA.UpdateUVE(1100U);
    replB.UpdateUVE(1100U);
    replA.UpdateUVE(1200U);
    replB.UpdateUVE(1200U);
    replA.UpdateUVE(1300U);
    replB.UpdateUVE(1300U);

    // Both shouted into the void, three sends each; neither heard a thing - and neither
    // cried malformed, because a foreign channel is silence, not garbage.
    EXPECT_EQ(replA.StatsUVE().snapshotsSent, 3U);
    EXPECT_EQ(replB.StatsUVE().snapshotsSent, 3U);
    EXPECT_EQ(replA.StatsUVE().snapshotsReceived, 0U);
    EXPECT_EQ(replB.StatsUVE().snapshotsReceived, 0U);
    EXPECT_EQ(replA.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.StatsUVE().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(positionsA[7U], (Math::Vector3UVE{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{2.0F, 2.0F, 2.0F}));
}

TEST(NetworkMultiplexUVETest, Shared_FeaturesPumpTheSession) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkReplicatorUVE replA(harness.sessionA, 1U);
    NetworkReplicatorUVE replB(harness.sessionB, 1U);
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;
    ASSERT_TRUE(replA.RegisterEntityUVE(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3UVE& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntityUVE(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3UVE& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3UVE{3.0F, 2.0F, 1.0F};
    positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};

    // From here on, nothing touches the sessions directly: every pump below runs through a
    // feature's Update, handshake included.
    ASSERT_TRUE(harness.sessionA.ConnectUVE(1000U));
    replA.UpdateUVE(1000U);
    replB.UpdateUVE(1000U);
    replA.UpdateUVE(1000U);
    replB.UpdateUVE(1000U);

    EXPECT_EQ(replA.SessionUVE().StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(replB.SessionUVE().StateUVE(), SessionStateUVE::Established);
    // The server established (and sent) first, so both sides hold its value.
    EXPECT_EQ(positionsA[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    positionsA[7U] = Math::Vector3UVE{7.0F, 7.0F, 7.0F};
    replA.UpdateUVE(1100U);
    replB.UpdateUVE(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3UVE{7.0F, 7.0F, 7.0F}));
}

TEST(NetworkMultiplexUVETest, Shared_SessionAccessorRoutesShared) {
    MultiplexHarnessUVE harness = MakeMultiplexedSessionsUVE();
    NetworkReplicatorUVE repl(harness.sessionA, 1U);
    NetworkRpcUVE rpc(harness.sessionB, 2U);
    NetworkPredictionUVE pred(harness.sessionA, 3U, SessionRoleUVE::Client, CarryStepperUVE());
    const NetworkReplicatorUVE& replConst = repl;
    const NetworkRpcUVE& rpcConst = rpc;
    const NetworkPredictionUVE& predConst = pred;

    EXPECT_EQ(&repl.SessionUVE(), &harness.sessionA);
    EXPECT_EQ(&replConst.SessionUVE(), &harness.sessionA);
    EXPECT_EQ(&rpc.SessionUVE(), &harness.sessionB);
    EXPECT_EQ(&rpcConst.SessionUVE(), &harness.sessionB);
    EXPECT_EQ(&pred.SessionUVE(), &harness.sessionA);
    EXPECT_EQ(&predConst.SessionUVE(), &harness.sessionA);
}

} // namespace
} // namespace UVE::Network::Tests
