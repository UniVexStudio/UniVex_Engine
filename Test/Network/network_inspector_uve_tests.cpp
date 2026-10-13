// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_inspector_uve.h"

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
[[nodiscard]] bool ValuesEqualUVE(const UVScript::ValueUVE& a, const UVScript::ValueUVE& b) {
    if (a.index() != b.index()) {
        return false;
    }
    const auto* collectionA =
        std::get_if<std::shared_ptr<const UVScript::CollectionValueUVE>>(&a);
    if (collectionA == nullptr) {
        return a == b;
    }
    const auto& collectionB = std::get<std::shared_ptr<const UVScript::CollectionValueUVE>>(b);
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
        if (!ValuesEqualUVE((*collectionA)->items[i], collectionB->items[i])) {
            return false;
        }
    }
    return true;
}

// Every positive case below steals REAL datagrams off a live loopback exchange - the inspector
// never meets a hand-framed message except in the malformed table, where hand-built garbage
// is the whole point. Stolen datagrams get re-injected afterwards, proving the tapped traffic
// still completes its business.

struct SessionTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkSessionUVE a;
    NetworkSessionUVE b;
};

[[nodiscard]] SessionTapUVE MakeSessionTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE a(SessionRoleUVE::Client, std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE b(SessionRoleUVE::Server, std::make_unique<LoopbackEndpointUVE>(pair.b));
    return SessionTapUVE{std::move(pair), std::move(a), std::move(b)};
}

struct ReplicationTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkReplicatorUVE a;
    NetworkReplicatorUVE b;

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        return a.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] ReplicationTapUVE MakeReplicationTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkReplicatorUVE a(std::move(sessionA));
    NetworkReplicatorUVE b(std::move(sessionB));
    return ReplicationTapUVE{std::move(pair), std::move(a), std::move(b)};
}

struct RpcTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkRpcUVE a;
    NetworkRpcUVE b;
    std::map<RpcCallIdUVE, RpcOutcomeUVE> doneA;

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
        return a.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }

    [[nodiscard]] RpcResponseCallbackUVE FileAUVE() {
        return [this](const RpcCallIdUVE callId, const RpcOutcomeUVE& outcome) {
            doneA.emplace(callId, outcome);
        };
    }
};

[[nodiscard]] RpcTapUVE MakeRpcTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkRpcUVE a(std::move(sessionA));
    NetworkRpcUVE b(std::move(sessionB));
    return RpcTapUVE{std::move(pair), std::move(a), std::move(b), {}};
}

struct PredictionTapUVE final {
    LoopbackTransportPairUVE link;
    NetworkPredictionUVE client;
    NetworkPredictionUVE server;

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!client.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        client.UpdateUVE(nowMs);
        server.UpdateUVE(nowMs);
        return client.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               server.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }
};

[[nodiscard]] PredictionStepperUVE CarryStepperUVE() {
    return [](const PredictedStateUVE& state, const PredictedInputUVE& input, const float dt) {
        PredictedStateUVE next = state;
        next.velocity = state.velocity * 0.9F + input.move * (20.0F * dt);
        next.position += next.velocity * dt;
        return next;
    };
}

[[nodiscard]] PredictionTapUVE MakePredictionTapUVE() {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkPredictionUVE client(std::move(sessionA), SessionRoleUVE::Client, CarryStepperUVE(),
                                PredictedStateUVE{});
    NetworkPredictionUVE server(std::move(sessionB), SessionRoleUVE::Server, CarryStepperUVE(),
                                PredictedStateUVE{});
    return PredictionTapUVE{std::move(pair), std::move(client), std::move(server)};
}

