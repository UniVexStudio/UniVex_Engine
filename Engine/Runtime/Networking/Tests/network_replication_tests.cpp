// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "network_replication.h"

#include "blueprint_value.h"

#include <cstdint>
#include <limits>
#include <map>
#include <memory>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

namespace UVE::Network::Tests {
namespace {

struct ReplicationHarness final {
    LoopbackTransportPair link;
    NetworkReplicator a;
    NetworkReplicator b;
    std::map<NetworkEntityId, Math::Vector3> positionsA;
    std::map<NetworkEntityId, Math::Vector3> positionsB;

    void UpdateBoth(const std::uint64_t nowMs) {
        a.Update(nowMs);
        b.Update(nowMs);
    }

    [[nodiscard]] bool Establish(const std::uint64_t nowMs) {
        if (!a.Session().Connect(nowMs)) {
            return false;
        }
        UpdateBoth(nowMs);
        UpdateBoth(nowMs);
        return a.Session().State() == SessionState::Established &&
               b.Session().State() == SessionState::Established;
    }

    void RegisterMirrored(const NetworkEntityId netId) {
        static_cast<void>(a.RegisterEntity(
            netId, [this, netId]() { return positionsA[netId]; },
            [this, netId](const Math::Vector3& position) { positionsA[netId] = position; }));
        static_cast<void>(b.RegisterEntity(
            netId, [this, netId]() { return positionsB[netId]; },
            [this, netId](const Math::Vector3& position) { positionsB[netId] = position; }));
    }
};

[[nodiscard]] ReplicationHarness MakeReplicators(
    const std::uint64_t sendIntervalMs = kDefaultReplicationIntervalMs) {
    LoopbackTransportPair pair = CreateLoopbackTransportPair(256U);
    NetworkSession sessionA(SessionRole::Client,
                               std::make_unique<LoopbackEndpoint>(pair.a));
    NetworkSession sessionB(SessionRole::Server,
                               std::make_unique<LoopbackEndpoint>(pair.b));
    NetworkReplicator a(std::move(sessionA), sendIntervalMs);
    NetworkReplicator b(std::move(sessionB), sendIntervalMs);
    return ReplicationHarness{
        std::move(pair), std::move(a), std::move(b), {}, {}};
}

struct TransferEvent final {
    NetworkEntityId netId = 0U;
    OwnershipTransferEvent event = OwnershipTransferEvent::Requested;

    [[nodiscard]] bool operator==(const TransferEvent&) const = default;
};

// One entity owned by A and proxied by B: the handoff's starting shape.
[[nodiscard]] bool RegisterOwnerProxy(ReplicationHarness& harness,
                                         const NetworkEntityId netId) {
    return harness.a.RegisterEntity(
               netId, [&harness, netId]() { return harness.positionsA[netId]; },
               [&harness, netId](const Math::Vector3& position) {
                   harness.positionsA[netId] = position;
               },
               ReplicationAuthority::Owner) &&
           harness.b.RegisterEntity(
               netId, [&harness, netId]() { return harness.positionsB[netId]; },
               [&harness, netId](const Math::Vector3& position) {
                   harness.positionsB[netId] = position;
               },
               ReplicationAuthority::Proxy);
}

TEST(NetworkReplicationTest, RegisterRules_DuplicatesAndEmptyCallbacksRejected) {
    ReplicationHarness harness = MakeReplicators();
    const PositionSampler sampler = []() { return Math::Vector3{}; };
    const PositionApplier applier = [](const Math::Vector3&) {};

    EXPECT_TRUE(harness.a.RegisterEntity(7U, sampler, applier));
    EXPECT_FALSE(harness.a.RegisterEntity(7U, sampler, applier));
    EXPECT_FALSE(harness.a.RegisterEntity(8U, PositionSampler{}, applier));
    EXPECT_FALSE(harness.a.RegisterEntity(8U, sampler, PositionApplier{}));
    EXPECT_FALSE(harness.a.UnregisterEntity(9U));
    EXPECT_TRUE(harness.a.UnregisterEntity(7U));
    EXPECT_TRUE(harness.a.RegisterEntity(7U, sampler, applier));
}

TEST(NetworkReplicationTest, Snapshot_DivergentValuesConvergeOnFirstSender) {
    // Shared authority by default: whoever's snapshot lands first wins, and the loser adopts it
    // before its own send goes out. The server establishes (and sends) first, so divergent
    // values deterministically converge on the server's: B-to-A delivery, proven by A holding a
    // value it never sampled. (A-to-B is proven directionally by Rate_SendsOnlyWhenDue.)
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    harness.positionsA[7U] = Math::Vector3{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
}

TEST(NetworkReplicationTest, Rate_SendsOnlyWhenDue) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.a.Stats().snapshotsSent, 1U);

    harness.positionsA[7U] = Math::Vector3{5.0F, 5.0F, 5.0F};
    harness.UpdateBoth(1050U);
    harness.UpdateBoth(1099U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 1U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{0.0F, 0.0F, 0.0F}));

    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 2U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{5.0F, 5.0F, 5.0F}));
}

TEST(NetworkReplicationTest, IntervalZero_SendsEveryUpdate) {
    ReplicationHarness harness = MakeReplicators(0U);
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));
    const std::uint64_t sentAfterHandshake = harness.a.Stats().snapshotsSent;

    harness.UpdateBoth(1001U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, sentAfterHandshake + 1U);
    harness.UpdateBoth(1002U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, sentAfterHandshake + 2U);
}

TEST(NetworkReplicationTest, UnknownNetIds_IgnoredAndCounted) {
    ReplicationHarness harness = MakeReplicators();
    const PositionSampler sampler = []() { return Math::Vector3{1.0F, 2.0F, 3.0F}; };
    const PositionApplier applier = [](const Math::Vector3&) {};
    ASSERT_TRUE(harness.a.RegisterEntity(7U, sampler, applier));

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 0U);
    EXPECT_EQ(harness.b.Stats().entriesIgnoredUnknown, 1U);
}

TEST(NetworkReplicationTest, MalformedSnapshot_IgnoredWhole) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    harness.positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};
    ASSERT_TRUE(harness.Establish(1000U));

    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTag::Data);
    // Declares two entries, carries one.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0x02U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U}));
    // Declares one entry, carries none.
    ASSERT_TRUE(
        harness.link.a.Send(std::vector<std::uint8_t>{dataTag, 0x01U, 0x00U}));
    // Trailing garbage after a well-formed entry.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU}));

    harness.b.Update(1100U);
    EXPECT_EQ(harness.b.Stats().messagesIgnoredMalformed, 3U);
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 1U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
}

