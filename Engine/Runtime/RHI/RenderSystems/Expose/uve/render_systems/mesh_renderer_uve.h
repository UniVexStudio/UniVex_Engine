// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <optional>
#include <vector>

#include "uve/render_systems/dynamic_mesh_bvh_uve.h"
#include "uve/render_systems/i_mesh_renderer_uve.h"

namespace UVE::Render {

/// MeshRendererUVE is the concrete, engine-standard implementation of IMeshRendererUVE.
/// Integrates DynamicMeshBvhUVE as an internal spatial acceleration structure to eliminate
/// linear O(N) entity traversal during visibility set generation and cull off-screen subtrees
/// before candidate placement, component inspection, or layer checks.
class MeshRendererUVE final : public IMeshRendererUVE {
public:
    MeshRendererUVE() = default;
    ~MeshRendererUVE() override = default;

    MeshRendererUVE(const MeshRendererUVE&) = default;
    MeshRendererUVE& operator=(const MeshRendererUVE&) = default;
    MeshRendererUVE(MeshRendererUVE&&) noexcept = default;
    MeshRendererUVE& operator=(MeshRendererUVE&&) noexcept = default;

    // --- IMeshRendererUVE Interface Overrides ---

    [[nodiscard]] RenderQueueUVE ExtractRenderQueueUVE(Scene::IEntityManagerUVE& entityManager,
                                                         Asset::IAssetManagerUVE& assetManager,
                                                         Asset::IAssetDatabaseUVE& assetDatabase,
                                                         const Math::FrustumUVE& cullFrustum) const override;

    void BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager, Asset::IAssetManagerUVE& assetManager,
                               Asset::IAssetDatabaseUVE& assetDatabase,
                               MeshVisibilitySetUVE& outVisibilitySet) const override;

    void CullVisibilitySetIntoUVE(const MeshVisibilitySetUVE& visibilitySet, const Math::FrustumUVE& cullFrustum,
                                  RenderQueueUVE& outQueue) const override;

    void ExtractRenderQueueIntoUVE(Scene::IEntityManagerUVE& entityManager, Asset::IAssetManagerUVE& assetManager,
                                   Asset::IAssetDatabaseUVE& assetDatabase, const Math::FrustumUVE& cullFrustum,
                                   RenderQueueUVE& outQueue) const override;

    // --- Spatial Acceleration Query Overloads ---

