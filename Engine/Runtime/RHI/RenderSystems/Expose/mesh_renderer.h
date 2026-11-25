// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <vector>

#include "dynamic_mesh_bvh.h"
#include "i_mesh_renderer.h"

namespace UVE::Render {

/// MeshRenderer is the concrete, engine-standard implementation of IMeshRenderer.
/// Integrates DynamicMeshBvh as an internal spatial acceleration structure to eliminate
/// linear O(N) entity traversal during visibility set generation and cull off-screen subtrees
/// before candidate placement, component inspection, or layer checks.
class MeshRenderer final : public IMeshRenderer {
public:
    MeshRenderer() = default;
    ~MeshRenderer() override = default;

    MeshRenderer(const MeshRenderer&) = default;
    MeshRenderer& operator=(const MeshRenderer&) = default;
    MeshRenderer(MeshRenderer&&) noexcept = default;
    MeshRenderer& operator=(MeshRenderer&&) noexcept = default;

    // --- IMeshRenderer Interface Overrides ---

    [[nodiscard]] RenderQueue ExtractRenderQueue(Scene::IEntityManager& entityManager,
                                                         Asset::IAssetManager& assetManager,
                                                         Asset::IAssetDatabase& assetDatabase,
                                                         const Math::Frustum& cullFrustum) const override;

    void BuildVisibilitySet(Scene::IEntityManager& entityManager, Asset::IAssetManager& assetManager,
                               Asset::IAssetDatabase& assetDatabase,
                               MeshVisibilitySet& outVisibilitySet) const override;

    void CullVisibilitySetInto(const MeshVisibilitySet& visibilitySet, const Math::Frustum& cullFrustum,
                                  RenderQueue& outQueue) const override;

    void ExtractRenderQueueInto(Scene::IEntityManager& entityManager, Asset::IAssetManager& assetManager,
                                   Asset::IAssetDatabase& assetDatabase, const Math::Frustum& cullFrustum,
                                   RenderQueue& outQueue) const override;

    // --- Spatial Acceleration Query Overloads ---

    /// Builds this frame's candidate set accelerated by the provided view cull frustum.
    /// Traverses the internal DynamicMeshBvh hierarchy, immediately pruning subtrees outside
    /// the frustum planes and populating candidates only from surviving entities.
    void BuildVisibilitySet(Scene::IEntityManager& entityManager, Asset::IAssetManager& assetManager,
                               Asset::IAssetDatabase& assetDatabase, const Math::Frustum& cullFrustum,
                               MeshVisibilitySet& outVisibilitySet) const;

    /// Builds this frame's candidate set accelerated across multiple active view/cascade frustums.
    /// Surviving entities intersecting any frustum in the list are retained.
    void BuildVisibilitySet(Scene::IEntityManager& entityManager, Asset::IAssetManager& assetManager,
                               Asset::IAssetDatabase& assetDatabase,
                               const std::vector<Math::Frustum>& cullFrustums,
                               MeshVisibilitySet& outVisibilitySet) const;

    /// Builds this frame's candidate set accelerated by an active spatial scene bounding box region.
    void BuildVisibilitySet(Scene::IEntityManager& entityManager, Asset::IAssetManager& assetManager,
                               Asset::IAssetDatabase& assetDatabase, const Math::Aabb& boundingRegion,
                               MeshVisibilitySet& outVisibilitySet) const;

    // --- Active View/Frustum State Management ---

    /// Sets an active camera/view frustum used by BuildVisibilitySet(...) when no explicit frustum is passed.
    void SetActiveCullFrustum(const Math::Frustum& frustum) const noexcept override;

    /// Sets active camera view + shadow cascade frustums used by BuildVisibilitySet(...).
    void SetActiveCullFrustums(std::vector<Math::Frustum> frustums) const noexcept;

    /// Sets an active spatial bounding region used by BuildVisibilitySet(...).
    void SetActiveSceneBoundingRegion(const Math::Aabb& region) const noexcept;

