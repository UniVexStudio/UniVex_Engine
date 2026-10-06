// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/physics/hitbox_strike_uve.h"

#include <cmath>
#include <cstdint>
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
#include "uve/physics/hitbox_strike_events_uve.h"
#include "uve/physics/hitbox_strike_lifecycle_tracker_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Physics::Tests {
namespace {

using Scene::EntityUVE;
using Scene::Hitbox3DComponentUVE;
using Scene::Hurtbox3DComponentUVE;
using Scene::kInvalidEntityUVE;

constexpr float kEpsilon = 1e-4F;

class HitboxStrikeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    Scene::EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    Scene::SceneGraphUVE sceneGraph;

    [[nodiscard]] EntityUVE MakeHitboxUVE(const Math::Vector3UVE& position,
                                          const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                          const std::string& channel = "melee",
                                          const std::uint32_t layer = 1U,
                                          const std::uint32_t mask = 0xFFFFFFFFU,
                                          const Math::QuaternionUVE& rotation = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hitbox3DComponentUVE hitbox;
        hitbox.halfExtents = halfExtents;
        hitbox.collisionLayer = layer;
        hitbox.collisionMask = mask;
        hitbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hitbox3DComponentUVE>(entity, hitbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] EntityUVE MakeHurtboxUVE(const Math::Vector3UVE& position,
                                           const Math::Vector3UVE& halfExtents = {1.0F, 1.0F, 1.0F},
                                           const std::string& channel = "melee",
                                           const std::uint32_t layer = 1U,
                                           const std::uint32_t mask = 0xFFFFFFFFU,
                                           const Math::QuaternionUVE& rotation = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        Scene::TransformComponentUVE transform{};
        transform.localPosition = position;
        transform.localRotation = rotation;
        sceneGraph.AttachTransformUVE(entityManager, entity, transform);
        Hurtbox3DComponentUVE hurtbox;
        hurtbox.halfExtents = halfExtents;
        hurtbox.collisionLayer = layer;
        hurtbox.collisionMask = mask;
        hurtbox.damageChannel = channel;
        entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(entity, hurtbox);
        sceneGraph.UpdateUVE(entityManager);
        return entity;
    }

    [[nodiscard]] Hitbox3DComponentUVE ComponentUVE(const EntityUVE entity) {
        return entityManager.GetComponentUVE<Hitbox3DComponentUVE>(entity);
    }

    void MoveUVE(const EntityUVE entity, const Math::Vector3UVE& position) {
        Scene::TransformComponentUVE transform = entityManager.GetComponentUVE<Scene::TransformComponentUVE>(entity);
        transform.localPosition = position;
        sceneGraph.SetLocalTransformUVE(entityManager, entity, transform);
        sceneGraph.UpdateUVE(entityManager);
    }
};

// -------------------------------------------------------------------------------------------------
// The scan: Physics::SyncHitboxes3DUVE()
// -------------------------------------------------------------------------------------------------

TEST_F(HitboxStrikeUVETest, AnOverlapIsRecordedWithItsDepthAndTheAxisTowardTheHurtbox) {
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    // A decoy on another channel: it must not appear in the list or the report.
    const EntityUVE other = MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile");

    const Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);

    const Hitbox3DComponentUVE struck = ComponentUVE(hitbox);
    ASSERT_EQ(struck.strikeCount, 1U);
    EXPECT_FALSE(struck.strikesTruncated);
    EXPECT_EQ(struck.strikes[0].hurtboxEntity, hurtbox);
    EXPECT_NE(struck.strikes[0].hurtboxEntity, other);
    // Two unit boxes 1.5m apart overlap by 2 - 1.5 along their X axes.
    EXPECT_NEAR(struck.strikes[0].penetrationDepth, 0.5F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.x, 1.0F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.y, 0.0F, kEpsilon);
    EXPECT_NEAR(struck.strikes[0].axis.z, 0.0F, kEpsilon);
    EXPECT_NEAR(Math::LengthUVE(struck.strikes[0].axis), 1.0F, kEpsilon);

    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.hitboxCount, 1U);
    EXPECT_EQ(report.hurtboxCount, 2U);
    EXPECT_FALSE(report.IsTruncatedUVE());
    EXPECT_EQ(report.strikes[0].hitbox, hitbox);
    EXPECT_EQ(report.strikes[0].hurtbox, hurtbox);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeUVETest, TheAxisPointsFromTheHitboxTowardTheHurtboxWhicheverSideItIsOn) {
    const EntityUVE east = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    const EntityUVE west = MakeHitboxUVE({4.0F, 0.0F, 0.0F});
    static_cast<void>(MakeHurtboxUVE({2.5F, 0.0F, 0.0F}));

    static_cast<void>(SyncHitboxes3DUVE(entityManager));

    EXPECT_NEAR(ComponentUVE(east).strikes[0].axis.x, 1.0F, kEpsilon)
        << "the hurtbox is to the +X side of the hitbox";
    EXPECT_NEAR(ComponentUVE(west).strikes[0].axis.x, -1.0F, kEpsilon)
        << "the hurtbox is to the -X side of the hitbox";
}

