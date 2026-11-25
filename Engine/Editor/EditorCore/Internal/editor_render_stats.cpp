// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_render_stats.h"

#include <array>
#include <cstdio>

namespace UVE::Editor {
namespace {

/// Formats a count with its percentage of a total, e.g. "1840 / 2000 (92%)".
///
/// snprintf rather than std::format: this file is compiled in configurations where
/// UVE_HAS_STD_FORMAT is 0, and a stat row is not worth a second formatting dependency.
[[nodiscard]] std::string FormatCountOfTotal(const std::size_t part, const std::size_t total) {
    std::array<char, 64U> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%zu / %zu (%.0f%%)", part, total,
                                      static_cast<double>(ComputeEditorStatPercentage(part, total)));
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)} : std::string{};
}

[[nodiscard]] std::string FormatCount(const std::size_t value) {
    return std::to_string(value);
}

/// "3 batches / 1668 items (556x)" - the ratio is the point, so it is spelled out rather than
/// left for the reader to divide. A batch is a draw call; items are what those draws covered.
[[nodiscard]] std::string FormatBatchRatio(const std::size_t batches, const std::size_t items) {
    if (batches == 0U) {
        return items == 0U ? std::string{"none"} : std::string{"0 batches / "} + std::to_string(items) + " items";
    }
    std::array<char, 80U> buffer{};
    const double ratio = static_cast<double>(items) / static_cast<double>(batches);
    const int written = std::snprintf(buffer.data(), buffer.size(), "%zu batches / %zu items (%.1fx)", batches,
                                      items, ratio);
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)} : std::string{};
}

constexpr const char* kSectionScene = "Scene";
constexpr const char* kSectionCulling = "Culling";
constexpr const char* kSectionCaches = "Caches";
constexpr const char* kSectionSubmission = "Submission";

} // namespace

float ComputeEditorStatPercentage(const std::size_t part, const std::size_t total) noexcept {
    // A zero total is the normal case, not an error: an empty scene, or any frame before the first
    // cull has run. Returning zero keeps the row printable instead of making the caller guard.
    if (total == 0U) {
        return 0.0F;
    }
    return (static_cast<float>(part) / static_cast<float>(total)) * 100.0F;
}

