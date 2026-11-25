// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_lag_compensation.h"

#include <cstdint>
#include <limits>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "transform_component.h"
#include "world_transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "quaternion.h"
#include "vector3.h"
#include "memory_manager.h"
#include "hitbox_3d.h"
#include "hurtbox_3d.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "hitbox_strike.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Entity;
using Scene::Hitbox3DComponent;
using Scene::Hurtbox3DComponent;
using Scene::kInvalidEntity;

constexpr float kEpsilon = 1e-4F;

class HitboxLagCompensationTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;
    HitboxLagCompensator compensator;

    [[nodiscard]] Entity MakeHitbox(const Math::Vector3& position,
                                          const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee") {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hitbox3DComponent hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.damageChannel = channel;
        entityManager.AddComponent<Hitbox3DComponent>(entity, hitbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeHurtbox(const Math::Vector3& position,
                                           const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee") {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hurtbox3DComponent hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.damageChannel = channel;
        entityManager.AddComponent<Hurtbox3DComponent>(entity, hurtbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    void Move(const Entity entity, const Math::Vector3& position) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    void Rotate(const Entity entity, const Math::Quaternion& rotation) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localRotation = rotation;
        transform.rotationEditMode = Scene::RotationEditMode::Quaternion;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }

    void Scale(const Entity entity, const Math::Vector3& scale) {
        Scene::TransformComponent transform =
            entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localScale = scale;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }
};

TEST_F(HitboxLagCompensationTest, Record_RejectsNonIncreasingTicks) {
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}));
    EXPECT_TRUE(compensator.Record(5U, entityManager));
    EXPECT_FALSE(compensator.Record(5U, entityManager));
    EXPECT_FALSE(compensator.Record(3U, entityManager));
    EXPECT_TRUE(compensator.Record(6U, entityManager));
    EXPECT_EQ(compensator.HistorySize(), 2U);
    EXPECT_EQ(compensator.Stats().snapshotsRecorded, 2U);
    EXPECT_EQ(compensator.Stats().recordsRejected, 2U);
    EXPECT_EQ(compensator.Stats().snapshotsEvicted, 0U);
}

TEST_F(HitboxLagCompensationTest, Shot_HitsWhereTheTargetWas) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});

    // The control: what the live scan saw before anything moved.
    const Hitbox3DSyncReport control = SyncHitboxes3D(entityManager);
    ASSERT_EQ(control.strikes.size(), 1U);
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    Move(target, {10.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The present misses - the rewind must not.
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
    const LagShotReport rewound = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_EQ(rewound.pairs, control.strikes);
    EXPECT_EQ(rewound.resolvedTick, 1U);
    EXPECT_FALSE(rewound.clamped);
    EXPECT_EQ(rewound.candidatesConsidered, 1U);
    ASSERT_EQ(rewound.pairs.size(), 1U);
    EXPECT_EQ(rewound.pairs[0].hurtbox, target);
    EXPECT_EQ(compensator.Stats().pairsFound, 1U);
}

TEST_F(HitboxLagCompensationTest, Shot_AgainstThePresentAgreesWithTheLiveScan) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    const Hitbox3DSyncReport live = SyncHitboxes3D(entityManager);
    const LagShotReport rewound = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_EQ(rewound.pairs, live.strikes);
    EXPECT_FALSE(rewound.clamped);

    // A target newer than the newest snapshot is the future: nearest survivor, flagged.
    const LagShotReport future = compensator.ResolveShot(shooter, 99U, entityManager);
    EXPECT_EQ(future.pairs, live.strikes);
    EXPECT_EQ(future.resolvedTick, 1U);
    EXPECT_TRUE(future.clamped);
    EXPECT_EQ(compensator.Stats().resolvesClamped, 1U);
}

TEST_F(HitboxLagCompensationTest, Shot_ClampsTargetsOlderThanHistory) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));
    ASSERT_TRUE(compensator.Record(3U, entityManager));

    // Tick 0 predates everything: the oldest survivor answers, flagged.
    const LagShotReport clamped = compensator.ResolveShot(shooter, 0U, entityManager);
    EXPECT_EQ(clamped.resolvedTick, 1U);
    EXPECT_TRUE(clamped.clamped);
    ASSERT_EQ(clamped.pairs.size(), 1U);

    // No history at all: nothing to resolve against.
    HitboxLagCompensator empty;
    const LagShotReport none = empty.ResolveShot(shooter, 5U, entityManager);
    EXPECT_TRUE(none.pairs.empty());
    EXPECT_EQ(none.resolvedTick, 0U);
    EXPECT_TRUE(none.clamped);
}

