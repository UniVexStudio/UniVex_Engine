// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "uve/component/hierarchy_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/entity_manager_uve.h"
#include "uve/events/event_system_uve.h"
#include "uve/memory/memory_manager_uve.h"
#include "uve/scene/children_index_uve.h"
#include "uve/scene/scene_graph_uve.h"

namespace UVE::Scene::Tests {
namespace {

class ChildrenIndexUVETest : public ::testing::Test {
protected:
    Memory::MemoryManagerUVE memoryManager;
    Events::EventSystemUVE eventSystem;
    EntityManagerUVE entityManager{memoryManager.GetDefaultAllocatorUVE(), eventSystem};
    SceneGraphUVE sceneGraph;

    EntityUVE AttachChildUVE(const EntityUVE parent) {
        const EntityUVE child = entityManager.CreateEntityUVE();
        sceneGraph.AttachTransformUVE(entityManager, child, TransformComponentUVE{});
        sceneGraph.SetParentUVE(entityManager, child, parent);
        return child;
    }
};

TEST_F(ChildrenIndexUVETest, Rebuild_AnswersIdenticallyToTheScan) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    const EntityUVE first = AttachChildUVE(parent);
    const EntityUVE second = AttachChildUVE(parent);
    const EntityUVE third = AttachChildUVE(parent);

    ChildrenIndexUVE index;
    EXPECT_TRUE(index.IsEmptyUVE());
    index.RebuildUVE(entityManager);
    EXPECT_FALSE(index.IsEmptyUVE());
    const std::vector<EntityUVE> scanned = sceneGraph.GetChildrenUVE(entityManager, parent);
    ASSERT_EQ(scanned.size(), 3U);
    const std::span<const EntityUVE> indexed = index.GetChildrenUVE(parent);
    ASSERT_EQ(indexed.size(), 3U);
    // Identical answers, same sibling order (creation order here).
    EXPECT_EQ(indexed[0U], first);
    EXPECT_EQ(indexed[1U], second);
    EXPECT_EQ(indexed[2U], third);
    EXPECT_EQ(std::vector<EntityUVE>(indexed.begin(), indexed.end()), scanned);
    // Childless parents answer empty, not garbage.
    EXPECT_TRUE(index.GetChildrenUVE(first).empty());
    // Roots hang under the invalid entity (their parent link), exactly as the scan answers.
    const std::vector<EntityUVE> scannedRoots = sceneGraph.GetChildrenUVE(entityManager, kInvalidEntityUVE);
    const std::span<const EntityUVE> indexedRoots = index.GetChildrenUVE(kInvalidEntityUVE);
    EXPECT_EQ(std::vector<EntityUVE>(indexedRoots.begin(), indexedRoots.end()), scannedRoots);
    ASSERT_EQ(indexedRoots.size(), 1U);
    EXPECT_EQ(indexedRoots[0U], parent);
}

TEST_F(ChildrenIndexUVETest, Rebuild_SeesReparentsOnlyAfterRebuilding) {
    const EntityUVE oldParent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, oldParent, TransformComponentUVE{});
    const EntityUVE newParent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, newParent, TransformComponentUVE{});
    const EntityUVE child = AttachChildUVE(oldParent);

    ChildrenIndexUVE index;
    index.RebuildUVE(entityManager);
    ASSERT_EQ(index.GetChildrenUVE(oldParent).size(), 1U);
    sceneGraph.SetParentUVE(entityManager, child, newParent);
    // Stale until rebuilt: the documented freshness contract, pinned, not assumed.
    EXPECT_EQ(index.GetChildrenUVE(oldParent).size(), 1U);
    EXPECT_TRUE(index.GetChildrenUVE(newParent).empty());
    index.RebuildUVE(entityManager);
    EXPECT_TRUE(index.GetChildrenUVE(oldParent).empty());
    ASSERT_EQ(index.GetChildrenUVE(newParent).size(), 1U);
    EXPECT_EQ(index.GetChildrenUVE(newParent)[0U], child);
    index.ClearUVE();
    EXPECT_TRUE(index.IsEmptyUVE());
    EXPECT_TRUE(index.GetChildrenUVE(newParent).empty());
}

TEST_F(ChildrenIndexUVETest, SceneGraph_ServesItsIndexBetweenRebuilds) {
    const EntityUVE parent = entityManager.CreateEntityUVE();
    sceneGraph.AttachTransformUVE(entityManager, parent, TransformComponentUVE{});
    const EntityUVE child = AttachChildUVE(parent);
    EXPECT_TRUE(sceneGraph.GetIndexedChildrenUVE(parent).empty()); // Never rebuilt yet.
    sceneGraph.RebuildChildrenIndexUVE(entityManager);
    const std::span<const EntityUVE> indexed = sceneGraph.GetIndexedChildrenUVE(parent);
    ASSERT_EQ(indexed.size(), 1U);
    EXPECT_EQ(indexed[0U], child);
}

} // namespace
} // namespace UVE::Scene::Tests