TEST_F(HitboxStrikeUVETest, AHitboxNeverStrikesAHurtboxOnItsOwnEntity) {
    const EntityUVE entity = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    entityManager.AddComponentUVE<Hurtbox3DComponentUVE>(entity, Hurtbox3DComponentUVE{});
    sceneGraph.UpdateUVE(entityManager);

    static_cast<void>(SyncHitboxes3DUVE(entityManager));

    EXPECT_EQ(ComponentUVE(entity).strikeCount, 0U);
}

TEST_F(HitboxStrikeUVETest, BothSidesHaveToAcceptTheOtherForAStrike) {
    // The hitbox accepts layer 2, the hurtbox is on layer 2 but only accepts layer 4.
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee", 1U,
                                           0xFFFFFFFFU);
    const EntityUVE refusingHurtbox =
        MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee", 2U, 0x04U);

    Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    EXPECT_TRUE(report.strikes.empty()) << "the hurtbox's own mask has the last word";

    // Now the other way around: the hurtbox accepts layer 1, the hitbox no longer looks for 2.
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(refusingHurtbox).collisionMask = 0xFFFFFFFFU;
    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).collisionMask = 0x04U;
    report = SyncHitboxes3DUVE(entityManager);
    EXPECT_TRUE(report.strikes.empty()) << "the hitbox's own mask has the last word too";

    // With both masks matching again, the same geometry strikes.
    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).collisionMask = 0xFFFFFFFFU;
    report = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, refusingHurtbox);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeUVETest, AStrikeNeedsTheSameDamageChannelOnBothSides) {
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee");
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "projectile"));

    Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 0U);

    // Matching channels: the same geometry - and only the matching hurtbox - strikes.
    const EntityUVE matching = MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F}, "melee");
    report = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, matching);
    EXPECT_EQ(report.strikes[0].damageChannel, "melee");
}

TEST_F(HitboxStrikeUVETest, DisabledOrInvalidSidesStrikeNothingAndStaleStateIsCleared) {
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);

    // Switching the HURTBOX off removes it from the candidate set entirely.
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).enabled = false;
    Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(report.hurtboxCount, 0U);
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 0U) << "the hitbox's list is refreshed, not appended";

    // A malformed hurtbox is refused the same way (a non-positive extent is not a volume).
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).enabled = true;
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).halfExtents.y = 0.0F;
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());

    // A malformed HITBOX ends the tick with zero strikes rather than with the previous list.
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).halfExtents = {1.0F, 1.0F, 1.0F};
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).enabled = false;
    report = SyncHitboxes3DUVE(entityManager);
    EXPECT_TRUE(report.strikes.empty());
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 0U);

    // Switched back on, the same geometry strikes again.
    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
}

TEST_F(HitboxStrikeUVETest, TouchingBoundariesAreNotStrikes) {
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    // Exactly adjacent: the boxes share a face and overlap by nothing.
    static_cast<void>(MakeHurtboxUVE({2.0F, 0.0F, 0.0F}));

    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 0U);
}