TEST_F(HitboxLagCompensationTest, Shot_FailsClosedWithoutARecordedArmedShooter) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    EXPECT_TRUE(compensator.ResolveShot(kInvalidEntity, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShot(target, 1U, entityManager).pairs.empty());

    Scene::Hitbox3DComponent& hitbox =
        entityManager.GetComponent<Hitbox3DComponent>(shooter);
    hitbox.enabled = false;
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());
    hitbox.enabled = true;

    // Recorded history predates this shooter: it was never posed, so there is no past pose.
    const Entity late = MakeHitbox({0.0F, 0.0F, 0.0F});
    EXPECT_TRUE(compensator.ResolveShot(late, 1U, entityManager).pairs.empty());

    // The dead keep their poses but lose their hurtboxes: destroying the target empties the
    // resolve without touching history.
    entityManager.DestroyEntity(target);
    const LagShotReport bereaved = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_TRUE(bereaved.pairs.empty());
    EXPECT_EQ(bereaved.candidatesConsidered, 0U);
    EXPECT_EQ(bereaved.resolvedTick, 1U);

    // And a destroyed shooter shoots nothing.
    entityManager.DestroyEntity(shooter);
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationTest, Shot_ReadsVulnerabilityLive) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // I-frames started after the shot still count: only positions rewind, and the target
    // with armor now was armored for the rewind too.
    Scene::Hurtbox3DComponent& hurtbox =
        entityManager.GetComponent<Scene::Hurtbox3DComponent>(target);
    hurtbox.enabled = false;
    const LagShotReport armored = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_TRUE(armored.pairs.empty());
    EXPECT_EQ(armored.candidatesConsidered, 0U);

    hurtbox.enabled = true;
    EXPECT_EQ(compensator.ResolveShot(shooter, 1U, entityManager).pairs.size(), 1U);
}

TEST_F(HitboxLagCompensationTest, Shot_IgnoresDecoysOnOtherChannels) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile"));
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // Considered (vulnerable, posed) but rejected: the acceptance gates rewind with the rest.
    const LagShotReport rewound = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_TRUE(rewound.pairs.empty());
    EXPECT_EQ(rewound.candidatesConsidered, 1U);
}

TEST_F(HitboxLagCompensationTest, Ray_HitsWhereTheTargetWas) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {5.0F, 5.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // From x = -5 along +X: the tick-1 box face at x = 4 is nine metres down the ray.
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReport hit = compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(hit.entity, target);
    EXPECT_NEAR(hit.distance, 9.0F, kEpsilon);
    EXPECT_NEAR(hit.point.x, 4.0F, kEpsilon);
    EXPECT_NEAR(hit.point.y, 0.0F, kEpsilon);
    EXPECT_NEAR(hit.point.z, 0.0F, kEpsilon);
    EXPECT_EQ(hit.resolvedTick, 1U);
    EXPECT_FALSE(hit.clamped);

    // At tick 2 the target stepped off the line: the same ray misses.
    const LagRayReport miss = compensator.ResolveRay(shooter, ray, 20.0F, 2U, entityManager);
    EXPECT_EQ(miss.entity, kInvalidEntity);
    EXPECT_EQ(miss.distance, 0.0F);

    // A doubled direction is normalized, not doubled: same hit, same metres.
    const Math::Ray fat{{-5.0F, 0.0F, 0.0F}, {2.0F, 0.0F, 0.0F}};
    const LagRayReport normalized =
        compensator.ResolveRay(shooter, fat, 20.0F, 1U, entityManager);
    EXPECT_EQ(normalized.entity, target);
    EXPECT_NEAR(normalized.distance, 9.0F, kEpsilon);

    // Degenerate rays resolve empty, never crash.
    const Math::Ray zero{{-5.0F, 0.0F, 0.0F}, {0.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRay(shooter, zero, 20.0F, 1U, entityManager).entity,
              kInvalidEntity);
    EXPECT_EQ(compensator.ResolveRay(shooter, ray, 0.0F, 1U, entityManager).entity,
              kInvalidEntity);
}

TEST_F(HitboxLagCompensationTest, History_EvictsOldestPastTheCap) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    for (LagTick tick = 1U; tick <= 70U; ++tick) {
        ASSERT_TRUE(compensator.Record(tick, entityManager));
    }
    EXPECT_EQ(compensator.HistorySize(), kLagHistoryMaximumTicks);
    EXPECT_EQ(compensator.Stats().snapshotsRecorded, 70U);
    EXPECT_EQ(compensator.Stats().snapshotsEvicted, 6U);

    // Tick 1 aged out: tick 7 is the oldest survivor and answers, flagged.
    const LagShotReport evicted = compensator.ResolveShot(shooter, 1U, entityManager);
    EXPECT_EQ(evicted.resolvedTick, 7U);
    EXPECT_TRUE(evicted.clamped);
    EXPECT_EQ(evicted.pairs.size(), 1U);
}

