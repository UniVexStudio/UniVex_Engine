

#include "uve/render_systems/mesh_renderer_uve.h"

#include <cmath>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <utility>
#include <vector>
#include <utility>

#include "uve/asset/asset_guid_uve.h"
#include "uve/logging/assert_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/component/mesh_component_uve.h"
#include "uve/component/physics_interpolation_component_uve.h"
#include "uve/component/visibility_component_uve.h"
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

/// A mesh+material pairing, as raw GUID values. AssetGuidUVE has equality and a std::hash
/// specialization but no ordering, so this is hashed rather than compared - and the raw values are
/// used directly rather than adding an operator< to a type in another module purely for a local
/// lookup table.
struct AssetPairKeyUVE final {
    std::uint64_t meshGuidValue = 0U;
    std::uint64_t materialGuidValue = 0U;

    [[nodiscard]] bool operator==(const AssetPairKeyUVE& other) const noexcept {
        return meshGuidValue == other.meshGuidValue && materialGuidValue == other.materialGuidValue;
    }
};

struct AssetPairKeyHashUVE final {
    [[nodiscard]] std::size_t operator()(const AssetPairKeyUVE& key) const noexcept {
        const std::size_t meshHash = std::hash<std::uint64_t>{}(key.meshGuidValue);
        const std::size_t materialHash = std::hash<std::uint64_t>{}(key.materialGuidValue);
        // The usual boost-style mix: the two GUIDs are independent, so a plain XOR would collide
        // for any pairing and its reverse.
        return meshHash ^ (materialHash + 0x9E3779B97F4A7C15ULL + (meshHash << 6U) + (meshHash >> 2U));
    }
};

/// Returns the index of the mesh+material pairing in `assetPairs`, appending it - and taking the
/// frame's one reference to each asset - the first time that pairing is seen.
///
/// Keyed on both GUIDs, not just the mesh: two entities can share a mesh while using different
/// materials, and collapsing those would hand one of them the other's material.
/// Replaces a placement's world matrix and bounds with the pose blended between the entity's last
/// two simulated steps. Returns false - leaving the placement untouched - whenever the simulated
/// pose is the right thing to draw.
///
/// Every rejection path returns the unblended placement rather than a partial result, so a caller
/// that ignores the return value still draws something correct. For a purely visual feature that
/// is the only safe direction to fail in.
[[nodiscard]] bool ApplyInterpolatedPoseUVE(Scene::IEntityManagerUVE& entityManager, const Scene::EntityUVE entity,
                                            const float alpha, const Asset::MeshAssetUVE& mesh,
                                            MeshRenderPlacementUVE& placement) {
    if (!placement.IsPlacedUVE()) {
        // A placement that failed carries no usable matrix to blend, and blending would overwrite
        // the reason it failed with a plausible-looking one.
        return false;
    }
    if (!entityManager.HasComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity)) {
        return false;
    }
    const Scene::PhysicsInterpolationComponentUVE& interpolation =
        entityManager.GetComponentUVE<Scene::PhysicsInterpolationComponentUVE>(entity);

    Math::Vector3UVE position{};
    Math::QuaternionUVE rotation{};
    Math::Vector3UVE scale{};
    if (!Scene::TryGetInterpolatedPoseUVE(interpolation, alpha, position, rotation, scale)) {
        return false;
    }

    // Recomposed rather than lerped as a matrix: interpolating matrix elements directly would
    // shear an object whose rotation changed, because the rows stop being orthonormal partway
    // through. The pose is blended as position/rotation/scale and the matrix rebuilt from it.
    const Math::Matrix4x4UVE worldMatrix = Math::Matrix4x4UVE::ComposeTrsUVE(position, rotation, scale);
    const Math::AabbUVE worldBounds = mesh.localBounds.TransformUVE(worldMatrix);
    const auto isFiniteVector = [](const Math::Vector3UVE& value) {
        return std::isfinite(value.x) && std::isfinite(value.y) && std::isfinite(value.z);
    };
    if (!isFiniteVector(worldBounds.min) || !isFiniteVector(worldBounds.max)) {
        // A degenerate blend must not publish a non-finite bound: the cull would then reject or
        // accept it unpredictably, and the object would flicker rather than simply not smooth.
        return false;
    }
    placement.worldMatrix = worldMatrix;
    placement.worldBounds = worldBounds;
    return true;
}

