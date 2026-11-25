// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "entity_enabled.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

class EntityEnabledTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(EntityEnabledTest, FreshEntity_IsEnabledByAbsenceOfTheTag) {
    const Entity entity = entityManager.CreateEntity();
    EXPECT_TRUE(IsEntityEnabled(entityManager, entity));
    EXPECT_FALSE(entityManager.HasComponent<DisabledComponent>(entity));
}

TEST_F(EntityEnabledTest, SetEntityEnabled_RoundTripsThroughTheTag) {
    const Entity entity = entityManager.CreateEntity();
    SetEntityEnabled(entityManager, entity, false);
    EXPECT_FALSE(IsEntityEnabled(entityManager, entity));
    EXPECT_TRUE(entityManager.HasComponent<DisabledComponent>(entity));
    SetEntityEnabled(entityManager, entity, true);
    EXPECT_TRUE(IsEntityEnabled(entityManager, entity));
    EXPECT_FALSE(entityManager.HasComponent<DisabledComponent>(entity));
}

TEST_F(EntityEnabledTest, SetEntityEnabled_IsIdempotent) {
    const Entity entity = entityManager.CreateEntity();
    SetEntityEnabled(entityManager, entity, true); // Already enabled: no tag appears.
    EXPECT_FALSE(entityManager.HasComponent<DisabledComponent>(entity));
    SetEntityEnabled(entityManager, entity, false);
    SetEntityEnabled(entityManager, entity, false); // Already disabled: no double-add assert.
    EXPECT_TRUE(entityManager.HasComponent<DisabledComponent>(entity));
}

TEST_F(EntityEnabledTest, StaleHandle_AssertsInDebugAndFailsClosedInRelease) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.DestroyEntity(entity);
    EXPECT_DEATH({ static_cast<void>(IsEntityEnabled(entityManager, entity)); }, "");
    EXPECT_DEATH({ SetEntityEnabled(entityManager, entity, false); }, "");
}

} // namespace
} // namespace UVE::Scene::Tests
