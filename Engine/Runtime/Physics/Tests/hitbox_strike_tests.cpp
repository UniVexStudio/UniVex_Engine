// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "hitbox_strike.h"

#include <cmath>
#include <cstdint>
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
#include "hitbox_strike_events.h"
#include "hitbox_strike_lifecycle_tracker.h"
#include "scene_graph.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::Entity;
using Scene::Hitbox3DComponent;
using Scene::Hurtbox3DComponent;
using Scene::kInvalidEntity;

constexpr float kEpsilon = 1e-4F;

class HitboxStrikeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    Scene::EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    Scene::SceneGraph sceneGraph;

    [[nodiscard]] Entity MakeHitbox(const Math::Vector3& position,
                                          const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee",
                                          const std::uint32_t layer = 1U,
                                          const std::uint32_t mask = 0xFFFFFFFFU,
                                          const Math::Quaternion& rotation = {}) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hitbox3DComponent hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.collisionLayer = layer;
        hitbox.collisionMask = mask;
        hitbox.damageChannel = channel;
        entityManager.AddComponent<Hitbox3DComponent>(entity, hitbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Entity MakeHurtbox(const Math::Vector3& position,
                                           const Math::Vector3& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee",
                                           const std::uint32_t layer = 1U,
                                           const std::uint32_t mask = 0xFFFFFFFFU,
                                           const Math::Quaternion& rotation = {}) {
        const Entity entity = entityManager.CreateEntity();
        Scene::TransformComponent transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransform(entityManager, entity, transform);
        Hurtbox3DComponent hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.collisionLayer = layer;
        hurtbox.collisionMask = mask;
        hurtbox.damageChannel = channel;
        entityManager.AddComponent<Hurtbox3DComponent>(entity, hurtbox);
        sceneGraph.Update(entityManager);
        return entity;
    }

    [[nodiscard]] Hitbox3DComponent Component(const Entity entity) {
        return entityManager.GetComponent<Hitbox3DComponent>(entity);
    }

    void Move(const Entity entity, const Math::Vector3& position) {
        Scene::TransformComponent transform = entityManager.GetComponent<Scene::TransformComponent>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransform(entityManager, entity, transform);
        sceneGraph.Update(entityManager);
    }
};

// -------------------------------------------------------------------------------------------------
// The scan: Physics::SyncHitboxes3D()
// -------------------------------------------------------------------------------------------------