TEST_F(HitboxLagCompensationTest, MergedReport_EntersOnceWithoutSpuriousExits) {
    const Entity liveShooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(liveShooter);
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    const Entity laggedShooter = MakeHitbox({10.0F, 0.0F, 0.0F});
    const Entity laggedTarget = MakeHurtbox({11.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    const Hitbox3DSyncReport first = SyncHitboxes3D(entityManager);
    ASSERT_EQ(first.strikes.size(), 2U);
    Hitbox3DStrikeLifecycleTracker merged;
    Hitbox3DStrikeLifecycleTracker unmerged;
    EXPECT_EQ(merged.Update(first).transitions.size(), 2U);
    EXPECT_EQ(unmerged.Update(first).transitions.size(), 2U);

    Move(laggedTarget, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));
    const Hitbox3DSyncReport live = SyncHitboxes3D(entityManager);
    ASSERT_EQ(live.strikes.size(), 1U);

    // The lagged shooter's victim is gone from the present but not from tick 1.
    const LagShotReport compensated =
        compensator.ResolveShot(laggedShooter, 1U, entityManager);
    ASSERT_EQ(compensated.pairs.size(), 1U);
    EXPECT_EQ(compensated.pairs[0], first.strikes[1]);

    // Merged before the diff: no edges at all - the live pair persists, the compensated pair
    // was already active, and a replayed merge stays silent too.
    Hitbox3DSyncReport both = live;
    both.strikes.insert(both.strikes.end(), compensated.pairs.begin(), compensated.pairs.end());
    EXPECT_TRUE(merged.Update(both).transitions.empty());
    EXPECT_EQ(merged.GetActiveCount(), 2U);
    EXPECT_TRUE(merged.Update(both).transitions.empty());

    // The contrast: feeding the live report alone exits the lagged pair - which is exactly
    // why production merges instead of updating twice.
    const Hitbox3DStrikeLifecycleReport bare = unmerged.Update(live);
    ASSERT_EQ(bare.transitions.size(), 1U);
    EXPECT_EQ(bare.transitions[0].kind, Hitbox3DStrikeTransitionKind::Exited);
}

TEST_F(HitboxLagCompensationTest, ShotAt_ZeroFractionMatchesIntegerResolve) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Fraction 0 is the integer spelling with another name: same pairs, same flags.
    EXPECT_EQ(compensator.ResolveShotAt(shooter, 1U, 0.0F, entityManager),
              compensator.ResolveShot(shooter, 1U, entityManager));
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRayAt(shooter, ray, 20.0F, 1U, 0.0F, entityManager),
              compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager));
}