TEST(NetworkInspectorUVETest, Session_TappedHandshakeDecodesAndStillEstablishes) {
    SessionTapUVE tap = MakeSessionTapUVE();
    // Watching the client: its hello leaves outbound, the welcome returns inbound.
    ASSERT_TRUE(tap.a.ConnectUVE(1000U, 123U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(hello.has_value());

    const InspectedPacketUVE tapped = InspectPacketUVE(*hello, PacketDirectionUVE::Outbound,
                                                       1000U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tapped.timestampMs, 1000U);
    EXPECT_EQ(tapped.direction, PacketDirectionUVE::Outbound);
    EXPECT_EQ(tapped.channel, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tapped.sessionKind, "hello");
    ASSERT_TRUE(tapped.helloVersion.has_value());
    EXPECT_EQ(*tapped.helloVersion, kSessionProtocolVersionUVE);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, 123U);
    EXPECT_EQ(tapped.trailingBytes, 0U);
    EXPECT_TRUE(tapped.dataKind.empty());
    EXPECT_TRUE(std::holds_alternative<std::monostate>(tapped.data));
    EXPECT_EQ(tapped.summary, "hello v2 token 0x000000000000007b");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(tapped.raw, *hello);
    EXPECT_EQ(DescribePacketUVE(tapped), "> [wire] hello v2 token 0x000000000000007b");

    // The session layer decodes no matter which channel watches the wire.
    const InspectedPacketUVE retagged = InspectPacketUVE(*hello, PacketDirectionUVE::Outbound,
                                                         1000U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(retagged.sessionKind, "hello");
    EXPECT_EQ(retagged.summary, tapped.summary);
    EXPECT_FALSE(retagged.malformed);

    // Re-inject: the tapped handshake still runs to established.
    ASSERT_TRUE(tap.link.a.SendUVE(*hello));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> welcome = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(welcome.has_value());
    const InspectedPacketUVE tappedWelcome = InspectPacketUVE(
        *welcome, PacketDirectionUVE::Inbound, 1000U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tappedWelcome.sessionKind, "welcome");
    EXPECT_EQ(tappedWelcome.summary, "welcome");
    EXPECT_FALSE(tappedWelcome.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedWelcome), "< [wire] welcome");

    ASSERT_TRUE(tap.link.b.SendUVE(*welcome));
    tap.a.UpdateUVE(1000U);
    EXPECT_EQ(tap.a.StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(tap.b.StateUVE(), SessionStateUVE::Established);

    ASSERT_TRUE(tap.a.DisconnectUVE());
    const std::optional<std::vector<std::uint8_t>> bye = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(bye.has_value());
    const InspectedPacketUVE tappedBye = InspectPacketUVE(*bye, PacketDirectionUVE::Outbound,
                                                          1100U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tappedBye.sessionKind, "bye");
    EXPECT_EQ(tappedBye.summary, "bye");
    EXPECT_FALSE(tappedBye.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedBye), "> [wire] bye");
}

TEST(NetworkInspectorUVETest, Session_AnonymousHelloShowsNoToken) {
    SessionTapUVE tap = MakeSessionTapUVE();
    ASSERT_TRUE(tap.a.ConnectUVE(1000U));
    const std::optional<std::vector<std::uint8_t>> hello = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(hello.has_value());

    const InspectedPacketUVE tapped = InspectPacketUVE(*hello, PacketDirectionUVE::Outbound,
                                                       1000U, InspectedChannelUVE::Unknown);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, kAnonymousSessionTokenUVE);
    EXPECT_EQ(tapped.summary, "hello v2 anonymous");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorUVETest, Session_WrongVersionHelloIsFlagged) {
    // A version the session would answer with a Bye: the inspector names the protocol's own.
    const std::vector<std::uint8_t> hello{0x01U, 0x09U, 0x00U, 0x7BU, 0x00U, 0x00U,
                                          0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const InspectedPacketUVE tapped =
        InspectPacketUVE(hello, PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tapped.sessionKind, "hello");
    ASSERT_TRUE(tapped.helloVersion.has_value());
    EXPECT_EQ(*tapped.helloVersion, 9U);
    ASSERT_TRUE(tapped.helloToken.has_value());
    EXPECT_EQ(*tapped.helloToken, 123U);
    EXPECT_EQ(tapped.summary, "hello v9 token 0x000000000000007b (protocol wants v2)");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorUVETest, Session_ShortAndPaddedControlMessages) {
    // Not even a version: nothing decodes.
    for (const auto& shortHello :
         {std::vector<std::uint8_t>{0x01U}, std::vector<std::uint8_t>{0x01U, 0x02U}}) {
        const InspectedPacketUVE tapped = InspectPacketUVE(shortHello, PacketDirectionUVE::Inbound,
                                                           0U, InspectedChannelUVE::Unknown);
        EXPECT_EQ(tapped.sessionKind, "hello");
        EXPECT_FALSE(tapped.helloVersion.has_value());
        EXPECT_EQ(tapped.summary, "hello (truncated)");
        EXPECT_TRUE(tapped.malformed);
    }
    // A version but no token: decodes as far as the bytes go, and says so.
    const InspectedPacketUVE noToken =
        InspectPacketUVE({0x01U, 0x02U, 0x00U}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Unknown);
    EXPECT_EQ(noToken.sessionKind, "hello");
    ASSERT_TRUE(noToken.helloVersion.has_value());
    EXPECT_EQ(*noToken.helloVersion, 2U);
    EXPECT_FALSE(noToken.helloToken.has_value());
    EXPECT_EQ(noToken.summary, "hello v2 (truncated)");
    EXPECT_FALSE(noToken.malformed);
    // Trailing bytes on control messages: the session ignores them, the inspector notes them.
    const InspectedPacketUVE paddedWelcome =
        InspectPacketUVE({0x02U, 0xAAU, 0xBBU}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Unknown);
    EXPECT_EQ(paddedWelcome.sessionKind, "welcome");
    EXPECT_EQ(paddedWelcome.trailingBytes, 2U);
    EXPECT_EQ(paddedWelcome.summary, "welcome (+2 trailing bytes)");
    EXPECT_FALSE(paddedWelcome.malformed);
    const InspectedPacketUVE paddedBye = InspectPacketUVE({0x03U, 0xCCU},
                                                          PacketDirectionUVE::Outbound, 0U,
                                                          InspectedChannelUVE::Unknown);
    EXPECT_EQ(paddedBye.trailingBytes, 1U);
    EXPECT_EQ(paddedBye.summary, "bye (+1 trailing byte)");
    EXPECT_FALSE(paddedBye.malformed);
}

TEST(NetworkInspectorUVETest, Wire_EmptyAndForeignDatagramsAreMalformed) {
    const InspectedPacketUVE empty =
        InspectPacketUVE({}, PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(empty.sessionKind, "empty");
    EXPECT_EQ(empty.summary, "empty datagram");
    EXPECT_TRUE(empty.malformed);
    EXPECT_EQ(DescribePacketUVE(empty), "< [wire] empty datagram");

    for (const auto& foreign :
         {std::vector<std::uint8_t>{0x09U}, std::vector<std::uint8_t>{0xFFU, 0x01U, 0x02U}}) {
        const InspectedPacketUVE tapped = InspectPacketUVE(foreign, PacketDirectionUVE::Inbound,
                                                           0U, InspectedChannelUVE::Rpc);
        EXPECT_EQ(tapped.sessionKind, "foreign");
        EXPECT_TRUE(tapped.malformed);
    }
    const InspectedPacketUVE foreign =
        InspectPacketUVE({0x09U}, PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(foreign.summary, "foreign tag 0x09");
    EXPECT_EQ(DescribePacketUVE(foreign), "< [rpc] foreign tag 0x09");
}

TEST(NetworkInspectorUVETest, Replication_TappedSnapshotsDecodeEmptyAndFull) {
    ReplicationTapUVE tap = MakeReplicationTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    // Legacy framing on demand: every send is full, so the tap proves full snapshots are
    // still byte for byte what they always were. (Deltas get their own test below.)
    ASSERT_TRUE(tap.a.SetFullSnapshotPeriodUVE(1U));

    // No entities registered: the keepalive snapshot is empty, and still well-formed.
    tap.a.UpdateUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> keepalive = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(keepalive.has_value());
    const InspectedPacketUVE tappedEmpty = InspectPacketUVE(*keepalive, PacketDirectionUVE::Outbound,
                                                            1100U, InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedEmpty.sessionKind, "data");
    EXPECT_EQ(tappedEmpty.dataKind, "snapshot");
    ASSERT_TRUE(
        std::holds_alternative<ReplicationSnapshotViewUVE>(tappedEmpty.data));
    EXPECT_TRUE(
        std::get<ReplicationSnapshotViewUVE>(tappedEmpty.data).entries.empty());
    EXPECT_EQ(tappedEmpty.summary, "snapshot 0 entries");
    EXPECT_FALSE(tappedEmpty.malformed);

    ASSERT_TRUE(tap.a.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    ASSERT_TRUE(tap.a.RegisterEntityUVE(9U, [] { return Math::Vector3UVE{4.0F, 5.0F, 6.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    tap.a.UpdateUVE(1200U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacketUVE tapped = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound, 1200U,
                                                       InspectedChannelUVE::Replication);
    EXPECT_EQ(tapped.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotViewUVE>(tapped.data));
    const ReplicationSnapshotViewUVE& view = std::get<ReplicationSnapshotViewUVE>(tapped.data);
    EXPECT_EQ(view.entryCount, 2U);
    ASSERT_EQ(view.entries.size(), 2U);
    EXPECT_EQ(view.entries[0].first, 7U);
    EXPECT_EQ(view.entries[0].second, Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(view.entries[1].first, 9U);
    EXPECT_EQ(view.entries[1].second, Math::Vector3UVE(4.0F, 5.0F, 6.0F));
    EXPECT_EQ(tapped.summary, "snapshot 2 entries");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacketUVE(tapped), "> [replication] snapshot 2 entries");
}

TEST(NetworkInspectorUVETest, Replication_StrictFramingRejectsShortAndPadded) {
    // Declares two entries, carries one.
    const std::vector<std::uint8_t> shortPayload{0x04U, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                                 0x00U, 0x00U, 0x80U, 0x3FU, 0x00U, 0x00U, 0x00U,
                                                 0x40U, 0x00U, 0x00U, 0x40U, 0x40U};
    const InspectedPacketUVE shortPacket =
        InspectPacketUVE(shortPayload, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(shortPacket.dataKind, "malformed");
    EXPECT_TRUE(shortPacket.malformed);

    // One entry plus one trailing byte: snapshots end exactly.
    std::vector<std::uint8_t> padded{0x04U, 0x01U, 0x00U};
    padded.insert(padded.end(), shortPayload.begin() + 3, shortPayload.end());
    padded.push_back(0xFFU);
    const InspectedPacketUVE paddedPacket = InspectPacketUVE(padded, PacketDirectionUVE::Inbound,
                                                             0U, InspectedChannelUVE::Replication);
    EXPECT_EQ(paddedPacket.dataKind, "malformed");
    EXPECT_TRUE(paddedPacket.malformed);

    // A bare tag carries no count at all.
    const InspectedPacketUVE bare =
        InspectPacketUVE({0x04U}, PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Replication);
    EXPECT_EQ(bare.dataKind, "malformed");
    EXPECT_EQ(bare.summary, "malformed replication payload (0 bytes)");
    EXPECT_TRUE(bare.malformed);
}

TEST(NetworkInspectorUVETest, Rpc_TappedCallAndResponseDecodeAndStillComplete) {
    RpcTapUVE tap = MakeRpcTapUVE();
    ASSERT_TRUE(tap.b.RegisterMethodUVE(
        "add", [](const std::span<const UVScript::ValueUVE> args) {
            const std::int64_t x = std::get<std::int64_t>(args[0]);
            const std::int64_t y = std::get<std::int64_t>(args[1]);
            return RpcOkUVE(UVScript::ValueUVE{x + y});
        }));
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    const RpcCallIdUVE callId =
        tap.a.CallUVE("add", {UVScript::ValueUVE{std::int64_t{20}},
                              UVScript::ValueUVE{std::int64_t{22}}},
                      tap.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacketUVE tappedCall = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound,
                                                           1000U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(tappedCall.sessionKind, "data");
    EXPECT_EQ(tappedCall.dataKind, "call");
    ASSERT_TRUE(std::holds_alternative<RpcCallViewUVE>(tappedCall.data));
    const RpcCallViewUVE& call = std::get<RpcCallViewUVE>(tappedCall.data);
    EXPECT_EQ(call.callId, callId);
    EXPECT_EQ(call.method, "add");
    ASSERT_EQ(call.args.size(), 2U);
    EXPECT_EQ(call.args[0], UVScript::ValueUVE{std::int64_t{20}});
    EXPECT_EQ(call.args[1], UVScript::ValueUVE{std::int64_t{22}});
    EXPECT_EQ(tappedCall.summary,
              "call #" + std::to_string(callId) + " \"add\" (20, 22)");
    EXPECT_FALSE(tappedCall.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedCall),
              "> [rpc] call #" + std::to_string(callId) + " \"add\" (20, 22)");

    // Re-inject: the tapped call still runs and answers.
    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacketUVE tappedResponse = InspectPacketUVE(*answer, PacketDirectionUVE::Inbound,
                                                               1000U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(tappedResponse.dataKind, "response");
    ASSERT_TRUE(std::holds_alternative<RpcResponseViewUVE>(tappedResponse.data));
    const RpcResponseViewUVE& response = std::get<RpcResponseViewUVE>(tappedResponse.data);
    EXPECT_EQ(response.callId, callId);
    EXPECT_TRUE(response.ok);
    EXPECT_EQ(response.value, UVScript::ValueUVE{std::int64_t{42}});
    EXPECT_TRUE(response.error.empty());
    EXPECT_EQ(tappedResponse.summary, "response #" + std::to_string(callId) + " ok 42");
    EXPECT_FALSE(tappedResponse.malformed);

    ASSERT_TRUE(tap.link.b.SendUVE(*answer));
    tap.a.UpdateUVE(1000U);
    ASSERT_EQ(tap.doneA.count(callId), 1U);
    EXPECT_TRUE(tap.doneA.at(callId).ok);
    EXPECT_EQ(tap.doneA.at(callId).value, UVScript::ValueUVE{std::int64_t{42}});
}

TEST(NetworkInspectorUVETest, Rpc_TappedCollectionsDecodeBranchForBranch) {
    RpcTapUVE tap = MakeRpcTapUVE();
    ASSERT_TRUE(tap.b.RegisterMethodUVE(
        "echo", [](const std::span<const UVScript::ValueUVE> args) { return RpcOkUVE(args[0]); }));
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    const UVScript::ValueUVE list = UVScript::MakeListValueUVE(
        {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{"two"}, UVScript::ValueUVE{2.5}});
    const UVScript::ValueUVE tuple =
        UVScript::MakeTupleValueUVE({UVScript::ValueUVE{std::int64_t{1}}});
    const UVScript::ValueUVE map = UVScript::MakeMapValueUVE(
        {UVScript::ValueUVE{"a"}, UVScript::ValueUVE{std::int64_t{1}}});
    const RpcCallIdUVE callId = tap.a.CallUVE("echo", {list, tuple, map}, tap.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacketUVE tappedCall = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound,
                                                           1000U, InspectedChannelUVE::Rpc);
    ASSERT_TRUE(std::holds_alternative<RpcCallViewUVE>(tappedCall.data));
    const RpcCallViewUVE& call = std::get<RpcCallViewUVE>(tappedCall.data);
    ASSERT_EQ(call.args.size(), 3U);
    EXPECT_TRUE(ValuesEqualUVE(call.args[0], list));
    EXPECT_TRUE(ValuesEqualUVE(call.args[1], tuple));
    EXPECT_TRUE(ValuesEqualUVE(call.args[2], map));
    EXPECT_EQ(tappedCall.summary, "call #" + std::to_string(callId) +
                                      " \"echo\" ([1, two, 2.5], (1,), {a: 1})");
    EXPECT_FALSE(tappedCall.malformed);

    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacketUVE tappedResponse = InspectPacketUVE(*answer, PacketDirectionUVE::Inbound,
                                                               1000U, InspectedChannelUVE::Rpc);
    ASSERT_TRUE(std::holds_alternative<RpcResponseViewUVE>(tappedResponse.data));
    const RpcResponseViewUVE& response = std::get<RpcResponseViewUVE>(tappedResponse.data);
    EXPECT_TRUE(response.ok);
    EXPECT_TRUE(ValuesEqualUVE(response.value, list));
    EXPECT_EQ(tappedResponse.summary,
              "response #" + std::to_string(callId) + " ok [1, two, 2.5]");
}

TEST(NetworkInspectorUVETest, Rpc_TappedErrorResponseDecodes) {
    RpcTapUVE tap = MakeRpcTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    const RpcCallIdUVE callId = tap.a.CallUVE("nope", {}, tap.FileAUVE());
    ASSERT_NE(callId, kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacketUVE tappedCall = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound,
                                                           1000U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(tappedCall.summary, "call #" + std::to_string(callId) + " \"nope\" ()");

    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacketUVE tappedResponse = InspectPacketUVE(*answer, PacketDirectionUVE::Inbound,
                                                               1000U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(tappedResponse.dataKind, "response");
    ASSERT_TRUE(std::holds_alternative<RpcResponseViewUVE>(tappedResponse.data));
    const RpcResponseViewUVE& response = std::get<RpcResponseViewUVE>(tappedResponse.data);
    EXPECT_EQ(response.callId, callId);
    EXPECT_FALSE(response.ok);
    EXPECT_EQ(response.error, "unknown method 'nope'");
    EXPECT_EQ(tappedResponse.summary,
              "response #" + std::to_string(callId) + " error \"unknown method 'nope'\"");
    EXPECT_FALSE(tappedResponse.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedResponse), "< [rpc] response #" + std::to_string(callId) +
                                                    " error \"unknown method 'nope'\"");
}

TEST(NetworkInspectorUVETest, Rpc_MutatedFramesAreMalformed) {
    RpcTapUVE tap = MakeRpcTapUVE();
    ASSERT_TRUE(tap.b.RegisterMethodUVE(
        "echo", [](const std::span<const UVScript::ValueUVE> args) { return RpcOkUVE(args[0]); }));
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    ASSERT_NE(tap.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, tap.FileAUVE()),
              kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
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
        const InspectedPacketUVE tapped = InspectPacketUVE(mutant, PacketDirectionUVE::Inbound,
                                                           0U, InspectedChannelUVE::Rpc);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    EXPECT_EQ(InspectPacketUVE(truncated, PacketDirectionUVE::Inbound, 0U,
                               InspectedChannelUVE::Rpc)
                  .summary,
              "malformed rpc payload (5 bytes)");

    // A live ok response with an impossible status, and a hand-built error whose payload is
    // not a string: the RPC peer rejects both, so does the inspector.
    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.b.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    std::vector<std::uint8_t> badStatus = *answer;
    badStatus[9U] = 0x02U;
    EXPECT_TRUE(InspectPacketUVE(badStatus, PacketDirectionUVE::Inbound, 0U,
                                 InspectedChannelUVE::Rpc)
                    .malformed);
    const std::vector<std::uint8_t> intError{0x04U, 0x52U, 0x43U, 0x01U, 0x02U, 0x01U, 0x00U,
                                             0x00U, 0x00U, 0x01U, 0x02U, 0x00U, 0x00U, 0x00U,
                                             0x00U, 0x00U, 0x00U, 0x00U, 0x00U};
    const InspectedPacketUVE tappedIntError =
        InspectPacketUVE(intError, PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Rpc);
    EXPECT_EQ(tappedIntError.dataKind, "malformed");
    EXPECT_TRUE(tappedIntError.malformed);
}

TEST(NetworkInspectorUVETest, Prediction_TappedInputsAndStateDecode) {
    PredictionTapUVE tap = MakePredictionTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    PredictedInputUVE input;
    input.move = Math::Vector3UVE{0.5F, -1.25F, 2.0F};
    input.rise = 0.75F;
    input.jumpPressed = true;
    ASSERT_EQ(tap.client.TickUVE(input), 1U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacketUVE tappedInputs = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound,
                                                             1000U, InspectedChannelUVE::Prediction);
    EXPECT_EQ(tappedInputs.sessionKind, "data");
    EXPECT_EQ(tappedInputs.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsViewUVE>(tappedInputs.data));
    const PredictionInputsViewUVE& inputs = std::get<PredictionInputsViewUVE>(tappedInputs.data);
    ASSERT_EQ(inputs.window.size(), 1U);
    EXPECT_EQ(inputs.window[0].tick, 1U);
    EXPECT_EQ(inputs.window[0].pawn, 0U);
    EXPECT_EQ(inputs.window[0].dtSeconds, 1.0F / 60.0F);
    EXPECT_EQ(inputs.window[0].input, input);
    EXPECT_EQ(tappedInputs.summary, "inputs 1 tick 1");
    EXPECT_FALSE(tappedInputs.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedInputs), "> [prediction] inputs 1 tick 1");

    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.server.UpdateUVE(1000U);
    ASSERT_EQ(tap.server.StatsUVE().statesSent, 1U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    const InspectedPacketUVE tappedState = InspectPacketUVE(*answer, PacketDirectionUVE::Inbound,
                                                            1000U, InspectedChannelUVE::Prediction);
    EXPECT_EQ(tappedState.dataKind, "state");
    ASSERT_TRUE(std::holds_alternative<PredictionStateViewUVE>(tappedState.data));
    const PredictionStateViewUVE& state = std::get<PredictionStateViewUVE>(tappedState.data);
    EXPECT_EQ(state.tick, 1U);
    EXPECT_EQ(state.pawn, 0U);
    EXPECT_EQ(state.state, CarryStepperUVE()(PredictedStateUVE{}, input, 1.0F / 60.0F));
    // Floats render through the script value printer; the shape is the inspector's promise.
    EXPECT_EQ(tappedState.summary.rfind("state tick 1 (", 0U), 0U);
    EXPECT_FALSE(tappedState.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedState).rfind("< [prediction] state tick 1 (", 0U), 0U);
}

TEST(NetworkInspectorUVETest, Prediction_TappedWindowDecodesOldestFirst) {
    PredictionTapUVE tap = MakePredictionTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    // Three ticks with no answers: each message repeats the unacked window, so the third
    // carries ticks one through three.
    PredictedInputUVE first;
    first.move = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    PredictedInputUVE second = first;
    second.jumpPressed = true;
    PredictedInputUVE third = first;
    third.rise = 1.0F;
    ASSERT_EQ(tap.client.TickUVE(first), 1U);
    ASSERT_EQ(tap.client.TickUVE(second), 2U);
    ASSERT_EQ(tap.client.TickUVE(third), 3U);
    ASSERT_TRUE(tap.link.b.TryReceiveUVE().has_value());
    ASSERT_TRUE(tap.link.b.TryReceiveUVE().has_value());
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacketUVE tapped = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound, 1000U,
                                                       InspectedChannelUVE::Prediction);
    EXPECT_EQ(tapped.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsViewUVE>(tapped.data));
    const PredictionInputsViewUVE& inputs = std::get<PredictionInputsViewUVE>(tapped.data);
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

TEST(NetworkInspectorUVETest, Prediction_MutatedMessagesAreMalformed) {
    PredictionTapUVE tap = MakePredictionTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    PredictedInputUVE input;
    input.move = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    ASSERT_EQ(tap.client.TickUVE(input), 1U);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
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
        const InspectedPacketUVE tapped =
            InspectPacketUVE(mutant, PacketDirectionUVE::Inbound, 0U,
                             InspectedChannelUVE::Prediction);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }

    // A live state message missing its last byte: states are exactly 39 bytes.
    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.server.UpdateUVE(1000U);
    const std::optional<std::vector<std::uint8_t>> answer = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(answer.has_value());
    ASSERT_EQ(answer->size(), 1U + 39U);
    const std::vector<std::uint8_t> shortState(answer->begin(), answer->end() - 1);
    const InspectedPacketUVE tappedShort =
        InspectPacketUVE(shortState, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Prediction);
    EXPECT_EQ(tappedShort.dataKind, "malformed");
    EXPECT_EQ(tappedShort.summary, "malformed prediction payload (38 bytes)");
    EXPECT_TRUE(tappedShort.malformed);
}

TEST(NetworkInspectorUVETest, UnknownChannel_PayloadStaysOpaque) {
    // Junk bytes: counted, never guessed at, never flagged.
    const InspectedPacketUVE junk =
        InspectPacketUVE({0x04U, 0x6AU, 0x75U, 0x6EU, 0x6BU}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Unknown);
    EXPECT_EQ(junk.sessionKind, "data");
    EXPECT_EQ(junk.dataKind, "opaque");
    EXPECT_TRUE(std::holds_alternative<std::monostate>(junk.data));
    EXPECT_EQ(junk.summary, "4 bytes (opaque)");
    EXPECT_FALSE(junk.malformed);
    EXPECT_EQ(DescribePacketUVE(junk), "< [wire] 4 bytes (opaque)");

    // Even a perfectly good call frame stays opaque when the channel is unknown.
    RpcTapUVE tap = MakeRpcTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    ASSERT_NE(tap.a.CallUVE("echo", {UVScript::ValueUVE{std::int64_t{1}}}, tap.FileAUVE()),
              kInvalidRpcCallIdUVE);
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());
    const InspectedPacketUVE tapped =
        InspectPacketUVE(*wire, PacketDirectionUVE::Outbound, 1000U, InspectedChannelUVE::Unknown);
    EXPECT_EQ(tapped.dataKind, "opaque");
    EXPECT_EQ(tapped.summary, std::to_string(wire->size() - 1U) + " bytes (opaque)");
    EXPECT_FALSE(tapped.malformed);
}

TEST(NetworkInspectorUVETest, Replication_TappedDeltaDecodesBesideFulls) {
    ReplicationTapUVE tap = MakeReplicationTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    // Every second send is full: the tap collects a delta, a full, and an empty delta live.
    ASSERT_TRUE(tap.a.SetFullSnapshotPeriodUVE(2U));
    ASSERT_TRUE(tap.a.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3UVE&) {}));

    tap.a.UpdateUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> delta = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(delta.has_value());
    const InspectedPacketUVE tappedDelta = InspectPacketUVE(*delta, PacketDirectionUVE::Outbound,
                                                            1100U, InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedDelta.dataKind, "delta");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotViewUVE>(tappedDelta.data));
    const ReplicationSnapshotViewUVE& deltaView = std::get<ReplicationSnapshotViewUVE>(tappedDelta.data);
    EXPECT_TRUE(deltaView.isDelta);
    EXPECT_EQ(deltaView.entryCount, 1U);
    ASSERT_EQ(deltaView.entries.size(), 1U);
    EXPECT_EQ(deltaView.entries[0].first, 7U);
    EXPECT_EQ(deltaView.entries[0].second, Math::Vector3UVE(1.0F, 2.0F, 3.0F));
    EXPECT_EQ(tappedDelta.summary, "delta 1 entry");
    EXPECT_FALSE(tappedDelta.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedDelta), "> [replication] delta 1 entry");

    tap.a.UpdateUVE(1200U);
    const std::optional<std::vector<std::uint8_t>> full = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(full.has_value());
    const InspectedPacketUVE tappedFull = InspectPacketUVE(*full, PacketDirectionUVE::Outbound,
                                                           1200U, InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedFull.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotViewUVE>(tappedFull.data));
    EXPECT_FALSE(std::get<ReplicationSnapshotViewUVE>(tappedFull.data).isDelta);
    EXPECT_EQ(tappedFull.summary, "snapshot 1 entry");

    tap.a.UpdateUVE(1300U);
    const std::optional<std::vector<std::uint8_t>> quiet = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(quiet.has_value());
    const InspectedPacketUVE tappedQuiet = InspectPacketUVE(*quiet, PacketDirectionUVE::Outbound,
                                                            1300U, InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedQuiet.dataKind, "delta");
    EXPECT_EQ(tappedQuiet.summary, "delta 0 entries");
    EXPECT_FALSE(tappedQuiet.malformed);
}

TEST(NetworkInspectorUVETest, Replication_MalformedDeltaFramesRejected) {
    // Marker with no count, a declared entry that never arrives, and trailing bytes on an
    // empty delta: the strict end rule holds for deltas exactly like fulls.
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU, 0x01U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFFU, 0xFFU, 0x00U, 0x00U, 0xFFU}}) {
        const InspectedPacketUVE tapped = InspectPacketUVE(mutant, PacketDirectionUVE::Inbound,
                                                           0U, InspectedChannelUVE::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
}

TEST(NetworkInspectorUVETest, Replication_TappedOwnershipDecodes) {
    ReplicationTapUVE tap = MakeReplicationTapUVE();
    ASSERT_TRUE(tap.a.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 0.0F, 0.0F}; },
                                        [](const Math::Vector3UVE&) {},
                                        ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(tap.b.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{}; },
                                        [](const Math::Vector3UVE&) {},
                                        ReplicationAuthorityUVE::Proxy));
    tap.a.SetOwnershipTransferHandlerUVE(
        [&tap](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(tap.a.AcceptTransferUVE(netId));
            }
        });
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    ASSERT_TRUE(tap.b.RequestOwnershipUVE(7U, 1000U));

    // Watching A: the request arrives inbound.
    const std::optional<std::vector<std::uint8_t>> request = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(request.has_value());
    const InspectedPacketUVE tappedRequest =
        InspectPacketUVE(*request, PacketDirectionUVE::Inbound, 1000U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedRequest.dataKind, "ownership");
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferViewUVE>(tappedRequest.data));
    const OwnershipTransferViewUVE& requestView =
        std::get<OwnershipTransferViewUVE>(tappedRequest.data);
    EXPECT_EQ(requestView.kind, OwnershipMessageKindUVE::Request);
    EXPECT_EQ(requestView.netId, 7U);
    EXPECT_EQ(tappedRequest.summary, "ownership request 7");
    EXPECT_FALSE(tappedRequest.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedRequest), "< [replication] ownership request 7");

    ASSERT_TRUE(tap.link.b.SendUVE(*request));
    tap.a.UpdateUVE(1100U);
    const std::optional<std::vector<std::uint8_t>> accept = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(accept.has_value());
    const InspectedPacketUVE tappedAccept =
        InspectPacketUVE(*accept, PacketDirectionUVE::Outbound, 1100U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedAccept.dataKind, "ownership");
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferViewUVE>(tappedAccept.data));
    const OwnershipTransferViewUVE& acceptView =
        std::get<OwnershipTransferViewUVE>(tappedAccept.data);
    EXPECT_EQ(acceptView.kind, OwnershipMessageKindUVE::Accept);
    EXPECT_EQ(acceptView.netId, 7U);
    EXPECT_EQ(tappedAccept.summary, "ownership accept 7");
    EXPECT_FALSE(tappedAccept.malformed);
    EXPECT_EQ(DescribePacketUVE(tappedAccept), "> [replication] ownership accept 7");
}