TEST_F(HitboxStrikeTest, AnOverlapIsRecordedWithItsDepthAndTheAxisTowardTheHurtbox) {
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    // A decoy on another channel: it must not appear in the list or the report.
    const Entity other = MakeHurtbox({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile");

    const Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);

    const Hitbox3DComponent struck = Component(hitbox);
    ASSERT_EQ(struck.strikeCount, 1U);
    EXPECT_FALSE(struck.strikesTruncated);
    EXPECT_EQ(struck.strikes[0].hurtboxEntity, hurtbox);
    EXPECT_NE(struck.strikes[0].hurtboxEntity, other);
    // Two unit boxes 1.5m apart overlap by 2 - 1.5 along their X axes.
    EXPECT_NEAR(struck.strikes[0].penetrationDepth, 0.5F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.x, 1.0F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.y, 0.0F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.z, 0.0F, kEpsilon);
    EXPECT_NEAR(Math::Length(struck.strikes[0].axis), 1.0F, kEpsilon);

    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.hitboxCount, 1U);
    EXPECT_EQ(report.hurtboxCount, 2U);
    EXPECT_FALSE(report.IsTruncated());
    EXPECT_EQ(report.strikes[0].hitbox, hitbox);
    EXPECT_EQ(report.strikes[0].hurtbox, hurtbox);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeTest, TheAxisPointsFromTheHitboxTowardTheHurtboxWhicheverSideItIsOn) {
    const Entity east = MakeHitbox({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    const Entity west = MakeHitbox({4.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtbox({2.5F, 0.0F, 0.0F}));

    static_cast<void>(SyncHitboxes3D(entityManager));

    EXPECT_NEAR(Component(east).strikes[0].axis.x, 1.0F, kEpsilon)
        << "the hurtbox is to the +X side of the hitbox";
    EXPECT_NEAR(Component(west).strikes[0].axis.x, -1.0F, kEpsilon)
        << "the hurtbox is to the -X side of the hitbox";
}

TEST_F(HitboxStrikeTest, AHitboxNeverStrikesAHurtboxOnItsOwnEntity) {
    const Entity entity = MakeHitbox({0.0F, 0.0F, 0.0F});
    entityManager.AddComponent<Hurtbox3DComponent>(entity, Hurtbox3DComponent{});
    sceneGraph.Update(entityManager);

    static_cast<void>(SyncHitboxes3D(entityManager));

    EXPECT_EQ(Component(entity).strikeCount, 0U);
}

TEST_F(HitboxStrikeTest, BothSidesHaveToAcceptTheOtherForAStrike) {
    // The hitbox accepts layer 2, the hurtbox is on layer 2 but only accepts layer 4.
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee", 1U,
                                           0xFFFFFFFFU);
    const Entity refusingHurtbox =
        MakeHurtbox({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee", 2U, 0x04U);

    Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    EXPECT_TRUE(report.strikes.empty()) << "the hurtbox's own mask has the last word";

    // Now the other way around: the hurtbox accepts layer 1, the hitbox no longer looks for 2.
    entityManager.GetComponent<Hurtbox3DComponent>(refusingHurtbox).collisionMask = 0xFFFFFFFFU;
    entityManager.GetComponent<Hitbox3DComponent>(hitbox).collisionMask = 0x04U;
    report = SyncHitboxes3D(entityManager);
    EXPECT_TRUE(report.strikes.empty()) << "the hitbox's own mask has the last word too";

    // With both masks matching again, the same geometry strikes.
    entityManager.GetComponent<Hitbox3DComponent>(hitbox).collisionMask = 0xFFFFFFFFU;
    report = SyncHitboxes3D(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, refusingHurtbox);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeTest, AStrikeNeedsTheSameDamageChannelOnBothSides) {
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee");
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile"));

    Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(Component(hitbox).strikeCount, 0U);

    // Matching channels: the same geometry - and only the matching hurtbox - strikes.
    const Entity matching = MakeHurtbox({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee");
    report = SyncHitboxes3D(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, matching);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeTest, DisabledOrInvalidSidesStrikeNothingAndStaleStateIsCleared) {
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);

    // Switching the HURTBOX off removes it from the candidate set entirely.
    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).enabled = false;
    Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(report.hurtboxCount, 0U);
    EXPECT_EQ(Component(hitbox).strikeCount, 0U) << "the hitbox's list is refreshed, not appended";

    // A malformed hurtbox is refused the same way (a non-positive extent is not a volume).
    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).enabled = true;
    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).halfExtents.y = 0.0F;
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());

    // A malformed HITBOX ends the tick with zero strikes rather than with the previous list.
    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).halfExtents = {1.0F, 1.0F, 1.0F};
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    entityManager.GetComponent<Hitbox3DComponent>(hitbox).enabled = false;
    report = SyncHitboxes3D(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(Component(hitbox).strikeCount, 0U);

    // Switched back on, the same geometry strikes again.
    entityManager.GetComponent<Hitbox3DComponent>(hitbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
}

TEST_F(HitboxStrikeTest, TouchingBoundariesAreNotStrikes) {
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});
    // Exactly adjacent: the boxes share a face and overlap by nothing.
    static_cast<void>(MakeHurtbox({2.0F, 0.0F, 0.0F}));

    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
    EXPECT_EQ(Component(hitbox).strikeCount, 0U);
}

TEST_F(HitboxStrikeTest, TheStrikeListIsBoundedAndTheOverflowIsReported) {
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    // One more overlapping hurtbox than the fixed storage can hold. They are spread along Z by
    // less than their own depth, so every one of them really does overlap (a box sitting exactly
    // on the boundary would be a separation, not a strike).
    for (std::size_t index = 0U; index < Scene::kMaximumHitbox3DStrikes + 1U; ++index) {
        static_cast<void>(MakeHurtbox(
            {1.5F, 0.0F, static_cast<float>(index) * 0.1F}, {1.0F, 1.0F, 2.0F}));
    }

    const Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);

    const Hitbox3DComponent struck = Component(hitbox);
    EXPECT_EQ(struck.strikeCount, Scene::kMaximumHitbox3DStrikes);
    EXPECT_TRUE(struck.strikesTruncated) << "the overflow is a fact, not a silent drop";
    EXPECT_TRUE(report.IsTruncated());
    EXPECT_EQ(report.hurtboxCount, Scene::kMaximumHitbox3DStrikes + 1U);
}