TEST_F(HitboxStrikeUVETest, TheStrikeListIsBoundedAndTheOverflowIsReported) {
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {1.0F, 1.0F, 1.0F});
    // One more overlapping hurtbox than the fixed storage can hold. They are spread along Z by
    // less than their own depth, so every one of them really does overlap (a box sitting exactly
    // on the boundary would be a separation, not a strike).
    for (std::size_t index = 0U; index < Scene::kMaximumHitbox3DStrikesUVE + 1U; ++index) {
        static_cast<void>(MakeHurtboxUVE(
            {1.5F, 0.0F, static_cast<float>(index) * 0.1F}, {1.0F, 1.0F, 2.0F}));
    }

    const Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);

    const Hitbox3DComponentUVE struck = ComponentUVE(hitbox);
    EXPECT_EQ(struck.strikeCount, Scene::kMaximumHitbox3DStrikesUVE);
    EXPECT_TRUE(struck.strikesTruncated) << "the overflow is a fact, not a silent drop";
    EXPECT_TRUE(report.IsTruncatedUVE());
    EXPECT_EQ(report.hurtboxCount, Scene::kMaximumHitbox3DStrikesUVE + 1U);
}

TEST_F(HitboxStrikeUVETest, TheAuthoredHalfExtentsAreUsedAndTheWorldScaleIsNot) {
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F}));
    const EntityUVE hurtbox = MakeHurtboxUVE({0.9F, 0.0F, 0.0F}, {0.5F, 0.5F, 0.5F});
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);

    // Scaling the hurtbox's own object does NOT grow its hurt volume: the volume is the authored
    // half-extents in world metres, the documented collider/area convention. A 10x scale still
    // leaves the boxes overlapping exactly as authored (a 0.1m overlap), never 5m of it.
    entityManager.GetComponentUVE<Scene::TransformComponentUVE>(hurtbox).localScale =
        Math::Vector3UVE{10.0F, 10.0F, 10.0F};
    sceneGraph.UpdateUVE(entityManager);
    const Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_NEAR(report.strikes[0].penetrationDepth, 0.1F, kEpsilon);

    // Moving it beyond the authored extents ends the strike regardless of the scale.
    MoveUVE(hurtbox, {1.5F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
}

TEST_F(HitboxStrikeUVETest, ARotatedVolumeIsSweptByTheExactOrientedBoxNotItsAabb) {
    // A long thin blade along +X, rotated 90 degrees about Y, so it points along -Z instead.
    Math::QuaternionUVE quarterTurn{};
    ASSERT_TRUE(Math::TryMakeAxisAngleUVE({0.0F, 1.0F, 0.0F}, 1.5707963F, quarterTurn));
    const EntityUVE blade = MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {2.0F, 0.1F, 0.1F}, "melee", 1U, 0xFFFFFFFFU,
                                          quarterTurn);
    // Straight down -Z: inside the rotated blade, and outside the axis-aligned one it would have
    // been without the rotation.
    const EntityUVE ahead = MakeHurtboxUVE({0.0F, 0.0F, -1.5F}, {0.2F, 0.2F, 0.2F});
    // Where the blade used to point: no longer covered.
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}, {0.2F, 0.2F, 0.2F}));

    const Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);

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

TEST_F(HitboxStrikeUVETest, TheReportCountsEveryHitboxAndHurtboxItVisited) {
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeHitboxUVE({10.0F, 0.0F, 0.0F}));
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeHurtboxUVE({20.0F, 0.0F, 0.0F}));

    const Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);

    EXPECT_EQ(report.hitboxCount, 2U) << "every hitbox is visited, striking or not";
    EXPECT_EQ(report.hurtboxCount, 2U);
    EXPECT_EQ(report.strikes.size(), 1U);
}

// -------------------------------------------------------------------------------------------------
// The consequence contract: Hitbox3DStrikeLifecycleTrackerUVE
// -------------------------------------------------------------------------------------------------

