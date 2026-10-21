// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/render_systems/dynamic_mesh_bvh_uve.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <numbers>
#include <random>
#include <unordered_set>
#include <vector>

#include <gtest/gtest.h>

#include "uve/math/matrix4x4_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Render::Tests {
namespace {

using DynamicMeshBvh = DynamicMeshBvhUVE;
using EntityEntry = DynamicMeshBvhUVE::EntityEntry;
using Node = DynamicMeshBvhUVE::Node;

/// Creates a standard camera view frustum at (0, 0, 0) looking down -Z with 90 degree FOV,
/// near plane = 1.0, far plane = 100.0, aspect ratio = 1.0.
[[nodiscard]] Math::FrustumUVE MakeTestFrustumUVE() {
    const Math::Matrix4x4UVE view =
        Math::Matrix4x4UVE::ViewFromPositionAndRotationUVE(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::QuaternionUVE{});
    const Math::Matrix4x4UVE projection =
        Math::Matrix4x4UVE::PerspectiveUVE(std::numbers::pi_v<float> / 2.0F, 1.0F, 1.0F, 100.0F);
    return Math::FrustumUVE::FromViewProjectionUVE(projection * view);
}

/// Helper to create an AABB with given center and half-extents.
[[nodiscard]] Math::AabbUVE MakeBox(const Math::Vector3UVE& center, const Math::Vector3UVE& halfExtents) {
    return Math::AabbUVE::FromCenterExtentsUVE(center, halfExtents);
}

/// Helper to generate N test entity entries distributed in 3D space.
[[nodiscard]] std::vector<EntityEntry> GenerateEntities(const std::size_t count, const float spread = 50.0F,
                                                        const std::uint32_t seed = 42U) {
    std::mt19937 rng(seed);
    std::uniform_real_distribution<float> posDist(-spread, spread);
    std::uniform_real_distribution<float> sizeDist(0.2F, 2.0F);

    std::vector<EntityEntry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 1U; i <= count; ++i) {
        const Scene::EntityUVE entity{i, 1U};
        const Math::Vector3UVE center{posDist(rng), posDist(rng), -std::abs(posDist(rng)) - 2.0F};
        const Math::Vector3UVE extents{sizeDist(rng), sizeDist(rng), sizeDist(rng)};
        entries.push_back(EntityEntry{entity, MakeBox(center, extents)});
    }
    return entries;
}