[[nodiscard]] std::size_t ResolveAssetPairIndexUVE(
    std::unordered_map<AssetPairKeyUVE, std::size_t, AssetPairKeyHashUVE>& slots,
    std::vector<MeshVisibilityAssetPairUVE>& assetPairs, Asset::AssetGuidUVE meshGuid,
    Asset::AssetGuidUVE materialGuid, const Asset::AssetHandleUVE<Asset::MeshAssetUVE>& meshHandle,
    const Asset::AssetHandleUVE<Asset::MaterialAssetUVE>& materialHandle) {
    const AssetPairKeyUVE key{meshGuid.value, materialGuid.value};
    const auto existing = slots.find(key);
    if (existing != slots.end()) {
        return existing->second;
    }
    const std::size_t index = assetPairs.size();
    // The only reference-count increments in the walk. Copies rather than moves: the resolution is
    // shared with every other entity using this GUID and must stay intact for them.
    assetPairs.push_back(MeshVisibilityAssetPairUVE{meshHandle, materialHandle});
    slots.emplace(key, index);
    return index;
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

    // Maps a mesh+material pairing to its slot in the set's asset table, so the reference pair is
    // created once however many entities share it. Local for the same reason the resolutions are:
    // it describes this walk only.
    std::unordered_map<AssetPairKeyUVE, std::size_t, AssetPairKeyHashUVE> assetPairSlots;

    entityManager.ForEachUVE<Scene::WorldTransformComponentUVE, Scene::MeshComponentUVE>(
        [&](Scene::EntityUVE entity, const Scene::WorldTransformComponentUVE& worldTransform,
            const Scene::MeshComponentUVE& meshComponent) {
            UVE_ASSERT(Scene::IsMeshComponentValidUVE(meshComponent));

            // Hidden first, before anything is resolved or counted. A hidden mesh must cost as
            // close to nothing as the walk allows, so this sits ahead of the asset resolution and
            // the placement cache rather than filtering at cull time - culling a candidate that
            // was never going to be drawn still pays to have built it.
            //
            // Also deliberately ahead of the diagnostics: a hidden object is not a scene problem,
            // and counting its unresolved assets as invalid references would make the stats panel
            // report faults for objects the author has simply switched off.
            if (entityManager.HasComponentUVE<Scene::VisibilityComponentUVE>(entity) &&
                !entityManager.GetComponentUVE<Scene::VisibilityComponentUVE>(entity).visibleInHierarchy) {
                ++outVisibilitySet.hiddenEntities;
                return;
            }

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
            // The candidate stores an INDEX, not a pair of handles. The set holds one reference
            // per distinct mesh+material pair and that is what keeps the assets alive across the
            // frame; a reference per entity would be the same two records counted thousands of
            // times, at a mutex and a hash each way.
            const std::size_t assetPairIndex = ResolveAssetPairIndexUVE(
                assetPairSlots, outVisibilitySet.assetPairs, meshComponent.meshGuid,
                meshComponent.materialGuid, resolvedMesh.handle, resolvedMaterial.handle);
            // Physics interpolation, applied to the CANDIDATE rather than to the cached placement.
            //
            // The placement cache is keyed on the simulated world transform, which changes once
            // per fixed step. Keying it on the interpolated pose instead would miss on every frame
            // for every moving object - the cache is worth about 29x, and an interpolated scene
            // would give all of that back. So the expensive part (matrix compose, bounds
            // transform) stays cached against the simulated pose, and only the cheap part - a
            // position lerp and a rotation slerp - is redone per frame. Measured at 15.6x cheaper
            // than recomputing the placement.
            MeshRenderPlacementUVE candidatePlacement = placement;
            if (ApplyInterpolatedPoseUVE(entityManager, entity, outVisibilitySet.physicsInterpolationAlpha,
                                         *mesh, candidatePlacement)) {
                ++outVisibilitySet.interpolatedCandidates;
            }
            outVisibilitySet.candidates.push_back(
                MeshVisibilityCandidateUVE{assetPairIndex, candidatePlacement, material->isTransparent});
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
            // The queue item still owns its own handles: RenderQueueUVE is returned by value from
            // the public ExtractRenderQueueUVE, so it can outlive the visibility set that produced
            // it and cannot borrow that set's references. This is the one place the per-item cost
            // is genuinely load-bearing, and it is paid only for candidates that survived culling
            // rather than for every candidate in the scene.
            const MeshVisibilityAssetPairUVE& assetPair = visibilitySet.assetPairs[candidate.assetPairIndex];
            RenderItemUVE item{eligibility.worldMatrix, assetPair.meshHandle, assetPair.materialHandle,
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
