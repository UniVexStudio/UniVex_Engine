// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include "i_asset_database.h"
#include "i_asset_manager.h"
#include "frustum.h"
#include "mesh_visibility_set.h"
#include "render_queue.h"
#include "i_entity_manager.h"

namespace UVE::Render {

/// IMeshRenderer walks every entity with a WorldTransformComponent + MeshComponent (the
/// spec's `MeshRenderer`, Part 7.2), resolves its mesh/material assets, culls it against a
/// frustum, and buckets it into a sorted RenderQueue — the ECS-to-draw-item extraction step
/// between CameraSystem (Increment 11) and a future Renderer3D (Increment 14).
/// Entities whose meshGuid/materialGuid is kInvalidAssetGuid, or whose asset load hasn't
/// finished yet this frame, are silently skipped (no error) — consistent with AssetManager's
/// async, non-blocking loading design; they appear automatically once loaded.
/// Thread-safety: implementations should be stateless (holding no members), matching
/// CameraSystem's contract. ExtractRenderQueue itself is not thread-safe to call
/// concurrently — intended to be called once per frame from the main engine thread, after
/// SceneGraph::Update() has produced this frame's WorldTransformComponent data.
class IMeshRenderer {
public:
    virtual ~IMeshRenderer() = default;

    /// Extracts this frame's RenderQueue: every visible, asset-ready entity with both
    /// WorldTransformComponent and MeshComponent, culled against `cullFrustum` and bucketed
    /// into opaque/transparent per its MaterialAsset::isTransparent.
    [[nodiscard]] virtual RenderQueue ExtractRenderQueue(Scene::IEntityManager& entityManager,
                                                                 Asset::IAssetManager& assetManager,
                                                                 Asset::IAssetDatabase& assetDatabase,
                                                                 const Math::Frustum& cullFrustum) const = 0;

    /// Builds this frame's frustum-INDEPENDENT candidate set: every entity resolved, validated and
    /// placed in the world, with no culling applied.
    ///
    /// Split from extraction because a frame culls against four frusta (three shadow cascades plus
    /// the main view) and the expensive work - asset resolution, quaternion normalization, TRS
    /// compose, bounds transform - is identical across all four. Build once, then call
    /// CullVisibilitySetInto per frustum.
    ///
    /// The default implementation is a no-op returning nothing useful, so existing test doubles
    /// keep compiling; MeshRenderer implements it. A caller that gets an empty set from a
    /// double simply sees no meshes, which is the same thing a double that renders nothing already
    /// did.
    virtual void BuildVisibilitySet(Scene::IEntityManager& entityManager,
                                       Asset::IAssetManager& assetManager,
                                       Asset::IAssetDatabase& assetDatabase,
                                       MeshVisibilitySet& outVisibilitySet) const {
        static_cast<void>(entityManager);
        static_cast<void>(assetManager);
        static_cast<void>(assetDatabase);
        outVisibilitySet.Clear();
    }

    /// Culls an already-built candidate set against one frustum into a queue. This is the part
    /// that genuinely has to run per frustum, and it is deliberately just a plane test, a depth
    /// derivation and a bucket push per candidate.
    ///
    /// Defaulted to the full extraction path for the same compatibility reason as above - a double
    /// that only implements ExtractRenderQueue still behaves correctly, just without the saving.
    virtual void CullVisibilitySetInto(const MeshVisibilitySet& visibilitySet,
                                          const Math::Frustum& cullFrustum,
                                          RenderQueue& outQueue) const {
        static_cast<void>(visibilitySet);
        static_cast<void>(cullFrustum);
        outQueue.Clear();
    }

    /// Fills caller-owned queue storage. The default preserves compatibility for existing
    /// implementations/test doubles; MeshRenderer overrides it to reuse vector capacity.
    virtual void ExtractRenderQueueInto(Scene::IEntityManager& entityManager,
                                           Asset::IAssetManager& assetManager,
                                           Asset::IAssetDatabase& assetDatabase,
                                           const Math::Frustum& cullFrustum, RenderQueue& outQueue) const {
        outQueue = ExtractRenderQueue(entityManager, assetManager, assetDatabase, cullFrustum);
    }

    virtual void SetActiveCullFrustum(const Math::Frustum& frustum) const noexcept {
        static_cast<void>(frustum);
    }

    virtual void ClearActiveCullFrustum() const noexcept {}

    virtual void SynchronizeSpatialAcceleration(Scene::IEntityManager& entityManager,
                                                   Asset::IAssetManager& assetManager,
                                                   Asset::IAssetDatabase& assetDatabase) const {
        static_cast<void>(entityManager);
        static_cast<void>(assetManager);
        static_cast<void>(assetDatabase);
    }
};

} // namespace UVE::Render