    /// Builds this frame's candidate set accelerated by the provided view cull frustum.
    /// Traverses the internal DynamicMeshBvhUVE hierarchy, immediately pruning subtrees outside
    /// the frustum planes and populating candidates only from surviving entities.
    void BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager, Asset::IAssetManagerUVE& assetManager,
                               Asset::IAssetDatabaseUVE& assetDatabase, const Math::FrustumUVE& cullFrustum,
                               MeshVisibilitySetUVE& outVisibilitySet) const;

    /// Builds this frame's candidate set accelerated across multiple active view/cascade frustums.
    /// Surviving entities intersecting any frustum in the list are retained.
    void BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager, Asset::IAssetManagerUVE& assetManager,
                               Asset::IAssetDatabaseUVE& assetDatabase,
                               const std::vector<Math::FrustumUVE>& cullFrustums,
                               MeshVisibilitySetUVE& outVisibilitySet) const;

    /// Builds this frame's candidate set accelerated by an active spatial scene bounding box region.
    void BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager, Asset::IAssetManagerUVE& assetManager,
                               Asset::IAssetDatabaseUVE& assetDatabase, const Math::AabbUVE& boundingRegion,
                               MeshVisibilitySetUVE& outVisibilitySet) const;

    // --- Active View/Frustum State Management ---

    /// Sets an active camera/view frustum used by BuildVisibilitySetUVE(...) when no explicit frustum is passed.
    void SetActiveCullFrustumUVE(const Math::FrustumUVE& frustum) const noexcept;

    /// Sets active camera view + shadow cascade frustums used by BuildVisibilitySetUVE(...).
    void SetActiveCullFrustumsUVE(std::vector<Math::FrustumUVE> frustums) const noexcept;

    /// Sets an active spatial bounding region used by BuildVisibilitySetUVE(...).
    void SetActiveSceneBoundingRegionUVE(const Math::AabbUVE& region) const noexcept;

    /// Clears any configured active cull frustum.
    void ClearActiveCullFrustumUVE() const noexcept;

    /// Clears any configured active bounding region.
    void ClearActiveSceneBoundingRegionUVE() const noexcept;

    [[nodiscard]] bool HasActiveCullFrustumUVE() const noexcept;
    [[nodiscard]] const Math::FrustumUVE& GetActiveCullFrustumUVE() const noexcept;
    [[nodiscard]] bool HasActiveSceneBoundingRegionUVE() const noexcept;
    [[nodiscard]] const Math::AabbUVE& GetActiveSceneBoundingRegionUVE() const noexcept;

    // --- BVH Maintenance & Dynamic Synchronization ---

    /// Inserts or updates an entity's bounding volume in the spatial BVH in O(log N) time with bottom-up refitting.
    void UpdateEntitySpatialIndexUVE(Scene::EntityUVE entity, const Math::AabbUVE& worldBounds) const;

    /// Resolves an entity's current world bounds from its WorldTransformComponent and MeshComponent,
    /// updating its entry in the spatial BVH.
    void UpdateEntitySpatialIndexUVE(Scene::IEntityManagerUVE& entityManager,
                                     Asset::IAssetManagerUVE& assetManager,
                                     Asset::IAssetDatabaseUVE& assetDatabase,
                                     Scene::EntityUVE entity) const;

    /// Removes an entity from the spatial BVH in O(log N) time.
    void RemoveEntitySpatialIndexUVE(Scene::EntityUVE entity) const;

    /// Synchronizes the internal spatial BVH with all valid mesh entities in the scene entity manager.
    void SynchronizeSpatialAccelerationUVE(Scene::IEntityManagerUVE& entityManager,
                                          Asset::IAssetManagerUVE& assetManager,
                                          Asset::IAssetDatabaseUVE& assetDatabase) const;

    /// Forces a full rebuild of the spatial BVH from scratch using median-split partitioning.
    void RebuildSpatialAccelerationUVE(Scene::IEntityManagerUVE& entityManager,
                                       Asset::IAssetManagerUVE& assetManager,
                                       Asset::IAssetDatabaseUVE& assetDatabase) const;

    /// Clears the spatial BVH and active query parameters.
    void ClearSpatialAccelerationUVE() const noexcept;

    // --- Spatial BVH Direct Inspection Accessors ---

    [[nodiscard]] const DynamicMeshBvhUVE& GetSpatialBvhUVE() const noexcept { return m_spatialBvh; }
    [[nodiscard]] DynamicMeshBvhUVE& GetSpatialBvhUVE() noexcept { return m_spatialBvh; }

private:
    void GetAllIndexedEntitiesUVE(std::vector<Scene::EntityUVE>& outEntities) const;

    void QueryFrustumsUVE(const std::vector<Math::FrustumUVE>& frustums,
                          std::vector<Scene::EntityUVE>& outEntities) const;

    void BuildCandidatesFromSurvivingEntitiesUVE(
        const std::vector<Scene::EntityUVE>& survivingEntities,
        Scene::IEntityManagerUVE& entityManager,
        Asset::IAssetManagerUVE& assetManager,
        Asset::IAssetDatabaseUVE& assetDatabase,
        MeshVisibilitySetUVE& outVisibilitySet) const;

    mutable DynamicMeshBvhUVE m_spatialBvh;
    mutable std::optional<Math::FrustumUVE> m_activeCullFrustum;
    mutable std::vector<Math::FrustumUVE> m_activeCullFrustums;
    mutable std::optional<Math::AabbUVE> m_activeBoundingRegion;
};

} // namespace UVE::Render

