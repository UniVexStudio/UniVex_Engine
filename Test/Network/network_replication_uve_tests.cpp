// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/network/network_replication_uve.h"

#include "uve/uvscript/uvscript_value_uve.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

struct ReplicationHarnessUVE final {
    LoopbackTransportPairUVE link;
    NetworkReplicatorUVE a;
    NetworkReplicatorUVE b;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsA;
    std::map<NetworkEntityIdUVE, Math::Vector3UVE> positionsB;

    void UpdateBothUVE(const std::uint64_t nowMs) {
        a.UpdateUVE(nowMs);
        b.UpdateUVE(nowMs);
    }

    [[nodiscard]] bool EstablishUVE(const std::uint64_t nowMs) {
        if (!a.SessionUVE().ConnectUVE(nowMs)) {
            return false;
        }
        UpdateBothUVE(nowMs);
        UpdateBothUVE(nowMs);
        return a.SessionUVE().StateUVE() == SessionStateUVE::Established &&
               b.SessionUVE().StateUVE() == SessionStateUVE::Established;
    }

    void RegisterMirroredUVE(const NetworkEntityIdUVE netId) {
        static_cast<void>(a.RegisterEntityUVE(
            netId, [this, netId]() { return positionsA[netId]; },
            [this, netId](const Math::Vector3UVE& position) { positionsA[netId] = position; }));
        static_cast<void>(b.RegisterEntityUVE(
            netId, [this, netId]() { return positionsB[netId]; },
            [this, netId](const Math::Vector3UVE& position) { positionsB[netId] = position; }));
    }
};

[[nodiscard]] ReplicationHarnessUVE MakeReplicatorsUVE(
    const std::uint64_t sendIntervalMs = kDefaultReplicationIntervalMsUVE) {
    LoopbackTransportPairUVE pair = CreateLoopbackTransportPairUVE(256U);
    NetworkSessionUVE sessionA(SessionRoleUVE::Client,
                               std::make_unique<LoopbackEndpointUVE>(pair.a));
    NetworkSessionUVE sessionB(SessionRoleUVE::Server,
                               std::make_unique<LoopbackEndpointUVE>(pair.b));
    NetworkReplicatorUVE a(std::move(sessionA), sendIntervalMs);
    NetworkReplicatorUVE b(std::move(sessionB), sendIntervalMs);
    return ReplicationHarnessUVE{
        std::move(pair), std::move(a), std::move(b), {}, {}};
}

struct TransferEventUVE final {
    NetworkEntityIdUVE netId = 0U;
    OwnershipTransferEventUVE event = OwnershipTransferEventUVE::Requested;

    [[nodiscard]] bool operator==(const TransferEventUVE&) const = default;
};

// One entity owned by A and proxied by B: the handoff's starting shape.
[[nodiscard]] bool RegisterOwnerProxyUVE(ReplicationHarnessUVE& harness,
                                         const NetworkEntityIdUVE netId) {
    return harness.a.RegisterEntityUVE(
               netId, [&harness, netId]() { return harness.positionsA[netId]; },
               [&harness, netId](const Math::Vector3UVE& position) {
                   harness.positionsA[netId] = position;
               },
               ReplicationAuthorityUVE::Owner) &&
           harness.b.RegisterEntityUVE(
               netId, [&harness, netId]() { return harness.positionsB[netId]; },
               [&harness, netId](const Math::Vector3UVE& position) {
                   harness.positionsB[netId] = position;
               },
               ReplicationAuthorityUVE::Proxy);
}

TEST(NetworkReplicationUVETest, RegisterRules_DuplicatesAndEmptyCallbacksRejected) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    const PositionSamplerUVE sampler = []() { return Math::Vector3UVE{}; };
    const PositionApplierUVE applier = [](const Math::Vector3UVE&) {};

    EXPECT_TRUE(harness.a.RegisterEntityUVE(7U, sampler, applier));
    EXPECT_FALSE(harness.a.RegisterEntityUVE(7U, sampler, applier));
    EXPECT_FALSE(harness.a.RegisterEntityUVE(8U, PositionSamplerUVE{}, applier));
    EXPECT_FALSE(harness.a.RegisterEntityUVE(8U, sampler, PositionApplierUVE{}));
    EXPECT_FALSE(harness.a.UnregisterEntityUVE(9U));
    EXPECT_TRUE(harness.a.UnregisterEntityUVE(7U));
    EXPECT_TRUE(harness.a.RegisterEntityUVE(7U, sampler, applier));
}

TEST(NetworkReplicationUVETest, Snapshot_DivergentValuesConvergeOnFirstSender) {
    // Shared authority by default: whoever's snapshot lands first wins, and the loser adopts it
    // before its own send goes out. The server establishes (and sends) first, so divergent
    // values deterministically converge on the server's: B-to-A delivery, proven by A holding a
    // value it never sampled. (A-to-B is proven directionally by Rate_SendsOnlyWhenDue.)
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
}

TEST(NetworkReplicationUVETest, Rate_SendsOnlyWhenDue) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.a.StatsUVE().snapshotsSent, 1U);

    harness.positionsA[7U] = Math::Vector3UVE{5.0F, 5.0F, 5.0F};
    harness.UpdateBothUVE(1050U);
    harness.UpdateBothUVE(1099U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 1U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));

    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 2U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{5.0F, 5.0F, 5.0F}));
}

TEST(NetworkReplicationUVETest, IntervalZero_SendsEveryUpdate) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE(0U);
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const std::uint64_t sentAfterHandshake = harness.a.StatsUVE().snapshotsSent;

    harness.UpdateBothUVE(1001U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, sentAfterHandshake + 1U);
    harness.UpdateBothUVE(1002U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, sentAfterHandshake + 2U);
}