TEST_F(HitboxLagCompensationTest, ShotAt_HalfTickFindsMidpointVictim) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {-4.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Neither snapshot overlaps: the target teleported clean through between records.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShot(shooter, 2U, entityManager).pairs.empty());

    // Halfway it stood inside the shooter: the blend hits where both floors miss.
    const LagShotReport between =
        compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_EQ(between.resolvedTick, 1U);
    EXPECT_FALSE(between.clamped);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationTest, ShotAt_RotationMidpointOnly) {
    const Entity shooter = MakeHitbox({3.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({0.0F, 0.0F, 0.0F}, {2.0F, 1.0F, 1.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    // A long box spinning 90 degrees about Y between records: yaw-90 as cos/sin of half
    // the angle, authored in Quaternion mode so the graph keeps it.
    Rotate(target, Math::Quaternion{0.0F, 0.70710678F, 0.0F, 0.70710678F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Tick 1 only touches the shooter at x = 2 (not a strike); tick 2 points down Z, clear.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());
    EXPECT_TRUE(compensator.ResolveShot(shooter, 2U, entityManager).pairs.empty());

    // At 45 degrees the corner reaches x = 2.12: the slerp midpoint overlaps.
    const LagShotReport between =
        compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationTest, ShotAt_FractionOutOfRangeFailsClosed) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Garbage fractions miss without consulting history - even 1.0, which names the next
    // tick and must be spelled that way instead.
    const float bad[] = {std::numeric_limits<float>::quiet_NaN(), -0.25F, 1.0F, 1.5F};
    for (const float fraction : bad) {
        const LagShotReport shot =
            compensator.ResolveShotAt(shooter, 1U, fraction, entityManager);
        EXPECT_TRUE(shot.pairs.empty());
        EXPECT_EQ(shot.resolvedTick, 0U);
        EXPECT_FALSE(shot.clamped);
        EXPECT_FALSE(shot.interpolated);
        const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
        const LagRayReport beam =
            compensator.ResolveRayAt(shooter, ray, 20.0F, 1U, fraction, entityManager);
        EXPECT_EQ(beam.entity, kInvalidEntity);
        EXPECT_EQ(beam.resolvedTick, 0U);
        EXPECT_FALSE(beam.interpolated);
    }
}

TEST_F(HitboxLagCompensationTest, ShotAt_MissingCeilFallsBackToFloor) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Tick 2 is the newest: no ceiling exists, so the fraction is ignored, not invented.
    const LagShotReport answered =
        compensator.ResolveShotAt(shooter, 2U, 0.5F, entityManager);
    ASSERT_EQ(answered.pairs.size(), 1U);
    EXPECT_EQ(answered.resolvedTick, 2U);
    EXPECT_FALSE(answered.clamped);
    EXPECT_FALSE(answered.interpolated);
}

TEST_F(HitboxLagCompensationTest, ShotAt_NonConsecutiveTicksFallBack) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));
    Move(target, {20.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(5U, entityManager));

    // Ticks 3 and 4 were never recorded: tick 2 has no ceiling, so no blend - while the
    // consecutive pair below still interpolates, proving the gap is seen, not skipped.
    const LagShotReport gapped =
        compensator.ResolveShotAt(shooter, 2U, 0.5F, entityManager);
    ASSERT_EQ(gapped.pairs.size(), 1U);
    EXPECT_EQ(gapped.resolvedTick, 2U);
    EXPECT_FALSE(gapped.interpolated);
    const LagShotReport blended =
        compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(blended.pairs.size(), 1U);
    EXPECT_TRUE(blended.interpolated);
}

TEST_F(HitboxLagCompensationTest, ShotAt_ClampedFloorFallsBack) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(5U, entityManager));

    // The floor (5) is not the tick (7): a clamped floor has no bracket to blend.
    const LagShotReport answered =
        compensator.ResolveShotAt(shooter, 7U, 0.5F, entityManager);
    ASSERT_EQ(answered.pairs.size(), 1U);
    EXPECT_EQ(answered.resolvedTick, 5U);
    EXPECT_TRUE(answered.clamped);
    EXPECT_FALSE(answered.interpolated);
}

TEST_F(HitboxLagCompensationTest, RayAt_HalfTickFindsMidpointVictim) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 3.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Move(target, {5.0F, -3.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The ray runs down y = 0: both snapshots hold the target three metres off the line.
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    EXPECT_EQ(compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager).entity,
              kInvalidEntity);
    EXPECT_EQ(compensator.ResolveRay(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntity);

    // Halfway the box sits on the line: face at x = 4, nine metres down the ray.
    const LagRayReport between =
        compensator.ResolveRayAt(shooter, ray, 20.0F, 1U, 0.5F, entityManager);
    EXPECT_EQ(between.entity, target);
    EXPECT_NEAR(between.distance, 9.0F, kEpsilon);
    EXPECT_EQ(between.resolvedTick, 1U);
    EXPECT_FALSE(between.clamped);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationTest, Stats_InterpolationsCounted) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    EXPECT_EQ(compensator.Stats().resolvesInterpolated, 0U);
    static_cast<void>(compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager));
    EXPECT_EQ(compensator.Stats().resolvesInterpolated, 1U);
    // Floor fallbacks are plain resolves: newest tick, no ceiling, no count.
    static_cast<void>(compensator.ResolveShotAt(shooter, 2U, 0.5F, entityManager));
    EXPECT_EQ(compensator.Stats().resolvesInterpolated, 1U);
    EXPECT_EQ(compensator.Stats().shotsResolved, 2U);
}

