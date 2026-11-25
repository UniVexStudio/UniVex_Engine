// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <gtest/gtest.h>

#include "collider_component.h"
#include "rigid_3d_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "static_3d.h"

namespace UVE::Scene::Tests {
namespace {

TEST(Static3DTest, ImmovableHasNoInverseMass) {
    EXPECT_TRUE(Static3D::IsImmovable());
    EXPECT_FLOAT_EQ(Static3D::InverseMass(), 0.0F);
}

class Static3DWorldGeometryTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(Static3DWorldGeometryTest, ColliderOnlyIsWorldGeometryAndADynamicBodyIsNot) {
    const Entity wall = entityManager.CreateEntity();
    entityManager.AddComponent<ColliderComponent>(wall, ColliderComponent{});
    EXPECT_TRUE(Static3D::IsWorldGeometry(entityManager, wall));

    ColliderComponent disabled{};
    disabled.disabled = true;
    const Entity off = entityManager.CreateEntity();
    entityManager.AddComponent<ColliderComponent>(off, disabled);
    EXPECT_FALSE(Static3D::IsWorldGeometry(entityManager, off));

    const Entity crate = entityManager.CreateEntity();
    entityManager.AddComponent<ColliderComponent>(crate, ColliderComponent{});
    entityManager.AddComponent<Rigid3DComponent>(crate, Rigid3DComponent{});
    EXPECT_FALSE(Static3D::IsWorldGeometry(entityManager, crate));

    Rigid3DComponent kinematic{};
    kinematic.isKinematic = true;
    const Entity platform = entityManager.CreateEntity();
    entityManager.AddComponent<ColliderComponent>(platform, ColliderComponent{});
    entityManager.AddComponent<Rigid3DComponent>(platform, kinematic);
    EXPECT_TRUE(Static3D::IsWorldGeometry(entityManager, platform));

    EXPECT_FALSE(Static3D::IsWorldGeometry(entityManager, kInvalidEntity));
}

} // namespace
} // namespace UVE::Scene::Tests