TEST(NetworkReplicationTest, Snapshot_TruncatedToFitLowestIdsWin) {
    ReplicationHarness harness = MakeReplicators();
    for (NetworkEntityId id = 0U; id < 80U; ++id) {
        const PositionSampler sampler = [id]() {
            return Math::Vector3{static_cast<float>(id), 0.0F, 0.0F};
        };
        const PositionApplier applier = [](const Math::Vector3&) {};
        ASSERT_TRUE(harness.a.RegisterEntity(id, sampler, applier));
    }
    for (NetworkEntityId id = 74U; id < 80U; ++id) {
        harness.positionsB[id] = Math::Vector3{-1.0F, -1.0F, -1.0F};
        static_cast<void>(harness.b.RegisterEntity(
            id, [id]() { return Math::Vector3{static_cast<float>(id), 0.0F, 0.0F}; },
            [&harness, id](const Math::Vector3& position) { harness.positionsB[id] = position; }));
    }

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 6U);
    EXPECT_EQ(harness.b.Stats().entriesIgnoredUnknown, 74U);
    for (NetworkEntityId id = 74U; id < 80U; ++id) {
        EXPECT_EQ(harness.positionsB[id], (Math::Vector3{-1.0F, -1.0F, -1.0F}));
    }
}

TEST(NetworkReplicationTest, Snapshot_MultiChunkModeReplicatesBeyond74EntitiesInOneTick) {
    ReplicationHarness harness = MakeReplicators();
    EXPECT_FALSE(harness.a.SetMaxSnapshotChunksPerTick(0U));
    ASSERT_TRUE(harness.a.SetMaxSnapshotChunksPerTick(2U));
    EXPECT_EQ(harness.a.MaxSnapshotChunksPerTick(), 2U);

    for (NetworkEntityId id = 0U; id < 100U; ++id) {
        harness.positionsA[id] = Math::Vector3{static_cast<float>(id), 1.0F, 2.0F};
        harness.positionsB[id] = Math::Vector3{-1.0F, -1.0F, -1.0F};
        ASSERT_TRUE(RegisterOwnerProxy(harness, id));
    }

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 0U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 2U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 100U);
    EXPECT_EQ(harness.positionsB[99U], (Math::Vector3{99.0F, 1.0F, 2.0F}));
}

TEST(NetworkReplicationTest, SendOnlyWhenEstablished) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);

    harness.UpdateBoth(1000U);
    harness.UpdateBoth(2000U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 0U);
    EXPECT_EQ(harness.b.Stats().snapshotsSent, 0U);

    ASSERT_TRUE(harness.a.Session().Connect(3000U));
    harness.a.Update(3100U);
    harness.a.Update(3200U);
    EXPECT_EQ(harness.a.Session().State(), SessionState::Connecting);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 0U);
}

TEST(NetworkReplicationTest, EmptyRegistration_SendsHeartbeatSnapshots) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(harness.a.Stats().snapshotsSent, 1U);
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 0U);
}

TEST(NetworkReplicationTest, Stats_CountersExactAfterAMixedExchange) {
    ReplicationHarness harness = MakeReplicators();
    const PositionApplier ignore = [](const Math::Vector3&) {};
    ASSERT_TRUE(harness.a.RegisterEntity(
        1U, []() { return Math::Vector3{1.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.a.RegisterEntity(
        2U, []() { return Math::Vector3{2.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.b.RegisterEntity(
        2U, []() { return Math::Vector3{20.0F, 0.0F, 0.0F}; }, ignore));
    ASSERT_TRUE(harness.b.RegisterEntity(
        3U, []() { return Math::Vector3{30.0F, 0.0F, 0.0F}; }, ignore));

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats(), (ReplicationStats{1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U}));
    EXPECT_EQ(harness.b.Stats(), (ReplicationStats{1U, 1U, 1U, 1U, 0U, 0U, 0U, 0U}));
}

TEST(NetworkReplicationTest, Authority_OwnerSendsButIgnoresInbound) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Owner));
    harness.positionsA[7U] = Math::Vector3{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.Establish(1000U));
    // A owns 7: B's snapshot is refused, A's is adopted - one direction flows.
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.a.Stats().entriesIgnoredOwned, 1U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 1U);
}

TEST(NetworkReplicationTest, Authority_ProxyAppliesButNeverSends) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    harness.positionsA[7U] = Math::Vector3{1.0F, 2.0F, 3.0F};
    harness.positionsB[7U] = Math::Vector3{9.0F, 9.0F, 9.0F};

    ASSERT_TRUE(harness.Establish(1000U));
    // A proxies 7: B's snapshot lands, A's value never leaves - and the proxy competes
    // for no slot, so truncation stays silent while the (empty) snapshot still goes out.
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{9.0F, 9.0F, 9.0F}));
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 1U);
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 0U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 0U);
}

TEST(NetworkReplicationTest, Authority_TransferFlipsDirectionLive) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Owner));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Proxy));
    EXPECT_FALSE(harness.a.SetAuthority(99U, ReplicationAuthority::Owner));
    EXPECT_FALSE(harness.a.SetRelevancy(99U, []() { return true; }));
    harness.positionsA[7U] = Math::Vector3{1.0F, 0.0F, 0.0F};

    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 0.0F, 0.0F}));

    // App-coordinated handoff: both sides rebind, and the flow reverses on the next tick.
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Owner));
    harness.positionsB[7U] = Math::Vector3{2.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1100U);
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3{2.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationTest, Relevancy_IrrelevantSkippedWithoutSampling) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    int samples8 = 0;
    ASSERT_TRUE(harness.a.RegisterEntity(
        8U,
        [&samples8]() {
            ++samples8;
            return Math::Vector3{8.0F, 8.0F, 8.0F};
        },
        [&harness](const Math::Vector3& position) { harness.positionsA[8U] = position; },
        ReplicationAuthority::Shared, []() { return false; }));
    ASSERT_TRUE(harness.b.RegisterEntity(
        8U, [&harness]() { return harness.positionsB[8U]; },
        [&harness](const Math::Vector3& position) { harness.positionsB[8U] = position; }));

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(samples8, 0);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 1U);
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 0U);

    // Only 7 crosses the wire: 8's sampler returns nonzero, so B holding zeros proves the
    // entry was filtered before its sampler ever ran.
    harness.positionsA[7U] = Math::Vector3{7.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1100U);
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{7.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3{0.0F, 0.0F, 0.0F}));
    EXPECT_EQ(samples8, 0);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 2U);

    // Interest can change live: 7 goes dark too, and the next tick carries nothing.
    ASSERT_TRUE(harness.a.SetRelevancy(7U, []() { return false; }));
    EXPECT_FALSE(harness.a.SetRelevancy(99U, []() { return true; }));
    const std::uint64_t appliedBefore = harness.b.Stats().entriesApplied;
    harness.UpdateBoth(1200U);
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 4U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, appliedBefore);
}

