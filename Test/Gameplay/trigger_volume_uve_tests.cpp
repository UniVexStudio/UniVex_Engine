// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/gameplay/trigger_volume_uve.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

Physics::AreaOverlapQueryResultUVE SnapshotUVE(std::vector<Physics::AreaOverlapPairUVE> overlaps) {
    Physics::AreaOverlapQueryResultUVE snapshot;
    snapshot.overlaps = std::move(overlaps);
    return snapshot;
}

std::vector<Physics::AreaOverlapTransitionUVE> EntersUVE(
    std::vector<Physics::AreaOverlapPairUVE> pairs) {
    std::vector<Physics::AreaOverlapTransitionUVE> transitions;
    for (const Physics::AreaOverlapPairUVE& pair : pairs) {
        transitions.push_back(Physics::AreaOverlapTransitionUVE{
            Physics::AreaOverlapTransitionKindUVE::Entered, pair});
    }
    return transitions;
}

class TriggerVolumeUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};

    EntityUVE MakeTriggerUVE(const TriggerVolumeComponentUVE& volume = {}) {
        const EntityUVE entity = entityManager.CreateEntityUVE();
        entityManager.AddComponentUVE<TriggerVolumeComponentUVE>(entity, volume);
        return entity;
    }
};

TEST_F(TriggerVolumeUVETest, OneShot_FiresOnFirstEnterThenStaysSpentUntilRearmed) {
    const EntityUVE trigger = MakeTriggerUVE();
    const EntityUVE body = entityManager.CreateEntityUVE();
    const Physics::AreaOverlapQueryResultUVE occupied = SnapshotUVE({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransitionUVE> enter = EntersUVE({{trigger, body, 0.0F}});

    const std::vector<TriggerFiredResultUVE> first =
        UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().trigger, trigger);
    EXPECT_EQ(first.front().interactor, body);
    EXPECT_EQ(first.front().firedCount, 1U);
    EXPECT_FALSE(entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(trigger).armed);

    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F).empty());

    EXPECT_TRUE(RearmTriggerVolumeUVE(entityManager, trigger));
    const std::vector<TriggerFiredResultUVE> second =
        UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second.front().firedCount, 2U);

    EXPECT_FALSE(RearmTriggerVolumeUVE(entityManager, body));
    EXPECT_TRUE(DisarmTriggerVolumeUVE(entityManager, trigger));
    EXPECT_FALSE(DisarmTriggerVolumeUVE(entityManager, body));
    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F).empty());
}

TEST_F(TriggerVolumeUVETest, EveryEnter_FiresOnEachEnterInEntityOrderAndNeverOnExit) {
    TriggerVolumeComponentUVE volume;
    volume.policy = TriggerFirePolicyUVE::EveryEnter;
    const EntityUVE first = MakeTriggerUVE(volume);
    const EntityUVE second = MakeTriggerUVE(volume);
    const EntityUVE body = entityManager.CreateEntityUVE();
    const Physics::AreaOverlapQueryResultUVE occupied =
        SnapshotUVE({{first, body, 0.0F}, {second, body, 0.0F}});
    // Transitions deliberately out of entity order; results still come back sorted.
    const std::vector<Physics::AreaOverlapTransitionUVE> enters =
        EntersUVE({{second, body, 0.0F}, {first, body, 0.0F}});

    const std::vector<TriggerFiredResultUVE> fires =
        UpdateTriggerVolumesUVE(entityManager, occupied, enters, 1.0F);
    ASSERT_EQ(fires.size(), 2U);
    EXPECT_EQ(fires[0].trigger, first);
    EXPECT_EQ(fires[1].trigger, second);
    EXPECT_TRUE(entityManager.GetComponentUVE<TriggerVolumeComponentUVE>(first).armed);

    const std::vector<Physics::AreaOverlapTransitionUVE> exits = {
        Physics::AreaOverlapTransitionUVE{Physics::AreaOverlapTransitionKindUVE::Exited,
                                          {first, body, 0.0F}}};
    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, SnapshotUVE({}), exits, 1.0F).empty());
}