TEST(NetworkReplicationUVETest, UnknownNetIds_IgnoredAndCounted) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    const PositionSamplerUVE sampler = []() { return Math::Vector3UVE{1.0F, 2.0F, 3.0F}; };
    const PositionApplierUVE applier = [](const Math::Vector3UVE&) {};
    ASSERT_TRUE(harness.a.RegisterEntityUVE(7U, sampler, applier));

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 0U);
    EXPECT_EQ(harness.b.StatsUVE().entriesIgnoredUnknown, 1U);
}

TEST(NetworkReplicationUVETest, MalformedSnapshot_IgnoredWhole) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    harness.positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTagUVE::Data);
    // Declares two entries, carries one.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}));
    // Declares one entry, carries none.
    ASSERT_TRUE(
        harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag, 0x01U, 0x00U}));
    // Trailing garbage after a well-formed entry.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU}));

    harness.b.UpdateUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().messagesIgnoredMalformed, 3U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 1U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
}

TEST(NetworkReplicationUVETest, Snapshot_TruncatedToFitLowestIdsWin) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    for (NetworkEntityIdUVE id = 0U; id < 80U; ++id) {
        const PositionSamplerUVE sampler = [id]() {
            return Math::Vector3UVE{static_cast<float>(id), 0.0F, 0.0F};
        };
        const PositionApplierUVE applier = [](const Math::Vector3UVE&) {};
        ASSERT_TRUE(harness.a.RegisterEntityUVE(id, sampler, applier));
    }
    for (NetworkEntityIdUVE id = 74U; id < 80U; ++id) {
        harness.positionsB[id] = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
        static_cast<void>(harness.b.RegisterEntityUVE(
            id, [id]() { return Math::Vector3UVE{static_cast<float>(id), 0.0F, 0.0F}; },
            [&harness, id](const Math::Vector3UVE& position) { harness.positionsB[id] = position; }));
    }

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 6U);
    EXPECT_EQ(harness.b.StatsUVE().entriesIgnoredUnknown, 74U);
    for (NetworkEntityIdUVE id = 74U; id < 80U; ++id) {
        EXPECT_EQ(harness.positionsB[id], (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));
    }
}

TEST(NetworkReplicationUVETest, SendOnlyWhenEstablished) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);

    harness.UpdateBothUVE(1000U);
    harness.UpdateBothUVE(2000U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 0U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsSent, 0U);

    ASSERT_TRUE(harness.a.SessionUVE().ConnectUVE(3000U));
    harness.a.UpdateUVE(3100U);
    harness.a.UpdateUVE(3200U);
    EXPECT_EQ(harness.a.SessionUVE().StateUVE(), SessionStateUVE::Connecting);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 0U);
}

TEST(NetworkReplicationUVETest, EmptyRegistration_SendsHeartbeatSnapshots) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 1U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 0U);
}

TEST(NetworkReplicationUVETest, Stats_CountersExactAfterAMixedExchange) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    const PositionApplierUVE ignore = [](const Math::Vector3UVE&) {};
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        1U, []() { return Math::Vector3UVE{1.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        2U, []() { return Math::Vector3UVE{2.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        2U, []() { return Math::Vector3UVE{20.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        3U, []() { return Math::Vector3UVE{30.0F, 0.0F, 0.0F}; }, ignore));

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.StatsUVE(), (ReplicationStatsUVE{1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U}));
    EXPECT_EQ(harness.b.StatsUVE(), (ReplicationStatsUVE{1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U}));
}

TEST(NetworkReplicationUVETest, Authority_OwnerSendsButIgnoresInbound) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // A owns 7: B's snapshot is refused, A's is adopted - one direction flows.
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.a.StatsUVE().entriesIgnoredOwned, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 1U);
}

TEST(NetworkReplicationUVETest, Authority_ProxyAppliesButNeverSends) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3UVE{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    // A proxies 7: B's snapshot lands, A's value never leaves - and the proxy competes
    // for no slot, so truncation stays silent while the (empty) snapshot still goes out.
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 0U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 0U);
}

TEST(NetworkReplicationUVETest, Authority_TransferFlipsDirectionLive) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    EXPECT_FALSE(harness.a.SetAuthorityUVE(99U, ReplicationAuthorityUVE::Owner));
    EXPECT_FALSE(harness.a.SetRelevancyUVE(99U, []() { return true; }));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 0.0F, 0.0F};

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));

    // App-coordinated handoff: both sides rebind, and the flow reverses on the next tick.
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    harness.positionsB[7U] = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1100U);
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationUVETest, Relevancy_IrrelevantSkippedWithoutSampling) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    int samples8 = 0;
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        8U,
        [&samples8]() {
            ++samples8;
            return Math::Vector3UVE{8.0F, 8.0F, 8.0F};
        },
        [&harness](const Math::Vector3UVE& position) { harness.positionsA[8U] = position; },
        ReplicationAuthorityUVE::Shared, []() { return false; }));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        8U, [&harness]() { return harness.positionsB[8U]; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsB[8U] = position; }));

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(samples8, 0);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 0U);

    // Only 7 crosses the wire: 8's sampler returns nonzero, so B holding zeros proves the
    // entry was filtered before its sampler ever ran.
    harness.positionsA[7U] = Math::Vector3UVE{7.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1100U);
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{7.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(samples8, 0);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 2U);

    // Interest can change live: 7 goes dark too, and the next tick carries nothing.
    ASSERT_TRUE(harness.a.SetRelevancyUVE(7U, []() { return false; }));
    EXPECT_FALSE(harness.a.SetRelevancyUVE(99U, []() { return true; }));
    const std::uint64_t appliedBefore = harness.b.StatsUVE().entriesApplied;
    harness.UpdateBothUVE(1200U);
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 4U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, appliedBefore);
}