TEST_F(HitboxStrikeUVETest, AContinuousStrikeEntersOnceAndOnlySeparatingEmitsTheExit) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    const Hitbox3DStrikeLifecycleReportUVE first = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
    ASSERT_EQ(first.transitions.size(), 1U);
    EXPECT_EQ(first.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Entered);
    EXPECT_EQ(first.transitions[0].strike.hitbox, hitbox);
    EXPECT_EQ(first.transitions[0].strike.hurtbox, hurtbox);
    EXPECT_EQ(first.transitions[0].strike.damageChannel, "melee");
    EXPECT_EQ(first.previousActiveCount, 0U);
    EXPECT_EQ(first.currentActiveCount, 1U);

    // Two more ticks of the same overlap: the strike is still live, but it is not news again.
    for (int tick = 0; tick < 2; ++tick) {
        const Hitbox3DStrikeLifecycleReportUVE steady = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
        EXPECT_TRUE(steady.transitions.empty()) << "a strike that lasts is one strike, not one per frame";
        EXPECT_EQ(steady.currentActiveCount, 1U);
    }

    // Getting deeper is the same strike, not a new one - the transition carries the new evidence.
    MoveUVE(hurtbox, {1.0F, 0.0F, 0.0F});
    const Hitbox3DStrikeLifecycleReportUVE deeper = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
    EXPECT_TRUE(deeper.transitions.empty());

    // Separating is the exit, and it carries the pair (and the last-known channel) as evidence.
    MoveUVE(hurtbox, {10.0F, 0.0F, 0.0F});
    const Hitbox3DStrikeLifecycleReportUVE exited = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
    ASSERT_EQ(exited.transitions.size(), 1U);
    EXPECT_EQ(exited.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Exited);
    EXPECT_EQ(exited.transitions[0].strike.hitbox, hitbox);
    EXPECT_EQ(exited.transitions[0].strike.hurtbox, hurtbox);
    EXPECT_EQ(exited.transitions[0].strike.damageChannel, "melee");
    EXPECT_EQ(exited.currentActiveCount, 0U);

    // And nothing after that until it starts again.
    EXPECT_TRUE(tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager)).transitions.empty());
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);
}

TEST_F(HitboxStrikeUVETest, DisablingASideEndsTheStrikeOnThatVeryTick) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager)).transitions.size(), 1U);

    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).enabled = false;
    const Hitbox3DStrikeLifecycleReportUVE report = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));

    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Exited);
    EXPECT_EQ(report.transitions[0].strike.hurtbox, hurtbox)
        << "an exit says which pair ended, with the evidence it had while it was alive";
}

TEST_F(HitboxStrikeUVETest, ADestroyedHurtboxEndsTheStrikeInsteadOfLeakingIt) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager)).currentActiveCount, 1U);

    entityManager.DestroyEntityUVE(hurtbox);
    const Hitbox3DStrikeLifecycleReportUVE report = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));

    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Exited);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U) << "the tracker drops the pair, it does not leak it";
}

TEST_F(HitboxStrikeUVETest, ATruncatedSnapshotInventsNoExitsAndKeepsTheBaseline) {
    // A hitbox striking one hurtbox, then a scan whose pair list is bounded below what the scan
    // saw: the pairs beyond the bound are not gone, they are simply not in the report.
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    Hitbox3DSyncReportUVE live = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(tracker.UpdateUVE(live).currentActiveCount, 1U);

    Hitbox3DSyncReportUVE truncated = live;
    truncated.strikesTruncated = true;
    const Hitbox3DStrikeLifecycleReportUVE report = tracker.UpdateUVE(truncated);

    EXPECT_TRUE(report.transitions.empty()) << "an incomplete snapshot is not evidence of an exit";
    EXPECT_TRUE(report.inputSnapshotTruncated);
    EXPECT_EQ(report.currentActiveCount, 1U);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);

    // The next complete snapshot still knows the strike is live: no spurious exit-then-enter.
    const Hitbox3DStrikeLifecycleReportUVE recovered = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
    EXPECT_TRUE(recovered.transitions.empty());

    // An over-cap pair list is treated the same way, without needing the component flag.
    Hitbox3DSyncReportUVE overCap;
    overCap.strikes.assign(kMaximumHitbox3DStrikeResultsUVE + 1U,
                           Hitbox3DStrikePairUVE{hitbox, hurtbox, 0.5F, {1.0F, 0.0F, 0.0F}, "melee"});
    EXPECT_TRUE(tracker.UpdateUVE(overCap).inputSnapshotTruncated);
    EXPECT_EQ(tracker.GetActiveCountUVE(), 1U);
}

