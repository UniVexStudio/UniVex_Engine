

#include "uve/render_systems/mesh_renderer_uve.h"

#include <cstddef>
#include <unordered_map>
#include <utility>

#include "uve/asset/asset_guid_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/world_transform_component_uve.h"

namespace UVE::Render {

namespace {

/// Resolves `guid` once per walk, returning the shared entry on every subsequent call for the same
/// GUID. Separate from the lambda so the mesh and material paths cannot drift into resolving or
/// classifying their handles differently from one another.
template <typename T>
[[nodiscard]] const ResolvedAssetUVE<T>& ResolveOnceUVE(
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<T>>& resolved, Asset::AssetGuidUVE guid,
    Asset::IAssetManagerUVE& assetManager, Asset::IAssetDatabaseUVE& assetDatabase) {
    const auto existing = resolved.find(guid);
    if (existing != resolved.end()) {
        return existing->second;
    }

    ResolvedAssetUVE<T> entry{assetManager.template LoadUVE<T>(guid, assetDatabase), nullptr, false, false};
    // Queried in the same order and with the same meaning as the per-entity code this replaces:
    // failure first, then pending as "not failed and not yet ready", then the pointer.
    entry.failed = entry.handle.HasFailedUVE();
    entry.pending = !entry.failed && !entry.handle.IsReadyUVE();
    if (!entry.failed && !entry.pending) {
        entry.value = entry.handle.TryGetUVE();
    }
    return resolved.emplace(guid, std::move(entry)).first->second;
}

} // namespace


RenderQueueUVE MeshRendererUVE::ExtractRenderQueueUVE(Scene::IEntityManagerUVE& entityManager,
                                                        Asset::IAssetManagerUVE& assetManager,
                                                        Asset::IAssetDatabaseUVE& assetDatabase,
                                                        const Math::FrustumUVE& cullFrustum) const {
    RenderQueueUVE queue;
    ExtractRenderQueueIntoUVE(entityManager, assetManager, assetDatabase, cullFrustum, queue);
    return queue;
}

void MeshRendererUVE::BuildVisibilitySetUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            MeshVisibilitySetUVE& outVisibilitySet) const {
    outVisibilitySet.ClearUVE();
    // Pre-increment, so no live entry can carry the new stamp before the walk assigns it. Starting
    // at 1 also lets 0 mean "never populated", which is how a default-constructed cache entry -
    // the one operator[] just inserted - is told apart from a genuine hit.
    ++outVisibilitySet.frameIndex;

    // Resolved once per distinct GUID for the duration of this walk, then discarded. Many
    // entities share one mesh and one material - that is the premise the instanced path is built
    // on - and resolving a handle costs about fourteen mutex-guarded lookups, so doing it per
    // entity meant asking the same question about the same GUID over and over.
    //
    // Deliberately local, not a member: asset state is asynchronous, and a load that completes, a
    // hot reload that replaces a pointer, or a load that fails must be visible on the very next
    // frame. These die with the walk.
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<Asset::MeshAssetUVE>> resolvedMeshes;
    std::unordered_map<Asset::AssetGuidUVE, ResolvedAssetUVE<Asset::MaterialAssetUVE>> resolvedMaterials;

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::MeshComponentUVE>(
        [&](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::MeshComponentUVE& meshComponent) {
            UVE_ASSERT(Scene::IsMeshComponentValidUVE(meshComponent));
            if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE) {
                ++outVisibilitySet.invalidAssetReferences;
            }
            if (meshComponent.materialGuid == Asset::kInvalidAssetGuidUVE) {
                ++outVisibilitySet.invalidAssetReferences;
            }
            if (meshComponent.meshGuid == Asset::kInvalidAssetGuidUVE ||
                meshComponent.materialGuid == Asset::kInvalidAssetGuidUVE) {
                return;
            }

            const ResolvedAssetUVE<Asset::MeshAssetUVE>& resolvedMesh =
                ResolveOnceUVE(resolvedMeshes, meshComponent.meshGuid, assetManager, assetDatabase);
            const ResolvedAssetUVE<Asset::MaterialAssetUVE>& resolvedMaterial =
                ResolveOnceUVE(resolvedMaterials, meshComponent.materialGuid, assetManager, assetDatabase);

            // Counted per ENTITY, not per resolution. Sharing the resolution is an implementation
            // detail of how the answer was obtained; the diagnostic answers "how many entities
            // could not be drawn this frame", and ten entities blocked on one pending mesh is ten
            // entities that did not draw. Folding these into the resolution would silently change
            // every one of these counters to mean something else.
            outVisibilitySet.failedAssetLoads += static_cast<std::size_t>(resolvedMesh.failed) +
                                                 static_cast<std::size_t>(resolvedMaterial.failed);
            outVisibilitySet.pendingAssetLoads += static_cast<std::size_t>(resolvedMesh.pending) +
                                                  static_cast<std::size_t>(resolvedMaterial.pending);
            if (!resolvedMesh.IsUsableUVE() || !resolvedMaterial.IsUsableUVE()) {
                return;
            }

            const Asset::MeshAssetUVE* const mesh = resolvedMesh.value;
            const Asset::MaterialAssetUVE* const material = resolvedMaterial.value;

            // The cache lookup. Placement is the frame's dominant cost - measured at roughly 29x
            // the price of comparing this key and reusing the answer - and most objects in most
            // scenes do not move, so most of that cost is recomputing last frame's answer.
            const MeshPlacementKeyUVE key{worldTransform.worldPosition, worldTransform.worldRotation,
                                          worldTransform.worldScale, meshComponent.meshGuid, mesh->localBounds};

            MeshPlacementCacheEntryUVE& cacheEntry = outVisibilitySet.placementCache[entity];
            const bool reusable = cacheEntry.lastSeenFrame != 0U && cacheEntry.key.MatchesUVE(key);
            if (reusable) {
                ++outVisibilitySet.placementCacheHits;
            } else {
                ++outVisibilitySet.placementCacheMisses;
                cacheEntry.key = key;
                static_cast<void>(
                    EvaluateMeshRenderPlacementUVE(meshComponent, worldTransform, *mesh, cacheEntry.placement));
            }
            // Stamped on hit as well as miss: the stamp records "seen this frame", which is what
            // the prune reads. Only stamping misses would evict every stationary object.
            cacheEntry.lastSeenFrame = outVisibilitySet.frameIndex;

            const MeshRenderPlacementUVE& placement = cacheEntry.placement;
            if (!placement.IsPlacedUVE()) {
                if (placement.reason == MeshRenderEligibilityReasonUVE::InvalidWorldTransform ||
                    placement.reason == MeshRenderEligibilityReasonUVE::InvalidLocalBounds) {
                    ++outVisibilitySet.invalidRenderEligibility;
                }
                return;
            }

            // Bucketing is decided here, not per frustum: transparency is a property of the
            // material, and no frustum can change it.
            //
            // The handles are COPIED out of the shared resolution rather than moved: the
            // resolution is shared by every entity using this GUID, and moving from it would leave
            // the next entity holding an empty handle. The copy is a reference-count increment,
            // which is what a candidate owning its handle has always cost.
            outVisibilitySet.candidates.push_back(MeshVisibilityCandidateUVE{
                resolvedMesh.handle, resolvedMaterial.handle, placement, material->isTransparent});
        });

    // Bound the cache. Without this it retains an entry for every entity the scene has ever had,
    // which for a streaming world is a slow leak rather than a cache.
    outVisibilitySet.PruneUnseenPlacementsUVE();

    // Built once here, consumed by every cull this set feeds. This reorders candidates, which is
    // safe precisely because nothing downstream may depend on their order - each cull sorts its
    // own queue by depth.
    // TEMP-DISABLED-FOR-BISECT
    outVisibilitySet.BuildSpatialClustersUVE();
}