/// Verifies that all parent-child links in the BVH are bidirectionally consistent.
void ValidateTreeIntegrity(const DynamicMeshBvh& bvh) {
    if (bvh.IsEmptyUVE()) {
        EXPECT_EQ(bvh.GetRootIndexUVE(), DynamicMeshBvh::kInvalidNodeIndex);
        EXPECT_EQ(bvh.GetNodeCountUVE(), 0U);
        return;
    }

    const auto& nodes = bvh.GetNodesUVE();
    const std::int32_t rootIndex = bvh.GetRootIndexUVE();
    ASSERT_GE(rootIndex, 0);
    ASSERT_LT(static_cast<std::size_t>(rootIndex), nodes.size());
    EXPECT_EQ(nodes[static_cast<std::size_t>(rootIndex)].parentIndex, DynamicMeshBvh::kInvalidNodeIndex);

    std::size_t visitedLeaves = 0U;
    std::size_t totalEntities = 0U;
    std::unordered_set<Scene::EntityUVE> seenEntities;

    std::vector<std::int32_t> stack;
    stack.push_back(rootIndex);

    while (!stack.empty()) {
        const std::int32_t currIndex = stack.back();
        stack.pop_back();

        ASSERT_GE(currIndex, 0);
        ASSERT_LT(static_cast<std::size_t>(currIndex), nodes.size());
        const Node& node = nodes[static_cast<std::size_t>(currIndex)];

        if (node.IsLeaf()) {
            ++visitedLeaves;
            EXPECT_LE(node.entryCount, DynamicMeshBvh::kLeafCapacity);
            EXPECT_GT(node.entryCount, 0U);

            Math::AabbUVE leafUnion = node.entries[0].bounds;
            for (std::uint32_t i = 0U; i < node.entryCount; ++i) {
                const auto& entry = node.entries[i];
                EXPECT_NE(entry.entity, Scene::kInvalidEntityUVE);
                EXPECT_TRUE(seenEntities.insert(entry.entity).second) << "Duplicate entity found in leaf";
                EXPECT_EQ(bvh.GetEntityLeafIndexUVE(entry.entity), currIndex);
                if (i > 0U) {
                    leafUnion = leafUnion.UnionUVE(entry.bounds);
                }
                ++totalEntities;
            }
            // Leaf bounds should enclose all leaf entries
            EXPECT_LE(node.bounds.min.x, leafUnion.min.x + 1e-4F);
            EXPECT_LE(node.bounds.min.y, leafUnion.min.y + 1e-4F);
            EXPECT_LE(node.bounds.min.z, leafUnion.min.z + 1e-4F);
            EXPECT_GE(node.bounds.max.x, leafUnion.max.x - 1e-4F);
            EXPECT_GE(node.bounds.max.y, leafUnion.max.y - 1e-4F);
            EXPECT_GE(node.bounds.max.z, leafUnion.max.z - 1e-4F);
        } else {
            // Internal node
            ASSERT_NE(node.leftChild, DynamicMeshBvh::kInvalidNodeIndex);
            ASSERT_NE(node.rightChild, DynamicMeshBvh::kInvalidNodeIndex);
            EXPECT_EQ(nodes[static_cast<std::size_t>(node.leftChild)].parentIndex, currIndex);
            EXPECT_EQ(nodes[static_cast<std::size_t>(node.rightChild)].parentIndex, currIndex);

            // Internal node bounds must enclose both children's bounds
            const auto& leftBounds = nodes[static_cast<std::size_t>(node.leftChild)].bounds;
            const auto& rightBounds = nodes[static_cast<std::size_t>(node.rightChild)].bounds;
            EXPECT_LE(node.bounds.min.x, std::min(leftBounds.min.x, rightBounds.min.x) + 1e-4F);
            EXPECT_GE(node.bounds.max.x, std::max(leftBounds.max.x, rightBounds.max.x) - 1e-4F);

            stack.push_back(node.rightChild);
            stack.push_back(node.leftChild);
        }
    }

    EXPECT_EQ(totalEntities, bvh.GetEntityCountUVE());
}

} // namespace

// ===========================================================================
// 1. Construction with Varying Numbers of Entities (empty, 1, 10, 100, 1000)
// ===========================================================================

