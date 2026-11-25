// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <vector>

#include "particle_render_bridge.h"
#include "render_item.h"

namespace UVE::Render {

/// A sorted, bucketed set of draw items for one frame — the spec's `RenderQueue` ("sortable
/// render command buffer", Part 7.2). Opaque items are sorted front-to-back (ascending
/// sortDepth) to maximize early-z rejection; transparent items are sorted back-to-front
/// (descending sortDepth) for correct alpha blending. Populated by
/// MeshRenderer::ExtractRenderQueue(); Sort() must be called before a future
/// Renderer3D consumes it.
/// Thread-safety: value type; not thread-safe to mutate concurrently.
struct RenderQueue {
    std::vector<RenderItem> opaqueItems;
    std::vector<RenderItem> transparentItems;
    /// Copied CPU particle draw facts; the renderer owns this frame-local DTO only.
    std::vector<ParticleRenderItem> particleItems;
    bool particleItemsTruncated = false;

    /// Per-frame asset-resolution facts copied during ECS extraction. Counts are references/handles,
    /// not fallback resources or ownership transfers; they explain why eligible entities were not
    /// emitted into either draw-item bucket.
    std::size_t invalidAssetReferences = 0U;
    std::size_t pendingAssetLoads = 0U;
    std::size_t failedAssetLoads = 0U;
    /// Scene/render handoff rejections caused by non-finite transforms or invalid mesh bounds.
    std::size_t invalidRenderEligibility = 0U;

    /// Clears frame items and diagnostics while preserving vector capacity for the next frame.
    void Clear() noexcept;

    /// Reserves storage for expected opaque, transparent, and particle item counts.
    void Reserve(std::size_t opaqueCapacity, std::size_t transparentCapacity, std::size_t particleCapacity);

    /// Sorts opaqueItems ascending by sortDepth (front-to-back), transparentItems descending by
    /// sortDepth (back-to-front), and particleItems descending by sortDepth with deterministic ties.
    void Sort();

    /// Orders opaqueItems by mesh instead of by depth, for a pass that renders depth only.
    ///
    /// WHY THIS EXISTS. BuildShadowBatches groups ADJACENT items that share a mesh, so the
    /// number of draw calls a shadow cascade issues depends entirely on the order it is handed.
    /// Sort orders by depth first, which interleaves meshes by distance and splits those runs
    /// apart: measured on a 1250-item cascade with three distinct meshes, depth order produced 197
    /// batches where mesh order produces 3.
    ///
    /// A depth-only pass binds no material and writes no colour, so front-to-back ordering buys it
    /// nothing that it would not get from the depth test anyway - the ordering is free to serve
    /// batching instead. The main view still uses Sort, where depth order does real work.
    ///
    /// Ties break on sortDepth and then on the same total order Sort uses, so the result is
    /// deterministic frame to frame rather than dependent on which entity the walk saw first.
    void SortForDepthOnlyPass();

    /// Appends copied particle items to the transparent particle bucket; no runtime ownership is transferred.
    void AppendParticleSnapshot(const ParticleRenderSnapshot& snapshot);
};

} // namespace UVE::Render