TEST(NetworkReplicationTest, Relevancy_AllIrrelevantStillSendsKeepalive) {
    ReplicationHarness harness = MakeReplicators();
    // Both sides filter everything: every tick is empty, and the session must never notice.
    ASSERT_TRUE(harness.a.RegisterEntity(
        7U, []() { return Math::Vector3{7.0F, 0.0F, 0.0F}; },
        [](const Math::Vector3&) {}, ReplicationAuthority::Shared,
        []() { return false; }));
    ASSERT_TRUE(harness.b.RegisterEntity(
        7U, []() { return Math::Vector3{7.0F, 0.0F, 0.0F}; },
        [](const Math::Vector3&) {}, ReplicationAuthority::Shared,
        []() { return false; }));

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 1U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 1U);
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 1U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 0U);

    // Sixty quiet ticks past the silence timeout: only empty snapshots cross, and both
    // sessions stay up on keepalive alone.
    for (std::uint64_t nowMs = 1100U; nowMs <= 7000U; nowMs += 100U) {
        harness.UpdateBoth(nowMs);
    }
    EXPECT_EQ(harness.a.Session().State(), SessionState::Established);
    EXPECT_EQ(harness.b.Session().State(), SessionState::Established);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 61U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 61U);
}

TEST(NetworkReplicationTest, Relevancy_PredicateNeverConsultedForProxies) {
    ReplicationHarness harness = MakeReplicators();
    int checks = 0;
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.a.SetRelevancy(7U, [&checks]() {
        ++checks;
        return false;
    }));

    ASSERT_TRUE(harness.Establish(1000U));
    harness.UpdateBoth(1100U);
    harness.UpdateBoth(1200U);
    // Stored but never called: proxies don't send, so interest is never asked.
    EXPECT_EQ(checks, 0);
    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 0U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 3U);
}

TEST(NetworkReplicationTest, Relevancy_TruncationCountsOnlyRelevantSenders) {
    ReplicationHarness harness = MakeReplicators();
    // 80 relevant senders over a 74-slot snapshot, 5 irrelevant, 5 proxies: the losers are
    // exactly the 6 lowest-priority relevant ids, and nobody else counts anywhere. A owns
    // its senders so B's opening snapshot can't zero their values before A sends.
    for (NetworkEntityId netId = 1U; netId <= 90U; ++netId) {
        harness.positionsA[netId] = Math::Vector3{static_cast<float>(netId), 0.0F, 0.0F};
        RelevancyCheck relevancy;
        ReplicationAuthority authority = ReplicationAuthority::Owner;
        if (netId > 80U && netId <= 85U) {
            authority = ReplicationAuthority::Shared;
            relevancy = []() { return false; };
        } else if (netId > 85U) {
            authority = ReplicationAuthority::Proxy;
        }
        ASSERT_TRUE(harness.a.RegisterEntity(
            netId, [&harness, netId]() { return harness.positionsA[netId]; },
            [&harness, netId](const Math::Vector3& position) {
                harness.positionsA[netId] = position;
            },
            authority, std::move(relevancy)));
        ASSERT_TRUE(harness.b.RegisterEntity(
            netId, [&harness, netId]() { return harness.positionsB[netId]; },
            [&harness, netId](const Math::Vector3& position) {
                harness.positionsB[netId] = position;
            }));
    }
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(harness.a.Stats().entriesSkippedIrrelevant, 5U);
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 6U);
    EXPECT_EQ(harness.a.Stats().entriesIgnoredOwned, 74U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 74U);
    EXPECT_EQ(harness.positionsB[74U], (Math::Vector3{74.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[75U], (Math::Vector3{0.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationTest, Delta_StationaryEntitiesGoQuietAfterFirstSighting) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.a.Stats().entriesApplied, 1U);

    // Nothing moves: the second send is an empty delta, the baselines hold, and nobody
    // re-applies what nobody sent. The repeat tick sends nothing (same millisecond) but
    // drains, so both sides hold each other's delta.
    harness.UpdateBoth(1100U);
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 2U);
    EXPECT_EQ(harness.a.Stats().deltaSnapshotsSent, 1U);
    EXPECT_EQ(harness.a.Stats().deltaSnapshotsReceived, 1U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 1U);
    EXPECT_EQ(harness.a.Stats().entriesApplied, 1U);
    EXPECT_EQ(harness.b.Stats().entriesSkippedUnchanged, 1U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 1U);
}

TEST(NetworkReplicationTest, Delta_OnlyChangedEntitiesShip) {
    ReplicationHarness harness = MakeReplicators();
    harness.positionsA[7U] = Math::Vector3{7.0F, 0.0F, 0.0F};
    harness.positionsA[8U] = Math::Vector3{8.0F, 0.0F, 0.0F};
    harness.positionsA[9U] = Math::Vector3{9.0F, 0.0F, 0.0F};
    for (const NetworkEntityId netId : {7U, 8U, 9U}) {
        ASSERT_TRUE(harness.a.RegisterEntity(
            netId, [&harness, netId]() { return harness.positionsA[netId]; },
            [&harness, netId](const Math::Vector3& position) {
                harness.positionsA[netId] = position;
            },
            ReplicationAuthority::Owner));
        ASSERT_TRUE(harness.b.RegisterEntity(
            netId, []() { return Math::Vector3{}; },
            [&harness, netId](const Math::Vector3& position) {
                harness.positionsB[netId] = position;
            },
            ReplicationAuthority::Proxy));
    }
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.positionsB[8U], (Math::Vector3{8.0F, 0.0F, 0.0F}));

    // Tamper with the peer's quiet entities (it never sends, so the tampering is local):
    // only 8 moves, so only 8 may cross - the sentinels prove 7 and 9 stayed home.
    harness.positionsB[7U] = Math::Vector3{-1.0F, -1.0F, -1.0F};
    harness.positionsB[9U] = Math::Vector3{-2.0F, -2.0F, -2.0F};
    harness.positionsA[8U] = Math::Vector3{80.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3{80.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{-1.0F, -1.0F, -1.0F}));
    EXPECT_EQ(harness.positionsB[9U], (Math::Vector3{-2.0F, -2.0F, -2.0F}));
    EXPECT_EQ(harness.a.Stats().deltaSnapshotsSent, 1U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 2U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 4U);
}