TEST(NetworkInspectorUVETest, Replication_MalformedOwnershipRejected) {
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x04U, 0x07U, 0x00U, 0x00U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x01U, 0x07U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFEU, 0xFFU, 0x01U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0xFFU}}) {
        const InspectedPacketUVE tapped = InspectPacketUVE(mutant, PacketDirectionUVE::Inbound,
                                                           0U, InspectedChannelUVE::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    // The third verb decodes too.
    const InspectedPacketUVE deny =
        InspectPacketUVE({0x04U, 0xFEU, 0xFFU, 0x03U, 0x07U, 0x00U, 0x00U, 0x00U},
                         PacketDirectionUVE::Inbound, 0U, InspectedChannelUVE::Replication);
    ASSERT_TRUE(std::holds_alternative<OwnershipTransferViewUVE>(deny.data));
    EXPECT_EQ(std::get<OwnershipTransferViewUVE>(deny.data).kind, OwnershipMessageKindUVE::Deny);
    EXPECT_EQ(deny.summary, "ownership deny 7");
    EXPECT_FALSE(deny.malformed);
}

TEST(NetworkInspectorUVETest, Replication_TappedPropertiesDecode) {
    ReplicationTapUVE tap = MakeReplicationTapUVE();
    ASSERT_TRUE(tap.a.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 0.0F, 0.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    ASSERT_TRUE(tap.b.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{}; },
                                        [](const Math::Vector3UVE&) {}));
    UVScript::ValueUVE applied{std::int64_t{0}};
    int applyCalls = 0;
    ASSERT_TRUE(tap.b.RegisterPropertyUVE(
        7U, 1U, [&applied, &applyCalls](const UVScript::ValueUVE& value) {
            applied = value;
            ++applyCalls;
        }));
    UVScript::ValueUVE appliedList{std::int64_t{0}};
    ASSERT_TRUE(tap.b.RegisterPropertyUVE(
        7U, 2U, [&appliedList](const UVScript::ValueUVE& value) { appliedList = value; }));
    ASSERT_TRUE(tap.EstablishUVE(1000U));
    const UVScript::ValueUVE health{std::int64_t{83}};
    const UVScript::ValueUVE tags = UVScript::MakeListValueUVE(
        {UVScript::ValueUVE{std::int64_t{1}}, UVScript::ValueUVE{std::string{"two"}}});
    ASSERT_TRUE(tap.a.SetPropertyUVE(7U, 1U, health));
    ASSERT_TRUE(tap.a.SetPropertyUVE(7U, 2U, tags));
    tap.a.UpdateUVE(1100U);

    // Watching B: A's snapshot arrives first, the property frame second.
    const std::optional<std::vector<std::uint8_t>> snapshot = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(snapshot.has_value());
    const InspectedPacketUVE tappedSnapshot =
        InspectPacketUVE(*snapshot, PacketDirectionUVE::Outbound, 1100U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(tappedSnapshot.dataKind, "delta");
    const std::optional<std::vector<std::uint8_t>> stolen = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(stolen.has_value());
    const InspectedPacketUVE tapped =
        InspectPacketUVE(*stolen, PacketDirectionUVE::Outbound, 1100U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(tapped.dataKind, "properties");
    ASSERT_TRUE(std::holds_alternative<ReplicationPropertiesViewUVE>(tapped.data));
    const ReplicationPropertiesViewUVE& view =
        std::get<ReplicationPropertiesViewUVE>(tapped.data);
    EXPECT_EQ(view.propertyCount, 2U);
    ASSERT_EQ(view.entries.size(), 2U);
    EXPECT_EQ(view.entries[0].netId, 7U);
    EXPECT_EQ(view.entries[0].propId, 1U);
    EXPECT_TRUE(ValuesEqualUVE(view.entries[0].value, health));
    EXPECT_EQ(view.entries[1].netId, 7U);
    EXPECT_EQ(view.entries[1].propId, 2U);
    EXPECT_TRUE(ValuesEqualUVE(view.entries[1].value, tags));
    EXPECT_EQ(tapped.summary, "properties 2 entries");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacketUVE(tapped), "> [replication] properties 2 entries");

    // Re-injected in order, the tapped traffic still completes.
    ASSERT_TRUE(tap.link.a.SendUVE(*snapshot));
    ASSERT_TRUE(tap.link.a.SendUVE(*stolen));
    tap.b.UpdateUVE(1100U);
    EXPECT_EQ(applyCalls, 1);
    EXPECT_TRUE(ValuesEqualUVE(applied, health));
    EXPECT_TRUE(ValuesEqualUVE(appliedList, tags));
    EXPECT_EQ(tap.b.StatsUVE().propertiesApplied, 2U);
}

TEST(NetworkInspectorUVETest, Replication_MalformedPropertiesRejected) {
    for (const auto& mutant :
         {std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x09U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x00U},
          std::vector<std::uint8_t>{0x04U, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U,
                                    0x03U, 0x00U, 0xFFU}}) {
        const InspectedPacketUVE tapped =
            InspectPacketUVE(mutant, PacketDirectionUVE::Inbound, 0U,
                             InspectedChannelUVE::Replication);
        EXPECT_EQ(tapped.dataKind, "malformed");
        EXPECT_TRUE(tapped.malformed);
    }
    // A count of zero decodes: the sender never emits it, but the shape is strict either way.
    const InspectedPacketUVE empty =
        InspectPacketUVE({0x04U, 0xFDU, 0xFFU, 0x00U, 0x00U}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Replication);
    EXPECT_EQ(empty.dataKind, "properties");
    ASSERT_TRUE(std::holds_alternative<ReplicationPropertiesViewUVE>(empty.data));
    EXPECT_EQ(std::get<ReplicationPropertiesViewUVE>(empty.data).propertyCount, 0U);
    EXPECT_TRUE(std::get<ReplicationPropertiesViewUVE>(empty.data).entries.empty());
    EXPECT_EQ(empty.summary, "properties 0 entries");
    EXPECT_FALSE(empty.malformed);
}

