// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/entity/entity_enabled_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

class EntityEnabledUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(EntityEnabledUVETest, FreshEntity_IsEnabledByAbsenceOfTheTag) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    EXPECT_TRUE(IsEntityEnabledUVE(entityManager, entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<DisabledComponentUVE>(entity));
}

TEST_F(EntityEnabledUVETest, SetEntityEnabled_RoundTripsThroughTheTag) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    SetEntityEnabledUVE(entityManager, entity, false);
    EXPECT_FALSE(IsEntityEnabledUVE(entityManager, entity));
    EXPECT_TRUE(entityManager.HasComponentUVE<DisabledComponentUVE>(entity));
    SetEntityEnabledUVE(entityManager, entity, true);
    EXPECT_TRUE(IsEntityEnabledUVE(entityManager, entity));
    EXPECT_FALSE(entityManager.HasComponentUVE<DisabledComponentUVE>(entity));
}

TEST_F(EntityEnabledUVETest, SetEntityEnabled_IsIdempotent) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    SetEntityEnabledUVE(entityManager, entity, true); // Already enabled: no tag appears.
    EXPECT_FALSE(entityManager.HasComponentUVE<DisabledComponentUVE>(entity));
    SetEntityEnabledUVE(entityManager, entity, false);
    SetEntityEnabledUVE(entityManager, entity, false); // Already disabled: no double-add assert.
    EXPECT_TRUE(entityManager.HasComponentUVE<DisabledComponentUVE>(entity));
}

TEST_F(EntityEnabledUVETest, StaleHandle_AssertsInDebugAndFailsClosedInRelease) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.DestroyEntityUVE(entity);
    EXPECT_DEATH({ static_cast<void>(IsEntityEnabledUVE(entityManager, entity)); }, "");
    EXPECT_DEATH({ SetEntityEnabledUVE(entityManager, entity, false); }, "");
}

} // namespace
} // namespace UVE::Scene::Tests