TEST_F(HitboxStrikeUVETest, ResetDiscardsTheBaselineWithoutFabricatingTransitions) {
    static_cast<void>(MakeHurtboxUVE({1.5F, 0.0F, 0.0F}));
    static_cast<void>(MakeHitboxUVE({0.0F, 0.0F, 0.0F}));

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    ASSERT_EQ(tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager)).currentActiveCount, 1U);

    tracker.ResetUVE();
    EXPECT_EQ(tracker.GetActiveCountUVE(), 0U);

    // The still-overlapping pairing reads as a fresh enter, because after a reset it genuinely is
    // one: there is no history left to diff against, and silence is the honest answer, not a lie
    // that the hit is still being tracked.
    const Hitbox3DStrikeLifecycleReportUVE report = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));
    ASSERT_EQ(report.transitions.size(), 1U);
    EXPECT_EQ(report.transitions[0].kind, Hitbox3DStrikeTransitionKindUVE::Entered);
    EXPECT_EQ(report.previousActiveCount, 0U);
}

TEST_F(HitboxStrikeUVETest, TransitionsComeOutInDeterministicHitboxThenHurtboxOrder) {
    // Two hitboxes and three hurtboxes, every pair overlapping: the report's order may not depend
    // on pool order or on which side enumerated first.
    const EntityUVE firstHitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F});
    const EntityUVE secondHitbox = MakeHitboxUVE({2.0F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F});
    std::vector<EntityUVE> hurtboxes;
    for (int index = 0; index < 3; ++index) {
        hurtboxes.push_back(MakeHurtboxUVE({1.0F + static_cast<float>(index) * 0.2F, 0.0F, 0.0F}, {4.0F, 4.0F, 4.0F}));
    }

    Hitbox3DStrikeLifecycleTrackerUVE tracker;
    const Hitbox3DStrikeLifecycleReportUVE report = tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager));

    ASSERT_EQ(report.transitions.size(), 6U);
    // Strictly increasing in (hitbox, hurtbox) order - the pair order the tracker contracts. Two
    // records for the same hitbox are ordered by their hurtbox, never equal.
    const auto orderedByPairUVE = [](const Hitbox3DStrikePairUVE& left, const Hitbox3DStrikePairUVE& right) {
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
        EXPECT_TRUE(orderedByPairUVE(report.transitions[index - 1U].strike, report.transitions[index].strike))
            << "sorted by hitbox first, then by hurtbox";
    }
    EXPECT_EQ(report.transitions.front().strike.hitbox, firstHitbox);
    EXPECT_EQ(report.transitions.front().strike.hurtbox, hurtboxes.front());
    EXPECT_EQ(report.transitions.back().strike.hitbox, secondHitbox);

    // And the whole thing is stable: a second identical scan emits nothing at all.
    EXPECT_TRUE(tracker.UpdateUVE(SyncHitboxes3DUVE(entityManager)).transitions.empty());
}

TEST_F(HitboxStrikeUVETest, AnIgnoredEntityIsNeverStruck) {
    const EntityUVE owner = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE other = MakeHurtboxUVE({-1.5F, 0.0F, 0.0F});
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).ignoreEntity = owner;

    Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hurtbox, other);
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 1U);
    EXPECT_EQ(ComponentUVE(hitbox).strikes[0].hurtboxEntity, other);

    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).ignoreEntity = kInvalidEntityUVE;
    report = SyncHitboxes3DUVE(entityManager);
    EXPECT_EQ(report.strikes.size(), 2U);
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 2U);
}

TEST_F(HitboxStrikeUVETest, AStrikeLandsOncePerActivationWithoutDroppingLaterOverlaps) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(ComponentUVE(hitbox).strikes[0].landed);
    EXPECT_EQ(ComponentUVE(hitbox).struckCount, 1U);

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_EQ(ComponentUVE(hitbox).strikeCount, 1U);
    EXPECT_FALSE(ComponentUVE(hitbox).strikes[0].landed);
    EXPECT_EQ(ComponentUVE(hitbox).strikes[0].hurtboxEntity, hurtbox);

    MoveUVE(hurtbox, {10.0F, 0.0F, 0.0F});
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
    EXPECT_EQ(ComponentUVE(hitbox).struckCount, 1U);
    MoveUVE(hurtbox, {1.5F, 0.0F, 0.0F});
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_FALSE(ComponentUVE(hitbox).strikes[0].landed);

    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).enabled = false;
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
    EXPECT_EQ(ComponentUVE(hitbox).struckCount, 0U);

    entityManager.GetComponentUVE<Hitbox3DComponentUVE>(hitbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(ComponentUVE(hitbox).strikes[0].landed);
}

