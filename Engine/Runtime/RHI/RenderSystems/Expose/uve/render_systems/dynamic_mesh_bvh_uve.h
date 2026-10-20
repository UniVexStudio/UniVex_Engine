// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <unordered_map>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/frustum_uve.h"
#include "uve/math/ray_uve.h"

namespace UVE::Render {

/// DynamicMeshBvhUVE is a binary Axis-Aligned Bounding Box (AABB) Bounding Volume Hierarchy
/// designed for high-performance spatial indexing and frustum culling of mesh entities.
///
/// Key Architectural Properties:
/// - Binary tree representation with contiguous node storage in std::vector<Node>.
/// - Leaf nodes store up to `kLeafCapacity = 4` entity entries (`Scene::EntityUVE` + `Math::AabbUVE`).
/// - Top-down median-split construction along the axis of longest extent.
/// - Incremental insertion using Surface Area Heuristic (SAH) enlargement minimization and dynamic leaf splitting.
/// - Incremental entity removal with sibling contraction to keep tree balanced and eliminate empty nodes.
/// - Fast bottom-up refitting (`RefitFromLeafUVE`) updating parent bounds up to the root in O(log N)
///   time when an entity transforms without reallocating tree nodes or changing topology.
/// - Fast iterative stack-based frustum culling query (`QueryFrustumUVE`) evaluating plane equations;
///   pruning subtrees entirely when outside, and accepting entire subtrees when completely inside
///   without further plane testing.
class DynamicMeshBvhUVE {
public:
    static constexpr std::size_t kLeafCapacity = 4U;
    static constexpr std::int32_t kInvalidNodeIndex = -1;

    /// An entity entry stored within a leaf node.
    struct EntityEntry {
        Scene::EntityUVE entity = Scene::kInvalidEntityUVE;
        Math::AabbUVE bounds{};

        [[nodiscard]] constexpr bool operator==(const EntityEntry& other) const noexcept {
            return entity == other.entity && bounds == other.bounds;
        }

        [[nodiscard]] constexpr bool operator!=(const EntityEntry& other) const noexcept {
            return !(*this == other);
        }
    };

    /// Node representation in the binary BVH.
    struct Node {
        Math::AabbUVE bounds{};
        std::int32_t leftChild = kInvalidNodeIndex;
        std::int32_t rightChild = kInvalidNodeIndex;
        std::int32_t parentIndex = kInvalidNodeIndex;
        std::array<EntityEntry, kLeafCapacity> entries{};
        std::uint32_t entryCount = 0U;

        [[nodiscard]] constexpr bool IsLeaf() const noexcept {
            return leftChild == kInvalidNodeIndex && rightChild == kInvalidNodeIndex;
        }
    };

    /// Diagnostic statistics describing the BVH topology.
    struct TreeStatsUVE {
        std::size_t nodeCount = 0U;
        std::size_t leafCount = 0U;
        std::size_t internalCount = 0U;
        std::size_t entityCount = 0U;
        std::size_t maxDepth = 0U;
        float averageLeafFill = 0.0F;
    };

    DynamicMeshBvhUVE() = default;
    explicit DynamicMeshBvhUVE(std::vector<EntityEntry> entries);

    ~DynamicMeshBvhUVE() = default;
    DynamicMeshBvhUVE(const DynamicMeshBvhUVE&) = default;
    DynamicMeshBvhUVE& operator=(const DynamicMeshBvhUVE&) = default;
    DynamicMeshBvhUVE(DynamicMeshBvhUVE&&) noexcept = default;
    DynamicMeshBvhUVE& operator=(DynamicMeshBvhUVE&&) noexcept = default;

    /// Builds the BVH top-down from scratch using median split partitioning along the longest extent axis.
    void BuildUVE(std::vector<EntityEntry> entries);

    /// Rebuilds the current tree topology using top-down median split from existing entities.
    void RebuildUVE();

    /// Inserts a new entity or updates an existing entity's world bounding box.
    /// If the entity already exists in the BVH, its bounds are updated and RefitFromLeafUVE
    /// is called to update ancestors in O(log N) time without node reallocations.
    /// If the entity does not exist, it is incrementally inserted using SAH surface area enlargement.
    void InsertOrUpdateUVE(Scene::EntityUVE entity, const Math::AabbUVE& worldBounds);