TEST(DynamicMeshBvhUVETest, Construction_EmptyHierarchy) {
    DynamicMeshBvh bvh;
    EXPECT_TRUE(bvh.IsEmptyUVE());
    EXPECT_EQ(bvh.GetEntityCountUVE(), 0U);
    EXPECT_EQ(bvh.GetNodeCountUVE(), 0U);
    EXPECT_EQ(bvh.GetRootIndexUVE(), DynamicMeshBvh::kInvalidNodeIndex);

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(MakeTestFrustumUVE(), visible);
    EXPECT_TRUE(visible.empty());

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Construction_SingleEntity) {
    const Scene::EntityUVE entity{1U, 1U};
    const Math::AabbUVE bounds = MakeBox(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});

    DynamicMeshBvh bvh(std::vector<EntityEntry>{EntityEntry{entity, bounds}});

    EXPECT_FALSE(bvh.IsEmptyUVE());
    EXPECT_EQ(bvh.GetEntityCountUVE(), 1U);
    EXPECT_EQ(bvh.GetNodeCountUVE(), 1U);
    EXPECT_TRUE(bvh.ContainsEntityUVE(entity));

    const auto entityBounds = bvh.GetEntityBoundsUVE(entity);
    ASSERT_TRUE(entityBounds.has_value());
    EXPECT_EQ(*entityBounds, bounds);

    const std::int32_t rootIndex = bvh.GetRootIndexUVE();
    ASSERT_GE(rootIndex, 0);
    const Node& rootNode = bvh.GetNodeUVE(static_cast<std::size_t>(rootIndex));
    EXPECT_TRUE(rootNode.IsLeaf());
    EXPECT_EQ(rootNode.entryCount, 1U);
    EXPECT_EQ(rootNode.entries[0].entity, entity);
    EXPECT_EQ(rootNode.bounds, bounds);

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Construction_SmallEntitySet_WithinLeafCapacity) {
    // 4 entities exactly fills 1 leaf without splitting
    std::vector<EntityEntry> entries;
    for (std::uint32_t i = 1U; i <= 4U; ++i) {
        entries.push_back(EntityEntry{Scene::EntityUVE{i, 1U},
                                      MakeBox(Math::Vector3UVE{static_cast<float>(i), 0.0F, -10.0F},
                                              Math::Vector3UVE{0.5F, 0.5F, 0.5F})});
    }

    DynamicMeshBvh bvh(entries);
    EXPECT_EQ(bvh.GetEntityCountUVE(), 4U);
    EXPECT_EQ(bvh.GetNodeCountUVE(), 1U);

    const Node& root = bvh.GetNodeUVE(static_cast<std::size_t>(bvh.GetRootIndexUVE()));
    EXPECT_TRUE(root.IsLeaf());
    EXPECT_EQ(root.entryCount, 4U);

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Construction_ExceedsLeafCapacity_TriggersSplit) {
    // 5 entities exceeds kLeafCapacity (4), causing a binary median-split
    std::vector<EntityEntry> entries;
    for (std::uint32_t i = 1U; i <= 5U; ++i) {
        entries.push_back(EntityEntry{Scene::EntityUVE{i, 1U},
                                      MakeBox(Math::Vector3UVE{static_cast<float>(i) * 2.0F, 0.0F, -10.0F},
                                              Math::Vector3UVE{0.5F, 0.5F, 0.5F})});
    }

    DynamicMeshBvh bvh(entries);
    EXPECT_EQ(bvh.GetEntityCountUVE(), 5U);
    // 1 root internal node + 2 leaf children = 3 nodes
    EXPECT_EQ(bvh.GetNodeCountUVE(), 3U);

    const Node& root = bvh.GetNodeUVE(static_cast<std::size_t>(bvh.GetRootIndexUVE()));
    EXPECT_FALSE(root.IsLeaf());
    EXPECT_NE(root.leftChild, DynamicMeshBvh::kInvalidNodeIndex);
    EXPECT_NE(root.rightChild, DynamicMeshBvh::kInvalidNodeIndex);

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Construction_10Entities) {
    const auto entries = GenerateEntities(10U, 20.0F);
    DynamicMeshBvh bvh(entries);

    EXPECT_EQ(bvh.GetEntityCountUVE(), 10U);
    ValidateTreeIntegrity(bvh);

    const auto stats = bvh.ComputeStatsUVE();
    EXPECT_EQ(stats.entityCount, 10U);
    EXPECT_GT(stats.leafCount, 1U);
    EXPECT_GT(stats.internalCount, 0U);
    EXPECT_LE(stats.maxDepth, 5U);
}

TEST(DynamicMeshBvhUVETest, Construction_100Entities) {
    const auto entries = GenerateEntities(100U, 100.0F);
    DynamicMeshBvh bvh(entries);

    EXPECT_EQ(bvh.GetEntityCountUVE(), 100U);
    ValidateTreeIntegrity(bvh);

    const auto stats = bvh.ComputeStatsUVE();
    EXPECT_EQ(stats.entityCount, 100U);
    EXPECT_LE(stats.maxDepth, 9U);
    EXPECT_GT(stats.averageLeafFill, 1.5F);
}

TEST(DynamicMeshBvhUVETest, Construction_1000Entities) {
    const auto entries = GenerateEntities(1000U, 500.0F);
    DynamicMeshBvh bvh(entries);

    EXPECT_EQ(bvh.GetEntityCountUVE(), 1000U);
    ValidateTreeIntegrity(bvh);

    const auto stats = bvh.ComputeStatsUVE();
    EXPECT_EQ(stats.entityCount, 1000U);
    // Balanced tree with leaf size 4 has depth ceil(log2(1000/4)) + 1 approx 8 to 11
    EXPECT_LE(stats.maxDepth, 14U);
    EXPECT_GE(stats.leafCount, 250U);
}

// ===========================================================================
// 2. Frustum Queries (entities completely inside, outside, intersecting)
// ===========================================================================

