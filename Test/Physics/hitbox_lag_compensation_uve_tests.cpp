// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_lag_compensation_uve.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/hitbox_3d_uve.h"
#include "uve/objects/3d/hurtbox_3d_uve.h"
#include "uve/physics/hitbox_strike_lifecycle_tracker_uve.h"
#include "uve/physics/hitbox_strike_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::EntityUVE;
using Scene::Hitbox3DComponentUVE;
using Scene::Hurtbox3DComponentUVE;
using Scene::kInvalidEntityUVE;

constexpr float kEpsilon = 1e-4F;

class HitboxLagCompensationUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;
    HitboxLagCompensatorUVE compensator;

    [[nodiscard]] EntityUVE MakeHitboxUVE(const Math::Vector3UVE& position,
                                          const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee") {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hitbox3DComponentUVE hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hitbox3DComponentUVE>(entity, hitbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeHurtboxUVE(const Math::Vector3UVE& position,
                                           const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee") {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hurtbox3DComponentUVE hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(entity, hurtbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    void MoveUVE(const EntityUVE entity, const Math::Vector3UVE& position) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    void RotateUVE(const EntityUVE entity, const Math::QuaternionUVE& rotation) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localRotation = rotation;
        transform.rotationEditMode = Scene::RotationEditModeUVE::Quaternion;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }

    void ScaleUVE(const EntityUVE entity, const Math::Vector3UVE& scale) {
        Scene::TransformComponentUVE transform =
            entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localScale = scale;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }
};

TEST_F(HitboxLagCompensationUVETest, Record_RejectsNonIncreasingTicks) {
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(compensator.RecordUVE(5U, entityManager));
    EXPECT_FALSE(compensator.RecordUVE(5U, entityManager));
    EXPECT_FALSE(compensator.RecordUVE(3U, entityManager));
    EXPECT_TRUE(compensator.RecordUVE(6U, entityManager));
    EXPECT_EQ(compensator.HistorySizeUVE(), 2U);
    EXPECT_EQ(compensator.StatsUVE().snapshotsRecorded, 2U);
    EXPECT_EQ(compensator.StatsUVE().recordsRejected, 2U);
    EXPECT_EQ(compensator.StatsUVE().snapshotsEvicted, 0U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_HitsWhereTheTargetWas) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});

    // The control: what the live scan saw before anything moved.
    const Hitbox3DSyncReportUVE control = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(control.strikes.size(), 1U);
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    MoveUVE(target, {10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The present misses - the rewind must not.
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
    const LagShotReportUVE rewound = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_EQ(rewound.pairs, control.strikes);
    EXPECT_EQ(rewound.resolvedTick, 1U);
    EXPECT_FALSE(rewound.clamped);
    EXPECT_EQ(rewound.candidatesConsidered, 1U);
    ASSERT_EQ(rewound.pairs.size(), 1U);
    EXPECT_EQ(rewound.pairs[0].hurtbox, target);
    EXPECT_EQ(compensator.StatsUVE().pairsFound, 1U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_AgainstThePresentAgreesWithTheLiveScan) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    const Hitbox3DSyncReportUVE live = SyncHitboxes3DUVE(entityManager);
    const LagShotReportUVE rewound = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_EQ(rewound.pairs, live.strikes);
    EXPECT_FALSE(rewound.clamped);

    // A target newer than the newest snapshot is the future: nearest survivor, flagged.
    const LagShotReportUVE future = compensator.ResolveShotUVE(shooter, 99U, entityManager);
    EXPECT_EQ(future.pairs, live.strikes);
    EXPECT_EQ(future.resolvedTick, 1U);
    EXPECT_TRUE(future.clamped);
    EXPECT_EQ(compensator.StatsUVE().resolvesClamped, 1U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_ClampsTargetsOlderThanHistory) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(3U, entityManager));

    // Tick 0 predates everything: the oldest survivor answers, flagged.
    const LagShotReportUVE clamped = compensator.ResolveShotUVE(shooter, 0U, entityManager);
    EXPECT_EQ(clamped.resolvedTick, 1U);
    EXPECT_TRUE(clamped.clamped);
    ASSERT_EQ(clamped.pairs.size(), 1U);

    // No history at all: nothing to resolve against.
    HitboxLagCompensatorUVE empty;
    const LagShotReportUVE none = empty.ResolveShotUVE(shooter, 5U, entityManager);
    EXPECT_TRUE(none.pairs.empty());
    EXPECT_EQ(none.resolvedTick, 0U);
    EXPECT_TRUE(none.clamped);
}

