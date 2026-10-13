// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <vector>

#include <gtest/gtest.h>

#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"

namespace UVE::Scene::Tests {
namespace {

struct PositionUVE final {
    float x = 0.0F;
};

struct VelocityUVE final {
    float dx = 0.0F;
};

class EntityViewUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
};

TEST_F(EntityViewUVETest, View_MatchesEveryEntityCarryingAllTypes) {
    const EntityUVE both = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionUVE>(both, 1.0F);
    entityManager.AddComponentUVE<VelocityUVE>(both, 2.0F);
    const EntityUVE positionOnly = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionUVE>(positionOnly, 3.0F);
    const EntityUVE bare = entityManager.CreateEntityUVE(); // Component-less: matches nothing.
    static_cast<void>(bare);

    const EntityViewUVE<PositionUVE, VelocityUVE> bothView = entityManager.ViewUVE<PositionUVE, VelocityUVE>();
    EXPECT_EQ(bothView.SizeUVE(), 1U);
    EXPECT_FALSE(bothView.IsEmptyUVE());
    for (auto entry : bothView) {
        EXPECT_EQ(entry.entity, both);
        EXPECT_FLOAT_EQ(entry.GetUVE<PositionUVE>().x, 1.0F);
        EXPECT_FLOAT_EQ(entry.GetUVE<VelocityUVE>().dx, 2.0F);
    }

    const EntityViewUVE<PositionUVE> positionView = entityManager.ViewUVE<PositionUVE>();
    EXPECT_EQ(positionView.SizeUVE(), 2U);
    std::vector<EntityUVE> seen;
    for (auto entry : positionView) {
        seen.push_back(entry.entity);
    }
    std::sort(seen.begin(), seen.end(),
              [](const EntityUVE a, const EntityUVE b) { return a.index < b.index; });
    EXPECT_EQ(seen, (std::vector<EntityUVE>{both, positionOnly}));
}

TEST_F(EntityViewUVETest, Entry_MutatesLiveStorage) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionUVE>(entity, 1.0F);
    for (auto entry : entityManager.ViewUVE<PositionUVE>()) {
        entry.GetUVE<PositionUVE>().x += 10.0F;
    }
    EXPECT_FLOAT_EQ(entityManager.GetComponentUVE<PositionUVE>(entity).x, 11.0F);
}

TEST_F(EntityViewUVETest, View_SnapshotsAndReQueries) {
    const EntityUVE entity = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionUVE>(entity, 0.0F);
    const EntityViewUVE<PositionUVE> before = entityManager.ViewUVE<PositionUVE>();
    EXPECT_EQ(before.SizeUVE(), 1U);
    const EntityUVE late = entityManager.CreateEntityUVE();
    entityManager.AddComponentUVE<PositionUVE>(late, 0.0F);
    EXPECT_EQ(before.SizeUVE(), 1U); // The old view does not see the new entity.
    EXPECT_EQ(entityManager.ViewUVE<PositionUVE>().SizeUVE(), 2U);
    EXPECT_TRUE(entityManager.ViewUVE<VelocityUVE>().IsEmptyUVE());
}

} // namespace
} // namespace UVE::Scene::Tests