TEST_F(HitboxStrikeTest, TheAuthoredHalfExtentsAreUsedAndTheWorldScaleIsNot) {
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}));
    const Entity hurtbox = MakeHurtbox({0.9F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);

    // Scaling the hurtbox's own object does NOT grow its hurt volume: the volume is the authored
    // half-extents in world metres, the documented collider/area convention. A 10x scale still
    // leaves the boxes overlapping exactly as authored (a 0.1m overlap), never 5m of it.
    entityManager.GetComponent<Scene::TransformComponent>(hurtbox).localScale =
        Math::Vector3{10.0F, 10.0F, 10.0F};
    sceneGraph.Update(entityManager);
    const Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_NEAR(report.strikes[0].penetrationDepth, 0.1F, kEpsilon);

    // Moving it beyond the authored extents ends the strike regardless of the scale.
    Move(hurtbox, {1.5F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
}

TEST_F(HitboxStrikeTest, ARotatedVolumeIsSweptByTheExactOrientedBoxNotItsAabb) {
    // A long thin blade along +X, rotated 90 degrees about Y, so it points along -Z instead.
    Math::Quaternion quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngle({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const Entity blade = MakeHitbox({0.0F, 0.0F, 0.0F}, {2.0F, 0.1F, 0.1F}, "melee", 1U, 0xFFFFFFFFU,
                                          quarterTurn);
    // Straight down -Z: inside the rotated blade, and outside the axis-aligned one it would have
    // been without the rotation.
    const Entity ahead = MakeHurtbox({0.0F, 0.0F, -1.5F}, {0.2F, 0.2F, 0.2F});
    // Where the blade used to point: no longer covered.
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}, {0.2F, 0.2F, 0.2F}));

    const Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);

    // The proof is which box was struck at all: a rotated blade's axis-aligned bounds would have
    // covered BOTH boxes, the oriented box covers only the one along its real direction.
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, ahead);
    // The minimum translation for a thin blade against a small box is the transversal one (0.3m:
    // the blade's 0.1 half-thickness plus the hurtbox's 0.2), not a lengthwise push - and it is
    // perpendicular to the blade's own long axis, which is what makes it the blade's axis.
    EXPECT_NEAR(report.strikes[0].penetrationDepth, 0.3F, kEpsilon);
    static_cast<void>(blade);
}

TEST_F(HitboxStrikeTest, TheReportCountsEveryHitboxAndHurtboxItVisited) {
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeHitbox({10.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeHurtbox({20.0F, 0.0F, 0.0F}));

    const Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);

    EXPECT_EQ(report.hitboxCount, 2U) << "every hitbox is visited, striking or not";
    EXPECT_EQ(report.hurtboxCount, 2U);
    EXPECT_EQ(report.strikes.size(), 1U);
}

// -------------------------------------------------------------------------------------------------
// The consequence contract: Hitbox3DStrikeLifecycleTracker
// -------------------------------------------------------------------------------------------------