TEST_F(HitboxLagCompensationUVETest, Shot_FailsClosedWithoutARecordedArmedShooter) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    EXPECT_TRUE(compensator.ResolveShotUVE(kInvalidEntityUVE, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShotUVE(target, 1U, entityManager).pairs.empty());

    Scene::Hitbox3DComponentUVE& hitbox =
        entityManager.GetComponentUVE<Hitbox3DComponentUVE>(shooter);
    hitbox.enabled = false;
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());
    hitbox.enabled = true;

    // Recorded history predates this shooter: it was never posed, so there is no past pose.
    const EntityUVE late = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    EXPECT_TRUE(compensator.ResolveShotUVE(late, 1U, entityManager).pairs.empty());

    // The dead keep their poses but lose their hurtboxes: destroying the target empties the
    // resolve without touching history.
    entityManager.DestroyEntityUVE(target);
    const LagShotReportUVE bereaved = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_TRUE(bereaved.pairs.empty());
    EXPECT_EQ(bereaved.candidatesConsidered, 0U);
    EXPECT_EQ(bereaved.resolvedTick, 1U);

    // And a destroyed shooter shoots nothing.
    entityManager.DestroyEntityUVE(shooter);
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationUVETest, Shot_ReadsVulnerabilityLive) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // I-frames started after the shot still count: only positions rewind, and the target
    // with armor now was armored for the rewind too.
    Scene::Hurtbox3DComponentUVE& hurtbox =
        entityManager.GetComponentUVE<Scene::Hurtbox3DComponentUVE>(target);
    hurtbox.enabled = false;
    const LagShotReportUVE armored = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_TRUE(armored.pairs.empty());
    EXPECT_EQ(armored.candidatesConsidered, 0U);

    hurtbox.enabled = true;
    EXPECT_EQ(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.size(), 1U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_IgnoresDecoysOnOtherChannels) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile"));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // Considered (vulnerable, posed) but rejected: the acceptance gates rewind with the rest.
    const LagShotReportUVE rewound = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_TRUE(rewound.pairs.empty());
    EXPECT_EQ(rewound.candidatesConsidered, 1U);
}

TEST_F(HitboxLagCompensationUVETest, Ray_HitsWhereTheTargetWas) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {5.0F, 5.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // From x = -5 along +X: the tick-1 box face at x = 4 is nine metres down the ray.
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReportUVE hit = compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(hit.entity, target);
    EXPECT_NEAR(hit.distance, 9.0F, kEpsilon);
    EXPECT_NEAR(hit.point.x, 4.0F, kEpsilon);
    EXPECT_NEAR(hit.point.y, 0.0F, kEpsilon);
    EXPECT_NEAR(hit.point.z, 0.0F, kEpsilon);
    EXPECT_EQ(hit.resolvedTick, 1U);
    EXPECT_FALSE(hit.clamped);

    // At tick 2 the target stepped off the line: the same ray misses.
    const LagRayReportUVE miss = compensator.ResolveRayUVE(shooter, ray, 20.0F, 2U, entityManager);
    EXPECT_EQ(miss.entity, kInvalidEntityUVE);
    EXPECT_EQ(miss.distance, 0.0F);

    // A doubled direction is normalized, not doubled: same hit, same metres.
    const Math::RayUVE fat{{-5.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}};
    const LagRayReportUVE normalized =
        compensator.ResolveRayUVE(shooter, fat, 20.0F, 1U, entityManager);
    EXPECT_EQ(normalized.entity, target);
    EXPECT_NEAR(normalized.distance, 9.0F, kEpsilon);

    // Degenerate rays resolve empty, never crash.
    const Math::RayUVE zero{{-5.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, zero, 20.0F, 1U, entityManager).entity,
              kInvalidEntityUVE);
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, ray, 0.0F, 1U, entityManager).entity,
              kInvalidEntityUVE);
}