TEST(NetworkReplicationTest, Delta_LateRegistrationShipsUnconditionally) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));

    // Registered mid-stream with no baseline: ships on the next tick even though 7 stays
    // quiet, and goes quiet itself the tick after.
    harness.RegisterMirrored(8U);
    harness.positionsA[8U] = Math::Vector3{8.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsB[8U], (Math::Vector3{8.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 1U);

    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 3U);
}

TEST(NetworkReplicationTest, Delta_EpsilonAbsorbsMicroJitter) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(harness.a.RegisterEntity(
        7U, [&harness]() { return harness.positionsA[7U]; },
        [&harness](const Math::Vector3& position) { harness.positionsA[7U] = position; },
        ReplicationAuthority::Owner));
    ASSERT_TRUE(harness.b.RegisterEntity(
        7U, []() { return Math::Vector3{}; },
        [&harness](const Math::Vector3& position) { harness.positionsB[7U] = position; },
        ReplicationAuthority::Proxy));
    harness.positionsA[7U] = Math::Vector3{1.0F, 0.0F, 0.0F};
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 0.0F, 0.0F}));

    // Half a millimetre of jitter: within the epsilon, so the delta stays empty.
    harness.positionsA[7U] = Math::Vector3{1.0005F, 0.0F, 0.0F};
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 1U);

    // A centimetre jump: past the epsilon, so it ships.
    harness.positionsA[7U] = Math::Vector3{1.01F, 0.0F, 0.0F};
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.01F, 0.0F, 0.0F}));

    EXPECT_FALSE(harness.a.SetPositionEpsilon(std::numeric_limits<float>::quiet_NaN()));
    EXPECT_FALSE(harness.a.SetPositionEpsilon(-1.0F));
    EXPECT_EQ(harness.a.PositionEpsilon(), kDefaultDeltaPositionEpsilon);
    // Zero means exact equality: the same half-millimetre now ships.
    EXPECT_TRUE(harness.a.SetPositionEpsilon(0.0F));
    harness.positionsA[7U] = Math::Vector3{1.0105F, 0.0F, 0.0F};
    harness.UpdateBoth(1300U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.0105F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationTest, Delta_PeriodicFullResyncsAfterLoss) {
    // Deltas over lossy mail can strand the peer: the sender's baseline advances on send,
    // so a dropped delta looks delivered until the next full re-baselines the world. This
    // test drops a delta on purpose, watches the divergence sit through an empty delta, and
    // proves the scheduled full heals it.
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(harness.a.RegisterEntity(
        7U, [&harness]() { return harness.positionsA[7U]; },
        [&harness](const Math::Vector3& position) { harness.positionsA[7U] = position; },
        ReplicationAuthority::Owner));
    ASSERT_TRUE(harness.b.RegisterEntity(
        7U, []() { return Math::Vector3{}; },
        [&harness](const Math::Vector3& position) { harness.positionsB[7U] = position; },
        ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriod(3U));
    ASSERT_EQ(harness.a.FullSnapshotPeriod(), 3U);
    ASSERT_TRUE(harness.Establish(1000U));

    harness.positionsA[7U] = Math::Vector3{1.0F, 0.0F, 0.0F};
    harness.a.Update(1100U);
    ASSERT_TRUE(harness.link.b.TryReceive().has_value()); // send #2: dropped on the floor.
    harness.b.Update(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{0.0F, 0.0F, 0.0F}));

    // Send #3 is an empty delta - the sender believes 7 delivered - so the divergence sits.
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{0.0F, 0.0F, 0.0F}));

    // Send #4 is the scheduled full: the peer converges without ever asking.
    harness.UpdateBoth(1300U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 4U);
    EXPECT_EQ(harness.a.Stats().deltaSnapshotsSent, 2U);
}

TEST(NetworkReplicationTest, Delta_PeriodOneIsLegacyFullSnapshots) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    harness.positionsA[7U] = Math::Vector3{5.0F, 5.0F, 5.0F};
    harness.positionsB[7U] = Math::Vector3{5.0F, 5.0F, 5.0F};
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriod(1U));
    ASSERT_TRUE(harness.b.SetFullSnapshotPeriod(1U));
    EXPECT_FALSE(harness.a.SetFullSnapshotPeriod(0U));
    EXPECT_EQ(harness.a.FullSnapshotPeriod(), 1U);

    ASSERT_TRUE(harness.Establish(1000U));
    harness.UpdateBoth(1100U);
    harness.UpdateBoth(1200U);
    // Every send full, every tick applied: the pre-delta behavior, kept as the escape hatch.
    EXPECT_EQ(harness.a.Stats().snapshotsSent, 3U);
    EXPECT_EQ(harness.a.Stats().deltaSnapshotsSent, 0U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 0U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 3U);
}

TEST(NetworkReplicationTest, Delta_UnregisterForgetsBaseline) {
    ReplicationHarness harness = MakeReplicators();
    const auto bindA = [&harness]() {
        return harness.a.RegisterEntity(
            7U, [&harness]() { return harness.positionsA[7U]; },
            [&harness](const Math::Vector3& position) { harness.positionsA[7U] = position; },
            ReplicationAuthority::Owner);
    };
    ASSERT_TRUE(bindA());
    ASSERT_TRUE(harness.b.RegisterEntity(
        7U, []() { return Math::Vector3{}; },
        [&harness](const Math::Vector3& position) { harness.positionsB[7U] = position; },
        ReplicationAuthority::Proxy));
    harness.positionsA[7U] = Math::Vector3{1.0F, 2.0F, 3.0F};
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));

    // Tamper the peer, then rebind the same value: with no baseline it must ship again.
    harness.positionsB[7U] = Math::Vector3{-9.0F, -9.0F, -9.0F};
    ASSERT_TRUE(harness.a.UnregisterEntity(7U));
    ASSERT_TRUE(bindA());
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 2.0F, 3.0F}));
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 0U);
}

