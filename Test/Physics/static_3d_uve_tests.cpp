// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "uve/component/collider_component_uve.h"
#include "uve/component/rigid_3d_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/objects/3d/static_3d_uve.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Static3DUVETest, ImmovableHasNoInverseMass) {
    EXPECT_TRUE(Static3DUVE::IsImmovableUVE());
    EXPECT_FLOAT_EQ(Static3DUVE::InverseMassUVE(), 0.0F);
}

class Static3DUVEWorldGeometryTest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(Static3DUVEWorldGeometryTest, ColliderOnlyIsWorldGeometryAndADynamicBodyIsNot) {
    const EntityUVE wall = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ColliderComponentUVE>(wall, ColliderComponentUVE{});
    EXPECT_TRUE(Static3DUVE::IsWorldGeometryUVE(entityManager, wall));

    ColliderComponentUVE disabled{};
    disabled.disabled = true;
    const EntityUVE off = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ColliderComponentUVE>(off, disabled);
    EXPECT_FALSE(Static3DUVE::IsWorldGeometryUVE(entityManager, off));

    const EntityUVE crate = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ColliderComponentUVE>(crate, ColliderComponentUVE{});
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(crate, Rigid3DComponentUVE{});
    EXPECT_FALSE(Static3DUVE::IsWorldGeometryUVE(entityManager, crate));

    Rigid3DComponentUVE kinematic{};
    kinematic.isKinematic = true;
    const EntityUVE platform = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<ColliderComponentUVE>(platform, ColliderComponentUVE{});
    entityManager.AddComponentUVE<Rigid3DComponentUVE>(platform, kinematic);
    EXPECT_TRUE(Static3DUVE::IsWorldGeometryUVE(entityManager, platform));

    EXPECT_FALSE(Static3DUVE::IsWorldGeometryUVE(entityManager, kInvalidEntityUVE));
}

} // namespace
} // namespace UVE::Scene::Tests