TEST_F(HitboxLagCompensationUVETest, History_EvictsOldestPastTheCap) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    for (LagTickUVE tick = 1U; tick <= 70U; ++tick) {
        ASSERT_TRUE(compensator.RecordUVE(tick, entityManager));
    }
    EXPECT_EQ(compensator.HistorySizeUVE(), kLagHistoryMaximumTicksUVE);
    EXPECT_EQ(compensator.StatsUVE().snapshotsRecorded, 70U);
    EXPECT_EQ(compensator.StatsUVE().snapshotsEvicted, 6U);

    // Tick 1 aged out: tick 7 is the oldest survivor and answers, flagged.
    const LagShotReportUVE evicted = compensator.ResolveShotUVE(shooter, 1U, entityManager);
    EXPECT_EQ(evicted.resolvedTick, 7U);
    EXPECT_TRUE(evicted.clamped);
    EXPECT_EQ(evicted.pairs.size(), 1U);
}

TEST_F(HitboxLagCompensationUVETest, MergedReport_EntersOnceWithoutSpuriousExits) {
    const EntityUVE liveShooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(liveShooter);
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    const EntityUVE laggedShooter = MakeHitboxUVE({10.0F, 0.0F, 0.0F});
    const EntityUVE laggedTarget = MakeHurtboxUVE({11.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    const Hitbox3DSyncReportUVE first = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(first.strikes.size(), 2U);
    Hitbox3DStrikeLifecycleTrackerUVE merged;
    Hitbox3DStrikeLifecycleTrackerUVE unmerged;
    EXPECT_EQ(merged.UpdateUVE(first).transitions.size(), 2U);
    EXPECT_EQ(unmerged.UpdateUVE(first).transitions.size(), 2U);

    MoveUVE(laggedTarget, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));
    const Hitbox3DSyncReportUVE live = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(live.strikes.size(), 1U);

    // The lagged shooter's victim is gone from the present but not from tick 1.
    const LagShotReportUVE compensated =
        compensator.ResolveShotUVE(laggedShooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    EXPECT_EQ(compensated.pairs[0], first.strikes[1]);

    // Merged before the diff: no edges at all - the live pair persists, the compensated pair
    // was already active, and a replayed merge stays silent too.
    Hitbox3DSyncReportUVE both = live;
    both.strikes.insert(both.strikes.end(), compensated.pairs.begin(), compensated.pairs.end());
    EXPECT_TRUE(merged.UpdateUVE(both).transitions.empty());
    EXPECT_EQ(merged.GetActiveCountUVE(), 2U);
    EXPECT_TRUE(merged.UpdateUVE(both).transitions.empty());

    // The contrast: feeding the live report alone exits the lagged pair - which is exactly
    // why production merges instead of updating twice.
    const Hitbox3DStrikeLifecycleReportUVE bare = unmerged.UpdateUVE(live);
    ASSERT_EQ(bare.transitions.size(), 1U);
    EXPECT_EQ(bare.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Exited);
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_ZeroFractionMatchesIntegerResolve) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Fraction 0 is the integer spelling with another name: same pairs, same flags.
    EXPECT_EQ(compensator.ResolveShotAtUVE(shooter, 1U, 0.0F, entityManager),
              compensator.ResolveShotUVE(shooter, 1U, entityManager));
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRayAtUVE(shooter, ray, 20.0F, 1U, 0.0F, entityManager),
              compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager));
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_HalfTickFindsMidpointVictim) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {-4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Neither snapshot overlaps: the target teleported clean through between records.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 2U, entityManager).pairs.empty());

    // Halfway it stood inside the shooter: the blend hits where both floors miss.
    const LagShotReportUVE between =
        compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_EQ(between.resolvedTick, 1U);
    EXPECT_FALSE(between.clamped);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_RotationMidpointOnly) {
    const EntityUVE shooter = MakeHitboxUVE({3.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({0.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    // A long box spinning 90 degrees about Y between records: yaw-90 as cos/sin of half
    // the angle, authored in Quaternion mode so the graph keeps it.
    RotateUVE(target, Math::QuaternionUVE{0.0F, 0.70710678F, 0.0F, 0.70710678F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Tick 1 only touches the shooter at x = 2 (not a strike); tick 2 points down Z, clear.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 2U, entityManager).pairs.empty());

    // At 45 degrees the corner reaches x = 2.12: the slerp midpoint overlaps.
    const LagShotReportUVE between =
        compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_FractionOutOfRangeFailsClosed) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Garbage fractions miss without consulting history - even 1.0, which names the next
    // tick and must be spelled that way instead.
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), -0.25F, 1.0F, 1.5F};
    for (const float fraction : bad) {
        const LagShotReportUVE shot =
            compensator.ResolveShotAtUVE(shooter, 1U, fraction, entityManager);
        EXPECT_TRUE(shot.pairs.empty());
        EXPECT_EQ(shot.resolvedTick, 0U);
        EXPECT_FALSE(shot.clamped);
        EXPECT_FALSE(shot.interpolated);
        const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
        const LagRayReportUVE beam =
            compensator.ResolveRayAtUVE(shooter, ray, 20.0F, 1U, fraction, entityManager);
        EXPECT_EQ(beam.entity, kInvalidEntityUVE);
        EXPECT_EQ(beam.resolvedTick, 0U);
        EXPECT_FALSE(beam.interpolated);
    }
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_MissingCeilFallsBackToFloor) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Tick 2 is the newest: no ceiling exists, so the fraction is ignored, not invented.
    const LagShotReportUVE answered =
        compensator.ResolveShotAtUVE(shooter, 2U, 0.5F, entityManager);
    ASSERT_EQ(answered.pairs.size(), 1U);
    EXPECT_EQ(answered.resolvedTick, 2U);
    EXPECT_FALSE(answered.clamped);
    EXPECT_FALSE(answered.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_NonConsecutiveTicksFallBack) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));
    MoveUVE(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(5U, entityManager));

    // Ticks 3 and 4 were never recorded: tick 2 has no ceiling, so no blend - while the
    // consecutive pair below still interpolates, proving the gap is seen, not skipped.
    const LagShotReportUVE gapped =
        compensator.ResolveShotAtUVE(shooter, 2U, 0.5F, entityManager);
    ASSERT_EQ(gapped.pairs.size(), 1U);
    EXPECT_EQ(gapped.resolvedTick, 2U);
    EXPECT_FALSE(gapped.interpolated);
    const LagShotReportUVE blended =
        compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(blended.pairs.size(), 1U);
    EXPECT_TRUE(blended.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_ClampedFloorFallsBack) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(5U, entityManager));

    // The floor (5) is not the tick (7): a clamped floor has no bracket to blend.
    const LagShotReportUVE answered =
        compensator.ResolveShotAtUVE(shooter, 7U, 0.5F, entityManager);
    ASSERT_EQ(answered.pairs.size(), 1U);
    EXPECT_EQ(answered.resolvedTick, 5U);
    EXPECT_TRUE(answered.clamped);
    EXPECT_FALSE(answered.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, RayAt_HalfTickFindsMidpointVictim) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 3.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    MoveUVE(target, {5.0F, -3.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The ray runs down y = 0: both snapshots hold the target three metres off the line.
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager).entity,
              kInvalidEntityUVE);
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntityUVE);

    // Halfway the box sits on the line: face at x = 4, nine metres down the ray.
    const LagRayReportUVE between =
        compensator.ResolveRayAtUVE(shooter, ray, 20.0F, 1U, 0.5F, entityManager);
    EXPECT_EQ(between.entity, target);
    EXPECT_NEAR(between.distance, 9.0F, kEpsilon);
    EXPECT_EQ(between.resolvedTick, 1U);
    EXPECT_FALSE(between.clamped);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, Stats_InterpolationsCounted) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    EXPECT_EQ(compensator.StatsUVE().resolvesInterpolated, 0U);
    static_cast<void>(compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager));
    EXPECT_EQ(compensator.StatsUVE().resolvesInterpolated, 1U);
    // Floor fallbacks are plain resolves: newest tick, no ceiling, no count.
    static_cast<void>(compensator.ResolveShotAtUVE(shooter, 2U, 0.5F, entityManager));
    EXPECT_EQ(compensator.StatsUVE().resolvesInterpolated, 1U);
    EXPECT_EQ(compensator.StatsUVE().shotsResolved, 2U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_ScaleRewindsNotLive) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ScaleUVE(target, {0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Shrunk to [1.4, 1.6]: tick 2 agrees with the geometry (a miss), while the live scan
    // - which ignores world scale - still reports the overlap the geometry left behind.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 2U, entityManager).pairs.empty());
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);

    // But tick 1 recorded full size: the rewound shot hits what the shrunken one cannot.
    const LagShotReportUVE past =
        compensator.ResolveShotUVE(shooter, 1U, entityManager);
    ASSERT_EQ(past.pairs.size(), 1U);
    EXPECT_EQ(past.pairs[0].hurtbox, target);
    EXPECT_EQ(past.resolvedTick, 1U);
}