TEST(DynamicMeshBvhUVETest, QueryFrustum_AllEntitiesInside_Accepted) {
    // Entities placed safely within the forward viewing pyramid:
    // View looks down -Z from origin, near=1, far=100.
    std::vector<EntityEntry> entries;
    entries.push_back(EntityEntry{Scene::EntityUVE{1U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{2U, 1U}, MakeBox(Math::Vector3UVE{1.0F, 1.0F, -10.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{3U, 1U}, MakeBox(Math::Vector3UVE{-1.0F, 0.0F, -15.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{4U, 1U}, MakeBox(Math::Vector3UVE{0.0F, -1.0F, -20.0F}, Math::Vector3UVE{0.5F, 0.5F, 0.5F})});

    DynamicMeshBvh bvh(entries);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);

    EXPECT_EQ(visible.size(), 4U);
    for (const auto& entry : entries) {
        EXPECT_TRUE(std::find(visible.begin(), visible.end(), entry.entity) != visible.end());
    }
}

TEST(DynamicMeshBvhUVETest, QueryFrustum_AllEntitiesOutside_Pruned) {
    // Entities placed behind camera or outside far/side planes:
    std::vector<EntityEntry> entries;
    // Behind camera (+Z)
    entries.push_back(EntityEntry{Scene::EntityUVE{1U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{2U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, 50.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    // Far to the right
    entries.push_back(EntityEntry{Scene::EntityUVE{3U, 1U}, MakeBox(Math::Vector3UVE{500.0F, 0.0F, -10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    // Beyond far plane (>100)
    entries.push_back(EntityEntry{Scene::EntityUVE{4U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -200.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});

    DynamicMeshBvh bvh(entries);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);

    EXPECT_TRUE(visible.empty());
}

TEST(DynamicMeshBvhUVETest, QueryFrustum_MixedInsideAndOutside) {
    const Scene::EntityUVE inside1{1U, 1U};
    const Scene::EntityUVE inside2{2U, 1U};
    const Scene::EntityUVE outside1{3U, 1U};
    const Scene::EntityUVE outside2{4U, 1U};

    std::vector<EntityEntry> entries;
    entries.push_back(EntityEntry{inside1, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{outside1, MakeBox(Math::Vector3UVE{0.0F, 0.0F, 10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{inside2, MakeBox(Math::Vector3UVE{2.0F, 0.0F, -20.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{outside2, MakeBox(Math::Vector3UVE{300.0F, 0.0F, -10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});

    DynamicMeshBvh bvh(entries);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);

    ASSERT_EQ(visible.size(), 2U);
    EXPECT_TRUE(std::find(visible.begin(), visible.end(), inside1) != visible.end());
    EXPECT_TRUE(std::find(visible.begin(), visible.end(), inside2) != visible.end());
    EXPECT_TRUE(std::find(visible.begin(), visible.end(), outside1) == visible.end());
    EXPECT_TRUE(std::find(visible.begin(), visible.end(), outside2) == visible.end());
}

TEST(DynamicMeshBvhUVETest, QueryFrustum_SubtreeAcceptance_OptimizedFastPath) {
    // Place a cluster of 50 entities completely inside the frustum in a tight box.
    // The BVH node bounding this cluster will be completely inside all 6 planes,
    // triggering the whole-subtree acceptance logic without child plane testing.
    std::vector<EntityEntry> entries;
    for (std::uint32_t i = 1U; i <= 50U; ++i) {
        const float offsetX = (static_cast<float>(i % 5) - 2.0F) * 0.2F;
        const float offsetY = (static_cast<float>((i / 5) % 5) - 2.0F) * 0.2F;
        const Math::Vector3UVE center{offsetX, offsetY, -15.0F};
        entries.push_back(EntityEntry{Scene::EntityUVE{i, 1U}, MakeBox(center, Math::Vector3UVE{0.1F, 0.1F, 0.1F})});
    }

    DynamicMeshBvh bvh(entries);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);

    EXPECT_EQ(visible.size(), 50U);
}

TEST(DynamicMeshBvhUVETest, QueryFrustum_SubtreePruning_EntireBranchRejectedEarly) {
    // 50 entities in a tight cluster far off-screen to the right (+X > 200).
    // The top-level branch bounding box is completely outside, pruning all 50 entities at once.
    std::vector<EntityEntry> entries;
    for (std::uint32_t i = 1U; i <= 50U; ++i) {
        const float offsetX = static_cast<float>(i % 5) * 0.5F;
        const float offsetY = static_cast<float>(i / 5) * 0.5F;
        const Math::Vector3UVE center{200.0F + offsetX, offsetY, -15.0F};
        entries.push_back(EntityEntry{Scene::EntityUVE{i, 1U}, MakeBox(center, Math::Vector3UVE{0.2F, 0.2F, 0.2F})});
    }

    DynamicMeshBvh bvh(entries);
    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);

    EXPECT_TRUE(visible.empty());
}

TEST(DynamicMeshBvhUVETest, QueryFrustum_EquivalenceWithExhaustiveBruteForce) {
    // Stress test: 300 entities randomly scattered across inside/outside/straddling positions.
    // QueryFrustumUVE result must be EXACTLY identical to testing every entity independently.
    const auto entries = GenerateEntities(300U, 80.0F, 12345U);
    DynamicMeshBvh bvh(entries);

    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    // 1. BVH Query
    std::vector<Scene::EntityUVE> bvhVisible;
    bvh.QueryFrustumUVE(frustum, bvhVisible);

    // 2. Ground-truth exhaustive check
    std::vector<Scene::EntityUVE> groundTruth;
    for (const auto& entry : entries) {
        if (frustum.IntersectsUVE(entry.bounds)) {
            groundTruth.push_back(entry.entity);
        }
    }

    ASSERT_EQ(bvhVisible.size(), groundTruth.size());

    std::sort(bvhVisible.begin(), bvhVisible.end(), [](const auto& a, const auto& b) { return a.index < b.index; });
    std::sort(groundTruth.begin(), groundTruth.end(), [](const auto& a, const auto& b) { return a.index < b.index; });

    for (std::size_t i = 0U; i < groundTruth.size(); ++i) {
        EXPECT_EQ(bvhVisible[i], groundTruth[i]);
    }
}

// ===========================================================================
// 3. Entity Dynamic Updates and Refitting (RefitFromLeafUVE)
// ===========================================================================

TEST(DynamicMeshBvhUVETest, DynamicUpdate_MoveEntity_ExpandsBoundsAndUpdatesQueryResult) {
    const Scene::EntityUVE movingEntity{1U, 1U};
    const Math::AabbUVE initialBounds = MakeBox(Math::Vector3UVE{0.0F, 0.0F, 20.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}); // Behind camera

    DynamicMeshBvh bvh;
    bvh.InsertOrUpdateUVE(movingEntity, initialBounds);
    EXPECT_EQ(bvh.GetEntityCountUVE(), 1U);

    const Math::FrustumUVE frustum = MakeTestFrustumUVE();

    std::vector<Scene::EntityUVE> visible;
    bvh.QueryFrustumUVE(frustum, visible);
    EXPECT_TRUE(visible.empty()) << "Entity behind camera should be culled";

    // Move entity in front of camera (-Z)
    const Math::AabbUVE newBounds = MakeBox(Math::Vector3UVE{0.0F, 0.0F, -15.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F});
    bvh.InsertOrUpdateUVE(movingEntity, newBounds);

    EXPECT_EQ(bvh.GetEntityCountUVE(), 1U);
    EXPECT_EQ(bvh.GetEntityBoundsUVE(movingEntity), newBounds);

    bvh.QueryFrustumUVE(frustum, visible);
    ASSERT_EQ(visible.size(), 1U);
    EXPECT_EQ(visible[0], movingEntity);

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, RefitFromLeaf_UpdatesParentBoundsInLogN_WithoutNodeReallocation) {
    const auto entries = GenerateEntities(40U, 30.0F);
    DynamicMeshBvh bvh(entries);
    ValidateTreeIntegrity(bvh);

    const std::size_t nodeCountBefore = bvh.GetNodeCountUVE();
    const Scene::EntityUVE targetEntity = entries[10].entity;
    const std::int32_t leafIdx = bvh.GetEntityLeafIndexUVE(targetEntity);
    ASSERT_NE(leafIdx, DynamicMeshBvh::kInvalidNodeIndex);

    // Drastically expand target entity bounds
    const Math::AabbUVE hugeBounds = MakeBox(Math::Vector3UVE{0.0F, 0.0F, -20.0F}, Math::Vector3UVE{100.0F, 100.0F, 100.0F});
    bvh.InsertOrUpdateUVE(targetEntity, hugeBounds);

    // Total node count must remain EXACTLY identical (zero reallocations or topology shifts)
    EXPECT_EQ(bvh.GetNodeCountUVE(), nodeCountBefore);

    // Root bounds must now enclose the hugeBounds
    const Node& root = bvh.GetNodeUVE(static_cast<std::size_t>(bvh.GetRootIndexUVE()));
    EXPECT_LE(root.bounds.min.x, hugeBounds.min.x + 1e-4F);
    EXPECT_GE(root.bounds.max.x, hugeBounds.max.x - 1e-4F);

    ValidateTreeIntegrity(bvh);
}

// ===========================================================================
// 4. Incremental Insertion, Removal, and Tree Balance
// ===========================================================================

TEST(DynamicMeshBvhUVETest, IncrementalInsertion_ProducesValidBalancedTree) {
    DynamicMeshBvh bvh;

    for (std::uint32_t i = 1U; i <= 60U; ++i) {
        const Scene::EntityUVE entity{i, 1U};
        const Math::AabbUVE bounds = MakeBox(Math::Vector3UVE{static_cast<float>(i * 2), 0.0F, -10.0F},
                                             Math::Vector3UVE{0.5F, 0.5F, 0.5F});
        bvh.InsertOrUpdateUVE(entity, bounds);
        EXPECT_EQ(bvh.GetEntityCountUVE(), i);
        EXPECT_TRUE(bvh.ContainsEntityUVE(entity));
    }

    ValidateTreeIntegrity(bvh);

    const auto stats = bvh.ComputeStatsUVE();
    EXPECT_EQ(stats.entityCount, 60U);
    EXPECT_LE(stats.maxDepth, 8U);
}

TEST(DynamicMeshBvhUVETest, Removal_SingleEntity_EmptiesTree) {
    const Scene::EntityUVE entity{1U, 1U};
    DynamicMeshBvh bvh;
    bvh.InsertOrUpdateUVE(entity, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}));

    EXPECT_EQ(bvh.GetEntityCountUVE(), 1U);
    EXPECT_TRUE(bvh.RemoveEntityUVE(entity));

    EXPECT_TRUE(bvh.IsEmptyUVE());
    EXPECT_EQ(bvh.GetEntityCountUVE(), 0U);
    EXPECT_FALSE(bvh.ContainsEntityUVE(entity));

    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Removal_MultipleEntities_SiblingContractionMaintainsIntegrity) {
    const auto entries = GenerateEntities(30U, 40.0F);
    DynamicMeshBvh bvh(entries);
    ValidateTreeIntegrity(bvh);

    // Remove half of the entities
    for (std::size_t i = 0U; i < 15U; ++i) {
        EXPECT_TRUE(bvh.RemoveEntityUVE(entries[i].entity));
        EXPECT_FALSE(bvh.ContainsEntityUVE(entries[i].entity));
        EXPECT_EQ(bvh.GetEntityCountUVE(), 30U - 1U - i);
        ValidateTreeIntegrity(bvh);
    }

    // Verify remaining entities are still queryable and valid
    for (std::size_t i = 15U; i < 30U; ++i) {
        EXPECT_TRUE(bvh.ContainsEntityUVE(entries[i].entity));
    }

    // Remove remaining entities
    for (std::size_t i = 15U; i < 30U; ++i) {
        EXPECT_TRUE(bvh.RemoveEntityUVE(entries[i].entity));
    }

    EXPECT_TRUE(bvh.IsEmptyUVE());
    ValidateTreeIntegrity(bvh);
}

TEST(DynamicMeshBvhUVETest, Removal_NonExistentEntity_ReturnsFalse) {
    DynamicMeshBvh bvh;
    bvh.InsertOrUpdateUVE(Scene::EntityUVE{1U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -5.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F}));

    EXPECT_FALSE(bvh.RemoveEntityUVE(Scene::EntityUVE{999U, 1U}));
    EXPECT_FALSE(bvh.RemoveEntityUVE(Scene::kInvalidEntityUVE));
    EXPECT_EQ(bvh.GetEntityCountUVE(), 1U);
}

// ===========================================================================
// 5. Additional Spatial Queries (AABB and Ray Queries)
// ===========================================================================

TEST(DynamicMeshBvhUVETest, QueryAabb_ReturnsIntersectingEntities) {
    std::vector<EntityEntry> entries;
    entries.push_back(EntityEntry{Scene::EntityUVE{1U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{2U, 1U}, MakeBox(Math::Vector3UVE{5.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{3U, 1U}, MakeBox(Math::Vector3UVE{20.0F, 0.0F, 0.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});

    DynamicMeshBvh bvh(entries);

    // Query box overlapping entities 1 and 2 but not 3
    const Math::AabbUVE queryBox = MakeBox(Math::Vector3UVE{2.5F, 0.0F, 0.0F}, Math::Vector3UVE{3.5F, 2.0F, 2.0F});

    std::vector<Scene::EntityUVE> result;
    bvh.QueryAabbUVE(queryBox, result);

    ASSERT_EQ(result.size(), 2U);
    EXPECT_TRUE(std::find(result.begin(), result.end(), Scene::EntityUVE{1U, 1U}) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), Scene::EntityUVE{2U, 1U}) != result.end());
    EXPECT_TRUE(std::find(result.begin(), result.end(), Scene::EntityUVE{3U, 1U}) == result.end());
}

TEST(DynamicMeshBvhUVETest, QueryRay_FindsIntersectingEntities) {
    std::vector<EntityEntry> entries;
    // On ray path along -Z
    entries.push_back(EntityEntry{Scene::EntityUVE{1U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -10.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    entries.push_back(EntityEntry{Scene::EntityUVE{2U, 1U}, MakeBox(Math::Vector3UVE{0.0F, 0.0F, -30.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});
    // Off ray path (+X = 10)
    entries.push_back(EntityEntry{Scene::EntityUVE{3U, 1U}, MakeBox(Math::Vector3UVE{10.0F, 0.0F, -20.0F}, Math::Vector3UVE{1.0F, 1.0F, 1.0F})});

    DynamicMeshBvh bvh(entries);

    const Math::RayUVE ray{Math::Vector3UVE{0.0F, 0.0F, 0.0F}, Math::Vector3UVE{0.0F, 0.0F, -1.0F}};

    std::vector<Scene::EntityUVE> hitEntities;
    bvh.QueryRayUVE(ray, 50.0F, hitEntities);

    ASSERT_EQ(hitEntities.size(), 2U);
    EXPECT_TRUE(std::find(hitEntities.begin(), hitEntities.end(), Scene::EntityUVE{1U, 1U}) != hitEntities.end());
    EXPECT_TRUE(std::find(hitEntities.begin(), hitEntities.end(), Scene::EntityUVE{2U, 1U}) != hitEntities.end());
    EXPECT_TRUE(std::find(hitEntities.begin(), hitEntities.end(), Scene::EntityUVE{3U, 1U}) == hitEntities.end());
}

// ===========================================================================
// 6. Tree Rebuild and Invariant Re-validation
// ===========================================================================

TEST(DynamicMeshBvhUVETest, Rebuild_RestoresOptimalMedianSplitTopology) {
    DynamicMeshBvh bvh;

    // Insert 50 entities sequentially
    for (std::uint32_t i = 1U; i <= 50U; ++i) {
        bvh.InsertOrUpdateUVE(Scene::EntityUVE{i, 1U},
                              MakeBox(Math::Vector3UVE{static_cast<float>(i), 0.0F, -static_cast<float>(i)},
                                      Math::Vector3UVE{0.5F, 0.5F, 0.5F}));
    }

    // Call Rebuild to reconstruct via top-down median split
    bvh.RebuildUVE();

    EXPECT_EQ(bvh.GetEntityCountUVE(), 50U);
    ValidateTreeIntegrity(bvh);

    const auto stats = bvh.ComputeStatsUVE();
    EXPECT_EQ(stats.entityCount, 50U);
    EXPECT_LE(stats.maxDepth, 7U);
}

} // namespace UVE::Render::Tests