TEST(NetworkReplicationTest, Delta_TruncatedEntriesStayDirtyAndConverge) {
    // Before deltas, the losers of a truncation starved forever: every full snapshot carried
    // the same lowest 74 ids. Now the shipped majority goes quiet and the losers surface on
    // the very next tick - truncation composes with deltas into eventual full convergence.
    ReplicationHarness harness = MakeReplicators();
    for (NetworkEntityId id = 0U; id < 80U; ++id) {
        harness.positionsA[id] = Math::Vector3{static_cast<float>(id), 0.0F, 0.0F};
        harness.positionsB[id] = Math::Vector3{-1.0F, -1.0F, -1.0F};
        ASSERT_TRUE(harness.a.RegisterEntity(
            id, [&harness, id]() { return harness.positionsA[id]; },
            [&harness, id](const Math::Vector3& position) { harness.positionsA[id] = position; },
            ReplicationAuthority::Owner));
        ASSERT_TRUE(harness.b.RegisterEntity(
            id, []() { return Math::Vector3{}; },
            [&harness, id](const Math::Vector3& position) { harness.positionsB[id] = position; },
            ReplicationAuthority::Proxy));
    }

    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 6U);
    EXPECT_EQ(harness.positionsB[73U], (Math::Vector3{73.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[74U], (Math::Vector3{-1.0F, -1.0F, -1.0F}));

    harness.UpdateBoth(1100U);
    for (NetworkEntityId id = 0U; id < 80U; ++id) {
        EXPECT_EQ(harness.positionsB[id], (Math::Vector3{static_cast<float>(id), 0.0F, 0.0F}));
    }
    EXPECT_EQ(harness.a.Stats().entriesTruncated, 6U);
    EXPECT_EQ(harness.a.Stats().entriesSkippedUnchanged, 74U);
    EXPECT_EQ(harness.b.Stats().entriesApplied, 80U);
}

TEST(NetworkReplicationTest, Delta_MalformedDeltaFramesIgnored) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTag::Data);

    // Marker with no count.
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU}));
    // Declares one entry, carries none.
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U}));
    // Trailing garbage after a well-formed delta entry.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U,
        0x40U, 0x00U, 0x00U, 0xA0U, 0x40U, 0x00U, 0x00U, 0xC0U, 0x40U, 0xFFU}));
    // Trailing garbage on an empty delta.
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x00U, 0x00U, 0xFFU}));
    // A well-formed empty delta, and one carrying 7 at (4, 5, 6).
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFFU, 0xFFU, 0x00U, 0x00U}));
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0xFFU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x80U,
        0x40U, 0x00U, 0x00U, 0xA0U, 0x40U, 0x00U, 0x00U, 0xC0U, 0x40U}));

    harness.b.Update(1100U);
    EXPECT_EQ(harness.b.Stats().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 3U);
    EXPECT_EQ(harness.b.Stats().deltaSnapshotsReceived, 2U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{4.0F, 5.0F, 6.0F}));
}

TEST(NetworkReplicationTest, Ownership_RequestAcceptFlipsBothSidesAndReversesFlow) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    harness.positionsA[7U] = Math::Vector3{1.0F, 0.0F, 0.0F};
    std::vector<TransferEvent> logA;
    std::vector<TransferEvent> logB;
    // The granter answers reentrantly, straight from the pump: the handshake's natural shape.
    harness.a.SetOwnershipTransferHandler(
        [&harness, &logA](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logA.push_back(TransferEvent{netId, event});
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransfer(netId));
            }
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_EQ(harness.positionsB[7U], (Math::Vector3{1.0F, 0.0F, 0.0F}));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(logA, (std::vector<TransferEvent>{
                        {7U, OwnershipTransferEvent::Requested},
                        {7U, OwnershipTransferEvent::Completed},
                    }));
    EXPECT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Completed}}));
    EXPECT_EQ(harness.a.Stats().transfersCompleted, 1U);
    EXPECT_EQ(harness.b.Stats().transfersRequested, 1U);
    EXPECT_EQ(harness.b.Stats().transfersCompleted, 1U);

    // The flow reverses: B's samples now land on A, and A's sampler goes unheard.
    harness.positionsB[7U] = Math::Vector3{2.0F, 0.0F, 0.0F};
    harness.positionsA[7U] = Math::Vector3{9.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1200U);
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.positionsA[7U], (Math::Vector3{2.0F, 0.0F, 0.0F}));
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{2.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationTest, Ownership_RequestDeniedChangesNothing) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    harness.positionsA[7U] = Math::Vector3{1.0F, 0.0F, 0.0F};
    std::vector<TransferEvent> logA;
    std::vector<TransferEvent> logB;
    harness.a.SetOwnershipTransferHandler(
        [&harness, &logA](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logA.push_back(TransferEvent{netId, event});
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(harness.a.DenyTransfer(netId));
            }
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(logA, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Requested}}));
    EXPECT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Denied}}));
    EXPECT_EQ(harness.a.Stats().transfersDenied, 1U);
    EXPECT_EQ(harness.b.Stats().transfersDenied, 1U);

    // A still owns: its samples still land.
    harness.positionsA[7U] = Math::Vector3{5.0F, 0.0F, 0.0F};
    harness.UpdateBoth(1200U);
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.positionsB[7U], (Math::Vector3{5.0F, 0.0F, 0.0F}));
}

TEST(NetworkReplicationTest, Ownership_RequestRulesRejectBadAsks) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(RegisterOwnerProxy(harness, 8U));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_FALSE(harness.a.RequestOwnership(7U, 1000U)); // Shared asks: no.
    EXPECT_FALSE(harness.a.RequestOwnership(8U, 1000U)); // Owner asks for its own: no.
    EXPECT_FALSE(harness.a.RequestOwnership(99U, 1000U)); // Unknown asks: no.
    EXPECT_TRUE(harness.b.RequestOwnership(8U, 1000U));
    EXPECT_FALSE(harness.b.RequestOwnership(8U, 1000U)); // Single-flight: no.
    EXPECT_EQ(harness.b.Stats().transfersRequested, 1U);
    // The refusals sent nothing: exactly one frame sits on the wire.
    ASSERT_TRUE(harness.link.a.TryReceive().has_value());
    EXPECT_FALSE(harness.link.a.TryReceive().has_value());

    // A down session sends nothing either.
    ReplicationHarness dark = MakeReplicators();
    ASSERT_TRUE(dark.b.RegisterEntity(
        7U, []() { return Math::Vector3{}; }, [](const Math::Vector3&) {},
        ReplicationAuthority::Proxy));
    EXPECT_FALSE(dark.b.RequestOwnership(7U, 1000U));
    EXPECT_EQ(dark.b.Stats().transfersRequested, 0U);
}

