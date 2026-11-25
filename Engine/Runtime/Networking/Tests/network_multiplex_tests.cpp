// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_prediction.h"
#include "network_replication.h"
#include "network_rpc.h"
#include "network_session.h"

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

struct MultiplexHarness final {
    LoopbackTransportPair link;
    NetworkSession sessionA;
    NetworkSession sessionB;

    void UpdateSessions(const std::uint64_t nowMs) {
        sessionA.Update(nowMs);
        sessionB.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!sessionA.Connect(nowMs)) {
            return false;
        }
        UpdateSessions(nowMs);
        UpdateSessions(nowMs);
        return sessionA.State() == SessionState::Established &&
               sessionB.State() == SessionState::Established;
    }
};

[[nodiscard]] MultiplexHarness MakeMultiplexedSessions() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    static_cast<void>(sessionA.EnableMultiplexing());
    static_cast<void>(sessionB.EnableMultiplexing());
    return MultiplexHarness{std::move(pair), std::move(sessionA), std::move(sessionB)};
}

// Velocity-carry integrator: the input pushes, the velocity decays, the position follows.
[[nodiscard]] PredictionStepper CarryStepper() {
    return [](const PredictedState& state, const PredictedInput& input, const float dt) {
        PredictedState next = state;
        next.velocity = state.velocity * 0.9F + input.move * (20.0F * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictedInput Push(const float x) {
    PredictedInput input;
    input.move.x = x;
    return input;
}

TEST(NetworkMultiplexTest, Shared_ReplicationSnapshotsAndPropertiesFlow) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkReplicator replA(harness.sessionA, 1U);
    NetworkReplicator replB(harness.sessionB, 1U);
    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;
    ASSERT_TRUE(replA.RegisterEntity(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntity(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3{1.0F, 2.0F, 3.0F};
    positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};
    Blueprint::Value applied{std::int64_t{0}};
    int applyCalls = 0;
    ASSERT_TRUE(replB.RegisterProperty(
        7U, 3U, [&applied, &applyCalls](const Blueprint::Value& value) {
            applied = value;
            ++applyCalls;
        }));
    ASSERT_TRUE(replA.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{83}}));
    ASSERT_TRUE(harness.Establish(1000U));

    replA.Update(1100U);
    replB.Update(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(applied, Blueprint::Value{std::int64_t{83}});
    EXPECT_EQ(applyCalls, 1);
    replA.Update(1200U);
    replB.Update(1200U);
    EXPECT_EQ(positionsA[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(applyCalls, 1); // Clean and non-full: no re-ship.
    EXPECT_EQ(replA.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(replB.Stats().propertyFramesSent, 0U);
    EXPECT_EQ(replB.Stats().messagesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexTest, Shared_RpcCallCompletesOnChannelZero) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkRpc rpcA(harness.sessionA, 0U);
    NetworkRpc rpcB(harness.sessionB, 0U);
    ASSERT_TRUE(rpcB.RegisterMethod(
        "add", [](const std::span<const Blueprint::Value> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOk(Blueprint::Value{x + y});
        }));
    ASSERT_TRUE(harness.Establish(1000U));

    std::map<RpcCallId, RpcOutcome> doneA;
    const RpcCallId callId = rpcA.Call(
        "add", {Blueprint::Value{std::int64_t{20}}, Blueprint::Value{std::int64_t{22}}},
        [&doneA](const RpcCallId id, const RpcOutcome& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallId);
    rpcA.Update(1100U);
    rpcB.Update(1100U);
    rpcA.Update(1100U);
    rpcB.Update(1100U);

    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, Blueprint::Value{std::int64_t{42}});
    EXPECT_EQ(rpcB.Stats().callsReceived, 1U);
}

TEST(NetworkMultiplexTest, Shared_PredictionInputsAndStatesFlow) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkPrediction predClient(harness.sessionA, 3U, SessionRole::Client,
                                    CarryStepper());
    NetworkPrediction predServer(harness.sessionB, 3U, SessionRole::Server,
                                    CarryStepper());
    ASSERT_TRUE(harness.Establish(1000U));

    PredictedState expect;
    for (std::uint64_t n = 1U; n <= 3U; ++n) {
        const PredictedInput input = Push(0.05F * static_cast<float>(n));
        ASSERT_EQ(predClient.Tick(input), n);
        predClient.Update(1000U);
        predServer.Update(1000U);
        predClient.Update(1000U);
        predServer.Update(1000U);
        expect = CarryStepper()(expect, input, 1.0F / 60.0F);
    }

    EXPECT_EQ(predServer.Stats().inputsApplied, 3U);
    EXPECT_EQ(predServer.LastAppliedTick(), 3U);
    EXPECT_EQ(predServer.LocalState(), expect);
    EXPECT_EQ(predClient.LastAckedTick(), 3U);
    EXPECT_EQ(predClient.Stats().reconciles, 0U);
    EXPECT_EQ(predClient.Stats().framesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexTest, Shared_AllThreeCoexistOnOneSession) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkReplicator replA(harness.sessionA, 1U);
    NetworkReplicator replB(harness.sessionB, 1U);
    NetworkRpc rpcA(harness.sessionA, 2U);
    NetworkRpc rpcB(harness.sessionB, 2U);
    NetworkPrediction predClient(harness.sessionA, 3U, SessionRole::Client,
                                    CarryStepper());
    NetworkPrediction predServer(harness.sessionB, 3U, SessionRole::Server,
                                    CarryStepper());
    const auto updateAll = [&](const std::uint64_t nowMs) {
        replA.Update(nowMs);
        replB.Update(nowMs);
        rpcA.Update(nowMs);
        rpcB.Update(nowMs);
        predClient.Update(nowMs);
        predServer.Update(nowMs);
    };

    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;
    ASSERT_TRUE(replA.RegisterEntity(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntity(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3{4.0F, 5.0F, 6.0F};
    Blueprint::Value applied{std::int64_t{0}};
    ASSERT_TRUE(replB.RegisterProperty(
        7U, 9U, [&applied](const Blueprint::Value& value) { applied = value; }));
    ASSERT_TRUE(replA.SetProperty(7U, 9U, Blueprint::Value{std::int64_t{7}}));
    ASSERT_TRUE(rpcB.RegisterMethod(
        "add", [](const std::span<const Blueprint::Value> args) {
            return RpcOk(Blueprint::Value{std::get<std::int64_t>(args[0]) +
                                               std::get<std::int64_t>(args[1])});
        }));
    ASSERT_TRUE(harness.Establish(1000U));

    std::map<RpcCallId, RpcOutcome> doneA;
    const RpcCallId callId = rpcA.Call(
        "add", {Blueprint::Value{std::int64_t{1}}, Blueprint::Value{std::int64_t{2}}},
        [&doneA](const RpcCallId id, const RpcOutcome& outcome) {
            doneA.emplace(id, outcome);
        });
    ASSERT_NE(callId, kInvalidRpcCallId);
    updateAll(1100U);
    updateAll(1100U);
    for (std::uint64_t n = 1U; n <= 2U; ++n) {
        ASSERT_EQ(predClient.Tick(Push(0.1F)), n);
        updateAll(1100U);
        updateAll(1100U);
    }

    EXPECT_EQ(positionsB[7U], (Math::Vector3{4.0F, 5.0F, 6.0F}));
    EXPECT_EQ(applied, Blueprint::Value{std::int64_t{7}});
    ASSERT_EQ(doneA.size(), 1U);
    EXPECT_TRUE(doneA[callId].ok);
    EXPECT_EQ(doneA[callId].value, Blueprint::Value{std::int64_t{3}});
    EXPECT_EQ(predClient.LastAckedTick(), 2U);
    EXPECT_EQ(predServer.LastAppliedTick(), 2U);
    EXPECT_EQ(replA.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(predClient.Stats().framesIgnoredMalformed, 0U);
    EXPECT_EQ(predServer.Stats().framesIgnoredMalformed, 0U);
}

TEST(NetworkMultiplexTest, Shared_ChannelMismatchIsSilence) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkReplicator replA(harness.sessionA, 1U);
    NetworkReplicator replB(harness.sessionB, 2U);
    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;
    ASSERT_TRUE(replA.RegisterEntity(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntity(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3{1.0F, 1.0F, 1.0F};
    positionsB[7U] = Math::Vector3{2.0F, 2.0F, 2.0F};
    ASSERT_TRUE(harness.Establish(1000U));

    replA.Update(1100U);
    replB.Update(1100U);
    replA.Update(1200U);
    replB.Update(1200U);
    replA.Update(1300U);
    replB.Update(1300U);

    // Both shouted into the void, three sends each; neither heard a thing - and neither
    // cried malformed, because a foreign channel is silence, not garbage.
    EXPECT_EQ(replA.Stats().snapshotsSent, 3U);
    EXPECT_EQ(replB.Stats().snapshotsSent, 3U);
    EXPECT_EQ(replA.Stats().snapshotsReceived, 0U);
    EXPECT_EQ(replB.Stats().snapshotsReceived, 0U);
    EXPECT_EQ(replA.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(replB.Stats().messagesIgnoredMalformed, 0U);
    EXPECT_EQ(positionsA[7U], (Math::Vector3{1.0F, 1.0F, 1.0F}));
    EXPECT_EQ(positionsB[7U], (Math::Vector3{2.0F, 2.0F, 2.0F}));
}

TEST(NetworkMultiplexTest, Shared_FeaturesPumpTheSession) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkReplicator replA(harness.sessionA, 1U);
    NetworkReplicator replB(harness.sessionB, 1U);
    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;
    ASSERT_TRUE(replA.RegisterEntity(
        7U, [&positionsA]() { return positionsA[7U]; },
        [&positionsA](const Math::Vector3& position) { positionsA[7U] = position; }));
    ASSERT_TRUE(replB.RegisterEntity(
        7U, [&positionsB]() { return positionsB[7U]; },
        [&positionsB](const Math::Vector3& position) { positionsB[7U] = position; }));
    positionsA[7U] = Math::Vector3{3.0F, 2.0F, 1.0F};
    positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};

    // From here on, nothing touches the sessions directly: every pump below runs through a
    // feature's Update, handshake included.
    ASSERT_TRUE(harness.sessionA.Connect(1000U));
    replA.Update(1000U);
    replB.Update(1000U);
    replA.Update(1000U);
    replB.Update(1000U);

    EXPECT_EQ(replA.Session().State(), SessionState::Established);
    EXPECT_EQ(replB.Session().State(), SessionState::Established);
    // The server established (and sent) first, so both sides hold its value.
    EXPECT_EQ(positionsA[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(positionsB[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
    positionsA[7U] = Math::Vector3{7.0F, 7.0F, 7.0F};
    replA.Update(1100U);
    replB.Update(1100U);
    EXPECT_EQ(positionsB[7U], (Math::Vector3{7.0F, 7.0F, 7.0F}));
}

TEST(NetworkMultiplexTest, Shared_SessionAccessorRoutesShared) {
    MultiplexHarness harness = MakeMultiplexedSessions();
    NetworkReplicator repl(harness.sessionA, 1U);
    NetworkRpc rpc(harness.sessionB, 2U);
    NetworkPrediction pred(harness.sessionA, 3U, SessionRole::Client, CarryStepper());
    const NetworkReplicator& replConst = repl;
    const NetworkRpc& rpcConst = rpc;
    const NetworkPrediction& predConst = pred;

    EXPECT_EQ(&repl.Session(), &harness.sessionA);
    EXPECT_EQ(&replConst.Session(), &harness.sessionA);
    EXPECT_EQ(&rpc.Session(), &harness.sessionB);
    EXPECT_EQ(&rpcConst.Session(), &harness.sessionB);
    EXPECT_EQ(&pred.Session(), &harness.sessionA);
    EXPECT_EQ(&predConst.Session(), &harness.sessionA);
}

} // namespace
} // namespace UVE::Network::Tests