TEST(NetworkInspectorUVETest, Replication_TappedMultiplexedSnapshotDecodes) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    ASSERT_TRUE(sessionA.EnableMultiplexingUVE());
    ASSERT_TRUE(sessionB.EnableMultiplexingUVE());
    NetworkReplicatorUVE replA(sessionA, 5U);
    NetworkReplicatorUVE replB(sessionB, 5U);
    Math::Vector3UVE positionB{};
    ASSERT_TRUE(replA.RegisterEntityUVE(7U, [] { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; },
                                        [](const Math::Vector3UVE&) {}));
    ASSERT_TRUE(replB.RegisterEntityUVE(7U, [&positionB] { return positionB; },
                                        [&positionB](const Math::Vector3UVE& position) {
                                            positionB = position;
                                        }));
    ASSERT_TRUE(sessionA.ConnectUVE(1000U));
    sessionA.UpdateUVE(1000U);
    sessionB.UpdateUVE(1000U);
    sessionA.UpdateUVE(1000U);
    sessionB.UpdateUVE(1000U);
    ASSERT_EQ(sessionA.StateUVE(), SessionStateUVE::Established);
    replA.UpdateUVE(1100U);

    const std::optional<std::vector<std::uint8_t>> stolen = pair.b.TryReceiveUVE();
    ASSERT_TRUE(stolen.has_value());
    const InspectedPacketUVE tapped =
        InspectPacketUVE(*stolen, PacketDirectionUVE::Outbound, 1100U,
                         InspectedChannelUVE::Replication, true);
    ASSERT_TRUE(tapped.channelId.has_value());
    EXPECT_EQ(*tapped.channelId, 5U);
    EXPECT_EQ(tapped.dataKind, "snapshot");
    ASSERT_TRUE(std::holds_alternative<ReplicationSnapshotViewUVE>(tapped.data));
    const ReplicationSnapshotViewUVE& view =
        std::get<ReplicationSnapshotViewUVE>(tapped.data);
    EXPECT_EQ(view.entryCount, 1U);
    ASSERT_EQ(view.entries.size(), 1U);
    EXPECT_EQ(view.entries[0].first, 7U);
    EXPECT_EQ(view.entries[0].second, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(tapped.summary, "snapshot 1 entry");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacketUVE(tapped), "> [replication ch5] snapshot 1 entry");

    ASSERT_TRUE(pair.a.SendUVE(*stolen));
    replB.UpdateUVE(1100U);
    EXPECT_EQ(positionB, (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
}

TEST(NetworkInspectorUVETest, UnknownChannel_MultiplexedPayloadStaysOpaque) {
    const InspectedPacketUVE tapped =
        InspectPacketUVE({0x04U, 0x07U, 0xAAU, 0xBBU, 0xCCU}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Unknown, true);
    ASSERT_TRUE(tapped.channelId.has_value());
    EXPECT_EQ(*tapped.channelId, 7U);
    EXPECT_EQ(tapped.dataKind, "opaque");
    EXPECT_EQ(tapped.summary, "3 bytes (opaque)");
    EXPECT_FALSE(tapped.malformed);
    EXPECT_EQ(DescribePacketUVE(tapped), "< [wire ch7] 3 bytes (opaque)");
}

TEST(NetworkInspectorUVETest, Multiplex_BareTagAndMistakenReadsAreMalformed) {
    const InspectedPacketUVE bare =
        InspectPacketUVE({0x04U}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Replication, true);
    EXPECT_EQ(bare.dataKind, "malformed");
    EXPECT_TRUE(bare.malformed);
    EXPECT_FALSE(bare.channelId.has_value());

    // A legacy empty snapshot read as multiplexed: the count's first byte becomes channel
    // id 0, and the one byte left cannot frame anything.
    const InspectedPacketUVE mistaken =
        InspectPacketUVE({0x04U, 0x00U, 0x00U}, PacketDirectionUVE::Inbound, 0U,
                         InspectedChannelUVE::Replication, true);
    ASSERT_TRUE(mistaken.channelId.has_value());
    EXPECT_EQ(*mistaken.channelId, 0U);
    EXPECT_EQ(mistaken.dataKind, "malformed");
    EXPECT_TRUE(mistaken.malformed);
}


TEST(NetworkInspectorUVETest, Prediction_MultiPawnWindowAndStateNamePawns) {
    PredictionTapUVE tap = MakePredictionTapUVE();
    ASSERT_TRUE(tap.EstablishUVE(1000U));

    // Two pawns tick - pawn 1 at an explicit half rate - and the second message carries
    // both streams' oldest unacked inputs.
    PredictedInputUVE primary;
    primary.move = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    PredictedInputUVE sibling;
    sibling.move = Math::Vector3UVE{0.0F, 0.0F, 2.0F};
    ASSERT_EQ(tap.client.TickUVE(0U, primary), 1U);
    ASSERT_EQ(tap.client.TickUVE(1U, sibling, 1.0F / 30.0F), 1U);
    ASSERT_TRUE(tap.link.b.TryReceiveUVE().has_value());
    const std::optional<std::vector<std::uint8_t>> wire = tap.link.b.TryReceiveUVE();
    ASSERT_TRUE(wire.has_value());

    const InspectedPacketUVE tapped = InspectPacketUVE(*wire, PacketDirectionUVE::Outbound,
                                                       1000U, InspectedChannelUVE::Prediction);
    EXPECT_EQ(tapped.dataKind, "inputs");
    ASSERT_TRUE(std::holds_alternative<PredictionInputsViewUVE>(tapped.data));
    const PredictionInputsViewUVE& inputs = std::get<PredictionInputsViewUVE>(tapped.data);
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
    ASSERT_TRUE(tap.link.a.SendUVE(*wire));
    tap.server.UpdateUVE(1000U);
    ASSERT_EQ(tap.server.StatsUVE().inputsApplied, 2U);
    const std::optional<std::vector<std::uint8_t>> first = tap.link.a.TryReceiveUVE();
    const std::optional<std::vector<std::uint8_t>> second = tap.link.a.TryReceiveUVE();
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(second.has_value());
    const InspectedPacketUVE tappedPrimary =
        InspectPacketUVE(*first, PacketDirectionUVE::Inbound, 1000U,
                         InspectedChannelUVE::Prediction);
    ASSERT_TRUE(std::holds_alternative<PredictionStateViewUVE>(tappedPrimary.data));
    EXPECT_EQ(std::get<PredictionStateViewUVE>(tappedPrimary.data).pawn, 0U);
    EXPECT_EQ(tappedPrimary.summary.rfind("state tick 1 (", 0U), 0U);
    const InspectedPacketUVE tappedSibling =
        InspectPacketUVE(*second, PacketDirectionUVE::Inbound, 1000U,
                         InspectedChannelUVE::Prediction);
    EXPECT_EQ(tappedSibling.dataKind, "state");
    ASSERT_TRUE(std::holds_alternative<PredictionStateViewUVE>(tappedSibling.data));
    const PredictionStateViewUVE& state = std::get<PredictionStateViewUVE>(tappedSibling.data);
    EXPECT_EQ(state.tick, 1U);
    EXPECT_EQ(state.pawn, 1U);
    EXPECT_EQ(tappedSibling.summary.rfind("state pawn 1 tick 1 (", 0U), 0U);
    EXPECT_FALSE(tappedSibling.malformed);
}

} // namespace
} // namespace UVE::Network::Tests