TEST_F(HitboxLagCompensationTest, Shot_ScaleRewindsNotLive) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Scale(target, {0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Shrunk to [1.4, 1.6]: tick 2 agrees with the geometry (a miss), while the live scan
    // - which ignores world scale - still reports the overlap the geometry left behind.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 2U, entityManager).pairs.empty());
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);

    // But tick 1 recorded full size: the rewound shot hits what the shrunken one cannot.
    const LagShotReport past =
        compensator.ResolveShot(shooter, 1U, entityManager);
    ASSERT_EQ(past.pairs.size(), 1U);
    EXPECT_EQ(past.pairs[0].hurtbox, target);
    EXPECT_EQ(past.resolvedTick, 1U);
}

TEST_F(HitboxLagCompensationTest, Shot_ShooterScaleRewinds) {
    const Entity shooter = MakeHitbox({2.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({0.0F, 0.0F, 0.0F}));
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Scale(shooter, {2.0F, 2.0F, 2.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Doubled to [0, 4]: tick 2 agrees with the geometry (a hit), while the live scan -
    // which ignores world scale - still sees the tick-1 touch and stays silent.
    ASSERT_EQ(compensator.ResolveShot(shooter, 2U, entityManager).pairs.size(), 1U);
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());

    // At tick 1 the shooter only touched at x = 1: the rewound swing misses.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationTest, ShotAt_ScaleInterpolates) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    Scale(target, {0.1F, 0.1F, 0.1F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Scale(target, {3.0F, 3.0F, 3.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // The floor (scale 0.1, box [1.4, 1.6]) misses on its own.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());

    // Halfway the scale is 1.55, the box [-0.05, 3.05]: overlap depth exactly 1.05, which
    // only the blended size produces (floor misses, ceiling would run 2.0 deep).
    const LagShotReport between =
        compensator.ResolveShotAt(shooter, 1U, 0.5F, entityManager);
    ASSERT_EQ(between.pairs.size(), 1U);
    EXPECT_EQ(between.pairs[0].hurtbox, target);
    EXPECT_NEAR(between.pairs[0].penetrationDepth, 1.05F, kEpsilon);
    EXPECT_TRUE(between.interpolated);
}

TEST_F(HitboxLagCompensationTest, Ray_ScaleRewinds) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Scale(target, {0.5F, 0.5F, 0.5F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Grazing down y = 0.9: inside the full-size box, above the shrunk one.
    const Math::Ray ray{{-5.0F, 0.9F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReport past =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(past.entity, target);
    EXPECT_NEAR(past.distance, 9.0F, kEpsilon);
    EXPECT_EQ(compensator.ResolveRay(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntity);
}

TEST_F(HitboxLagCompensationTest, Shot_ZeroScaleUntargetable) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    Scale(target, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // Zero size at an overlapping pose: no hit, no crash.
    EXPECT_TRUE(compensator.ResolveShot(shooter, 1U, entityManager).pairs.empty());

    // The mirror case: a zero-scale attacker swings nothing either.
    Scale(target, {1.0F, 1.0F, 1.0F});
    Scale(shooter, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));
    EXPECT_TRUE(compensator.ResolveShot(shooter, 2U, entityManager).pairs.empty());
}

TEST_F(HitboxLagCompensationTest, Shot_NegativeScaleMirrors) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({1.5F, 0.0F, 0.0F});
    Scale(target, {-1.0F, -1.0F, -1.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));

    // A mirror occupies the same box: the rewound size takes the absolute scale.
    const LagShotReport mirrored =
        compensator.ResolveShot(shooter, 1U, entityManager);
    ASSERT_EQ(mirrored.pairs.size(), 1U);
    EXPECT_EQ(mirrored.pairs[0].hurtbox, target);
}

TEST_F(HitboxLagCompensationTest, Ray_ZeroScaleMisses) {
    const Entity shooter = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity target = MakeHurtbox({5.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(1U, entityManager));
    Scale(target, {0.0F, 0.0F, 0.0F});
    ASSERT_TRUE(compensator.Record(2U, entityManager));

    // Full size on the line hits at nine metres; shrunk to a point, the ray passes through.
    const Math::Ray ray{{-5.0F, 0.0F, 0.0F}, {1.0F, 0.0F, 0.0F}};
    const LagRayReport past =
        compensator.ResolveRay(shooter, ray, 20.0F, 1U, entityManager);
    EXPECT_EQ(past.entity, target);
    EXPECT_NEAR(past.distance, 9.0F, kEpsilon);
    EXPECT_EQ(compensator.ResolveRay(shooter, ray, 20.0F, 2U, entityManager).entity,
              kInvalidEntity);
}

} // namespace
} // namespace UVE::Physics::Tests
