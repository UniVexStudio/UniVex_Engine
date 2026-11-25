// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "trigger_volume.h"

#include <limits>
#include <vector>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

Physics::AreaOverlapQueryResult Snapshot(std::vector<Physics::AreaOverlapPair> overlaps) {
    Physics::AreaOverlapQueryResult snapshot;
    snapshot.overlaps = std::move(overlaps);
    return snapshot;
}

std::vector<Physics::AreaOverlapTransition> Enters(
    std::vector<Physics::AreaOverlapPair> pairs) {
    std::vector<Physics::AreaOverlapTransition> transitions;
    for (const Physics::AreaOverlapPair& pair : pairs) {
        transitions.push_back(Physics::AreaOverlapTransition{
            Physics::AreaOverlapTransitionKind::Entered, pair});
    }
    return transitions;
}

class TriggerVolumeTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};

    Entity MakeTrigger(const TriggerVolumeComponent& volume = {}) {
        const Entity entity = entityManager.CreateEntity();
        entityManager.AddComponent<TriggerVolumeComponent>(entity, volume);
        return entity;
    }
};

TEST_F(TriggerVolumeTest, OneShot_FiresOnFirstEnterThenStaysSpentUntilRearmed) {
    const Entity trigger = MakeTrigger();
    const Entity body = entityManager.CreateEntity();
    const Physics::AreaOverlapQueryResult occupied = Snapshot({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransition> enter = Enters({{trigger, body, 0.0F}});

    const std::vector<TriggerFiredResult> first =
        UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(first.size(), 1U);
    EXPECT_EQ(first.front().trigger, trigger);
    EXPECT_EQ(first.front().interactor, body);
    EXPECT_EQ(first.front().firedCount, 1U);
    EXPECT_FALSE(entityManager.GetComponent<TriggerVolumeComponent>(trigger).armed);

    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F).empty());

    EXPECT_TRUE(RearmTriggerVolume(entityManager, trigger));
    const std::vector<TriggerFiredResult> second =
        UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(second.size(), 1U);
    EXPECT_EQ(second.front().firedCount, 2U);

    EXPECT_FALSE(RearmTriggerVolume(entityManager, body));
    EXPECT_TRUE(DisarmTriggerVolume(entityManager, trigger));
    EXPECT_FALSE(DisarmTriggerVolume(entityManager, body));
    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F).empty());
}

TEST_F(TriggerVolumeTest, EveryEnter_FiresOnEachEnterInEntityOrderAndNeverOnExit) {
    TriggerVolumeComponent volume;
    volume.policy = TriggerFirePolicy::EveryEnter;
    const Entity first = MakeTrigger(volume);
    const Entity second = MakeTrigger(volume);
    const Entity body = entityManager.CreateEntity();
    const Physics::AreaOverlapQueryResult occupied =
        Snapshot({{first, body, 0.0F}, {second, body, 0.0F}});
    // Transitions deliberately out of entity order; results still come back sorted.
    const std::vector<Physics::AreaOverlapTransition> enters =
        Enters({{second, body, 0.0F}, {first, body, 0.0F}});

    const std::vector<TriggerFiredResult> fires =
        UpdateTriggerVolumes(entityManager, occupied, enters, 1.0F);
    ASSERT_EQ(fires.size(), 2U);
    EXPECT_EQ(fires[0].trigger, first);
    EXPECT_EQ(fires[1].trigger, second);
    EXPECT_TRUE(entityManager.GetComponent<TriggerVolumeComponent>(first).armed);

    const std::vector<Physics::AreaOverlapTransition> exits = {
        Physics::AreaOverlapTransition{Physics::AreaOverlapTransitionKind::Exited,
                                          {first, body, 0.0F}}};
    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, Snapshot({}), exits, 1.0F).empty());
}