TEST(NetworkReplicationUVETest, Relevancy_AllIrrelevantStillSendsKeepalive) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    // Both sides filter everything: every tick is empty, and the session must never notice.
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{7.0F, 0.0F, 0.0F}; },
        [](const Math::Vector3UVE&) {}, ReplicationAuthorityUVE::Shared,
        []() { return false; }));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{7.0F, 0.0F, 0.0F}; },
        [](const Math::Vector3UVE&) {}, ReplicationAuthorityUVE::Shared,
        []() { return false; }));

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 1U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 0U);

    // Sixty quiet ticks past the silence timeout: only empty snapshots cross, and both
    // sessions stay up on keepalive alone.
    for (std::uint64_t nowMs = 1100U; nowMs <= 7000U; nowMs += 100U) {
        harness.UpdateBothUVE(nowMs);
    }
    EXPECT_EQ(harness.a.SessionUVE().StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.b.SessionUVE().StateUVE(), SessionStateUVE::Established);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 61U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 61U);
}

TEST(NetworkReplicationUVETest, Relevancy_PredicateNeverConsultedForProxies) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    int checks = 0;
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.a.SetRelevancyUVE(7U, [&checks]() {
        ++checks;
        return false;
    }));

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.UpdateBothUVE(1100U);
    harness.UpdateBothUVE(1200U);
    // Stored but never called: proxies don't send, so interest is never asked.
    EXPECT_EQ(checks, 0);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 0U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 3U);
}

TEST(NetworkReplicationUVETest, Relevancy_TruncationCountsOnlyRelevantSenders) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    // 80 relevant senders over a 74-slot snapshot, 5 irrelevant, 5 proxies: the losers are
    // exactly the 6 lowest-priority relevant ids, and nobody else counts anywhere. A owns
    // its senders so B's opening snapshot can't zero their values before A sends.
    for (NetworkEntityIdUVE netId = 1U; netId <= 90U; ++netId) {
        harness.positionsA[netId] = Math::Vector3UVE{static_cast<float>(netId), 0.0F, 0.0F};
        RelevancyCheckUVE relevancy;
        ReplicationAuthorityUVE authority = ReplicationAuthorityUVE::Owner;
        if (netId > 80U && netId <= 85U) {
            authority = ReplicationAuthorityUVE::Shared;
            relevancy = []() { return false; };
        } else if (netId > 85U) {
            authority = ReplicationAuthorityUVE::Proxy;
        }
        ASSERT_TRUE(harness.a.RegisterEntityUVE(
            netId, [&harness, netId]() { return harness.positionsA[netId]; },
            [&harness, netId](const Math::Vector3UVE& position) {
                harness.positionsA[netId] = position;
            },
            authority, std::move(relevancy)));
        ASSERT_TRUE(harness.b.RegisterEntityUVE(
            netId, [&harness, netId]() { return harness.positionsB[netId]; },
            [&harness, netId](const Math::Vector3UVE& position) {
                harness.positionsB[netId] = position;
            }));
    }
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedIrrelevant, 5U);
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 6U);
    EXPECT_EQ(harness.a.StatsUVE().entriesIgnoredOwned, 74U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 74U);
    EXPECT_EQ(harness.positionsB[74U], (Math::Vector3UVE{74.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[75U], (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationUVETest, Delta_StationaryEntitiesGoQuietAfterFirstSighting) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.a.StatsUVE().entriesApplied, 1U);

    // Nothing moves: the second send is an empty delta, the baselines hold, and nobody
    // re-applies what nobody sent. The repeat tick sends nothing (same millisecond) but
    // drains, so both sides hold each other's delta.
    harness.UpdateBothUVE(1100U);
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 2U);
    EXPECT_EQ(harness.a.StatsUVE().deltaSnapshotsSent, 1U);
    EXPECT_EQ(harness.a.StatsUVE().deltaSnapshotsReceived, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesApplied, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesSkippedUnchanged, 1U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 1U);
}

TEST(NetworkReplicationUVETest, Delta_OnlyChangedEntitiesShip) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.positionsA[7U] = Math::Vector3UVE{7.0F, 0.0F, 0.0F};
    harness.positionsA[8U] = Math::Vector3UVE{8.0F, 0.0F, 0.0F};
    harness.positionsA[9U] = Math::Vector3UVE{9.0F, 0.0F, 0.0F};
    for (const NetworkEntityIdUVE netId : {7U, 8U, 9U}) {
        ASSERT_TRUE(harness.a.RegisterEntityUVE(
            netId, [&harness, netId]() { return harness.positionsA[netId]; },
            [&harness, netId](const Math::Vector3UVE& position) {
                harness.positionsA[netId] = position;
            },
            ReplicationAuthorityUVE::Owner));
        ASSERT_TRUE(harness.b.RegisterEntityUVE(
            netId, []() { return Math::Vector3UVE{}; },
            [&harness, netId](const Math::Vector3UVE& position) {
                harness.positionsB[netId] = position;
            },
            ReplicationAuthorityUVE::Proxy));
    }
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.positionsB[8U], (Math::Vector3UVE{8.0F, 0.0F, 0.0F}));

    // Tamper with the peer's quiet entities (it never sends, so the tampering is local):
    // only 8 moves, so only 8 may cross - the sentinels prove 7 and 9 stayed home.
    harness.positionsB[7U] = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
    harness.positionsB[9U] = Math::Vector3UVE{-2.0F, -2.0F, -2.0F};
    harness.positionsA[8U] = Math::Vector3UVE{80.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3UVE{80.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));
    EXPECT_EQ(harness.positionsB[9U], (Math::Vector3UVE{-2.0F, -2.0F, -2.0F}));
    EXPECT_EQ(harness.a.StatsUVE().deltaSnapshotsSent, 1U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 2U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 4U);
}