TEST_F(HitboxLagCompensationUVETest, Shot_ShooterScaleRewinds) {
    const EntityUVE shooter = MakeHitboxUVE({2.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({0.0F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ScaleUVE(shooter, {2.0F, 2.0F, 2.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Doubled to [0, 4]: tick 2 agrees with the geometry (a hit), while the live scan -
    // which ignores world scale - still sees the tick-1 touch and stays silent.
    ASSERT_EQ(compensator.ResolveShotUVE(shooter, 2U, entityManager).pairs.size(), 1U);
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());

    // At tick 1 the shooter only touched at x = 1: the rewound swing misses.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationUVETest, ShotAt_ScaleInterpolates) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ScaleUVE(target, {0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ScaleUVE(target, {3.0F, 3.0F, 3.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // The floor (scale 0.1, box [1.4, 1.6]) misses on its own.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());

    // Halfway the scale is 1.55, the box [-0.05, 3.05]: overlap depth exactly 1.05, which
    // only the blended size produces (floor misses, ceiling would run 2.0 deep).
    const LagShotReportUVE between =
        compensator.ResolveShotAtUVE(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_NEAR(between.pairs[0].penetrationDepth, 1.05F, kEpsilon);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationUVETest, Ray_ScaleRewinds) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ScaleUVE(target, {0.5F, 0.5F, 0.5F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Grazing down y = 0.9: inside the full-size box, above the shrunk one.
    const Math::RayUVE ray{{-5.0F, 0.9F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReportUVE past =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(past.entity, target);
    EXPECT_NEAR(past.distance, 9.0F, kEpsilon);
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntityUVE);
}

TEST_F(HitboxLagCompensationUVETest, Shot_ZeroScaleUntargetable) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ScaleUVE(target, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // Zero size at an overlapping pose: no hit, no crash.
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 1U, entityManager).pairs.empty());

    // The mirror case: a zero-scale attacker swings nothing either.
    ScaleUVE(target, {1.0F, 1.0F, 1.0F});
    ScaleUVE(shooter, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));
    EXPECT_TRUE(compensator.ResolveShotUVE(shooter, 2U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationUVETest, Shot_NegativeScaleMirrors) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ScaleUVE(target, {-1.0F, -1.0F, -1.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));

    // A mirror occupies the same box: the rewound size takes the absolute scale.
    const LagShotReportUVE mirrored =
        compensator.ResolveShotUVE(shooter, 1U, entityManager);
    ASSERT_EQ(mirrored.pairs.size(), 1U);
    EXPECT_EQ(mirrored.pairs[0].hurtbox, target);
}

TEST_F(HitboxLagCompensationUVETest, Ray_ZeroScaleMisses) {
    const EntityUVE shooter = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE target = MakeHurtboxUVE({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(1U, entityManager));
    ScaleUVE(target, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.RecordUVE(2U, entityManager));

    // Full size on the line hits at nine metres; shrunk to a point, the ray passes through.
    const Math::RayUVE ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReportUVE past =
        compensator.ResolveRayUVE(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(past.entity, target);
    EXPECT_NEAR(past.distance, 9.0F, kEpsilon);
    EXPECT_EQ(compensator.ResolveRayUVE(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntityUVE);
}

} // namespace
} // namespace UVE::Physics::Tests
