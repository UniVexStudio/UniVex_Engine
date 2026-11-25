// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_inspector.h"

#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

// Structural equality for values: plain == compares collections by shared-pointer identity,
// so a decoded-then-rebuilt twin never matches. This walks both and compares by shape.
[[nodiscard]] bool ValuesEqual(const Blueprint::Value& a, const Blueprint::Value& b) {
    if (a.index() != b.index()) {
        return false;
    }
    const auto* collectionA =
        std::get_if<std::shared_ptr<const Blueprint::CollectionValue>>(&a);
    if (collectionA == nullptr) {
        return a == b;
    }
    const auto& collectionB = std::get<std::shared_ptr<const Blueprint::CollectionValue>>(b);
    if ((*collectionA == nullptr) != (collectionB == nullptr)) {
        return false;
    }
    if (*collectionA == nullptr) {
        return true;
    }
    if ((*collectionA)->kind != collectionB->kind ||
        (*collectionA)->items.size() != collectionB->items.size()) {
        return false;
    }
    for (std::size_t i = 0U; i < (*collectionA)->items.size(); ++i) {
        if (!ValuesEqual((*collectionA)->items[i], collectionB->items[i])) {
            return false;
        }
    }
    return true;
}

// Every positive case below steals REAL datagrams off a live loopback exchange - the inspector
// never meets a hand-framed message except in the malformed table, where hand-built garbage
// is the whole point. Stolen datagrams get re-injected afterwards, proving the tapped traffic
// still completes its business.

struct SessionTap final {
    LoopbackTransportPair link;
    NetworkSession a;
    NetworkSession b;
};

[[nodiscard]] SessionTap MakeSessionTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession a(SessionRole::Client, std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession b(SessionRole::Server, std::make_unique<LoopbackEndpoint>(pair.b));
    return SessionTap{std::move(pair), std::move(a), std::move(b)};
}

struct ReplicationTap final {
    LoopbackTransportPair link;
    NetworkReplicator a;
    NetworkReplicator b;

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        a.Update(nowMs);
        b.Update(nowMs);
        a.Update(nowMs);
        b.Update(nowMs);
        return a.Session().State() == SessionState::Established &&
               b.Session().State() == SessionState::Established;
    }
};

[[nodiscard]] ReplicationTap MakeReplicationTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkReplicator a(std::move(sessionA));
    NetworkReplicator b(std::move(sessionB));
    return ReplicationTap{std::move(pair), std::move(a), std::move(b)};
}

struct RpcTap final {
    LoopbackTransportPair link;
    NetworkRpc a;
    NetworkRpc b;
    std::map<RpcCallId, RpcOutcome> doneA;

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        a.Update(nowMs);
        b.Update(nowMs);
        a.Update(nowMs);
        b.Update(nowMs);
        return a.Session().State() == SessionState::Established &&
               b.Session().State() == SessionState::Established;
    }

    [[nodiscard]] RpcResponseCallback FileA() {
        return [this](const RpcCallId callId, const RpcOutcome& outcome) {
            doneA.emplace(callId, outcome);
        };
    }
};

[[nodiscard]] RpcTap MakeRpcTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkRpc a(std::move(sessionA));
    NetworkRpc b(std::move(sessionB));
    return RpcTap{std::move(pair), std::move(a), std::move(b), {}};
}

struct PredictionTap final {
    LoopbackTransportPair link;
    NetworkPrediction client;
    NetworkPrediction server;

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!client.Session().Connect(nowMs)) {
            return false;
        }
        client.Update(nowMs);
        server.Update(nowMs);
        client.Update(nowMs);
        server.Update(nowMs);
        return client.Session().State() == SessionState::Established &&
               server.Session().State() == SessionState::Established;
    }
};