void MeshRendererUVE::CullVisibilitySetIntoUVE(const MeshVisibilitySetUVE& visibilitySet,
                                               const Math::FrustumUVE& cullFrustum,
                                               RenderQueueUVE& outQueue) const {
    outQueue.ClearUVE();

    // The scene-wide counters are copied onto every queue this set produces. They describe the
    // scene rather than this view of it, so they are the same for all four frusta - a broken
    // material is one broken material, not one per cascade.
    outQueue.invalidAssetReferences = visibilitySet.invalidAssetReferences;
    outQueue.pendingAssetLoads = visibilitySet.pendingAssetLoads;
    outQueue.failedAssetLoads = visibilitySet.failedAssetLoads;
    outQueue.invalidRenderEligibility = visibilitySet.invalidRenderEligibility;

    // Cluster-first. A frustum that misses a cluster's enclosing box misses every candidate in it,
    // so the run is rejected with one plane test rather than one per member. On a scene where most
    // objects are off-screen - which is most scenes, and all four of this frame's frusta - that is
    // where the culling time goes.
    //
    // Falls back to testing everything when the cluster list is empty, so a caller that populated
    // candidates without calling BuildSpatialClustersUVE still gets correct output rather than an
    // empty frame. Correctness must not depend on the optimization having run.
    const auto cullRangeUVE = [&](const std::size_t first, const std::size_t count) {
        for (std::size_t index = first; index < first + count; ++index) {
            const MeshVisibilityCandidateUVE& candidate = visibilitySet.candidates[index];
            MeshRenderEligibilityUVE eligibility;
            if (!TestMeshRenderVisibilityUVE(candidate.placement, cullFrustum, eligibility)) {
                continue;
            }

            // Copies, not moves: one candidate set feeds four queues in a frame, so a move here
            // would empty the handles the remaining cascades still need. AssetHandleUVE copies are
            // refcount bumps, which is exactly what this wants - the handle outlives all four
            // queues anyway.
            RenderItemUVE item{eligibility.worldMatrix, candidate.meshHandle, candidate.materialHandle,
                               eligibility.sortDepth};
            if (candidate.isTransparent) {
                outQueue.transparentItems.push_back(std::move(item));
            } else {
                outQueue.opaqueItems.push_back(std::move(item));
            }
        }
    };

    if (visibilitySet.clusters.empty()) {
        cullRangeUVE(0U, visibilitySet.candidates.size());
        return;
    }
    for (const MeshVisibilitySetUVE::CandidateClusterUVE& cluster : visibilitySet.clusters) {
        if (!cullFrustum.IntersectsUVE(cluster.bounds)) {
            continue;
        }
        cullRangeUVE(cluster.first, cluster.count);
    }
}

void MeshRendererUVE::ExtractRenderQueueIntoUVE(Scene::IEntityManagerUVE& entityManager,
                                            Asset::IAssetManagerUVE& assetManager,
                                            Asset::IAssetDatabaseUVE& assetDatabase,
                                            const Math::FrustumUVE& cullFrustum, RenderQueueUVE& outQueue) const {
    // Kept as the single-frustum entry point, now expressed as build-then-cull rather than a
    // second copy of the walk. A caller that culls once pays nothing for the split; a caller that
    // culls four times calls the two halves directly and pays the walk once.
    MeshVisibilitySetUVE visibilitySet;
    BuildVisibilitySetUVE(entityManager, assetManager, assetDatabase, visibilitySet);
    CullVisibilitySetIntoUVE(visibilitySet, cullFrustum, outQueue);
}

} // namespace UVE::Render