TEST(NetworkReplicationUVETest, Delta_LateRegistrationShipsUnconditionally) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // Registered mid-stream with no baseline: ships on the next tick even though 7 stays
    // quiet, and goes quiet itself the tick after.
    harness.RegisterMirroredUVE(8U);
    harness.positionsA[8U] = Math::Vector3UVE{8.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3UVE{8.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 1U);

    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 3U);
}

TEST(NetworkReplicationUVETest, Delta_EpsilonAbsorbsMicroJitter) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        7U, [&harness]() { return harness.positionsA[7U]; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsA[7U] = position; },
        ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{}; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsB[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));

    // Half a millimetre of jitter: within the epsilon, so the delta stays empty.
    harness.positionsA[7U] = Math::Vector3UVE{1.0005F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 1U);

    // A centimetre jump: past the epsilon, so it ships.
    harness.positionsA[7U] = Math::Vector3UVE{1.01F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.01F, 0.0F, 0.0F}));

    EXPECT_FALSE(harness.a.SetPositionEpsilonUVE(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(harness.a.SetPositionEpsilonUVE(-1.0F));
    EXPECT_EQ(harness.a.PositionEpsilonUVE(), kDefaultDeltaPositionEpsilonUVE);
    // Zero means exact equality: the same half-millimetre now ships.
    EXPECT_TRUE(harness.a.SetPositionEpsilonUVE(0.0F));
    harness.positionsA[7U] = Math::Vector3UVE{1.0105F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1300U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0105F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationUVETest, Delta_PeriodicFullResyncsAfterLoss) {
    // Deltas over lossy mail can strand the peer: the sender's baseline advances on send,
    // so a dropped delta looks delivered until the next full re-baselines the world. This
    // test drops a delta on purpose, watches the divergence sit through an empty delta, and
    // proves the scheduled full heals it.
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        7U, [&harness]() { return harness.positionsA[7U]; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsA[7U] = position; },
        ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{}; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsB[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriodUVE(3U));
    ASSERT_EQ(harness.a.FullSnapshotPeriodUVE(), 3U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    harness.a.UpdateUVE(1100U);
    ASSERT_TRUE(harness.link.b.TryReceiveUVE().has_value()); // send #2: dropped on the floor.
    harness.b.UpdateUVE(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));

    // Send #3 is an empty delta - the sender believes 7 delivered - so the divergence sits.
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{0.0F, 0.0F, 0.0F}));

    // Send #4 is the scheduled full: the peer converges without ever asking.
    harness.UpdateBothUVE(1300U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 4U);
    EXPECT_EQ(harness.a.StatsUVE().deltaSnapshotsSent, 2U);
}

TEST(NetworkReplicationUVETest, Delta_PeriodOneIsLegacyFullSnapshots) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    harness.positionsA[7U] = Math::Vector3UVE{5.0F, 5.0F, 5.0F};
    harness.positionsB[7U] = Math::Vector3UVE{5.0F, 5.0F, 5.0F};
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriodUVE(1U));
    ASSERT_TRUE(harness.b.SetFullSnapshotPeriodUVE(1U));
    EXPECT_FALSE(harness.a.SetFullSnapshotPeriodUVE(0U));
    EXPECT_EQ(harness.a.FullSnapshotPeriodUVE(), 1U);

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.UpdateBothUVE(1100U);
    harness.UpdateBothUVE(1200U);
    // Every send full, every tick applied: the pre-delta behavior, kept as the escape hatch.
    EXPECT_EQ(harness.a.StatsUVE().snapshotsSent, 3U);
    EXPECT_EQ(harness.a.StatsUVE().deltaSnapshotsSent, 0U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 0U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 3U);
}

TEST(NetworkReplicationUVETest, Delta_UnregisterForgetsBaseline) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    const auto bindA = [&harness]() {
        return harness.a.RegisterEntityUVE(
            7U, [&harness]() { return harness.positionsA[7U]; },
            [&harness](const Math::Vector3UVE& position) { harness.positionsA[7U] = position; },
            ReplicationAuthorityUVE::Owner);
    };
    ASSERT_TRUE(bindA());
    ASSERT_TRUE(harness.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{}; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsB[7U] = position; },
        ReplicationAuthorityUVE::Proxy));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 2.0F, 3.0F};
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));

    // Tamper the peer, then rebind the same value: with no baseline it must ship again.
    harness.positionsB[7U] = Math::Vector3UVE{-9.0F, -9.0F, -9.0F};
    ASSERT_TRUE(harness.a.UnregisterEntityUVE(7U));
    ASSERT_TRUE(bindA());
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 0U);
}