    /// Removes an entity from the BVH.
    /// If the entity was the last element in its leaf node, the node is contracted and
    /// its sibling is promoted, maintaining a clean binary tree structure with zero empty leaves.
    void RemoveUVE(Scene::EntityUVE entity);

    /// Removes an entity from the BVH, returning true if the entity was found and removed.
    bool RemoveEntityUVE(Scene::EntityUVE entity);

    /// Bottom-up refitting starting from a given node index (leaf or internal) up to the root.
    /// Recomputes bounds in O(log N) time without reallocating nodes.
    void RefitFromLeafUVE(std::size_t leafIndex) noexcept;

    /// Traverses the BVH against the view frustum and populates outVisibleEntities.
    /// Evaluates frustum planes hierarchically:
    /// - If a node is completely outside any frustum plane, its entire subtree is pruned immediately.
    /// - If a node is completely inside all frustum planes, its entire subtree is accepted without
    ///   any further plane testing.
    /// - If a node intersects the frustum boundary, leaf entities are tested individually and internal
    ///   children are pushed to the traversal stack.
    void QueryFrustumUVE(const Math::FrustumUVE& frustum,
                         std::vector<Scene::EntityUVE>& outVisibleEntities) const;

    /// Queries entities whose bounds intersect the given query AABB.
    void QueryAabbUVE(const Math::AabbUVE& queryBounds,
                      std::vector<Scene::EntityUVE>& outEntities) const;

    /// Queries entities whose bounds intersect the given query ray within [0, maxDistance].
    void QueryRayUVE(const Math::RayUVE& ray, float maxDistance,
                     std::vector<Scene::EntityUVE>& outEntities) const;

    /// Clears the hierarchy, releasing all nodes and entity mappings.
    void ClearUVE() noexcept;

    /// Returns the total number of indexed entities.
    [[nodiscard]] std::size_t GetEntityCountUVE() const noexcept;

    /// Returns the total number of active allocated nodes in the tree.
    [[nodiscard]] std::size_t GetNodeCountUVE() const noexcept;

    /// Returns the index of the root node, or kInvalidNodeIndex if empty.
    [[nodiscard]] std::int32_t GetRootIndexUVE() const noexcept;

    /// Returns true if the hierarchy contains zero entities.
    [[nodiscard]] bool IsEmptyUVE() const noexcept;

    /// Returns true if the entity is currently present in the BVH.
    [[nodiscard]] bool ContainsEntityUVE(Scene::EntityUVE entity) const noexcept;

    /// Returns the leaf node index for the given entity, or kInvalidNodeIndex if not present.
    [[nodiscard]] std::int32_t GetEntityLeafIndexUVE(Scene::EntityUVE entity) const noexcept;

    /// Returns the current world bounds for the given entity, if present.
    [[nodiscard]] std::optional<Math::AabbUVE> GetEntityBoundsUVE(Scene::EntityUVE entity) const noexcept;

    /// Returns a const reference to a node by index.
    [[nodiscard]] const Node& GetNodeUVE(std::size_t index) const;

    /// Returns const access to the internal node array.
    [[nodiscard]] const std::vector<Node>& GetNodesUVE() const noexcept;

    /// Computes and returns topology diagnostics and statistics.
    [[nodiscard]] TreeStatsUVE ComputeStatsUVE() const noexcept;

private:
    [[nodiscard]] std::int32_t AllocateNode();
    void FreeNode(std::int32_t index) noexcept;

    std::int32_t BuildRecursive(std::vector<EntityEntry>& entries, std::size_t begin, std::size_t end,
                                std::int32_t parent);

    [[nodiscard]] std::int32_t FindBestLeafForInsertion(const Math::AabbUVE& bounds) const noexcept;
    void SplitLeaf(std::int32_t leafIndex, const EntityEntry& newEntry);

    std::vector<Node> m_nodes;
    std::unordered_map<Scene::EntityUVE, std::int32_t> m_entityToLeafNode;
    std::vector<std::int32_t> m_freeNodeIndices;
    std::int32_t m_root = kInvalidNodeIndex;
    std::size_t m_entityCount = 0U;
};

} // namespace UVE::Render