TEST(NetworkReplicationTest, Ownership_NonOwnerAndUnknownAutoDeny) {
    // Both sides proxy: neither can grant, so the answer is deny - without ever notifying.
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Proxy));
    std::vector<TransferEvent> logA;
    std::vector<TransferEvent> logB;
    harness.a.SetOwnershipTransferHandler(
        [&logA](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logA.push_back(TransferEvent{netId, event});
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));
    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.UpdateBoth(1100U);
    EXPECT_TRUE(logA.empty());
    EXPECT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Denied}}));
    EXPECT_EQ(harness.a.Stats().transfersDenied, 1U);

    // Unknown entity: no registration, no grant - deny all the same.
    ReplicationHarness stranger = MakeReplicators();
    ASSERT_TRUE(stranger.b.RegisterEntity(
        7U, []() { return Math::Vector3{}; }, [](const Math::Vector3&) {},
        ReplicationAuthority::Proxy));
    std::vector<TransferEvent> strangerLogA;
    std::vector<TransferEvent> strangerLogB;
    stranger.a.SetOwnershipTransferHandler(
        [&strangerLogA](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            strangerLogA.push_back(TransferEvent{netId, event});
        });
    stranger.b.SetOwnershipTransferHandler(
        [&strangerLogB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            strangerLogB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(stranger.Establish(1000U));
    ASSERT_TRUE(stranger.b.RequestOwnership(7U, 1000U));
    stranger.UpdateBoth(1100U);
    EXPECT_TRUE(strangerLogA.empty());
    EXPECT_EQ(strangerLogB,
              (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Denied}}));
    EXPECT_EQ(stranger.a.Stats().transfersDenied, 1U);
}

TEST(NetworkReplicationTest, Ownership_NoHandlerAutoDenies) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logB;
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    // A never set a handler: its requests cannot be decided, so they are denied for it.
    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Denied}}));
    EXPECT_EQ(harness.a.Stats().transfersDenied, 1U);
}

TEST(NetworkReplicationTest, Ownership_RequestTimesOutAndMayRetry) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logA;
    std::vector<TransferEvent> logB;
    // A records but never answers: silence the requester must survive.
    harness.a.SetOwnershipTransferHandler(
        [&logA](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logA.push_back(TransferEvent{netId, event});
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.UpdateBoth(1100U);
    ASSERT_EQ(logA, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::Requested}}));
    harness.UpdateBoth(1500U);
    EXPECT_TRUE(logB.empty());
    EXPECT_FALSE(harness.b.RequestOwnership(7U, 1500U)); // Still pending: no re-ask.
    harness.UpdateBoth(2000U); // Deadline inclusive: 2000 - 1000 >= 1000 expires.
    EXPECT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::TimedOut}}));
    EXPECT_EQ(harness.b.Stats().transfersTimedOut, 1U);

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 2000U));
    EXPECT_EQ(harness.b.Stats().transfersRequested, 2U);
    harness.UpdateBoth(2100U); // The retry re-notifies the undecided granter.
    EXPECT_EQ(logA.size(), 2U);
    EXPECT_EQ(logB.size(), 1U);
}

TEST(NetworkReplicationTest, Ownership_StaleAcceptAfterTimeoutIsIgnored) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logB;
    harness.a.SetOwnershipTransferHandler(
        [&harness](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransfer(netId));
            }
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    harness.a.Update(1100U); // A answers; the accept never leaves the tap.
    const std::optional<std::vector<std::uint8_t>> accept = harness.link.b.TryReceive();
    ASSERT_TRUE(accept.has_value());
    harness.b.Update(1100U);
    harness.b.Update(2001U); // The request dies unanswered...
    ASSERT_EQ(logB, (std::vector<TransferEvent>{{7U, OwnershipTransferEvent::TimedOut}}));
    ASSERT_TRUE(harness.link.a.Send(*accept)); // ...then the late accept arrives.
    harness.b.Update(2002U);
    EXPECT_EQ(harness.b.Stats().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.b.Stats().transfersCompleted, 0U);
    EXPECT_EQ(logB.size(), 1U); // No Completed: the flip never happened.
}

TEST(NetworkReplicationTest, Ownership_ManualRebindClearsPending) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logB;
    harness.a.SetOwnershipTransferHandler(
        [&harness](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransfer(netId));
            }
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Owner)); // Takes the wheel.
    harness.UpdateBoth(1100U);
    // A's accept lands on no open request: stale, and B's log stays silent. (A did its
    // half anyway, so both sides agree B owns - the override and the late answer converge.)
    EXPECT_TRUE(logB.empty());
    EXPECT_EQ(harness.b.Stats().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.a.Stats().transfersCompleted, 1U);
}

TEST(NetworkReplicationTest, Ownership_UnregisterClearsPending) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logB;
    harness.a.SetOwnershipTransferHandler(
        [&harness](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            if (event == OwnershipTransferEvent::Requested) {
                EXPECT_TRUE(harness.a.AcceptTransfer(netId));
            }
        });
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.b.RequestOwnership(7U, 1000U));
    ASSERT_TRUE(harness.b.UnregisterEntity(7U));
    harness.UpdateBoth(1100U);
    EXPECT_TRUE(logB.empty());
    EXPECT_EQ(harness.b.Stats().transfersIgnoredStale, 1U);
}

TEST(NetworkReplicationTest, Ownership_AnswerAndTimeoutRules) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_FALSE(harness.a.AcceptTransfer(7U)); // Nothing awaiting: no.
    EXPECT_FALSE(harness.a.DenyTransfer(7U));
    EXPECT_FALSE(harness.a.AcceptTransfer(99U));
    EXPECT_FALSE(harness.a.DenyTransfer(99U));
    EXPECT_FALSE(harness.a.SetOwnershipTimeout(0U));
    EXPECT_EQ(harness.a.OwnershipTimeout(), kDefaultOwnershipTimeoutMs);
    EXPECT_TRUE(harness.a.SetOwnershipTimeout(500U));
    EXPECT_EQ(harness.a.OwnershipTimeout(), 500U);
}

