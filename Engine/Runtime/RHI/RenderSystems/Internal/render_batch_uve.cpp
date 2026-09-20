// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "uve/render_systems/render_batch_uve.h"

namespace UVE::Render {

void RenderBatchSetUVE::ClearUVE() noexcept {
    // Capacity is kept deliberately: this is rebuilt every frame, and a renderer that reallocated
    // two vectors per frame per bucket would be paying for batching rather than saving with it.
    batches.clear();
    instanceMatrices.clear();
}

std::size_t RenderBatchSetUVE::GetTotalInstanceCountUVE() const noexcept {
    return instanceMatrices.size();
}

void BuildRenderBatchesUVE(const std::span<const RenderItemUVE> items,
                           RenderBatchSetUVE& outBatches) {
    outBatches.ClearUVE();
    if (items.empty()) {
        return;
    }
    outBatches.instanceMatrices.reserve(items.size());

    for (std::size_t index = 0U; index < items.size(); ++index) {
        const RenderItemUVE& item = items[index];
        const Asset::AssetGuidUVE meshGuid = item.meshHandle.GetGuidUVE();
        const Asset::AssetGuidUVE materialGuid = item.materialHandle.GetGuidUVE();

        // Extend the open batch only if this item is adjacent to it AND matches it. Comparing
        // against just the last batch - rather than searching every batch built so far - is what
        // keeps the queue's sort order intact; see the header for why that is worth more than a
        // lower batch count.
        const bool extendsOpenBatch = !outBatches.batches.empty() &&
                                      outBatches.batches.back().meshGuid == meshGuid &&
                                      outBatches.batches.back().materialGuid == materialGuid;
        if (extendsOpenBatch) {
            ++outBatches.batches.back().itemCount;
        } else {
            RenderBatchUVE batch;
            batch.meshGuid = meshGuid;
            batch.materialGuid = materialGuid;
            batch.firstItem = index;
            batch.itemCount = 1U;
            outBatches.batches.push_back(batch);
        }
        outBatches.instanceMatrices.push_back(item.worldMatrix);
    }
}

void BuildShadowBatchesUVE(std::span<const RenderItemUVE> items, RenderBatchSetUVE& outBatches) {
    outBatches.ClearUVE();
    outBatches.instanceMatrices.reserve(items.size());

    for (std::size_t index = 0U; index < items.size(); ++index) {
        const Asset::AssetGuidUVE meshGuid = items[index].meshHandle.GetGuidUVE();

        // Mesh only - see the header. No material comparison, because a depth-only draw binds no
        // material and therefore cannot render two same-mesh objects differently.
        if (!outBatches.batches.empty() && outBatches.batches.back().meshGuid == meshGuid) {
            ++outBatches.batches.back().itemCount;
        } else {
            RenderBatchUVE batch;
            batch.meshGuid = meshGuid;
            batch.materialGuid = Asset::kInvalidAssetGuidUVE;
            batch.firstItem = index;
            batch.itemCount = 1U;
            outBatches.batches.push_back(batch);
        }

        outBatches.instanceMatrices.push_back(items[index].worldMatrix);
    }
}

} // namespace UVE::Render
