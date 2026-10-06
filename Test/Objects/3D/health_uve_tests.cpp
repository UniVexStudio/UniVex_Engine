// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/health_uve.h"

#include <limits>

#include <gtest/gtest.h>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class HealthUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;
};

TEST_F(HealthUVETest, DefaultContractIsValidAndZeroMaxIsRefused) {
    EXPECT_TRUE(IsHealthComponentValidUVE(HealthComponentUVE{}));
    HealthComponentUVE bad{};
    bad.maxHealth = 0.0F;
    EXPECT_FALSE(IsHealthComponentValidUVE(bad));
    bad = {};
    bad.maxHealth = std::numeric_limits<float>::infinity();
    EXPECT_FALSE(IsHealthComponentValidUVE(bad));
}

TEST_F(HealthUVETest, DamageWalksToTheParentAndStopsAtZero) {
    const EntityUVE body = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, body, TransformComponentUVE{});
    entityManager.AddComponentUVE<HealthComponentUVE>(body, HealthComponentUVE{});

    const EntityUVE hurtbox = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, hurtbox, TransformComponentUVE{});
    sceneGraph.SetParentUVE(entityManager, hurtbox, body);

    EXPECT_EQ(FindHealthEntityUVE(entityManager, hurtbox), body);

    const HealthDamageResultUVE hit = ApplyHitboxStrikeToHealthUVE(entityManager, hurtbox, 40.0F);
    EXPECT_TRUE(hit.applied);
    EXPECT_FALSE(hit.depleted);
    EXPECT_EQ(hit.entity, body);
    EXPECT_FLOAT_EQ(hit.remaining, 60.0F);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(body).health, 60.0F);

    const HealthDamageResultUVE kill = ApplyHitboxStrikeToHealthUVE(entityManager, hurtbox, 60.0F);
    EXPECT_TRUE(kill.applied);
    EXPECT_TRUE(kill.depleted);
    EXPECT_FLOAT_EQ(kill.remaining, 0.0F);

    const HealthDamageResultUVE afterDeath = ApplyHitboxStrikeToHealthUVE(entityManager, hurtbox, 1.0F);
    EXPECT_FALSE(afterDeath.applied);
}

TEST_F(HealthUVETest, InvulnerableAndMissingHealthDoNothing) {
    const EntityUVE body = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, body, TransformComponentUVE{});
    HealthComponentUVE health{};
    health.invulnerable = true;
    entityManager.AddComponentUVE<HealthComponentUVE>(body, health);

    const HealthDamageResultUVE blocked = ApplyHitboxStrikeToHealthUVE(entityManager, body, 10.0F);
    EXPECT_FALSE(blocked.applied);
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<HealthComponentUVE>(body).health, 100.0F);

    const EntityUVE stray = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, stray, TransformComponentUVE{});
    const HealthDamageResultUVE missing = ApplyHitboxStrikeToHealthUVE(entityManager, stray, 1.0F);
    EXPECT_FALSE(missing.applied);
    EXPECT_EQ(missing.entity, kInvalidEntityUVE);
}

} // namespace
} // namespace UVE::Scene::Tests