[[nodiscard]] PredictionStepper CarryStepper() {
    return [](const PredictedState& state, const PredictedInput& input, const float dt) {
        PredictedState next = state;
        next.velocity = state.velocity * 0.9F + input.move * (20.0F * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictionTap MakePredictionTap() {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkPrediction client(std::move(sessionA), SessionRole::Client, CarryStepper(),
                                PredictedState{});
    NetworkPrediction server(std::move(sessionB), SessionRole::Server, CarryStepper(),
                                PredictedState{});
    return PredictionTap{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkInspectorTest, Session_TappedHandshakeDecodesAndStillEstablishes) {
    SessionTap tap = MakeSessionTap();
    // Watching the client: its hello leaves outbound, the welcome returns inbound.
    ASSERT_TRUE(tap.a.Connect(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceive();
    ASSERT_TRUE(hello.has_value());

    const InspectedPacket tapped = InspectPacket(*hello, PacketDirection::Outbound,
                                                       1000U, InspectedChannel::Unknown);
    EXPECT_EQ(tapped.timestampMs, 1000U);
    EXPECT_EQ(tapped.direction, PacketDirection::Outbound);
    EXPECT_EQ(tapped.channel, InspectedChannel::Unknown);
    EXPECT_EQ(tapped.sessionKind, "hello");
    ASSERT_TRUE(tapped.helloVersion.has_value());
    EXPECT_EQ(*tapped.helloVersion, kSessionProtocolVersion);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, 123U);
    EXPECT_EQ(tapped.trailingBytes, 0U);
    EXPECT_TRUE(tapped.dataKind.empty());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(tapped.data));
    EXPECT_EQ(tapped.summary, "hello v2 token 0x000000000000007b");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(tapped.raw, *hello);
    EXPECT_EQ(DescribePacket(tapped), "> [wire] hello v2 token 0x000000000000007b");

    // The session layer decodes no matter which channel watches the wire.
    const InspectedPacket retagged = InspectPacket(*hello, PacketDirection::Outbound,
                                                         1000U, InspectedChannel::Rpc);
    EXPECT_EQ(retagged.sessionKind, "hello");
    EXPECT_EQ(retagged.summary, tapped.summary);
    EXPECT_FALSE(retagged.malformed);

    // Re-inject: the tapped handshake still runs to established.
    ASSERT_TRUE(tap.link.a.Send(*hello));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> welcome = tap.link.a.TryReceive();
    ASSERT_TRUE(welcome.has_value());
    const InspectedPacket tappedWelcome = InspectPacket(
        *welcome, PacketDirection::Inbound, 1000U, InspectedChannel::Unknown);
    EXPECT_EQ(tappedWelcome.sessionKind, "welcome");
    EXPECT_EQ(tappedWelcome.summary, "welcome");
    EXPECT_FALSE(tappedWelcome.malformed);
    EXPECT_EQ(DescribePacket(tappedWelcome), "< [wire] welcome");

    ASSERT_TRUE(tap.link.b.Send(*welcome));
    tap.a.Update(1000U);
    EXPECT_EQ(tap.a.State(), SessionState::Established);
    EXPECT_EQ(tap.b.State(), SessionState::Established);

    ASSERT_TRUE(tap.a.Disconnect());
    const std::optional<std::vector<std::uint8_t>> bye = tap.link.b.TryReceive();
    ASSERT_TRUE(bye.has_value());
    const InspectedPacket tappedBye = InspectPacket(*bye, PacketDirection::Outbound,
                                                          1100U, InspectedChannel::Unknown);
    EXPECT_EQ(tappedBye.sessionKind, "bye");
    EXPECT_EQ(tappedBye.summary, "bye");
    EXPECT_FALSE(tappedBye.malformed);
    EXPECT_EQ(DescribePacket(tappedBye), "> [wire] bye");
}

TEST(NetworkInspectorTest, Session_AnonymousHelloShowsNoToken) {
    SessionTap tap = MakeSessionTap();
    ASSERT_TRUE(tap.a.Connect(1000U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceive();
    ASSERT_TRUE(hello.has_value());

    const InspectedPacket tapped = InspectPacket(*hello, PacketDirection::Outbound,
                                                       1000U, InspectedChannel::Unknown);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, kAnonymousSessionToken);
    EXPECT_EQ(tapped.summary, "hello v2 anonymous");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorTest, Session_WrongVersionHelloIsFlagged) {
    // A version the session would answer with a Bye: the inspector names the protocol's own.
    const std::vector<std::uint8_t> hello{0x01U, 0x09U, 0x00U, 0x7BU, 0x00U, 0x00U,
                                          0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const InspectedPacket tapped =
        InspectPacket(hello, PacketDirection::Inbound, 0U, InspectedChannel::Unknown);
    EXPECT_EQ(tapped.sessionKind, "hello");
    ASSERT_TRUE(tapped.helloVersion.has_value());
    EXPECT_EQ(*tapped.helloVersion, 9U);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, 123U);
    EXPECT_EQ(tapped.summary, "hello v9 token 0x000000000000007b (protocol wants v2)");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorTest, Session_ShortAndPaddedControlMessages) {
    // Not even a version: nothing decodes.
    for (const auto& shortHello :
         {std::vector<std::uint8_t>{0x01U}, std::vector<std::uint8_t>{0x01U, 0x02U}}) {
        const InspectedPacket tapped = InspectPacket(shortHello, PacketDirection::Inbound,
                                                           0U, InspectedChannel::Unknown);
        EXPECT_EQ(tapped.sessionKind, "hello");
        EXPECT_FALSE(tapped.helloVersion.has_value());
        EXPECT_EQ(tapped.summary, "hello (truncated)");
        EXPECT_TRUE(tapped.malformed);
    }
    // A version but no token: decodes as far as the bytes go, and says so.
    const InspectedPacket noToken =
        InspectPacket({0x01U, 0x02U, 0x00U}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Unknown);
    EXPECT_EQ(noToken.sessionKind, "hello");
    ASSERT_TRUE(noToken.helloVersion.has_value());
    EXPECT_EQ(*noToken.helloVersion, 2U);
    EXPECT_FALSE(noToken.helloToken.has_value());
    EXPECT_EQ(noToken.summary, "hello v2 (truncated)");
    EXPECT_FALSE(noToken.malformed);
    // Trailing bytes on control messages: the session ignores them, the inspector notes them.
    const InspectedPacket paddedWelcome =
        InspectPacket({0x02U, 0xAAU, 0xBBU}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Unknown);
    EXPECT_EQ(paddedWelcome.sessionKind, "welcome");
    EXPECT_EQ(paddedWelcome.trailingBytes, 2U);
    EXPECT_EQ(paddedWelcome.summary, "welcome (+2 trailing bytes)");
    EXPECT_FALSE(paddedWelcome.malformed);
    const InspectedPacket paddedBye = InspectPacket({0x03U, 0xCCU},
                                                          PacketDirection::Outbound, 0U,
                                                          InspectedChannel::Unknown);
    EXPECT_EQ(paddedBye.trailingBytes, 1U);
    EXPECT_EQ(paddedBye.summary, "bye (+1 trailing byte)");
    EXPECT_FALSE(paddedBye.malformed);
}

TEST(NetworkInspectorTest, Wire_EmptyAndForeignDatagramsAreMalformed) {
    const InspectedPacket empty =
        InspectPacket({}, PacketDirection::Inbound, 0U, InspectedChannel::Unknown);
    EXPECT_EQ(empty.sessionKind, "empty");
    EXPECT_EQ(empty.summary, "empty datagram");
    EXPECT_TRUE(empty.malformed);
    EXPECT_EQ(DescribePacket(empty), "< [wire] empty datagram");

    for (const auto& foreign :
         {std::vector<std::uint8_t>{0x09U}, std::vector<std::uint8_t>{0xFFU, 0x01U, 0x02U}}) {
        const InspectedPacket tapped = InspectPacket(foreign, PacketDirection::Inbound,
                                                           0U, InspectedChannel::Rpc);
        EXPECT_EQ(tapped.sessionKind, "foreign");
        EXPECT_TRUE(tapped.malformed);
    }
    const InspectedPacket foreign =
        InspectPacket({0x09U}, PacketDirection::Inbound, 0U, InspectedChannel::Rpc);
    EXPECT_EQ(foreign.summary, "foreign tag 0x09");
    EXPECT_EQ(DescribePacket(foreign), "< [rpc] foreign tag 0x09");
}

TEST(NetworkInspectorTest, Replication_TappedSnapshotsDecodeEmptyAndFull) {
    ReplicationTap tap = MakeReplicationTap();
    ASSERT_TRUE(tap.Establish(1000U));
    // Legacy framing on demand: every send is full, so the tap proves full snapshots are
    // still byte for byte what they always were. (Deltas get their own test below.)
    ASSERT_TRUE(tap.a.SetFullSnapshotPeriod(1U));

    // No entities registered: the keepalive snapshot is empty, and still well-formed.
    tap.a.Update(1100U);
    const std::optional<std::vector<std::uint8_t>> keepalive = tap.link.b.TryReceive();
    ASSERT_TRUE(keepalive.has_value());
    const InspectedPacket tappedEmpty = InspectPacket(*keepalive, PacketDirection::Outbound,
                                                            1100U, InspectedChannel::Replication);
    EXPECT_EQ(tappedEmpty.sessionKind, "data");
    EXPECT_EQ(tappedEmpty.dataKind, "snapshot");
    ASSERT_TRUE(
        std::holds_alternative<ReplicationSnapshotView>(tappedEmpty.data));
    EXPECT_TRUE(
        std::get<ReplicationSnapshotView>(tappedEmpty.data).entries.empty());
    EXPECT_EQ(tappedEmpty.summary, "snapshot 0 entries");
    EXPECT_FALSE(tappedEmpty.malformed);

    ASSERT_TRUE(tap.a.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3&) {}));
    ASSERT_TRUE(tap.a.RegisterEntity(9U, [] { return Math::Vector3{4.0F, 5.0F, 6.0F}; },
                                        [](const Math::Vector3&) {}));
    tap.a.Update(1200U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacket tapped = InspectPacket(*wire, PacketDirection::Outbound, 1200U,
                                                       InspectedChannel::Replication);
    EXPECT_EQ(tapped.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotView>(tapped.data));
    const ReplicationSnapshotView& view = std::get<ReplicationSnapshotView>(tapped.data);
    EXPECT_EQ(view.entryCount, 2U);
    ASSERT_EQ(view.entries.size(), 2U);
    EXPECT_EQ(view.entries[0].first, 7U);
    EXPECT_EQ(view.entries[0].second, Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(view.entries[1].first, 9U);
    EXPECT_EQ(view.entries[1].second, Math::Vector3(4.0F, 5.0F, 6.0F));
    EXPECT_EQ(tapped.summary, "snapshot 2 entries");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacket(tapped), "> [replication] snapshot 2 entries");
}

TEST(NetworkInspectorTest, Replication_StrictFramingRejectsShortAndPadded) {
    // Declares two entries, carries one.
    const std::vector<std::uint8_t> shortPayload{0x04U, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                                 0x00U, 0x00U, 0x80U, 0x3FU, 0x00U, 0x00U, 0x00U,
                                                 0x40U, 0x00U, 0x00U, 0x40U, 0x40U};
    const InspectedPacket shortPacket =
        InspectPacket(shortPayload, PacketDirection::Inbound, 0U,
                         InspectedChannel::Replication);
    EXPECT_EQ(shortPacket.dataKind, "malformed");
    EXPECT_TRUE(shortPacket.malformed);

    // One entry plus one trailing byte: snapshots end exactly.
    std::vector<std::uint8_t> padded{0x04U, 0x01U, 0x00U};
    padded.insert(padded.end(), shortPayload.begin() + 3, shortPayload.end());
    padded.push_back(0xFFU);
    const InspectedPacket paddedPacket = InspectPacket(padded, PacketDirection::Inbound,
                                                             0U, InspectedChannel::Replication);
    EXPECT_EQ(paddedPacket.dataKind, "malformed");
    EXPECT_TRUE(paddedPacket.malformed);

    // A bare tag carries no count at all.
    const InspectedPacket bare =
        InspectPacket({0x04U}, PacketDirection::Inbound, 0U, InspectedChannel::Replication);
    EXPECT_EQ(bare.dataKind, "malformed");
    EXPECT_EQ(bare.summary, "malformed replication payload (0 bytes)");
    EXPECT_TRUE(bare.malformed);
}

TEST(NetworkInspectorTest, Rpc_TappedCallAndResponseDecodeAndStillComplete) {
    RpcTap tap = MakeRpcTap();
    ASSERT_TRUE(tap.b.RegisterMethod(
        "add", [](const std::span<const Blueprint::Value> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOk(Blueprint::Value{x + y});
        }));
    ASSERT_TRUE(tap.Establish(1000U));

    const RpcCallId callId =
        tap.a.Call("add", {Blueprint::Value{std::int64_t{20}},
                              Blueprint::Value{std::int64_t{22}}},
                      tap.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacket tappedCall = InspectPacket(*wire, PacketDirection::Outbound,
                                                           1000U, InspectedChannel::Rpc);
    EXPECT_EQ(tappedCall.sessionKind, "data");
    EXPECT_EQ(tappedCall.dataKind, "call");
    ASSERT_TRUE(std::holds_alternative<RpcCallView>(tappedCall.data));
    const RpcCallView& call = std::get<RpcCallView>(tappedCall.data);
    EXPECT_EQ(call.callId, callId);
    EXPECT_EQ(call.method, "add");
    ASSERT_EQ(call.args.size(), 2U);
    EXPECT_EQ(call.args[0], Blueprint::Value{std::int64_t{20}});
    EXPECT_EQ(call.args[1], Blueprint::Value{std::int64_t{22}});
    EXPECT_EQ(tappedCall.summary,
              "call #" + std::to_string(callId) + " \"add\" (20, 22)");
    EXPECT_FALSE(tappedCall.malformed);
    EXPECT_EQ(DescribePacket(tappedCall),
              "> [rpc] call #" + std::to_string(callId) + " \"add\" (20, 22)");

    // Re-inject: the tapped call still runs and answers.
    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacket tappedResponse = InspectPacket(*answer, PacketDirection::Inbound,
                                                               1000U, InspectedChannel::Rpc);
    EXPECT_EQ(tappedResponse.dataKind, "response");
    ASSERT_TRUE(std::holds_alternative<RpcResponseView>(tappedResponse.data));
    const RpcResponseView& response = std::get<RpcResponseView>(tappedResponse.data);
    EXPECT_EQ(response.callId, callId);
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(response.value, Blueprint::Value{std::int64_t{42}});
    EXPECT_TRUE(response.error.empty());
    EXPECT_EQ(tappedResponse.summary, "response #" + std::to_string(callId) + " ok 42");
    EXPECT_FALSE(tappedResponse.malformed);

    ASSERT_TRUE(tap.link.b.Send(*answer));
    tap.a.Update(1000U);
    ASSERT_EQ(tap.doneA.count(callId), 1U);
    EXPECT_TRUE(tap.doneA.at(callId).ok);
    EXPECT_EQ(tap.doneA.at(callId).value, Blueprint::Value{std::int64_t{42}});
}

TEST(NetworkInspectorTest, Rpc_TappedCollectionsDecodeBranchForBranch) {
    RpcTap tap = MakeRpcTap();
    ASSERT_TRUE(tap.b.RegisterMethod(
        "echo", [](const std::span<const Blueprint::Value> args) { return RpcOk(args[0]); }));
    ASSERT_TRUE(tap.Establish(1000U));

    const Blueprint::Value list = Blueprint::MakeListValue(
        {Blueprint::Value{std::int64_t{1}}, Blueprint::Value{"two"}, Blueprint::Value{2.5}});
    const Blueprint::Value tuple =
        Blueprint::MakeTupleValue({Blueprint::Value{std::int64_t{1}}});
    const Blueprint::Value map = Blueprint::MakeMapValue(
        {Blueprint::Value{"a"}, Blueprint::Value{std::int64_t{1}}});
    const RpcCallId callId = tap.a.Call("echo", {list, tuple, map}, tap.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacket tappedCall = InspectPacket(*wire, PacketDirection::Outbound,
                                                           1000U, InspectedChannel::Rpc);
    ASSERT_TRUE(std::holds_alternative<RpcCallView>(tappedCall.data));
    const RpcCallView& call = std::get<RpcCallView>(tappedCall.data);
    ASSERT_EQ(call.args.size(), 3U);
    EXPECT_TRUE(ValuesEqual(call.args[0], list));
    EXPECT_TRUE(ValuesEqual(call.args[1], tuple));
    EXPECT_TRUE(ValuesEqual(call.args[2], map));
    EXPECT_EQ(tappedCall.summary, "call #" + std::to_string(callId) +
                                      " \"echo\" ([1, two, 2.5], (1,), {a: 1})");
    EXPECT_FALSE(tappedCall.malformed);

    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacket tappedResponse = InspectPacket(*answer, PacketDirection::Inbound,
                                                               1000U, InspectedChannel::Rpc);
    ASSERT_TRUE(std::holds_alternative<RpcResponseView>(tappedResponse.data));
    const RpcResponseView& response = std::get<RpcResponseView>(tappedResponse.data);
    EXPECT_TRUE(response.ok);
    EXPECT_TRUE(ValuesEqual(response.value, list));
    EXPECT_EQ(tappedResponse.summary,
              "response #" + std::to_string(callId) + " ok [1, two, 2.5]");
}

TEST(NetworkInspectorTest, Rpc_TappedErrorResponseDecodes) {
    RpcTap tap = MakeRpcTap();
    ASSERT_TRUE(tap.Establish(1000U));

    const RpcCallId callId = tap.a.Call("nope", {}, tap.FileA());
    ASSERT_NE(callId, kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacket tappedCall = InspectPacket(*wire, PacketDirection::Outbound,
                                                           1000U, InspectedChannel::Rpc);
    EXPECT_EQ(tappedCall.summary, "call #" + std::to_string(callId) + " \"nope\" ()");

    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacket tappedResponse = InspectPacket(*answer, PacketDirection::Inbound,
                                                               1000U, InspectedChannel::Rpc);
    EXPECT_EQ(tappedResponse.dataKind, "response");
    ASSERT_TRUE(std::holds_alternative<RpcResponseView>(tappedResponse.data));
    const RpcResponseView& response = std::get<RpcResponseView>(tappedResponse.data);
    EXPECT_EQ(response.callId, callId);
    EXPECT_FALSE(response.ok);
    EXPECT_EQ(response.error, "unknown method 'nope'");
    EXPECT_EQ(tappedResponse.summary,
              "response #" + std::to_string(callId) + " error \"unknown method 'nope'\"");
    EXPECT_FALSE(tappedResponse.malformed);
    EXPECT_EQ(DescribePacket(tappedResponse), "< [rpc] response #" + std::to_string(callId) +
                                                    " error \"unknown method 'nope'\"");
}

TEST(NetworkInspectorTest, Rpc_MutatedFramesAreMalformed) {
    RpcTap tap = MakeRpcTap();
    ASSERT_TRUE(tap.b.RegisterMethod(
        "echo", [](const std::span<const Blueprint::Value> args) { return RpcOk(args[0]); }));
    ASSERT_TRUE(tap.Establish(1000U));
    ASSERT_NE(tap.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, tap.FileA()),
              kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    // A live call frame, corrupted five ways: magic, version, truncation, padding, kind.
    std::vector<std::uint8_t> badMagic = *wire;
    badMagic[1U] = static_cast<std::uint8_t>('X');
    std::vector<std::uint8_t> badVersion = *wire;
    badVersion[3U] = 0x09U;
    const std::vector<std::uint8_t> truncated(wire->begin(), wire->begin() + 6);
    std::vector<std::uint8_t> padded = *wire;
    padded.push_back(0xFFU);
    std::vector<std::uint8_t> badKind = *wire;
    badKind[4U] = 0x03U;
    for (const std::vector<std::uint8_t>& mutant : {badMagic, badVersion, truncated, padded,
                                                    badKind}) {
        const InspectedPacket tapped = InspectPacket(mutant, PacketDirection::Inbound,
                                                           0U, InspectedChannel::Rpc);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    EXPECT_EQ(InspectPacket(truncated, PacketDirection::Inbound, 0U,
                               InspectedChannel::Rpc)
                  .summary,
              "malformed rpc payload (5 bytes)");

    // A live ok response with an impossible status, and a hand-built error whose payload is
    // not a string: the RPC peer rejects both, so does the inspector.
    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.b.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    std::vector<std::uint8_t> badStatus = *answer;
    badStatus[9U] = 0x02U;
    EXPECT_TRUE(InspectPacket(badStatus, PacketDirection::Inbound, 0U,
                                 InspectedChannel::Rpc)
                    .malformed);
    const std::vector<std::uint8_t> intError{0x04U, 0x52U, 0x43U, 0x01U, 0x02U, 0x01U, 0x00U,
                                             0x00U, 0x00U, 0x01U, 0x02U, 0x00U, 0x00U, 0x00U,
                                             0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const InspectedPacket tappedIntError =
        InspectPacket(intError, PacketDirection::Inbound, 0U, InspectedChannel::Rpc);
    EXPECT_EQ(tappedIntError.dataKind, "malformed");
    EXPECT_TRUE(tappedIntError.malformed);
}

TEST(NetworkInspectorTest, Prediction_TappedInputsAndStateDecode) {
    PredictionTap tap = MakePredictionTap();
    ASSERT_TRUE(tap.Establish(1000U));

    PredictedInput input;
    input.move = Math::Vector3{0.5F, -1.25F, 2.0F};
    input.rise = 0.75F;
    input.jumpPressed = true;
    ASSERT_EQ(tap.client.Tick(input), 1U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacket tappedInputs = InspectPacket(*wire, PacketDirection::Outbound,
                                                             1000U, InspectedChannel::Prediction);
    EXPECT_EQ(tappedInputs.sessionKind, "data");
    EXPECT_EQ(tappedInputs.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsView>(tappedInputs.data));
    const PredictionInputsView& inputs = std::get<PredictionInputsView>(tappedInputs.data);
    ASSERT_EQ(inputs.window.size(), 1U);
    EXPECT_EQ(inputs.window[0].tick, 1U);
    EXPECT_EQ(inputs.window[0].pawn, 0U);
    EXPECT_EQ(inputs.window[0].dtSeconds, 1.0F / 60.0F);
    EXPECT_EQ(inputs.window[0].input, input);
    EXPECT_EQ(tappedInputs.summary, "inputs 1 tick 1");
    EXPECT_FALSE(tappedInputs.malformed);
    EXPECT_EQ(DescribePacket(tappedInputs), "> [prediction] inputs 1 tick 1");

    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.server.Update(1000U);
    ASSERT_EQ(tap.server.Stats().statesSent, 1U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacket tappedState = InspectPacket(*answer, PacketDirection::Inbound,
                                                            1000U, InspectedChannel::Prediction);
    EXPECT_EQ(tappedState.dataKind, "state");
    ASSERT_TRUE(std::holds_alternative<PredictionStateView>(tappedState.data));
    const PredictionStateView& state = std::get<PredictionStateView>(tappedState.data);
    EXPECT_EQ(state.tick, 1U);
    EXPECT_EQ(state.pawn, 0U);
    EXPECT_EQ(state.state, CarryStepper()(PredictedState{}, input, 1.0F / 60.0F));
    // Floats render through the script value printer; the shape is the inspector's promise.
    EXPECT_EQ(tappedState.summary.rfind("state tick 1 (", 0U), 0U);
    EXPECT_FALSE(tappedState.malformed);
    EXPECT_EQ(DescribePacket(tappedState).rfind("< [prediction] state tick 1 (", 0U), 0U);
}

TEST(NetworkInspectorTest, Prediction_TappedWindowDecodesOldestFirst) {
    PredictionTap tap = MakePredictionTap();
    ASSERT_TRUE(tap.Establish(1000U));

    // Three ticks with no answers: each message repeats the unacked window, so the third
    // carries ticks one through three.
    PredictedInput first;
    first.move = Math::Vector3{1.0F, 0.0F, 0.0F};
    PredictedInput second = first;
    second.jumpPressed = true;
    PredictedInput third = first;
    third.rise = 1.0F;
    ASSERT_EQ(tap.client.Tick(first), 1U);
    ASSERT_EQ(tap.client.Tick(second), 2U);
    ASSERT_EQ(tap.client.Tick(third), 3U);
    ASSERT_TRUE(tap.link.b.TryReceive().has_value());
    ASSERT_TRUE(tap.link.b.TryReceive().has_value());
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacket tapped = InspectPacket(*wire, PacketDirection::Outbound, 1000U,
                                                       InspectedChannel::Prediction);
    EXPECT_EQ(tapped.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsView>(tapped.data));
    const PredictionInputsView& inputs = std::get<PredictionInputsView>(tapped.data);
    ASSERT_EQ(inputs.window.size(), 3U);
    EXPECT_EQ(inputs.window[0].tick, 1U);
    EXPECT_EQ(inputs.window[0].input, first);
    EXPECT_EQ(inputs.window[1].tick, 2U);
    EXPECT_EQ(inputs.window[1].input, second);
    EXPECT_EQ(inputs.window[2].tick, 3U);
    EXPECT_EQ(inputs.window[2].input, third);
    EXPECT_EQ(tapped.summary, "inputs 3 ticks 1-3");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorTest, Prediction_MutatedMessagesAreMalformed) {
    PredictionTap tap = MakePredictionTap();
    ASSERT_TRUE(tap.Establish(1000U));
    PredictedInput input;
    input.move = Math::Vector3{1.0F, 0.0F, 0.0F};
    ASSERT_EQ(tap.client.Tick(input), 1U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    // A live input message, corrupted four ways: kind, count, jump flag, truncation.
    std::vector<std::uint8_t> badKind = *wire;
    badKind[1U] = 0x03U;
    std::vector<std::uint8_t> badCount = *wire;
    badCount[2U] = 0x03U;
    std::vector<std::uint8_t> badJump = *wire;
    badJump.back() = 0x02U;
    const std::vector<std::uint8_t> truncated(wire->begin(), wire->end() - 1);
    for (const std::vector<std::uint8_t>& mutant : {badKind, badCount, badJump, truncated}) {
        const InspectedPacket tapped =
            InspectPacket(mutant, PacketDirection::Inbound, 0U,
                             InspectedChannel::Prediction);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }

    // A live state message missing its last byte: states are exactly 39 bytes.
    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.server.Update(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceive();
    ASSERT_TRUE(answer.has_value());
    ASSERT_EQ(answer->size(), 1U + 39U);
    const std::vector<std::uint8_t> shortState(answer->begin(), answer->end() - 1);
    const InspectedPacket tappedShort =
        InspectPacket(shortState, PacketDirection::Inbound, 0U,
                         InspectedChannel::Prediction);
    EXPECT_EQ(tappedShort.dataKind, "malformed");
    EXPECT_EQ(tappedShort.summary, "malformed prediction payload (38 bytes)");
    EXPECT_TRUE(tappedShort.malformed);
}

TEST(NetworkInspectorTest, UnknownChannel_PayloadStaysOpaque) {
    // Junk bytes: counted, never guessed at, never flagged.
    const InspectedPacket junk =
        InspectPacket({0x04U, 0x6AU, 0x75U, 0x6EU, 0x6BU}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Unknown);
    EXPECT_EQ(junk.sessionKind, "data");
    EXPECT_EQ(junk.dataKind, "opaque");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(junk.data));
    EXPECT_EQ(junk.summary, "4 bytes (opaque)");
    EXPECT_FALSE(junk.malformed);
    EXPECT_EQ(DescribePacket(junk), "< [wire] 4 bytes (opaque)");

    // Even a perfectly good call frame stays opaque when the channel is unknown.
    RpcTap tap = MakeRpcTap();
    ASSERT_TRUE(tap.Establish(1000U));
    ASSERT_NE(tap.a.Call("echo", {Blueprint::Value{std::int64_t{1}}}, tap.FileA()),
              kInvalidRpcCallId);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacket tapped =
        InspectPacket(*wire, PacketDirection::Outbound, 1000U, InspectedChannel::Unknown);
    EXPECT_EQ(tapped.dataKind, "opaque");
    EXPECT_EQ(tapped.summary, std::to_string(wire->size() - 1U) + " bytes (opaque)");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorTest, Replication_TappedDeltaDecodesBesideFulls) {
    ReplicationTap tap = MakeReplicationTap();
    ASSERT_TRUE(tap.Establish(1000U));
    // Every second send is full: the tap collects a delta, a full, and an empty delta live.
    ASSERT_TRUE(tap.a.SetFullSnapshotPeriod(2U));
    ASSERT_TRUE(tap.a.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3&) {}));

    tap.a.Update(1100U);
    const std::optional<std::vector<std::uint8_t>> delta = tap.link.b.TryReceive();
    ASSERT_TRUE(delta.has_value());
    const InspectedPacket tappedDelta = InspectPacket(*delta, PacketDirection::Outbound,
                                                            1100U, InspectedChannel::Replication);
    EXPECT_EQ(tappedDelta.dataKind, "delta");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotView>(tappedDelta.data));
    const ReplicationSnapshotView& deltaView = std::get<ReplicationSnapshotView>(tappedDelta.data);
    EXPECT_TRUE(deltaView.isDelta);
    EXPECT_EQ(deltaView.entryCount, 1U);
    ASSERT_EQ(deltaView.entries.size(), 1U);
    EXPECT_EQ(deltaView.entries[0].first, 7U);
    EXPECT_EQ(deltaView.entries[0].second, Math::Vector3(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(tappedDelta.summary, "delta 1 entry");
    EXPECT_FALSE(tappedDelta.malformed);
    EXPECT_EQ(DescribePacket(tappedDelta), "> [replication] delta 1 entry");

    tap.a.Update(1200U);
    const std::optional<std::vector<std::uint8_t>> full = tap.link.b.TryReceive();
    ASSERT_TRUE(full.has_value());
    const InspectedPacket tappedFull = InspectPacket(*full, PacketDirection::Outbound,
                                                           1200U, InspectedChannel::Replication);
    EXPECT_EQ(tappedFull.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotView>(tappedFull.data));
    EXPECT_FALSE(std::get<ReplicationSnapshotView>(tappedFull.data).isDelta);
    EXPECT_EQ(tappedFull.summary, "snapshot 1 entry");

    tap.a.Update(1300U);
    const std::optional<std::vector<std::uint8_t>> quiet = tap.link.b.TryReceive();
    ASSERT_TRUE(quiet.has_value());
    const InspectedPacket tappedQuiet = InspectPacket(*quiet, PacketDirection::Outbound,
                                                            1300U, InspectedChannel::Replication);
    EXPECT_EQ(tappedQuiet.dataKind, "delta");
    EXPECT_EQ(tappedQuiet.summary, "delta 0 entries");
    EXPECT_FALSE(tappedQuiet.malformed);
}

TEST(NetworkInspectorTest, Replication_MalformedDeltaFramesRejected) {
    // Marker with no count, a declared entry that never arrives, and trailing bytes on an
    // empty delta: the strict end rule holds for deltas exactly like fulls.
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU, 0x01U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU, 0x00U, 0x00U, 0xFFU}}) {
        const InspectedPacket tapped = InspectPacket(mutant, PacketDirection::Inbound,
                                                           0U, InspectedChannel::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
}

TEST(NetworkInspectorTest, Replication_TappedOwnershipDecodes) {
    ReplicationTap tap = MakeReplicationTap();
    ASSERT_TRUE(tap.a.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 0.0F, 0.0F}; },
                                        [](const Math::Vector3&) {},
                                        ReplicationAuthority::Owner));
    ASSERT_TRUE(tap.b.RegisterEntity(7U, [] { return Math::Vector3{}; },
                                        [](const Math::Vector3&) {},
                                        ReplicationAuthority::Proxy));
    tap.a.SetOwnershipTransferHandler(
        [&tap](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(tap.a.AcceptTransfer(netId));
            }
        });
    ASSERT_TRUE(tap.Establish(1000U));
    ASSERT_TRUE(tap.b.RequestOwnership(7U, 1000U));

    // Watching A: the request arrives inbound.
    const std::optional<std::vector<std::uint8_t>> request = tap.link.a.TryReceive();
    ASSERT_TRUE(request.has_value());
    const InspectedPacket tappedRequest =
        InspectPacket(*request, PacketDirection::Inbound, 1000U,
                         InspectedChannel::Replication);
    EXPECT_EQ(tappedRequest.dataKind, "ownership");
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferView>(tappedRequest.data));
    const OwnershipTransferView& requestView =
        std::get<OwnershipTransferView>(tappedRequest.data);
    EXPECT_EQ(requestView.kind, OwnershipMessageKind::Request);
    EXPECT_EQ(requestView.netId, 7U);
    EXPECT_EQ(tappedRequest.summary, "ownership request 7");
    EXPECT_FALSE(tappedRequest.malformed);
    EXPECT_EQ(DescribePacket(tappedRequest), "< [replication] ownership request 7");

    ASSERT_TRUE(tap.link.b.Send(*request));
    tap.a.Update(1100U);
    const std::optional<std::vector<std::uint8_t>> accept = tap.link.b.TryReceive();
    ASSERT_TRUE(accept.has_value());
    const InspectedPacket tappedAccept =
        InspectPacket(*accept, PacketDirection::Outbound, 1100U,
                         InspectedChannel::Replication);
    EXPECT_EQ(tappedAccept.dataKind, "ownership");
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferView>(tappedAccept.data));
    const OwnershipTransferView& acceptView =
        std::get<OwnershipTransferView>(tappedAccept.data);
    EXPECT_EQ(acceptView.kind, OwnershipMessageKind::Accept);
    EXPECT_EQ(acceptView.netId, 7U);
    EXPECT_EQ(tappedAccept.summary, "ownership accept 7");
    EXPECT_FALSE(tappedAccept.malformed);
    EXPECT_EQ(DescribePacket(tappedAccept), "> [replication] ownership accept 7");
}

TEST(NetworkInspectorTest, Replication_MalformedOwnershipRejected) {
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x04U, 0x07U, 0x00U, 0x00U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x01U, 0x07U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x01U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0xFFU}}) {
        const InspectedPacket tapped = InspectPacket(mutant, PacketDirection::Inbound,
                                                           0U, InspectedChannel::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    // The third verb decodes too.
    const InspectedPacket deny =
        InspectPacket({0x04U, 0xFEU, 0xFFU, 0x03U, 0x07U, 0x00U, 0x00U, 0x00U},
                         PacketDirection::Inbound, 0U, InspectedChannel::Replication);
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferView>(deny.data));
    EXPECT_EQ(std::get<OwnershipTransferView>(deny.data).kind, OwnershipMessageKind::Deny);
    EXPECT_EQ(deny.summary, "ownership deny 7");
    EXPECT_FALSE(deny.malformed);
}

TEST(NetworkInspectorTest, Replication_TappedPropertiesDecode) {
    ReplicationTap tap = MakeReplicationTap();
    ASSERT_TRUE(tap.a.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 0.0F, 0.0F}; },
                                        [](const Math::Vector3&) {}));
    ASSERT_TRUE(tap.b.RegisterEntity(7U, [] { return Math::Vector3{}; },
                                        [](const Math::Vector3&) {}));
    Blueprint::Value applied{std::int64_t{0}};
    int applyCalls = 0;
    ASSERT_TRUE(tap.b.RegisterProperty(
        7U, 1U, [&applied, &applyCalls](const Blueprint::Value& value) {
            applied = value;
            ++applyCalls;
        }));
    Blueprint::Value appliedList{std::int64_t{0}};
    ASSERT_TRUE(tap.b.RegisterProperty(
        7U, 2U, [&appliedList](const Blueprint::Value& value) { appliedList = value; }));
    ASSERT_TRUE(tap.Establish(1000U));
    const Blueprint::Value health{std::int64_t{83}};
    const Blueprint::Value tags = Blueprint::MakeListValue(
        {Blueprint::Value{std::int64_t{1}}, Blueprint::Value{std::string{"two"}}});
    ASSERT_TRUE(tap.a.SetProperty(7U, 1U, health));
    ASSERT_TRUE(tap.a.SetProperty(7U, 2U, tags));
    tap.a.Update(1100U);

    // Watching B: A's snapshot arrives first, the property frame second.
    const std::optional<std::vector<std::uint8_t>> snapshot = tap.link.b.TryReceive();
    ASSERT_TRUE(snapshot.has_value());
    const InspectedPacket tappedSnapshot =
        InspectPacket(*snapshot, PacketDirection::Outbound, 1100U,
                         InspectedChannel::Replication);
    EXPECT_EQ(tappedSnapshot.dataKind, "delta");
    const std::optional<std::vector<std::uint8_t>> stolen = tap.link.b.TryReceive();
    ASSERT_TRUE(stolen.has_value());
    const InspectedPacket tapped =
        InspectPacket(*stolen, PacketDirection::Outbound, 1100U,
                         InspectedChannel::Replication);
    EXPECT_EQ(tapped.dataKind, "properties");
    ASSERT_TRUE(std::holds_alternative<ReplicationPropertiesView>(tapped.data));
    const ReplicationPropertiesView& view =
        std::get<ReplicationPropertiesView>(tapped.data);
    EXPECT_EQ(view.propertyCount, 2U);
    ASSERT_EQ(view.entries.size(), 2U);
    EXPECT_EQ(view.entries[0].netId, 7U);
    EXPECT_EQ(view.entries[0].propId, 1U);
    EXPECT_TRUE(ValuesEqual(view.entries[0].value, health));
    EXPECT_EQ(view.entries[1].netId, 7U);
    EXPECT_EQ(view.entries[1].propId, 2U);
    EXPECT_TRUE(ValuesEqual(view.entries[1].value, tags));
    EXPECT_EQ(tapped.summary, "properties 2 entries");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacket(tapped), "> [replication] properties 2 entries");

    // Re-injected in order, the tapped traffic still completes.
    ASSERT_TRUE(tap.link.a.Send(*snapshot));
    ASSERT_TRUE(tap.link.a.Send(*stolen));
    tap.b.Update(1100U);
    EXPECT_EQ(applyCalls, 1);
    EXPECT_TRUE(ValuesEqual(applied, health));
    EXPECT_TRUE(ValuesEqual(appliedList, tags));
    EXPECT_EQ(tap.b.Stats().propertiesApplied, 2U);
}