TEST(NetworkReplicationUVETest, Delta_TruncatedEntriesStayDirtyAndConverge) {
    // Before deltas, the losers of a truncation starved forever: every full snapshot carried
    // the same lowest 74 ids. Now the shipped majority goes quiet and the losers surface on
    // the very next tick - truncation composes with deltas into eventual full convergence.
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    for (NetworkEntityIdUVE id = 0U; id < 80U; ++id) {
        harness.positionsA[id] = Math::Vector3UVE{static_cast<float>(id), 0.0F, 0.0F};
        harness.positionsB[id] = Math::Vector3UVE{-1.0F, -1.0F, -1.0F};
        ASSERT_TRUE(harness.a.RegisterEntityUVE(
            id, [&harness, id]() { return harness.positionsA[id]; },
            [&harness, id](const Math::Vector3UVE& position) { harness.positionsA[id] = position; },
            ReplicationAuthorityUVE::Owner));
        ASSERT_TRUE(harness.b.RegisterEntityUVE(
            id, []() { return Math::Vector3UVE{}; },
            [&harness, id](const Math::Vector3UVE& position) { harness.positionsB[id] = position; },
            ReplicationAuthorityUVE::Proxy));
    }

    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 6U);
    EXPECT_EQ(harness.positionsB[73U], (Math::Vector3UVE{73.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[74U], (Math::Vector3UVE{-1.0F, -1.0F, -1.0F}));

    harness.UpdateBothUVE(1100U);
    for (NetworkEntityIdUVE id = 0U; id < 80U; ++id) {
        EXPECT_EQ(harness.positionsB[id], (Math::Vector3UVE{static_cast<float>(id), 0.0F, 0.0F}));
    }
    EXPECT_EQ(harness.a.StatsUVE().entriesTruncated, 6U);
    EXPECT_EQ(harness.a.StatsUVE().entriesSkippedUnchanged, 74U);
    EXPECT_EQ(harness.b.StatsUVE().entriesApplied, 80U);
}

TEST(NetworkReplicationUVETest, Delta_MalformedDeltaFramesIgnored) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTagUVE::Data);

    // Marker with no count.
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU}));
    // Declares one entry, carries none.
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U}));
    // Trailing garbage after a well-formed delta entry.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U,
        0x40U, 0x00U, 0x00U, 0xA0U, 0x40U, 0x00U, 0x00U, 0xC0U, 0x40U, 0xFFU}));
    // Trailing garbage on an empty delta.
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x00U, 0x00U, 0xFFU}));
    // A well-formed empty delta, and one carrying 7 at (4, 5, 6).
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x00U, 0x00U}));
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U,
        0x40U, 0x00U, 0x00U, 0xA0U, 0x40U, 0x00U, 0x00U, 0xC0U, 0x40U}));

    harness.b.UpdateUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 3U);
    EXPECT_EQ(harness.b.StatsUVE().deltaSnapshotsReceived, 2U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{4.0F, 5.0F, 6.0F}));
}

TEST(NetworkReplicationUVETest, Ownership_RequestAcceptFlipsBothSidesAndReversesFlow) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    std::vector<TransferEventUVE> logA;
    std::vector<TransferEventUVE> logB;
    // The granter answers reentrantly, straight from the pump: the handshake's natural shape.
    harness.a.SetOwnershipTransferHandlerUVE(
        [&harness, &logA](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logA.push_back(TransferEventUVE{netId, event});
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransferUVE(netId));
            }
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3UVE{1.0F, 0.0F, 0.0F}));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(logA, (std::vector<TransferEventUVE>{
                        {7U, OwnershipTransferEventUVE::Requested},
                        {7U, OwnershipTransferEventUVE::Completed},
                    }));
    EXPECT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Completed}}));
    EXPECT_EQ(harness.a.StatsUVE().transfersCompleted, 1U);
    EXPECT_EQ(harness.b.StatsUVE().transfersRequested, 1U);
    EXPECT_EQ(harness.b.StatsUVE().transfersCompleted, 1U);

    // The flow reverses: B's samples now land on A, and A's sampler goes unheard.
    harness.positionsB[7U] = Math::Vector3UVE{2.0F, 0.0F, 0.0F};
    harness.positionsA[7U] = Math::Vector3UVE{9.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1200U);
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{2.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationUVETest, Ownership_RequestDeniedChangesNothing) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    harness.positionsA[7U] = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
    std::vector<TransferEventUVE> logA;
    std::vector<TransferEventUVE> logB;
    harness.a.SetOwnershipTransferHandlerUVE(
        [&harness, &logA](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logA.push_back(TransferEventUVE{netId, event});
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(harness.a.DenyTransferUVE(netId));
            }
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(logA, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Requested}}));
    EXPECT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Denied}}));
    EXPECT_EQ(harness.a.StatsUVE().transfersDenied, 1U);
    EXPECT_EQ(harness.b.StatsUVE().transfersDenied, 1U);

    // A still owns: its samples still land.
    harness.positionsA[7U] = Math::Vector3UVE{5.0F, 0.0F, 0.0F};
    harness.UpdateBothUVE(1200U);
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3UVE{5.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationUVETest, Ownership_RequestRulesRejectBadAsks) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 8U));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_FALSE(harness.a.RequestOwnershipUVE(7U, 1000U)); // Shared asks: no.
    EXPECT_FALSE(harness.a.RequestOwnershipUVE(8U, 1000U)); // Owner asks for its own: no.
    EXPECT_FALSE(harness.a.RequestOwnershipUVE(99U, 1000U)); // Unknown asks: no.
    EXPECT_TRUE(harness.b.RequestOwnershipUVE(8U, 1000U));
    EXPECT_FALSE(harness.b.RequestOwnershipUVE(8U, 1000U)); // Single-flight: no.
    EXPECT_EQ(harness.b.StatsUVE().transfersRequested, 1U);
    // The refusals sent nothing: exactly one frame sits on the wire.
    ASSERT_TRUE(harness.link.a.TryReceiveUVE().has_value());
    EXPECT_FALSE(harness.link.a.TryReceiveUVE().has_value());

    // A down session sends nothing either.
    ReplicationHarnessUVE dark = MakeReplicatorsUVE();
    ASSERT_TRUE(dark.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{}; }, [](const Math::Vector3UVE&) {},
        ReplicationAuthorityUVE::Proxy));
    EXPECT_FALSE(dark.b.RequestOwnershipUVE(7U, 1000U));
    EXPECT_EQ(dark.b.StatsUVE().transfersRequested, 0U);
}

