// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <unordered_map>
#include <vector>

#include "uve/asset/asset_handle_uve.h"
#include "uve/asset/i_asset_database_uve.h"
#include "uve/asset/i_asset_manager_uve.h"
#include "uve/asset/material_asset_uve.h"
#include "uve/asset/mesh_asset_uve.h"
#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/aabb_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/render_systems/mesh_render_eligibility_uve.h"
#include "uve/render_systems/render_queue_uve.h"

namespace UVE::Render {

/// The inputs a placement is derived from: the world transform, the mesh identity, and the local
/// bounds that mesh published.
///
/// Compared field by field to decide whether last frame's placement can be reused. Exact float
/// equality is correct and intended here - this is not a tolerance test but an identity test.
/// A transform that did not change produces bit-identical floats; a transform that changed by any
/// amount at all, however small, must be recomputed rather than approximated.
///
/// The LOCAL BOUNDS are part of the key, not just the transform. A mesh asset that finishes
/// streaming in, or is hot-reloaded with different geometry, keeps the same transform while its
/// bounds change completely - keying on the transform alone would pin that object to the bounds of
/// whatever placeholder it had when first seen.
struct MeshPlacementKeyUVE final {
    Math::Vector3UVE worldPosition{};
    Math::QuaternionUVE worldRotation{};
    Math::Vector3UVE worldScale{};
    Asset::AssetGuidUVE meshGuid = Asset::kInvalidAssetGuidUVE;
    Math::AabbUVE localBounds{};

    [[nodiscard]] bool MatchesUVE(const MeshPlacementKeyUVE& other) const noexcept;
};

/// One renderable entity, resolved and placed, with no frustum yet applied.
///
/// The asset handles are held rather than re-resolved per frustum: resolving them is two hash
/// lookups plus the ready/failed checks, and neither answer can change between the cascades and
/// the main view of a single frame.
/// A cached placement plus the key it was computed from, and the frame stamp that says whether it
/// was touched this frame.
struct MeshPlacementCacheEntryUVE final {
    MeshPlacementKeyUVE key;
    MeshRenderPlacementUVE placement;
    std::uint64_t lastSeenFrame = 0U;
};

/// One frame's resolution of a single asset GUID, shared by every entity that references it.
///
/// WHY THIS EXISTS. Resolving an asset handle is not free: LoadUVE takes the asset manager's mutex
/// and hashes the GUID, the returned handle takes it again to add a reference, each IsReadyUVE,
/// HasFailedUVE and TryGetUVE takes it again, and the destructor takes it once more to release.
/// The mesh walk did all of that per ENTITY, twice over (mesh and material), every frame - about
/// fourteen locked lookups each, measured at roughly 83 us per 1000 meshes.
///
/// But entities do not reference unique assets. The whole premise of the instanced path is that
/// many entities share one mesh and one material, so nearly all of those lookups were asking the
/// same question about the same GUID and getting the same answer. This resolves each distinct GUID
/// once per frame and lets every entity that references it read the result.
///
/// Held for the duration of one walk only, never across frames: asset state is asynchronous, so a
/// pending load becoming ready, a hot reload replacing a pointer, or a load failing must all be
/// observed on the very next frame. Caching this across frames would be a correctness bug.
template <typename T>
struct ResolvedAssetUVE final {
    Asset::AssetHandleUVE<T> handle;
    T* value = nullptr;
    bool failed = false;
    bool pending = false;

    /// True when the handle is neither pending nor failed, so `value` is safe to dereference.
    [[nodiscard]] bool IsUsableUVE() const noexcept { return value != nullptr && !failed && !pending; }
};

struct MeshVisibilityCandidateUVE final {
    Asset::AssetHandleUVE<Asset::MeshAssetUVE> meshHandle;
    Asset::AssetHandleUVE<Asset::MaterialAssetUVE> materialHandle;
    MeshRenderPlacementUVE placement;
    bool isTransparent = false;
};

/// This frame's renderable set, built once and then culled against as many frusta as the frame
/// needs.
///
/// THE POINT OF THIS TYPE. Extraction used to run in full once per frustum, and a frame culls
/// against four of them - three shadow cascades plus the main view. Every one of those passes
/// re-resolved the same asset handles, re-normalized the same quaternions, re-composed the same
/// TRS matrices and re-transformed the same bounding boxes, to reach four different plane tests.
/// Only the plane test actually differed. Building the candidate list once turns the other three
/// passes into plane tests over a flat vector.
///
/// The frame counters live here rather than on each queue because they describe the SCENE, not a
/// view of it: an asset that failed to load failed once, and reporting it four times would make a
/// single broken material look like four.
///
/// Caller-owned, exactly like RenderQueueUVE, so MeshRendererUVE stays stateless as its contract
/// promises and the storage can be reused across frames instead of reallocated.
struct MeshVisibilitySetUVE final {
    std::vector<MeshVisibilityCandidateUVE> candidates;