TEST_F(TriggerVolumeTest, WhileOccupied_TicksOnCadenceAndStopsWhenEmpty) {
    TriggerVolumeComponent volume;
    volume.policy = TriggerFirePolicy::WhileOccupied;
    volume.intervalSeconds = 2.0F;
    const Entity trigger = MakeTrigger(volume);
    const Entity body = entityManager.CreateEntity();
    const Physics::AreaOverlapQueryResult occupied = Snapshot({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransition> enter = Enters({{trigger, body, 0.0F}});

    // The enter fires and restarts the cadence clock: no double fire on the entry frame.
    ASSERT_EQ(UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F).size(), 1U);
    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, occupied, {}, 1.0F).empty());

    const std::vector<TriggerFiredResult> tick =
        UpdateTriggerVolumes(entityManager, occupied, {}, 1.0F);
    ASSERT_EQ(tick.size(), 1U);
    EXPECT_EQ(tick.front().interactor, body);
    EXPECT_EQ(tick.front().firedCount, 2U);

    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, Snapshot({}), {}, 1.0F).empty());

    const std::vector<TriggerFiredResult> reenter =
        UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(reenter.size(), 1U);
    EXPECT_EQ(reenter.front().firedCount, 3U);
}

TEST_F(TriggerVolumeTest, Cooldown_BlocksRefiresUntilItLapses) {
    TriggerVolumeComponent volume;
    volume.policy = TriggerFirePolicy::EveryEnter;
    volume.cooldownSeconds = 5.0F;
    const Entity trigger = MakeTrigger(volume);
    const Entity body = entityManager.CreateEntity();
    const Physics::AreaOverlapQueryResult occupied = Snapshot({{trigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransition> enter = Enters({{trigger, body, 0.0F}});

    ASSERT_EQ(UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F).size(), 1U);
    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F).empty());
    EXPECT_TRUE(UpdateTriggerVolumes(entityManager, occupied, enter, 3.0F).empty());

    const std::vector<TriggerFiredResult> refire =
        UpdateTriggerVolumes(entityManager, occupied, enter, 1.0F);
    ASSERT_EQ(refire.size(), 1U);
    EXPECT_EQ(refire.front().firedCount, 2U);
}

TEST_F(TriggerVolumeTest, BadStepsFreezeClocksButNeverDropEdgesOrTouchBrokenVolumes) {
    const Entity trigger = MakeTrigger();
    const Entity body = entityManager.CreateEntity();
    TriggerVolumeComponent broken;
    broken.policy = static_cast<TriggerFirePolicy>(7U);
    const Entity brokenTrigger = MakeTrigger(broken);
    const Physics::AreaOverlapQueryResult occupied =
        Snapshot({{trigger, body, 0.0F}, {brokenTrigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransition> enters =
        Enters({{trigger, body, 0.0F}, {brokenTrigger, body, 0.0F}});
    const std::vector<Physics::AreaOverlapTransition> enter = Enters({{trigger, body, 0.0F}});

    const std::vector<TriggerFiredResult> zeroStep =
        UpdateTriggerVolumes(entityManager, occupied, enters, 0.0F);
    ASSERT_EQ(zeroStep.size(), 1U);
    EXPECT_EQ(zeroStep.front().trigger, trigger);

    ASSERT_TRUE(RearmTriggerVolume(entityManager, trigger));
    EXPECT_EQ(UpdateTriggerVolumes(entityManager, occupied, enter,
                                      std::numeric_limits<float>::quiet_NaN())
                  .size(),
              1U);
    ASSERT_TRUE(RearmTriggerVolume(entityManager, trigger));
    EXPECT_EQ(UpdateTriggerVolumes(entityManager, occupied, enter, -1.0F).size(), 1U);
}

TEST_F(TriggerVolumeTest, Validity_RejectsBadPoliciesTimesAndClockDrift) {
    EXPECT_TRUE(IsTriggerVolumeComponentValid(TriggerVolumeComponent{}));

    TriggerVolumeComponent bad;
    bad.policy = static_cast<TriggerFirePolicy>(7U);
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));

    bad = {};
    bad.intervalSeconds = 0.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
    bad.intervalSeconds = -1.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
    bad.intervalSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));

    bad = {};
    bad.cooldownSeconds = -1.0F;
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
    bad.cooldownSeconds = std::numeric_limits<float>::quiet_NaN();
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));

    bad = {};
    bad.cooldownRemaining = -0.5F;
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
    bad.cooldownRemaining = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));

    bad = {};
    bad.intervalRemaining = -0.5F;
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
    bad.intervalRemaining = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsTriggerVolumeComponentValid(bad));
}

} // namespace
} // namespace UVE::Scene::Tests