TEST(NetworkReplicationUVETest, Ownership_NonOwnerAndUnknownAutoDeny) {
    // Both sides proxy: neither can grant, so the answer is deny - without ever notifying.
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    std::vector<TransferEventUVE> logA;
    std::vector<TransferEventUVE> logB;
    harness.a.SetOwnershipTransferHandlerUVE(
        [&logA](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logA.push_back(TransferEventUVE{netId, event});
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.UpdateBothUVE(1100U);
    EXPECT_TRUE(logA.empty());
    EXPECT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Denied}}));
    EXPECT_EQ(harness.a.StatsUVE().transfersDenied, 1U);

    // Unknown entity: no registration, no grant - deny all the same.
    ReplicationHarnessUVE stranger = MakeReplicatorsUVE();
    ASSERT_TRUE(stranger.b.RegisterEntityUVE(
        7U, []() { return Math::Vector3UVE{}; }, [](const Math::Vector3UVE&) {},
        ReplicationAuthorityUVE::Proxy));
    std::vector<TransferEventUVE> strangerLogA;
    std::vector<TransferEventUVE> strangerLogB;
    stranger.a.SetOwnershipTransferHandlerUVE(
        [&strangerLogA](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            strangerLogA.push_back(TransferEventUVE{netId, event});
        });
    stranger.b.SetOwnershipTransferHandlerUVE(
        [&strangerLogB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            strangerLogB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(stranger.EstablishUVE(1000U));
    ASSERT_TRUE(stranger.b.RequestOwnershipUVE(7U, 1000U));
    stranger.UpdateBothUVE(1100U);
    EXPECT_TRUE(strangerLogA.empty());
    EXPECT_EQ(strangerLogB,
              (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Denied}}));
    EXPECT_EQ(stranger.a.StatsUVE().transfersDenied, 1U);
}

TEST(NetworkReplicationUVETest, Ownership_NoHandlerAutoDenies) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    std::vector<TransferEventUVE> logB;
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    // A never set a handler: its requests cannot be decided, so they are denied for it.
    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Denied}}));
    EXPECT_EQ(harness.a.StatsUVE().transfersDenied, 1U);
}

TEST(NetworkReplicationUVETest, Ownership_RequestTimesOutAndMayRetry) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    std::vector<TransferEventUVE> logA;
    std::vector<TransferEventUVE> logB;
    // A records but never answers: silence the requester must survive.
    harness.a.SetOwnershipTransferHandlerUVE(
        [&logA](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logA.push_back(TransferEventUVE{netId, event});
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.UpdateBothUVE(1100U);
    ASSERT_EQ(logA, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::Requested}}));
    harness.UpdateBothUVE(1500U);
    EXPECT_TRUE(logB.empty());
    EXPECT_FALSE(harness.b.RequestOwnershipUVE(7U, 1500U)); // Still pending: no re-ask.
    harness.UpdateBothUVE(2000U); // Deadline inclusive: 2000 - 1000 >= 1000 expires.
    EXPECT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::TimedOut}}));
    EXPECT_EQ(harness.b.StatsUVE().transfersTimedOut, 1U);

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 2000U));
    EXPECT_EQ(harness.b.StatsUVE().transfersRequested, 2U);
    harness.UpdateBothUVE(2100U); // The retry re-notifies the undecided granter.
    EXPECT_EQ(logA.size(), 2U);
    EXPECT_EQ(logB.size(), 1U);
}

TEST(NetworkReplicationUVETest, Ownership_StaleAcceptAfterTimeoutIsIgnored) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    std::vector<TransferEventUVE> logB;
    harness.a.SetOwnershipTransferHandlerUVE(
        [&harness](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransferUVE(netId));
            }
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    harness.a.UpdateUVE(1100U); // A answers; the accept never leaves the tap.
    const std::optional<std::vector<std::uint8_t>> accept = harness.link.b.TryReceiveUVE();
    ASSERT_TRUE(accept.has_value());
    harness.b.UpdateUVE(1100U);
    harness.b.UpdateUVE(2001U); // The request dies unanswered...
    ASSERT_EQ(logB, (std::vector<TransferEventUVE>{{7U, OwnershipTransferEventUVE::TimedOut}}));
    ASSERT_TRUE(harness.link.a.SendUVE(*accept)); // ...then the late accept arrives.
    harness.b.UpdateUVE(2002U);
    EXPECT_EQ(harness.b.StatsUVE().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.b.StatsUVE().transfersCompleted, 0U);
    EXPECT_EQ(logB.size(), 1U); // No Completed: the flip never happened.
}

TEST(NetworkReplicationUVETest, Ownership_ManualRebindClearsPending) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    std::vector<TransferEventUVE> logB;
    harness.a.SetOwnershipTransferHandlerUVE(
        [&harness](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransferUVE(netId));
            }
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner)); // Takes the wheel.
    harness.UpdateBothUVE(1100U);
    // A's accept lands on no open request: stale, and B's log stays silent. (A did its
    // half anyway, so both sides agree B owns - the override and the late answer converge.)
    EXPECT_TRUE(logB.empty());
    EXPECT_EQ(harness.b.StatsUVE().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.a.StatsUVE().transfersCompleted, 1U);
}

