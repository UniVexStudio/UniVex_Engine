// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"

namespace UVE::Scene::Tests {
namespace {

struct Position final {
    float x = 0.0F;
};

struct Velocity final {
    float dx = 0.0F;
};

class EntityViewTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
};

TEST_F(EntityViewTest, View_MatchesEveryEntityCarryingAllTypes) {
    const Entity both = entityManager.CreateEntity();
    entityManager.AddComponent<Position>(both, 1.0F);
    entityManager.AddComponent<Velocity>(both, 2.0F);
    const Entity positionOnly = entityManager.CreateEntity();
    entityManager.AddComponent<Position>(positionOnly, 3.0F);
    const Entity bare = entityManager.CreateEntity(); // Component-less: matches nothing.
    static_cast<void>(bare);

    const EntityView<Position, Velocity> bothView = entityManager.View<Position, Velocity>();
    EXPECT_EQ(bothView.Size(), 1U);
    EXPECT_FALSE(bothView.IsEmpty());
    for (auto entry : bothView) {
        EXPECT_EQ(entry.entity, both);
        EXPECT_FLOAT_EQ(entry.Get<Position>().x, 1.0F);
        EXPECT_FLOAT_EQ(entry.Get<Velocity>().dx, 2.0F);
    }

    const EntityView<Position> positionView = entityManager.View<Position>();
    EXPECT_EQ(positionView.Size(), 2U);
    std::vector<Entity> seen;
    for (auto entry : positionView) {
        seen.push_back(entry.entity);
    }
    std::sort(seen.begin(), seen.end(),
              [](const Entity a, const Entity b) { return a.index < b.index; });
    EXPECT_EQ(seen, (std::vector<Entity>{both, positionOnly}));
}

TEST_F(EntityViewTest, Entry_MutatesLiveStorage) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Position>(entity, 1.0F);
    for (auto entry : entityManager.View<Position>()) {
        entry.Get<Position>().x += 10.0F;
    }
    EXPECT_FLOAT_EQ(entityManager.GetComponent<Position>(entity).x, 11.0F);
}

TEST_F(EntityViewTest, View_SnapshotsAndReQueries) {
    const Entity entity = entityManager.CreateEntity();
    entityManager.AddComponent<Position>(entity, 0.0F);
    const EntityView<Position> before = entityManager.View<Position>();
    EXPECT_EQ(before.Size(), 1U);
    const Entity late = entityManager.CreateEntity();
    entityManager.AddComponent<Position>(late, 0.0F);
    EXPECT_EQ(before.Size(), 1U); // The old view does not see the new entity.
    EXPECT_EQ(entityManager.View<Position>().Size(), 2U);
    EXPECT_TRUE(entityManager.View<Velocity>().IsEmpty());
}

} // namespace
} // namespace UVE::Scene::Tests