std::vector<EditorRenderStatRow> BuildEditorRenderStatRows(
    const Render::Renderer3DFrameDiagnostics& diagnostics) {
    std::vector<EditorRenderStatRow> rows;
    rows.reserve(12U);

    rows.push_back({kSectionScene, "Mesh items", FormatCount(diagnostics.meshItemsExtracted), false});
    rows.push_back({kSectionScene, "Primitive items", FormatCount(diagnostics.primitiveItemsExtracted), false});
    rows.push_back({kSectionScene, "Particle items", FormatCount(diagnostics.particleItemsExtracted), false});
    rows.push_back({kSectionScene, "Decals considered", FormatCount(diagnostics.decalsConsidered), false});
    rows.push_back({kSectionScene, "Decal draws", FormatCount(diagnostics.decalDrawsExtracted), false});
    rows.push_back({kSectionScene, "Decal patches", FormatCount(diagnostics.decalPatchesExtracted), false});
    rows.push_back({kSectionScene, "Decal triangles", FormatCount(diagnostics.decalTrianglesExtracted),
                    false});
    rows.push_back({kSectionScene, "Decals on nothing", FormatCount(diagnostics.decalsWithoutReceivers),
                    diagnostics.decalsWithoutReceivers > 0U});
    // Extracted and recorded are different numbers on purpose: the first says the projection pass
    // found geometry, the second says the GPU was actually handed it. A frame whose decals extract
    // but never record is the shape of a decal nobody can see, which is the bug this pair exists to
    // make visible.
    rows.push_back({kSectionScene, "Decal draw calls", FormatCount(diagnostics.decalDrawCallsRecorded),
                    diagnostics.decalDrawsExtracted > 0U && diagnostics.decalDrawCallsRecorded == 0U});
    rows.push_back({kSectionScene, "Decal draws dropped", FormatCount(diagnostics.decalDrawsDropped),
                    diagnostics.decalDrawsDropped > 0U});

    // Clustering. The rejected count is the whole return on building clusters at all: a rejected
    // cluster skips a plane test for every candidate inside it. Flagged when clusters exist and
    // none are being rejected, because that is the shape of the failure - the clusters are being
    // built and paid for while buying nothing.
    const bool hasClusters = diagnostics.visibilityClusters > 0U;
    rows.push_back({kSectionCulling, "Clusters rejected",
                    hasClusters ? FormatCountOfTotal(diagnostics.visibilityClustersRejected,
                                                        diagnostics.visibilityClusters)
                                : std::string{"no clusters"},
                    hasClusters && diagnostics.visibilityClustersRejected == 0U});

    // Caches. A cache that misses every frame is worse than no cache - it pays for a comparison
    // and then does the original work anyway - and it looks identical from the outside, which is
    // precisely why it is worth a row.
    const std::size_t meshLookups = diagnostics.placementCacheHits + diagnostics.placementCacheMisses;
    rows.push_back({kSectionCaches, "Mesh placement hits",
                    meshLookups > 0U ? FormatCountOfTotal(diagnostics.placementCacheHits, meshLookups)
                                     : std::string{"unused"},
                    meshLookups > 0U && diagnostics.placementCacheHits == 0U});
    const std::size_t primitiveLookups =
        diagnostics.primitivePlacementCacheHits + diagnostics.primitivePlacementCacheMisses;
    rows.push_back({kSectionCaches, "Primitive placement hits",
                    primitiveLookups > 0U
                        ? FormatCountOfTotal(diagnostics.primitivePlacementCacheHits, primitiveLookups)
                        : std::string{"unused"},
                    primitiveLookups > 0U && diagnostics.primitivePlacementCacheHits == 0U});

    rows.push_back({kSectionSubmission, "Mesh draws", FormatCount(diagnostics.meshDrawCallsRecorded), false});
    rows.push_back({kSectionSubmission, "Instanced draws",
                    FormatCount(diagnostics.instancedDrawCallsRecorded) + " covering " +
                        FormatCount(diagnostics.instancedObjectsRecorded),
                    false});

    // Shadow batching. Equal batches and items means the batcher merged nothing, which happens
    // when the cascade queue arrives in an order that splits same-mesh runs apart - an ordering
    // change multiplies draw calls while every rendered image stays identical, so nothing except
    // this row would report it.
    rows.push_back({kSectionSubmission, "Shadow batches",
                    FormatBatchRatio(diagnostics.shadowBatchesRecorded, diagnostics.shadowBatchedItems),
                    diagnostics.shadowBatchedItems > 0U &&
                        diagnostics.shadowBatchesRecorded >= diagnostics.shadowBatchedItems});

    rows.push_back({kSectionSubmission, "GL draws", FormatCount(diagnostics.glDrawCallsIssued), false});

    // Asset trouble is reported last and flagged, because unlike the rows above these are never
    // normal: a failed load means something the scene references is not going to appear.
    rows.push_back({kSectionSubmission, "Failed asset loads", FormatCount(diagnostics.failedAssetLoads),
                    diagnostics.failedAssetLoads > 0U});
    rows.push_back({kSectionSubmission, "Invalid asset refs",
                    FormatCount(diagnostics.invalidAssetReferences),
                    diagnostics.invalidAssetReferences > 0U});

    return rows;
}

std::string FormatProfilerRuntimeLine(const Core::DiagnosticRuntimeSnapshot& runtime) {
    return "frame " + std::to_string(runtime.frameNumber) + ", " + std::to_string(runtime.activeAllocationCount) +
           " allocations (" + std::to_string(runtime.activeBytes) + " bytes, peak " +
           std::to_string(runtime.peakBytes) + "), " + std::to_string(runtime.workerCount) + " workers (" +
           std::to_string(runtime.activeWorkerCount) + " active, " + std::to_string(runtime.pendingTaskCount) +
           " pending, " + std::to_string(runtime.stolenJobCount) + " stolen), animation " +
           std::to_string(runtime.animationEvaluationCount) + ", ecs " + std::to_string(runtime.ecsSystemCount) +
           ", vm " + std::to_string(runtime.vmInstructionCount) + ", render passes " +
           std::to_string(runtime.renderPassCount);
}

} // namespace UVE::Editor