TEST(NetworkReplicationUVETest, Ownership_UnregisterClearsPending) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    std::vector<TransferEventUVE> logB;
    harness.a.SetOwnershipTransferHandlerUVE(
        [&harness](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            if (event == OwnershipTransferEventUVE::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransferUVE(netId));
            }
        });
    harness.b.SetOwnershipTransferHandlerUVE(
        [&logB](const NetworkEntityIdUVE netId, const OwnershipTransferEventUVE event) {
            logB.push_back(TransferEventUVE{netId, event});
        });
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.b.RequestOwnershipUVE(7U, 1000U));
    ASSERT_TRUE(harness.b.UnregisterEntityUVE(7U));
    harness.UpdateBothUVE(1100U);
    EXPECT_TRUE(logB.empty());
    EXPECT_EQ(harness.b.StatsUVE().transfersIgnoredStale, 1U);
}

TEST(NetworkReplicationUVETest, Ownership_AnswerAndTimeoutRules) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_FALSE(harness.a.AcceptTransferUVE(7U)); // Nothing awaiting: no.
    EXPECT_FALSE(harness.a.DenyTransferUVE(7U));
    EXPECT_FALSE(harness.a.AcceptTransferUVE(99U));
    EXPECT_FALSE(harness.a.DenyTransferUVE(99U));
    EXPECT_FALSE(harness.a.SetOwnershipTimeoutUVE(0U));
    EXPECT_EQ(harness.a.OwnershipTimeoutUVE(), kDefaultOwnershipTimeoutMsUVE);
    EXPECT_TRUE(harness.a.SetOwnershipTimeoutUVE(500U));
    EXPECT_EQ(harness.a.OwnershipTimeoutUVE(), 500U);
}

TEST(NetworkReplicationUVETest, Ownership_MalformedControlFramesIgnored) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTagUVE::Data);

    // Marker alone.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU}));
    // Kind without an entity.
    ASSERT_TRUE(
        harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x01U}));
    // Unknown kind, right size.
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x04U, 0x07U, 0x00U, 0x00U, 0x00U}));
    // Trailing byte on a well-formed deny.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x03U,
                                                                0x07U, 0x00U, 0x00U, 0x00U, 0xFFU}));
    // Well-formed deny with nothing open: stale, not malformed.
    ASSERT_TRUE(harness.link.a.SendUVE(
        std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x03U, 0x07U, 0x00U, 0x00U, 0x00U}));

    harness.b.UpdateUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.StatsUVE().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.b.StatsUVE().snapshotsReceived, 1U); // Control frames are not snapshots.
}

// Captures received property values by (entity, property), with per-pair call counts.
struct PropertySinkUVE final {
    std::map<std::pair<NetworkEntityIdUVE, ReplicationPropertyIdUVE>, UVScript::ValueUVE> received;
    std::map<std::pair<NetworkEntityIdUVE, ReplicationPropertyIdUVE>, int> calls;

    [[nodiscard]] PropertyApplierUVE ApplierFor(const NetworkEntityIdUVE netId,
                                                const ReplicationPropertyIdUVE propId) {
        return [this, netId, propId](const UVScript::ValueUVE& value) {
            received[{netId, propId}] = value;
            ++calls[{netId, propId}];
        };
    }
};

TEST(NetworkReplicationUVETest, Property_RoundTripsToApplier) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{83}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(sink.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{83}});
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesApplied, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesSent, 0U); // Nothing dirty, no frame.
}

TEST(NetworkReplicationUVETest, Property_RegistrationGuards) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;

    EXPECT_FALSE(harness.b.RegisterPropertyUVE(9U, 3U, sink.ApplierFor(9U, 3U)));
    EXPECT_FALSE(harness.b.RegisterPropertyUVE(7U, 3U, PropertyApplierUVE{}));
    EXPECT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_FALSE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_FALSE(harness.b.UnregisterPropertyUVE(7U, 4U));
    EXPECT_FALSE(
        harness.a.SetPropertyUVE(9U, 3U, UVScript::ValueUVE{std::int64_t{1}}));
    EXPECT_FALSE(harness.a.SetPropertyUVE(
        7U, 3U, UVScript::ValueUVE{UVScript::ObjectRefUVE{42U}}));
    EXPECT_FALSE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::string(65536U, 'x')}));
    EXPECT_FALSE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::string(2000U, 'y')}));
    EXPECT_EQ(harness.a.StatsUVE().propertiesSkippedOversized, 3U);
}

TEST(NetworkReplicationUVETest, Property_UnchangedSetDoesNotResend) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{83}}));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{83}}));
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U); // Byte-identical: no re-ship.
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{84}}));
    harness.UpdateBothUVE(1300U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 2U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesApplied, 2U);
    EXPECT_EQ(sink.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{84}});
}

TEST(NetworkReplicationUVETest, Property_FullTickRecarriesClean) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriodUVE(1U));
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{83}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.UpdateBothUVE(1100U);

    // Every tick is full, so the clean value re-carries: at-least-once, like positions.
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 2U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesApplied, 2U);
    EXPECT_EQ(sink.calls.at({7U, 3U}), 2);
}

TEST(NetworkReplicationUVETest, Property_ProxyHoldsUntilFlip) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.a.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_TRUE(harness.b.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{11}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesSent, 0U);

    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    harness.UpdateBothUVE(1200U);
    harness.a.UpdateUVE(1200U); // Deliver B's last send; A's cadence just ran, so A sends nothing.
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(sink.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{11}});
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 0U);
}