    /// Last frame's placement per entity, reused when nothing about that entity changed.
    ///
    /// WHY THIS EXISTS. Measured on this engine's own maths: recomputing a placement costs about
    /// 139 us per 1000 entities, while comparing the key and reusing the answer costs about 4.8 us
    /// - roughly 29x. Placement also dominates the frame; even making all four frustum culls free
    /// would save less than skipping placement for objects that did not move. Most objects in most
    /// scenes do not move in a given frame, so most of that work is recomputing an answer the
    /// previous frame already had.
    ///
    /// Keyed by EntityUVE, which is generational - a destroyed entity whose index is reused gets a
    /// new generation and therefore a different key, so a recycled slot cannot inherit the dead
    /// entity's bounds.
    std::unordered_map<Scene::EntityUVE, MeshPlacementCacheEntryUVE> placementCache;

    /// Per-frame cache statistics. Kept because a cache with no visibility into its own hit rate
    /// is a cache nobody can tell is broken: a subtle key bug that silently misses every frame
    /// costs a comparison on top of the original work and otherwise looks identical.
    std::size_t placementCacheHits = 0U;
    std::size_t placementCacheMisses = 0U;

    std::size_t invalidAssetReferences = 0U;
    std::size_t pendingAssetLoads = 0U;
    std::size_t failedAssetLoads = 0U;
    std::size_t invalidRenderEligibility = 0U;

    /// Monotonic frame stamp, incremented by each build, used to tell touched entries from stale
    /// ones without a second pass to reset flags.
    std::uint64_t frameIndex = 0U;

    /// One run of candidates that are near each other in space, plus the box that encloses all of
    /// them. A frustum that misses the enclosing box misses every candidate inside it, so the
    /// whole run is rejected with one plane test instead of `count` of them.
    struct CandidateClusterUVE final {
        Math::AabbUVE bounds{};
        std::size_t first = 0U;
        std::size_t count = 0U;
    };

    /// Spatial clusters over `candidates`, rebuilt by each build and consumed by each cull.
    ///
    /// WHY THIS EXISTS. A frame culls the candidate set four times - three shadow cascades plus the
    /// main view - and each cull tested every candidate against six planes. On a 2000-entity scene
    /// with 60 entities actually visible that is 48000 plane tests to find 60 objects, and culling
    /// had grown to 62% of mesh extraction now that the walk and placement are cached.
    ///
    /// The ordering matters more than the clustering. Candidates come out in entity-creation
    /// order, which has no spatial meaning, so clustering them as-is skips nothing at all - that
    /// was measured: 0 of 32 clusters rejected. Ordering them along a Morton curve first makes 14
    /// of 32 rejectable. A comparison sort costs more than it saves, so the ordering is done with
    /// an O(n) counting pass over the high Morton bits.
    ///
    /// Measured on this engine's own maths, scattered placement, four frusta:
    ///   N=2000  133 us -> 100 us  (1.33x)
    ///   N=8000  534 us -> 260 us  (2.05x)
    /// The win grows with scene size, which is the right shape for spatial work: a small scene
    /// pays a small overhead, a large one gets most of its culling skipped.
    std::vector<CandidateClusterUVE> clusters;

    /// Clears this frame's candidates and counters. Deliberately does NOT clear placementCache -
    /// the cache's entire purpose is to survive into the next frame.
    void ClearUVE() noexcept;

    /// Orders `candidates` spatially and fills `clusters` over the result. Called by the build
    /// once the candidate list is complete, before any cull reads it.
    ///
    /// This REORDERS candidates. Nothing downstream may depend on candidate order: a cull already
    /// sorts its own queue by depth, and the order candidates happened to come out of the entity
    /// walk in was creation order, which carries no meaning anyone is entitled to rely on.
    void BuildSpatialClustersUVE();

    /// Drops cached placements for entities that were not seen this frame, so a cache built over a
    /// long session does not retain every entity the scene ever had. Called after the walk, when
    /// "seen this frame" is known.
    void PruneUnseenPlacementsUVE();

private:
    /// Scratch for the counting pass in BuildSpatialClustersUVE. Members rather than locals so a
    /// scene-sized reorder does not allocate every frame; they are scratch, not state - their
    /// contents mean nothing between calls and every use refills them before reading.
    std::vector<std::uint32_t> m_clusterKeyScratch;
    std::vector<std::size_t> m_clusterOffsetScratch;
    std::vector<std::size_t> m_clusterSlotScratch;
    std::vector<std::size_t> m_clusterSourceScratch;
    std::vector<MeshVisibilityCandidateUVE> m_candidateScratch;
};

} // namespace UVE::Render