    /// Clears any configured active cull frustum.
    void ClearActiveCullFrustum() const noexcept override;

    /// Clears any configured active bounding region.
    void ClearActiveSceneBoundingRegion() const noexcept;

    [[nodiscard]] bool HasActiveCullFrustum() const noexcept;
    [[nodiscard]] const Math::Frustum& GetActiveCullFrustum() const noexcept;
    [[nodiscard]] bool HasActiveSceneBoundingRegion() const noexcept;
    [[nodiscard]] const Math::Aabb& GetActiveSceneBoundingRegion() const noexcept;

    // --- BVH Maintenance & Dynamic Synchronization ---

    /// Inserts or updates an entity's bounding volume in the spatial BVH in O(log N) time with bottom-up refitting.
    void UpdateEntitySpatialIndex(Scene::Entity entity, const Math::Aabb& worldBounds) const;

    /// Resolves an entity's current world bounds from its WorldTransformComponent and MeshComponent,
    /// updating its entry in the spatial BVH.
    void UpdateEntitySpatialIndex(Scene::IEntityManager& entityManager,
                                     Asset::IAssetManager& assetManager,
                                     Asset::IAssetDatabase& assetDatabase,
                                     Scene::Entity entity) const;

    /// Removes an entity from the spatial BVH in O(log N) time.
    void RemoveEntitySpatialIndex(Scene::Entity entity) const;

    /// Synchronizes the internal spatial BVH with all valid mesh entities in the scene entity manager.
    void SynchronizeSpatialAcceleration(Scene::IEntityManager& entityManager,
                                          Asset::IAssetManager& assetManager,
                                          Asset::IAssetDatabase& assetDatabase) const override;

    /// Forces a full rebuild of the spatial BVH from scratch using median-split partitioning.
    void RebuildSpatialAcceleration(Scene::IEntityManager& entityManager,
                                       Asset::IAssetManager& assetManager,
                                       Asset::IAssetDatabase& assetDatabase) const;

    /// Clears the spatial BVH and active query parameters.
    void ClearSpatialAcceleration() const noexcept;

    // --- Spatial BVH Direct Inspection Accessors ---

    [[nodiscard]] const DynamicMeshBvh& GetSpatialBvh() const noexcept { return m_spatialBvh; }
    [[nodiscard]] DynamicMeshBvh& GetSpatialBvh() noexcept { return m_spatialBvh; }

private:
    void GetAllIndexedEntities(std::vector<Scene::Entity>& outEntities) const;

    void QueryFrustums(const std::vector<Math::Frustum>& frustums,
                          std::vector<Scene::Entity>& outEntities) const;

    /// Drops any BVH entry and records the entity as unindexable. Entities without a finite
    /// spatial index (an invalid material-without-mesh pair, or non-finite placement bounds)
    /// cannot be frustum-tested, but they must still reach the visibility walk: that is where
    /// Debug builds assert the misconfiguration and where the diagnostics count it.
    void MarkUnindexable(Scene::Entity entity) const;

    /// Removes the unindexable record once the entity is indexable again (or gone entirely).
    void ForgetUnindexable(Scene::Entity entity) const;

    /// Appends every unindexable entity to a BVH survivor list so the walk always sees them.
    void AppendUnindexableEntities(std::vector<Scene::Entity>& survivingEntities) const;

    void BuildCandidatesFromSurvivingEntities(
        const std::vector<Scene::Entity>& survivingEntities,
        Scene::IEntityManager& entityManager,
        Asset::IAssetManager& assetManager,
        Asset::IAssetDatabase& assetDatabase,
        MeshVisibilitySet& outVisibilitySet) const;

    mutable DynamicMeshBvh m_spatialBvh;
    mutable std::vector<Scene::Entity> m_unindexableEntities;
    mutable std::optional<Math::Frustum> m_activeCullFrustum;
    mutable std::vector<Math::Frustum> m_activeCullFrustums;
    mutable std::optional<Math::Aabb> m_activeBoundingRegion;
};

} // namespace UVE::Render