TEST_F(TriggerVolumeUVETest, WhileOccupied_TicksOnCadenceAndStopsWhenEmpty) {
    TriggerVolumeComponentUVE volume;
    volume.policy = TriggerFirePolicyUVE::WhileOccupied;
    volume.intervalSeconds = 2.0F;
    const EntityUVE trigger = MakeTriggerUVE(volume);
    const EntityUVE body = entityManager.CreateEntityUVE();
    const Physics::AreaOverlapQueryResultUVE occupied = SnapshotUVE({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransitionUVE> enter = EntersUVE({{trigger, body, 0.0F}});

    // The enter fires and restarts the cadence clock: no double fire on the entry frame.
    ASSERT_EQ(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F).size(), 1U);
    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, occupied, {}, 1.0F).empty());

    const std::vector<TriggerFiredResultUVE> tick =
        UpdateTriggerVolumesUVE(entityManager, occupied, {}, 1.0F);
    ASSERT_EQ(tick.size(), 1U);
    EXPECT_EQ(tick.front().interactor, body);
    EXPECT_EQ(tick.front().firedCount, 2U);

    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, SnapshotUVE({}), {}, 1.0F).empty());

    const std::vector<TriggerFiredResultUVE> reenter =
        UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(reenter.size(), 1U);
    EXPECT_EQ(reenter.front().firedCount, 3U);
}

TEST_F(TriggerVolumeUVETest, Cooldown_BlocksRefiresUntilItLapses) {
    TriggerVolumeComponentUVE volume;
    volume.policy = TriggerFirePolicyUVE::EveryEnter;
    volume.cooldownSeconds = 5.0F;
    const EntityUVE trigger = MakeTriggerUVE(volume);
    const EntityUVE body = entityManager.CreateEntityUVE();
    const Physics::AreaOverlapQueryResultUVE occupied = SnapshotUVE({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransitionUVE> enter = EntersUVE({{trigger, body, 0.0F}});

    ASSERT_EQ(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F).size(), 1U);
    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F).empty());
    EXPECT_TRUE(UpdateTriggerVolumesUVE(entityManager, occupied, enter, 3.0F).empty());

    const std::vector<TriggerFiredResultUVE> refire =
        UpdateTriggerVolumesUVE(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(refire.size(), 1U);
    EXPECT_EQ(refire.front().firedCount, 2U);
}

TEST_F(TriggerVolumeUVETest, BadStepsFreezeClocksButNeverDropEdgesOrTouchBrokenVolumes) {
    const EntityUVE trigger = MakeTriggerUVE();
    const EntityUVE body = entityManager.CreateEntityUVE();
    TriggerVolumeComponentUVE broken;
    broken.policy = static_cast<TriggerFirePolicyUVE>(7U);
    const EntityUVE brokenTrigger = MakeTriggerUVE(broken);
    const Physics::AreaOverlapQueryResultUVE occupied =
        SnapshotUVE({{trigger, body, 0.0F}, {brokenTrigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransitionUVE> enters =
        EntersUVE({{trigger, body, 0.0F}, {brokenTrigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransitionUVE> enter = EntersUVE({{trigger, body, 0.0F}});

    const std::vector<TriggerFiredResultUVE> zeroStep =
        UpdateTriggerVolumesUVE(entityManager, occupied, enters, 0.0F);
    ASSERT_EQ(zeroStep.size(), 1U);
    EXPECT_EQ(zeroStep.front().trigger, trigger);

    ASSERT_TRUE(RearmTriggerVolumeUVE(entityManager, trigger));
    EXPECT_EQ(UpdateTriggerVolumesUVE(entityManager, occupied, enter,
                                      std::numeric_limits<float>::quiet_NaN())
                  .size(),
              1U);
    ASSERT_TRUE(RearmTriggerVolumeUVE(entityManager, trigger));
    EXPECT_EQ(UpdateTriggerVolumesUVE(entityManager, occupied, enter, -1.0F).size(), 1U);
}

TEST_F(TriggerVolumeUVETest, Validity_RejectsBadPoliciesTimesAndClockDrift) {
    EXPECT_TRUE(IsTriggerVolumeComponentValidUVE(TriggerVolumeComponentUVE{}));

    TriggerVolumeComponentUVE bad;
    bad.policy = static_cast<TriggerFirePolicyUVE>(7U);
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));

    bad = {};
    bad.intervalSeconds = 0.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
    bad.intervalSeconds = -1.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
    bad.intervalSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));

    bad = {};
    bad.cooldownSeconds = -1.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
    bad.cooldownSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));

    bad = {};
    bad.cooldownRemaining = -0.5F;
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
    bad.cooldownRemaining = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));

    bad = {};
    bad.intervalRemaining = -0.5F;
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
    bad.intervalRemaining = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTriggerVolumeComponentValidUVE(bad));
}

} // namespace
} // namespace UVE::Scene::Tests
