// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "health.h"

#include <limits>

#include <gtest/gtest.h>

#include "hierarchy_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class HealthTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;
};

TEST_F(HealthTest, DefaultContractIsValidAndZeroMaxIsRefused) {
    EXPECT_TRUE(IsHealthComponentValid(HealthComponent{}));
    HealthComponent bad{};
    bad.maxHealth = 0.0F;
    EXPECT_FALSE(IsHealthComponentValid(bad));
    bad = {};
    bad.maxHealth = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsHealthComponentValid(bad));
}

TEST_F(HealthTest, DamageWalksToTheParentAndStopsAtZero) {
    const Entity body = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, body, TransformComponent{});
    entityManager.AddComponent<HealthComponent>(body, HealthComponent{});

    const Entity hurtbox = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, hurtbox, TransformComponent{});
    sceneGraph.SetParent(entityManager, hurtbox, body);

    EXPECT_EQ(FindHealthEntity(entityManager, hurtbox), body);

    const HealthDamageResult hit = ApplyHitboxStrikeToHealth(entityManager, hurtbox, 40.0F);
    EXPECT_TRUE(hit.applied);
    EXPECT_FALSE(hit.depleted);
    EXPECT_EQ(hit.entity, body);
    EXPECT_FLOAT_EQ(hit.remaining, 60.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(body).health, 60.0F);

    const HealthDamageResult kill = ApplyHitboxStrikeToHealth(entityManager, hurtbox, 60.0F);
    EXPECT_TRUE(kill.applied);
    EXPECT_TRUE(kill.depleted);
    EXPECT_FLOAT_EQ(kill.remaining, 0.0F);

    const HealthDamageResult afterDeath = ApplyHitboxStrikeToHealth(entityManager, hurtbox, 1.0F);
    EXPECT_FALSE(afterDeath.applied);
}

TEST_F(HealthTest, InvulnerableAndMissingHealthDoNothing) {
    const Entity body = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, body, TransformComponent{});
    HealthComponent health{};
    health.invulnerable = true;
    entityManager.AddComponent<HealthComponent>(body, health);

    const HealthDamageResult blocked = ApplyHitboxStrikeToHealth(entityManager, body, 10.0F);
    EXPECT_FALSE(blocked.applied);
    EXPECT_FLOAT_EQ(entityManager.GetComponent<HealthComponent>(body).health, 100.0F);

    const Entity stray = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, stray, TransformComponent{});
    const HealthDamageResult missing = ApplyHitboxStrikeToHealth(entityManager, stray, 1.0F);
    EXPECT_FALSE(missing.applied);
    EXPECT_EQ(missing.entity, kInvalidEntity);
}

TEST_F(HealthTest, HealRestoresUpToMaxAndIgnoresInvulnerability) {
    HealthComponent health{};
    static_cast<void>(ApplyHealthDamage(health, 30.0F));
    ASSERT_FLOAT_EQ(health.health, 70.0F);

    const HealthDamageResult first = ApplyHealthHeal(health, 20.0F);
    EXPECT_TRUE(first.applied);
    EXPECT_FALSE(first.depleted);
    EXPECT_FLOAT_EQ(first.amount, 20.0F);
    EXPECT_FLOAT_EQ(first.remaining, 90.0F);

    health.invulnerable = true;
    const HealthDamageResult through = ApplyHealthHeal(health, 20.0F);
    EXPECT_TRUE(through.applied);
    EXPECT_FLOAT_EQ(through.amount, 20.0F);
    EXPECT_FLOAT_EQ(through.remaining, 100.0F);
    EXPECT_FLOAT_EQ(health.health, 100.0F);

    EXPECT_FALSE(ApplyHealthHeal(health, 10.0F).applied);
    EXPECT_FALSE(ApplyHealthHeal(health, 0.0F).applied);
    EXPECT_FALSE(ApplyHealthHeal(health, -5.0F).applied);
    EXPECT_FALSE(ApplyHealthHeal(health, std::numeric_limits<float>::quiet_NaN()).applied);
}

TEST_F(HealthTest, HealRevivesFromZeroButNotTheInvalid) {
    HealthComponent health{};
    static_cast<void>(ApplyHealthDamage(health, 200.0F));
    ASSERT_FLOAT_EQ(health.health, 0.0F);

    const HealthDamageResult revived = ApplyHealthHeal(health, 50.0F);
    EXPECT_TRUE(revived.applied);
    EXPECT_FLOAT_EQ(revived.remaining, 50.0F);

    HealthComponent bad{};
    bad.maxHealth = 0.0F;
    EXPECT_FALSE(ApplyHealthHeal(bad, 10.0F).applied);
}

} // namespace
} // namespace UVE::Scene::Tests