TEST_F(HitboxStrikeTest, AContinuousStrikeEntersOnceAndOnlySeparatingEmitsTheExit) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});

    Hitbox3DStrikeLifecycleTracker tracker;
    const Hitbox3DStrikeLifecycleReport first = tracker.Update(SyncHitboxes3D(entityManager));
    ASSERT_EQ(first.transitions.size(), 1U);
    EXPECT_EQ(first.transitions[0].kind, Hitbox3DStrikeTransitionKind::Entered);
    EXPECT_EQ(first.transitions[0].strike.hitbox, hitbox);
    EXPECT_EQ(first.transitions[0].strike.hurtbox, hurtbox);
    EXPECT_EQ(first.transitions[0].strike.damageChannel, "melee");
    EXPECT_EQ(first.previousActiveCount, 0U);
    EXPECT_EQ(first.currentActiveCount, 1U);

    // Two more ticks of the same overlap: the strike is still live, but it is not news again.
    for (int tick = 0; tick < 2; ++tick) {
        const Hitbox3DStrikeLifecycleReport steady = tracker.Update(SyncHitboxes3D(entityManager));
        EXPECT_TRUE(steady.transitions.empty()) << "a strike that lasts is one strike, not one per frame";
        EXPECT_EQ(steady.currentActiveCount, 1U);
    }

    // Getting deeper is the same strike, not a new one - the transition carries the new evidence.
    Move(hurtbox, {1.0F, 0.0F, 0.0F});
    const Hitbox3DStrikeLifecycleReport deeper = tracker.Update(SyncHitboxes3D(entityManager));
    EXPECT_TRUE(deeper.transitions.empty());

    // Separating is the exit, and it carries the pair (and the last-known channel) as evidence.
    Move(hurtbox, {10.0F, 0.0F, 0.0F});
    const Hitbox3DStrikeLifecycleReport exited = tracker.Update(SyncHitboxes3D(entityManager));
    ASSERT_EQ(exited.transitions.size(), 1U);
    EXPECT_EQ(exited.transitions[0].kind, Hitbox3DStrikeTransitionKind::Exited);
    EXPECT_EQ(exited.transitions[0].strike.hitbox, hitbox);
    EXPECT_EQ(exited.transitions[0].strike.hurtbox, hurtbox);
    EXPECT_EQ(exited.transitions[0].strike.damageChannel, "melee");
    EXPECT_EQ(exited.currentActiveCount, 0U);

    // And nothing after that until it starts again.
    EXPECT_TRUE(tracker.Update(SyncHitboxes3D(entityManager)).transitions.empty());
    EXPECT_EQ(tracker.GetActiveCount(), 0U);
}

TEST_F(HitboxStrikeTest, DisablingASideEndsTheStrikeOnThatVeryTick) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update(SyncHitboxes3D(entityManager)).transitions.size(), 1U);

    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).enabled = false;
    const Hitbox3DStrikeLifecycleReport report = tracker.Update(SyncHitboxes3D(entityManager));

    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKind::Exited);
    EXPECT_EQ(report.transitions[0].strike.hurtbox, hurtbox)
        << "an exit says which pair ended, with the evidence it had while it was alive";
}

TEST_F(HitboxStrikeTest, ADestroyedHurtboxEndsTheStrikeInsteadOfLeakingIt) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update(SyncHitboxes3D(entityManager)).currentActiveCount, 1U);

    entityManager.DestroyEntity(hurtbox);
    const Hitbox3DStrikeLifecycleReport report = tracker.Update(SyncHitboxes3D(entityManager));

    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKind::Exited);
    EXPECT_EQ(tracker.GetActiveCount(), 0U) << "the tracker drops the pair, it does not leak it";
}

TEST_F(HitboxStrikeTest, ATruncatedSnapshotInventsNoExitsAndKeepsTheBaseline) {
    // A hitbox striking one hurtbox, then a scan whose pair list is bounded below what the scan
    // saw: the pairs beyond the bound are not gone, they are simply not in the report.
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});

    Hitbox3DStrikeLifecycleTracker tracker;
    Hitbox3DSyncReport live = SyncHitboxes3D(entityManager);
    ASSERT_EQ(tracker.Update(live).currentActiveCount, 1U);

    Hitbox3DSyncReport truncated = live;
    truncated.strikesTruncated = true;
    const Hitbox3DStrikeLifecycleReport report = tracker.Update(truncated);

    EXPECT_TRUE(report.transitions.empty()) << "an incomplete snapshot is not evidence of an exit";
    EXPECT_TRUE(report.inputSnapshotTruncated);
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);

    // The next complete snapshot still knows the strike is live: no spurious exit-then-enter.
    const Hitbox3DStrikeLifecycleReport recovered = tracker.Update(SyncHitboxes3D(entityManager));
    EXPECT_TRUE(recovered.transitions.empty());

    // An over-cap pair list is treated the same way, without needing the component flag.
    Hitbox3DSyncReport overCap;
    overCap.strikes.assign(kMaximumHitbox3DStrikeResults + 1U,
                           Hitbox3DStrikePair{hitbox, hurtbox, 0.5F, {1.0F, 0.0F, 0.0F}, "melee"});
    EXPECT_TRUE(tracker.Update(overCap).inputSnapshotTruncated);
    EXPECT_EQ(tracker.GetActiveCount(), 1U);
}