TEST(NetworkReplicationTest, Ownership_MalformedControlFramesIgnored) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.Establish(1000U));
    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTag::Data);

    // Marker alone.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU}));
    // Kind without an entity.
    ASSERT_TRUE(
        harness.link.a.Send(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x01U}));
    // Unknown kind, right size.
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x04U, 0x07U, 0x00U, 0x00U, 0x00U}));
    // Trailing byte on a well-formed deny.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x03U,
                                                                0x07U, 0x00U, 0x00U, 0x00U, 0xFFU}));
    // Well-formed deny with nothing open: stale, not malformed.
    ASSERT_TRUE(harness.link.a.Send(
        std::vector<std::uint8_t>{dataTag, 0xFEU, 0xFFU, 0x03U, 0x07U, 0x00U, 0x00U, 0x00U}));

    harness.b.Update(1100U);
    EXPECT_EQ(harness.b.Stats().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.Stats().transfersIgnoredStale, 1U);
    EXPECT_EQ(harness.b.Stats().snapshotsReceived, 1U); // Control frames are not snapshots.
}

// Captures received property values by (entity, property), with per-pair call counts.
struct PropertySink final {
    std::map<std::pair<NetworkEntityId, ReplicationPropertyId>, Blueprint::Value> received;
    std::map<std::pair<NetworkEntityId, ReplicationPropertyId>, int> calls;

    [[nodiscard]] PropertyApplier ApplierFor(const NetworkEntityId netId,
                                                const ReplicationPropertyId propId) {
        return [this, netId, propId](const Blueprint::Value& value) {
            received[{netId, propId}] = value;
            ++calls[{netId, propId}];
        };
    }
};

TEST(NetworkReplicationTest, Property_RoundTripsToApplier) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{83}}));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(sink.received.at({7U, 3U}), Blueprint::Value{std::int64_t{83}});
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.Stats().propertyFramesReceived, 1U);
    EXPECT_EQ(harness.b.Stats().propertiesApplied, 1U);
    EXPECT_EQ(harness.b.Stats().propertyFramesSent, 0U); // Nothing dirty, no frame.
}

TEST(NetworkReplicationTest, Property_RegistrationGuards) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;

    EXPECT_FALSE(harness.b.RegisterProperty(9U, 3U, sink.ApplierFor(9U, 3U)));
    EXPECT_FALSE(harness.b.RegisterProperty(7U, 3U, PropertyApplier{}));
    EXPECT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_FALSE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_FALSE(harness.b.UnregisterProperty(7U, 4U));
    EXPECT_FALSE(
        harness.a.SetProperty(9U, 3U, Blueprint::Value{std::int64_t{1}}));
    EXPECT_FALSE(harness.a.SetProperty(
        7U, 3U, Blueprint::Value{Blueprint::ObjectRef{42U}}));
    EXPECT_FALSE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::string(65536U, 'x')}));
    EXPECT_FALSE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::string(2000U, 'y')}));
    EXPECT_EQ(harness.a.Stats().propertiesSkippedOversized, 3U);
}

TEST(NetworkReplicationTest, Property_UnchangedSetDoesNotResend) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.Establish(1000U));

    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{83}}));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{83}}));
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U); // Byte-identical: no re-ship.
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{84}}));
    harness.UpdateBoth(1300U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 2U);
    EXPECT_EQ(harness.b.Stats().propertiesApplied, 2U);
    EXPECT_EQ(sink.received.at({7U, 3U}), Blueprint::Value{std::int64_t{84}});
}

TEST(NetworkReplicationTest, Property_FullTickRecarriesClean) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetFullSnapshotPeriod(1U));
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{83}}));
    ASSERT_TRUE(harness.Establish(1000U));
    harness.UpdateBoth(1100U);

    // Every tick is full, so the clean value re-carries: at-least-once, like positions.
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 2U);
    EXPECT_EQ(harness.b.Stats().propertiesApplied, 2U);
    EXPECT_EQ(sink.calls.at({7U, 3U}), 2);
}

TEST(NetworkReplicationTest, Property_ProxyHoldsUntilFlip) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    PropertySink sink;
    ASSERT_TRUE(harness.a.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    EXPECT_TRUE(harness.b.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{11}}));
    ASSERT_TRUE(harness.Establish(1000U));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.b.Stats().propertyFramesSent, 0U);

    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Owner));
    harness.UpdateBoth(1200U);
    harness.a.Update(1200U); // Deliver B's last send; A's cadence just ran, so A sends nothing.
    EXPECT_EQ(harness.b.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(sink.received.at({7U, 3U}), Blueprint::Value{std::int64_t{11}});
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 0U);
}

TEST(NetworkReplicationTest, Property_OwnerIgnoresInbound) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Owner));
    PropertySink sink;
    ASSERT_TRUE(harness.a.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.b.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{5}}));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(harness.a.Stats().propertiesIgnoredOwned, 1U);
    EXPECT_EQ(harness.a.Stats().propertiesApplied, 0U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationTest, Property_UnknownPairsIgnored) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    ASSERT_TRUE(harness.a.RegisterEntity(
        9U, [&harness]() { return harness.positionsA[9U]; },
        [&harness](const Math::Vector3& position) { harness.positionsA[9U] = position; }));
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 6U, sink.ApplierFor(7U, 6U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 5U, Blueprint::Value{std::int64_t{1}}));
    ASSERT_TRUE(harness.a.SetProperty(9U, 5U, Blueprint::Value{std::int64_t{2}}));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(harness.b.Stats().propertyFramesReceived, 1U);
    EXPECT_EQ(harness.b.Stats().propertiesIgnoredUnknown, 2U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationTest, Property_IrrelevantSkipsAndRunsPredicateOnce) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.Establish(1000U));

    int relevancyCalls = 0;
    bool relevant = false;
    ASSERT_TRUE(harness.a.SetRelevancy(7U, [&relevancyCalls, &relevant]() {
        ++relevancyCalls;
        return relevant;
    }));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{9}}));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 0U);
    EXPECT_EQ(relevancyCalls, 1);
    relevant = true;
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(relevancyCalls, 2);
    EXPECT_EQ(sink.received.at({7U, 3U}), Blueprint::Value{std::int64_t{9}});
}