TEST(NetworkReplicationUVETest, Property_OwnerIgnoresInbound) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.a.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.b.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{5}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(harness.a.StatsUVE().propertiesIgnoredOwned, 1U);
    EXPECT_EQ(harness.a.StatsUVE().propertiesApplied, 0U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationUVETest, Property_UnknownPairsIgnored) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    ASSERT_TRUE(harness.a.RegisterEntityUVE(
        9U, [&harness]() { return harness.positionsA[9U]; },
        [&harness](const Math::Vector3UVE& position) { harness.positionsA[9U] = position; }));
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 6U, sink.ApplierFor(7U, 6U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 5U, UVScript::ValueUVE{std::int64_t{1}}));
    ASSERT_TRUE(harness.a.SetPropertyUVE(9U, 5U, UVScript::ValueUVE{std::int64_t{2}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(harness.b.StatsUVE().propertyFramesReceived, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesIgnoredUnknown, 2U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationUVETest, Property_IrrelevantSkipsAndRunsPredicateOnce) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    int relevancyCalls = 0;
    bool relevant = false;
    ASSERT_TRUE(harness.a.SetRelevancyUVE(7U, [&relevancyCalls, &relevant]() {
        ++relevancyCalls;
        return relevant;
    }));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{9}}));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 0U);
    EXPECT_EQ(relevancyCalls, 1);
    relevant = true;
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(relevancyCalls, 2);
    EXPECT_EQ(sink.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{9}});
}

TEST(NetworkReplicationUVETest, Property_TruncationPacksInOrderAndLosersSurface) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 1U, sink.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 2U, sink.ApplierFor(7U, 2U)));
    const UVScript::ValueUVE first{std::string(600U, 'a')};
    const UVScript::ValueUVE second{std::string(600U, 'b')};
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 1U, first));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 2U, second));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    harness.b.UpdateUVE(1000U); // Deliver A's sends without advancing B's cadence.

    EXPECT_EQ(sink.received.at({7U, 1U}), first);
    EXPECT_EQ(sink.received.count({7U, 2U}), 0U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(harness.a.StatsUVE().propertiesTruncated, 1U);
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(sink.received.at({7U, 2U}), second);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 2U);
}

TEST(NetworkReplicationUVETest, Property_UnregisterEntityDropsRow) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{1}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);

    EXPECT_TRUE(harness.a.UnregisterEntityUVE(7U));
    harness.RegisterMirroredUVE(7U); // A re-registers; B's duplicate is ignored.
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U); // Cache dropped: nothing to send.
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{2}}));
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 2U);
    EXPECT_EQ(sink.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{2}});
}

TEST(NetworkReplicationUVETest, Property_MalformedFramesIgnoredWhole) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTagUVE::Data);
    // Count declared, entry cut before the property id.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U}));
    // Well-formed entry plus a trailing byte: the whole frame goes, applied nothing.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x03U, 0x02U, 0x09U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU}));
    // Unknown value tag.
    ASSERT_TRUE(harness.link.a.SendUVE(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x03U, 0x09U}));
    // Marker alone, no count.
    ASSERT_TRUE(
        harness.link.a.SendUVE(std::vector<std::uint8_t>{dataTag, 0xFDU, 0xFFU}));

    harness.b.UpdateUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesReceived, 0U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesApplied, 0U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationUVETest, Property_FlipReversesDirection) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    ASSERT_TRUE(RegisterOwnerProxyUVE(harness, 7U));
    PropertySinkUVE sinkA;
    PropertySinkUVE sinkB;
    ASSERT_TRUE(harness.a.RegisterPropertyUVE(7U, 3U, sinkA.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sinkB.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{21}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(sinkB.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{21}});

    ASSERT_TRUE(harness.a.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Proxy));
    ASSERT_TRUE(harness.b.SetAuthorityUVE(7U, ReplicationAuthorityUVE::Owner));
    ASSERT_TRUE(harness.b.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{22}}));
    harness.UpdateBothUVE(1100U);
    harness.a.UpdateUVE(1100U); // Deliver B's last send; A's cadence just ran, so A sends nothing.
    EXPECT_EQ(sinkA.received.at({7U, 3U}), UVScript::ValueUVE{std::int64_t{22}});
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesIgnoredOwned, 0U);
}

TEST(NetworkReplicationUVETest, Property_SharedBothDirections) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sinkA;
    PropertySinkUVE sinkB;
    ASSERT_TRUE(harness.a.RegisterPropertyUVE(7U, 1U, sinkA.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 1U, sinkB.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 1U, UVScript::ValueUVE{std::int64_t{100}}));
    ASSERT_TRUE(harness.b.SetPropertyUVE(7U, 1U, UVScript::ValueUVE{std::int64_t{200}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));

    EXPECT_EQ(sinkA.received.at({7U, 1U}), UVScript::ValueUVE{std::int64_t{200}});
    EXPECT_EQ(sinkB.received.at({7U, 1U}), UVScript::ValueUVE{std::int64_t{100}});
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.StatsUVE().propertyFramesSent, 1U);
}

TEST(NetworkReplicationUVETest, Property_UnregisterPropertyStopsBothWays) {
    ReplicationHarnessUVE harness = MakeReplicatorsUVE();
    harness.RegisterMirroredUVE(7U);
    PropertySinkUVE sink;
    ASSERT_TRUE(harness.b.RegisterPropertyUVE(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{1}}));
    ASSERT_TRUE(harness.EstablishUVE(1000U));
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);

    EXPECT_TRUE(harness.b.UnregisterPropertyUVE(7U, 3U));
    ASSERT_TRUE(harness.a.SetPropertyUVE(7U, 3U, UVScript::ValueUVE{std::int64_t{2}}));
    harness.UpdateBothUVE(1100U);
    EXPECT_EQ(harness.b.StatsUVE().propertiesIgnoredUnknown, 1U);
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);
    EXPECT_TRUE(harness.a.UnregisterPropertyUVE(7U, 3U)); // The cached value goes too.
    harness.UpdateBothUVE(1200U);
    EXPECT_EQ(harness.a.StatsUVE().propertyFramesSent, 2U);
    EXPECT_FALSE(harness.a.UnregisterPropertyUVE(7U, 3U));
}

} // namespace
} // namespace UVE::Network::Tests
