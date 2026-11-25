// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "hierarchy_component.h"
#include "transform_component.h"
#include "entity_manager.h"
#include "event_system.h"
#include "memory_manager.h"
#include "children_index.h"
#include "scene_graph.h"

namespace UVE::Scene::Tests {
namespace {

class ChildrenIndexTest : public ::testing::Test {
protected:
    Memory::MemoryManager memoryManager;
    Events::EventSystem eventSystem;
    EntityManager entityManager{memoryManager.GetDefaultAllocator(), eventSystem};
    SceneGraph sceneGraph;

    Entity AttachChild(const Entity parent) {
        const Entity child = entityManager.CreateEntity();
        sceneGraph.AttachTransform(entityManager, child, TransformComponent{});
        sceneGraph.SetParent(entityManager, child, parent);
        return child;
    }
};

TEST_F(ChildrenIndexTest, Rebuild_AnswersIdenticallyToTheScan) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    const Entity first = AttachChild(parent);
    const Entity second = AttachChild(parent);
    const Entity third = AttachChild(parent);

    ChildrenIndex index;
    EXPECT_TRUE(index.IsEmpty());
    index.Rebuild(entityManager);
    EXPECT_FALSE(index.IsEmpty());
    const std::vector<Entity> scanned = sceneGraph.GetChildren(entityManager, parent);
    ASSERT_EQ(scanned.size(), 3U);
    const std::span<const Entity> indexed = index.GetChildren(parent);
    ASSERT_EQ(indexed.size(), 3U);
    // Identical answers, same sibling order (creation order here).
    EXPECT_EQ(indexed[0U], first);
    EXPECT_EQ(indexed[1U], second);
    EXPECT_EQ(indexed[2U], third);
    EXPECT_EQ(std::vector<Entity>(indexed.begin(), indexed.end()), scanned);
    // Childless parents answer empty, not garbage.
    EXPECT_TRUE(index.GetChildren(first).empty());
    // Roots hang under the invalid entity (their parent link), exactly as the scan answers.
    const std::vector<Entity> scannedRoots = sceneGraph.GetChildren(entityManager, kInvalidEntity);
    const std::span<const Entity> indexedRoots = index.GetChildren(kInvalidEntity);
    EXPECT_EQ(std::vector<Entity>(indexedRoots.begin(), indexedRoots.end()), scannedRoots);
    ASSERT_EQ(indexedRoots.size(), 1U);
    EXPECT_EQ(indexedRoots[0U], parent);
}

TEST_F(ChildrenIndexTest, Rebuild_SeesReparentsOnlyAfterRebuilding) {
    const Entity oldParent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, oldParent, TransformComponent{});
    const Entity newParent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, newParent, TransformComponent{});
    const Entity child = AttachChild(oldParent);

    ChildrenIndex index;
    index.Rebuild(entityManager);
    ASSERT_EQ(index.GetChildren(oldParent).size(), 1U);
    sceneGraph.SetParent(entityManager, child, newParent);
    // Stale until rebuilt: the documented freshness contract, pinned, not assumed.
    EXPECT_EQ(index.GetChildren(oldParent).size(), 1U);
    EXPECT_TRUE(index.GetChildren(newParent).empty());
    index.Rebuild(entityManager);
    EXPECT_TRUE(index.GetChildren(oldParent).empty());
    ASSERT_EQ(index.GetChildren(newParent).size(), 1U);
    EXPECT_EQ(index.GetChildren(newParent)[0U], child);
    index.Clear();
    EXPECT_TRUE(index.IsEmpty());
    EXPECT_TRUE(index.GetChildren(newParent).empty());
}

TEST_F(ChildrenIndexTest, SceneGraph_ServesItsIndexBetweenRebuilds) {
    const Entity parent = entityManager.CreateEntity();
    sceneGraph.AttachTransform(entityManager, parent, TransformComponent{});
    const Entity child = AttachChild(parent);
    EXPECT_TRUE(sceneGraph.GetIndexedChildren(parent).empty()); // Never rebuilt yet.
    sceneGraph.RebuildChildrenIndex(entityManager);
    const std::span<const Entity> indexed = sceneGraph.GetIndexedChildren(parent);
    ASSERT_EQ(indexed.size(), 1U);
    EXPECT_EQ(indexed[0U], child);
}

} // namespace
} // namespace UVE::Scene::Tests