TEST_F(HitboxStrikeUVETest, TheHurtboxRecordsIncomingHitsFromEveryAttacker) {
    const EntityUVE hurtbox = MakeHurtboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE east = MakeHitboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE west = MakeHitboxUVE({-1.5F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 2U);
    const Hurtbox3DComponentUVE received = entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox);
    EXPECT_EQ(received.hitCount, 2U);
    EXPECT_TRUE(Scene::Hurtbox3DUVE::HasHitFromUVE(received, east));
    EXPECT_TRUE(Scene::Hurtbox3DUVE::HasHitFromUVE(received, west));
    EXPECT_TRUE(received.hits[0].received);
    EXPECT_TRUE(received.hits[1].received);

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 2U);
    const Hurtbox3DComponentUVE stillHit = entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox);
    EXPECT_EQ(stillHit.hitCount, 2U);
    EXPECT_FALSE(stillHit.hits[0].received);
    EXPECT_FALSE(stillHit.hits[1].received);
    EXPECT_EQ(stillHit.receivedCount, 2U);
}

TEST_F(HitboxStrikeUVETest, AHurtboxIgnoreRefusesThatAttackerOnBothSides) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE ignored = MakeHitboxUVE({0.0F, 0.0F, 0.0F});
    const EntityUVE other = MakeHitboxUVE({3.0F, 0.0F, 0.0F});
    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).ignoreEntity = ignored;

    Hitbox3DSyncReportUVE report = SyncHitboxes3DUVE(entityManager);
    ASSERT_EQ(report.strikes.size(), 1U);
    EXPECT_EQ(report.strikes[0].hitbox, other);
    EXPECT_EQ(ComponentUVE(ignored).strikeCount, 0U);
    EXPECT_EQ(ComponentUVE(other).strikeCount, 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hitCount, 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hits[0].hitboxEntity, other);

    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).ignoreEntity = kInvalidEntityUVE;
    report = SyncHitboxes3DUVE(entityManager);
    EXPECT_EQ(report.strikes.size(), 2U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hitCount, 2U);
}

TEST_F(HitboxStrikeUVETest, AHurtboxReceivesOncePerEnableWithoutDroppingLaterHits) {
    const EntityUVE hurtbox = MakeHurtboxUVE({1.5F, 0.0F, 0.0F});
    const EntityUVE hitbox = MakeHitboxUVE({0.0F, 0.0F, 0.0F});

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hits[0].received);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).receivedCount, 1U);

    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hitCount, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hits[0].received);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hits[0].hitboxEntity, hitbox);

    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).enabled = false;
    EXPECT_TRUE(SyncHitboxes3DUVE(entityManager).strikes.empty());
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).receivedCount, 0U);
    EXPECT_EQ(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hitCount, 0U);

    entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).enabled = true;
    ASSERT_EQ(SyncHitboxes3DUVE(entityManager).strikes.size(), 1U);
    EXPECT_TRUE(entityManager.GetComponentUVE<Hurtbox3DComponentUVE>(hurtbox).hits[0].received);
}

TEST_F(HitboxStrikeUVETest, TheEventsCarryThePairAndCompareByValue) {
    const Hitbox3DStrikePairUVE strike{Scene::EntityUVE{3U, 1U}, Scene::EntityUVE{9U, 2U}, 0.25F,
                                      {0.0F, 1.0F, 0.0F}, "melee"};
    const Hitbox3DStrikeEnteredEventUVE entered{strike};
    const Hitbox3DStrikeExitedEventUVE exited{strike};

    EXPECT_EQ(Hitbox3DStrikeEnteredEventUVE{strike}, entered);
    EXPECT_EQ(Hitbox3DStrikeExitedEventUVE{strike}, exited);
    EXPECT_EQ(entered.strike.hitbox, (Scene::EntityUVE{3U, 1U}));
    EXPECT_EQ(entered.strike.hurtbox, (Scene::EntityUVE{9U, 2U}));
    EXPECT_FLOAT_EQ(entered.strike.penetrationDepth, 0.25F);
    EXPECT_EQ(entered.strike.damageChannel, "melee");
}

} // namespace
} // namespace UVE::Physics::Tests