TEST_F(HitboxStrikeTest, ResetDiscardsTheBaselineWithoutFabricatingTransitions) {
    static_cast<void>(MakeHurtbox({1.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeHitbox({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTracker tracker;
    ASSERT_EQ(tracker.Update(SyncHitboxes3D(entityManager)).currentActiveCount, 1U);

    tracker.Reset();
    EXPECT_EQ(tracker.GetActiveCount(), 0U);

    // The still-overlapping pairing reads as a fresh enter, because after a reset it genuinely is
    // one: there is no history left to diff against, and silence is the honest answer, not a lie
    // that the hit is still being tracked.
    const Hitbox3DStrikeLifecycleReport report = tracker.Update(SyncHitboxes3D(entityManager));
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKind::Entered);
    EXPECT_EQ(report.previousActiveCount, 0U);
}

TEST_F(HitboxStrikeTest, TransitionsComeOutInDeterministicHitboxThenHurtboxOrder) {
    // Two hitboxes and three hurtboxes, every pair overlapping: the report's order may not depend
    // on pool order or on which side enumerated first.
    const Entity firstHitbox = MakeHitbox({0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F});
    const Entity secondHitbox = MakeHitbox({2.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F});
    std::vector<Entity> hurtboxes;
    for (int index = 0; index < 3; ++index) {
        hurtboxes.push_back(MakeHurtbox({1.0F + static_cast<float>(index) * 0.2F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F}));
    }

    Hitbox3DStrikeLifecycleTracker tracker;
    const Hitbox3DStrikeLifecycleReport report = tracker.Update(SyncHitboxes3D(entityManager));

    ASSERT_EQ(report.transitions.size(), 6U);
    // Strictly increasing in (hitbox, hurtbox) order - the pair order the tracker contracts. Two
    // records for the same hitbox are ordered by their hurtbox, never equal.
    const auto orderedByPair = [](const Hitbox3DStrikePair& left, const Hitbox3DStrikePair& right) {
        if (left.hitbox.index != right.hitbox.index) {
            return left.hitbox.index < right.hitbox.index;
        }
        if (left.hitbox.generation != right.hitbox.generation) {
            return left.hitbox.generation < right.hitbox.generation;
        }
        if (left.hurtbox.index != right.hurtbox.index) {
            return left.hurtbox.index < right.hurtbox.index;
        }
        return left.hurtbox.generation < right.hurtbox.generation;
    };
    for (std::size_t index = 1U; index < report.transitions.size(); ++index) {
        EXPECT_TRUE(orderedByPair(report.transitions[index - 1U].strike, report.transitions[index].strike))
            << "sorted by hitbox first, then by hurtbox";
    }
    EXPECT_EQ(report.transitions.front().strike.hitbox, firstHitbox);
    EXPECT_EQ(report.transitions.front().strike.hurtbox, hurtboxes.front());
    EXPECT_EQ(report.transitions.back().strike.hitbox, secondHitbox);

    // And the whole thing is stable: a second identical scan emits nothing at all.
    EXPECT_TRUE(tracker.Update(SyncHitboxes3D(entityManager)).transitions.empty());
}

TEST_F(HitboxStrikeTest, AnIgnoredEntityIsNeverStruck) {
    const Entity owner = MakeHurtbox({1.5F, 0.0F, 0.0F});
    const Entity other = MakeHurtbox({-1.5F, 0.0F, 0.0F});
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});
    entityManager.GetComponent<Hitbox3DComponent>(hitbox).ignoreEntity = owner;

    Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, other);
    EXPECT_EQ(Component(hitbox).strikeCount, 1U);
    EXPECT_EQ(Component(hitbox).strikes[0].hurtboxEntity, other);

    entityManager.GetComponent<Hitbox3DComponent>(hitbox).ignoreEntity = kInvalidEntity;
    report = SyncHitboxes3D(entityManager);
    EXPECT_EQ(report.strikes.size(), 2U);
    EXPECT_EQ(Component(hitbox).strikeCount, 2U);
}

TEST_F(HitboxStrikeTest, AStrikeLandsOncePerActivationWithoutDroppingLaterOverlaps) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(Component(hitbox).strikes[0].landed);
    EXPECT_EQ(Component(hitbox).struckCount, 1U);

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_EQ(Component(hitbox).strikeCount, 1U);
    EXPECT_FALSE(Component(hitbox).strikes[0].landed);
    EXPECT_EQ(Component(hitbox).strikes[0].hurtboxEntity, hurtbox);

    Move(hurtbox, {10.0F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
    EXPECT_EQ(Component(hitbox).struckCount, 1U);
    Move(hurtbox, {1.5F, 0.0F, 0.0F});
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_FALSE(Component(hitbox).strikes[0].landed);

    entityManager.GetComponent<Hitbox3DComponent>(hitbox).enabled = false;
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
    EXPECT_EQ(Component(hitbox).struckCount, 0U);

    entityManager.GetComponent<Hitbox3DComponent>(hitbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(Component(hitbox).strikes[0].landed);
}

TEST_F(HitboxStrikeTest, TheHurtboxRecordsIncomingHitsFromEveryAttacker) {
    const Entity hurtbox = MakeHurtbox({0.0F, 0.0F, 0.0F});
    const Entity east = MakeHitbox({1.5F, 0.0F, 0.0F});
    const Entity west = MakeHitbox({-1.5F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 2U);
    const Hurtbox3DComponent received = entityManager.GetComponent<Hurtbox3DComponent>(hurtbox);
    EXPECT_EQ(received.hitCount, 2U);
    EXPECT_TRUE(Scene::Hurtbox3D::HasHitFrom(received, east));
    EXPECT_TRUE(Scene::Hurtbox3D::HasHitFrom(received, west));
    EXPECT_TRUE(received.hits[0].received);
    EXPECT_TRUE(received.hits[1].received);

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 2U);
    const Hurtbox3DComponent stillHit = entityManager.GetComponent<Hurtbox3DComponent>(hurtbox);
    EXPECT_EQ(stillHit.hitCount, 2U);
    EXPECT_FALSE(stillHit.hits[0].received);
    EXPECT_FALSE(stillHit.hits[1].received);
    EXPECT_EQ(stillHit.receivedCount, 2U);
}

TEST_F(HitboxStrikeTest, AHurtboxIgnoreRefusesThatAttackerOnBothSides) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    const Entity ignored = MakeHitbox({0.0F, 0.0F, 0.0F});
    const Entity other = MakeHitbox({3.0F, 0.0F, 0.0F});
    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).ignoreEntity = ignored;

    Hitbox3DSyncReport report = SyncHitboxes3D(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hitbox, other);
    EXPECT_EQ(Component(ignored).strikeCount, 0U);
    EXPECT_EQ(Component(other).strikeCount, 1U);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hitCount, 1U);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hits[0].hitboxEntity, other);

    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).ignoreEntity = kInvalidEntity;
    report = SyncHitboxes3D(entityManager);
    EXPECT_EQ(report.strikes.size(), 2U);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hitCount, 2U);
}

