// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "uve/asset/asset_guid_uve.h"
#include "uve/math/matrix4x4_uve.h"
#include "uve/render_systems/render_item_uve.h"

namespace UVE::Render {

/// One run of consecutive render-queue items that share a mesh AND a material, and can therefore
/// be drawn by a single instanced draw call instead of one call each.
///
/// `firstItem`/`itemCount` index into the queue bucket the batch was built from, deliberately
/// rather than copying the items: the queue already owns them in the right order, and a batch is a
/// frame-local view over that order, not a second copy of the scene.
struct RenderBatchUVE final {
    Asset::AssetGuidUVE meshGuid = Asset::kInvalidAssetGuidUVE;
    Asset::AssetGuidUVE materialGuid = Asset::kInvalidAssetGuidUVE;
    std::size_t firstItem = 0U;
    std::size_t itemCount = 0U;
};

/// The batching result for one queue bucket: the batches themselves, plus the per-instance model
/// matrices in the exact order the instanced vertex shader will index them with gl_InstanceID.
///
/// `instanceMatrices` is flat across every batch. Batch b's instances occupy
/// [firstItem, firstItem + itemCount), which is why firstItem is an index into THIS array as well
/// as into the source bucket - the two are kept in lockstep on purpose, so a draw call needs one
/// base offset rather than a second indirection table.
struct RenderBatchSetUVE final {
    std::vector<RenderBatchUVE> batches;
    std::vector<Math::Matrix4x4UVE> instanceMatrices;

    void ClearUVE() noexcept;

    /// The count a non-instanced path would have issued: one per item. Kept so a caller can state
    /// what batching actually saved rather than asserting it saved something.
    [[nodiscard]] std::size_t GetTotalInstanceCountUVE() const noexcept;
};

/// Groups `items` into runs that share a mesh+material pair, writing the result into `outBatches`.
///
/// ORDER IS PRESERVED EXACTLY. Only ADJACENT items are merged - the function never reorders the
/// bucket to bring distant matching items together. That restraint is the whole design:
/// RenderQueueUVE::SortUVE() has already sorted opaque items front-to-back for early-z rejection
/// and transparent items back-to-front for correct alpha blending, and regrouping to chase a
/// better batch count would silently undo both. A depth-sorted opaque bucket batches well anyway
/// when a scene actually has repeated meshes near each other, and a transparent bucket that
/// batches poorly is a transparent bucket whose draw order is still correct, which matters more.
///
/// A run of one is still emitted as a batch with itemCount == 1. That keeps the consumer a single
/// loop over batches instead of a loop plus a leftover path, and a one-instance instanced draw is
/// exactly as correct as a non-instanced one.
void BuildRenderBatchesUVE(std::span<const RenderItemUVE> items, RenderBatchSetUVE& outBatches);

/// The depth-only variant, for the shadow cascades: groups by MESH ALONE, ignoring the material.
///
/// The difference from BuildRenderBatchesUVE is deliberate and is not an optimisation shortcut. A
/// shadow pass binds no material - it writes depth through a vertex shader that reads only the
/// model matrix - so two objects sharing a mesh produce byte-identical depth however differently
/// they are painted. Splitting them on material would cost a draw call to express a distinction
/// this pass physically cannot observe.
///
/// Everything else matches its sibling exactly: adjacency-only merging, order preserved, runs of
/// one still emitted, instanceMatrices flat and in lockstep with firstItem. Order matters here at
/// least as much as in the main pass, because a depth-only pass is precisely where front-to-back
/// early-z rejection pays off most.
///
/// `materialGuid` on the emitted batches is left invalid rather than filled with the first item's,
/// so a consumer that wrongly tries to bind a material from a shadow batch fails loudly instead of
/// quietly binding whichever material happened to sort first.
void BuildShadowBatchesUVE(std::span<const RenderItemUVE> items, RenderBatchSetUVE& outBatches);

} // namespace UVE::Render