TEST(NetworkInspectorTest, Replication_MalformedPropertiesRejected) {
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x09U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x00U, 0xFFU}}) {
        const InspectedPacket tapped =
            InspectPacket(mutant, PacketDirection::Inbound, 0U,
                             InspectedChannel::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    // A count of zero decodes: the sender never emits it, but the shape is strict either way.
    const InspectedPacket empty =
        InspectPacket({0x04U, 0xFDU, 0xFFU, 0x00U, 0x00U}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Replication);
    EXPECT_EQ(empty.dataKind, "properties");
    ASSERT_TRUE(std::holds_alternative<ReplicationPropertiesView>(empty.data));
    EXPECT_EQ(std::get<ReplicationPropertiesView>(empty.data).propertyCount, 0U);
    EXPECT_TRUE(std::get<ReplicationPropertiesView>(empty.data).entries.empty());
    EXPECT_EQ(empty.summary, "properties 0 entries");
    EXPECT_FALSE(empty.malformed);
}

TEST(NetworkInspectorTest, Replication_TappedMultiplexedSnapshotDecodes) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    ASSERT_TRUE(sessionA.EnableMultiplexing());
    ASSERT_TRUE(sessionB.EnableMultiplexing());
    NetworkReplicator replA(sessionA, 5U);
    NetworkReplicator replB(sessionB, 5U);
    Math::Vector3 positionB{};
    ASSERT_TRUE(replA.RegisterEntity(7U, [] { return Math::Vector3{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3&) {}));
    ASSERT_TRUE(replB.RegisterEntity(7U, [&positionB] { return positionB; },
                                        [&positionB](const Math::Vector3& position) {
                                            positionB = position;
                                        }));
    ASSERT_TRUE(sessionA.Connect(1000U));
    sessionA.Update(1000U);
    sessionB.Update(1000U);
    sessionA.Update(1000U);
    sessionB.Update(1000U);
    ASSERT_EQ(sessionA.State(), SessionState::Established);
    replA.Update(1100U);

    const std::optional<std::vector<std::uint8_t>> stolen = pair.b.TryReceive();
    ASSERT_TRUE(stolen.has_value());
    const InspectedPacket tapped =
        InspectPacket(*stolen, PacketDirection::Outbound, 1100U,
                         InspectedChannel::Replication, true);
    ASSERT_TRUE(tapped.channelId.has_value());
    EXPECT_EQ(*tapped.channelId, 5U);
    EXPECT_EQ(tapped.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotView>(tapped.data));
    const ReplicationSnapshotView& view =
        std::get<ReplicationSnapshotView>(tapped.data);
    EXPECT_EQ(view.entryCount, 1U);
    ASSERT_EQ(view.entries.size(), 1U);
    EXPECT_EQ(view.entries[0].first, 7U);
    EXPECT_EQ(view.entries[0].second, (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(tapped.summary, "snapshot 1 entry");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacket(tapped), "> [replication ch5] snapshot 1 entry");

    ASSERT_TRUE(pair.a.Send(*stolen));
    replB.Update(1100U);
    EXPECT_EQ(positionB, (Math::Vector3{1.0F, 2.0F, 3.0F}));
}

TEST(NetworkInspectorTest, UnknownChannel_MultiplexedPayloadStaysOpaque) {
    const InspectedPacket tapped =
        InspectPacket({0x04U, 0x07U, 0xAAU, 0xBBU, 0xCCU}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Unknown, true);
    ASSERT_TRUE(tapped.channelId.has_value());
    EXPECT_EQ(*tapped.channelId, 7U);
    EXPECT_EQ(tapped.dataKind, "opaque");
    EXPECT_EQ(tapped.summary, "3 bytes (opaque)");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacket(tapped), "< [wire ch7] 3 bytes (opaque)");
}

TEST(NetworkInspectorTest, Multiplex_BareTagAndMistakenReadsAreMalformed) {
    const InspectedPacket bare =
        InspectPacket({0x04U}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Replication, true);
    EXPECT_EQ(bare.dataKind, "malformed");
    EXPECT_TRUE(bare.malformed);
    EXPECT_FALSE(bare.channelId.has_value());

    // A legacy empty snapshot read as multiplexed: the count's first byte becomes channel
    // id 0, and the one byte left cannot frame anything.
    const InspectedPacket mistaken =
        InspectPacket({0x04U, 0x00U, 0x00U}, PacketDirection::Inbound, 0U,
                         InspectedChannel::Replication, true);
    ASSERT_TRUE(mistaken.channelId.has_value());
    EXPECT_EQ(*mistaken.channelId, 0U);
    EXPECT_EQ(mistaken.dataKind, "malformed");
    EXPECT_TRUE(mistaken.malformed);
}


TEST(NetworkInspectorTest, Prediction_MultiPawnWindowAndStateNamePawns) {
    PredictionTap tap = MakePredictionTap();
    ASSERT_TRUE(tap.Establish(1000U));

    // Two pawns tick - pawn 1 at an explicit half rate - and the second message carries
    // both streams' oldest unacked inputs.
    PredictedInput primary;
    primary.move = Math::Vector3{1.0F, 0.0F, 0.0F};
    PredictedInput sibling;
    sibling.move = Math::Vector3{0.0F, 0.0F, 2.0F};
    ASSERT_EQ(tap.client.Tick(0U, primary), 1U);
    ASSERT_EQ(tap.client.Tick(1U, sibling, 1.0F / 30.0F), 1U);
    ASSERT_TRUE(tap.link.b.TryReceive().has_value());
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceive();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacket tapped = InspectPacket(*wire, PacketDirection::Outbound,
                                                       1000U, InspectedChannel::Prediction);
    EXPECT_EQ(tapped.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsView>(tapped.data));
    const PredictionInputsView& inputs = std::get<PredictionInputsView>(tapped.data);
    ASSERT_EQ(inputs.window.size(), 2U);
    EXPECT_EQ(inputs.window[0].tick, 1U);
    EXPECT_EQ(inputs.window[0].pawn, 0U);
    EXPECT_EQ(inputs.window[0].dtSeconds, 1.0F / 60.0F);
    EXPECT_EQ(inputs.window[0].input, primary);
    EXPECT_EQ(inputs.window[1].tick, 1U);
    EXPECT_EQ(inputs.window[1].pawn, 1U);
    EXPECT_EQ(inputs.window[1].dtSeconds, 1.0F / 30.0F);
    EXPECT_EQ(inputs.window[1].input, sibling);
    EXPECT_EQ(tapped.summary, "inputs 2 ticks 1-1 pawns 0+1");
    EXPECT_FALSE(tapped.malformed);

    // Both ticks apply server-side; the sibling's state names its pawn in the view and the
    // one-line summary, while the primary keeps the old shape.
    ASSERT_TRUE(tap.link.a.Send(*wire));
    tap.server.Update(1000U);
    ASSERT_EQ(tap.server.Stats().inputsApplied, 2U);
    const std::optional<std::vector<std::uint8_t>> first = tap.link.a.TryReceive();
    const std::optional<std::vector<std::uint8_t>> second = tap.link.a.TryReceive();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const InspectedPacket tappedPrimary =
        InspectPacket(*first, PacketDirection::Inbound, 1000U,
                         InspectedChannel::Prediction);
    ASSERT_TRUE(std::holds_alternative<PredictionStateView>(tappedPrimary.data));
    EXPECT_EQ(std::get<PredictionStateView>(tappedPrimary.data).pawn, 0U);
    EXPECT_EQ(tappedPrimary.summary.rfind("state tick 1 (", 0U), 0U);
    const InspectedPacket tappedSibling =
        InspectPacket(*second, PacketDirection::Inbound, 1000U,
                         InspectedChannel::Prediction);
    EXPECT_EQ(tappedSibling.dataKind, "state");
    ASSERT_TRUE(std::holds_alternative<PredictionStateView>(tappedSibling.data));
    const PredictionStateView& state = std::get<PredictionStateView>(tappedSibling.data);
    EXPECT_EQ(state.tick, 1U);
    EXPECT_EQ(state.pawn, 1U);
    EXPECT_EQ(tappedSibling.summary.rfind("state pawn 1 tick 1 (", 0U), 0U);
    EXPECT_FALSE(tappedSibling.malformed);
}

} // namespace
} // namespace UVE::Network::Tests