TEST_F(HitboxStrikeTest, AHurtboxReceivesOncePerEnableWithoutDroppingLaterHits) {
    const Entity hurtbox = MakeHurtbox({1.5F, 0.0F, 0.0F});
    const Entity hitbox = MakeHitbox({0.0F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hits[0].received);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).receivedCount, 1U);

    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hitCount, 1U);
    EXPECT_FALSE(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hits[0].received);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hits[0].hitboxEntity, hitbox);

    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).enabled = false;
    EXPECT_TRUE(SyncHitboxes3D(entityManager).strikes.empty());
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).receivedCount, 0U);
    EXPECT_EQ(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hitCount, 0U);

    entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3D(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(entityManager.GetComponent<Hurtbox3DComponent>(hurtbox).hits[0].received);
}

TEST_F(HitboxStrikeTest, TheEventsCarryThePairAndCompareByValue) {
    const Hitbox3DStrikePair strike{Scene::Entity{3U, 1U}, Scene::Entity{9U, 2U}, 0.25F,
                                      {0.0F, 1.0F, 0.0F}, "melee"};
    const Hitbox3DStrikeEnteredEvent entered{strike};
    const Hitbox3DStrikeExitedEvent exited{strike};

    EXPECT_EQ(Hitbox3DStrikeEnteredEvent{strike}, entered);
    EXPECT_EQ(Hitbox3DStrikeExitedEvent{strike}, exited);
    EXPECT_EQ(entered.strike.hitbox, (Scene::Entity{3U, 1U}));
    EXPECT_EQ(entered.strike.hurtbox, (Scene::Entity{9U, 2U}));
    EXPECT_FLOAT_EQ(entered.strike.penetrationDepth, 0.25F);
    EXPECT_EQ(entered.strike.damageChannel, "melee");
}

} // namespace
} // namespace UVE::Physics::Tests