TEST(NetworkReplicationTest, Property_TruncationPacksInOrderAndLosersSurface) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 1U, sink.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 2U, sink.ApplierFor(7U, 2U)));
    const Blueprint::Value first{std::string(600U, 'a')};
    const Blueprint::Value second{std::string(600U, 'b')};
    ASSERT_TRUE(harness.a.SetProperty(7U, 1U, first));
    ASSERT_TRUE(harness.a.SetProperty(7U, 2U, second));
    ASSERT_TRUE(harness.Establish(1000U));
    harness.b.Update(1000U); // Deliver A's sends without advancing B's cadence.

    EXPECT_EQ(sink.received.at({7U, 1U}), first);
    EXPECT_EQ(sink.received.count({7U, 2U}), 0U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(harness.a.Stats().propertiesTruncated, 1U);
    harness.UpdateBoth(1100U);
    EXPECT_EQ(sink.received.at({7U, 2U}), second);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 2U);
}

TEST(NetworkReplicationTest, Property_UnregisterEntityDropsRow) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{1}}));
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);

    EXPECT_TRUE(harness.a.UnregisterEntity(7U));
    harness.RegisterMirrored(7U); // A re-registers; B's duplicate is ignored.
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U); // Cache dropped: nothing to send.
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{2}}));
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 2U);
    EXPECT_EQ(sink.received.at({7U, 3U}), Blueprint::Value{std::int64_t{2}});
}

TEST(NetworkReplicationTest, Property_MalformedFramesIgnoredWhole) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.Establish(1000U));

    const std::uint8_t dataTag = static_cast<std::uint8_t>(SessionMessageTag::Data);
    // Count declared, entry cut before the property id.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U}));
    // Well-formed entry plus a trailing byte: the whole frame goes, applied nothing.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x03U, 0x02U, 0x09U,
        0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0x00U, 0xFFU}));
    // Unknown value tag.
    ASSERT_TRUE(harness.link.a.Send(std::vector<std::uint8_t>{
        dataTag, 0xFDU, 0xFFU, 0x01U, 0x00U, 0x07U, 0x00U, 0x00U, 0x00U, 0x03U, 0x09U}));
    // Marker alone, no count.
    ASSERT_TRUE(
        harness.link.a.Send(std::vector<std::uint8_t>{dataTag, 0xFDU, 0xFFU}));

    harness.b.Update(1100U);
    EXPECT_EQ(harness.b.Stats().messagesIgnoredMalformed, 4U);
    EXPECT_EQ(harness.b.Stats().propertyFramesReceived, 0U);
    EXPECT_EQ(harness.b.Stats().propertiesApplied, 0U);
    EXPECT_TRUE(sink.received.empty());
}

TEST(NetworkReplicationTest, Property_FlipReversesDirection) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    PropertySink sinkA;
    PropertySink sinkB;
    ASSERT_TRUE(harness.a.RegisterProperty(7U, 3U, sinkA.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sinkB.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{21}}));
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(sinkB.received.at({7U, 3U}), Blueprint::Value{std::int64_t{21}});

    ASSERT_TRUE(harness.a.SetAuthority(7U, ReplicationAuthority::Proxy));
    ASSERT_TRUE(harness.b.SetAuthority(7U, ReplicationAuthority::Owner));
    ASSERT_TRUE(harness.b.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{22}}));
    harness.UpdateBoth(1100U);
    harness.a.Update(1100U); // Deliver B's last send; A's cadence just ran, so A sends nothing.
    EXPECT_EQ(sinkA.received.at({7U, 3U}), Blueprint::Value{std::int64_t{22}});
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.Stats().propertiesIgnoredOwned, 0U);
}

TEST(NetworkReplicationTest, Property_SharedBothDirections) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sinkA;
    PropertySink sinkB;
    ASSERT_TRUE(harness.a.RegisterProperty(7U, 1U, sinkA.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 1U, sinkB.ApplierFor(7U, 1U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 1U, Blueprint::Value{std::int64_t{100}}));
    ASSERT_TRUE(harness.b.SetProperty(7U, 1U, Blueprint::Value{std::int64_t{200}}));
    ASSERT_TRUE(harness.Establish(1000U));

    EXPECT_EQ(sinkA.received.at({7U, 1U}), Blueprint::Value{std::int64_t{200}});
    EXPECT_EQ(sinkB.received.at({7U, 1U}), Blueprint::Value{std::int64_t{100}});
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 1U);
    EXPECT_EQ(harness.b.Stats().propertyFramesSent, 1U);
}

TEST(NetworkReplicationTest, Property_UnregisterPropertyStopsBothWays) {
    ReplicationHarness harness = MakeReplicators();
    harness.RegisterMirrored(7U);
    PropertySink sink;
    ASSERT_TRUE(harness.b.RegisterProperty(7U, 3U, sink.ApplierFor(7U, 3U)));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{1}}));
    ASSERT_TRUE(harness.Establish(1000U));
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);

    EXPECT_TRUE(harness.b.UnregisterProperty(7U, 3U));
    ASSERT_TRUE(harness.a.SetProperty(7U, 3U, Blueprint::Value{std::int64_t{2}}));
    harness.UpdateBoth(1100U);
    EXPECT_EQ(harness.b.Stats().propertiesIgnoredUnknown, 1U);
    EXPECT_EQ(sink.calls.at({7U, 3U}), 1);
    EXPECT_TRUE(harness.a.UnregisterProperty(7U, 3U)); // The cached value goes too.
    harness.UpdateBoth(1200U);
    EXPECT_EQ(harness.a.Stats().propertyFramesSent, 2U);
    EXPECT_FALSE(harness.a.UnregisterProperty(7U, 3U));
}

TEST(NetworkReplicationTest, Ownership_EarlierClockUpdateDoesNotUnderflowTimeout) {
    ReplicationHarness harness = MakeReplicators();
    ASSERT_TRUE(RegisterOwnerProxy(harness, 7U));
    std::vector<TransferEvent> logB;
    harness.b.SetOwnershipTransferHandler(
        [&logB](const NetworkEntityId netId, const OwnershipTransferEvent event) {
            logB.push_back(TransferEvent{netId, event});
        });
    ASSERT_TRUE(harness.Establish(2000U));

    // Request recorded at t = 2000 ms; calling Update with an earlier timestamp must not underflow.
    ASSERT_TRUE(harness.b.RequestOwnership(7U, 2000U));
    harness.b.Update(1500U);
    EXPECT_TRUE(logB.empty());
    EXPECT_EQ(harness.b.Stats().transfersTimedOut, 0U);
}

} // namespace
} // namespace UVE::Network::Tests
