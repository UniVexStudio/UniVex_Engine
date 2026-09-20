// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_render_stats_uve.h"

#include <array>
#include <cstdio>

namespace UVE::Editor {
namespace {

/// Formats a count with its percentage of a total, e.g. "1840 / 2000 (92%)".
///
/// snprintf rather than std::format: this file is compiled in configurations where
/// UVE_HAS_STD_FORMAT is 0, and a stat row is not worth a second formatting dependency.
[[nodiscard]] std::string FormatCountOfTotalUVE(const std::size_t part, const std::size_t total) {
    std::array<char, 64U> buffer{};
    const int written = std::snprintf(buffer.data(), buffer.size(), "%zu / %zu (%.0f%%)", part, total,
                                      static_cast<double>(ComputeEditorStatPercentageUVE(part, total)));
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)} : std::string{};
}

[[nodiscard]] std::string FormatCountUVE(const std::size_t value) {
    return std::to_string(value);
}

/// "3 batches / 1668 items (556x)" - the ratio is the point, so it is spelled out rather than
/// left for the reader to divide. A batch is a draw call; items are what those draws covered.
[[nodiscard]] std::string FormatBatchRatioUVE(const std::size_t batches, const std::size_t items) {
    if (batches == 0U) {
        return items == 0U ? std::string{"none"} : std::string{"0 batches / "} + std::to_string(items) + " items";
    }
    std::array<char, 80U> buffer{};
    const double ratio = static_cast<double>(items) / static_cast<double>(batches);
    const int written = std::snprintf(buffer.data(), buffer.size(), "%zu batches / %zu items (%.1fx)", batches,
                                      items, ratio);
    return written > 0 ? std::string{buffer.data(), static_cast<std::size_t>(written)} : std::string{};
}

constexpr const char* kSectionSceneUVE = "Scene";
constexpr const char* kSectionCullingUVE = "Culling";
constexpr const char* kSectionCachesUVE = "Caches";
constexpr const char* kSectionSubmissionUVE = "Submission";

} // namespace

float ComputeEditorStatPercentageUVE(const std::size_t part, const std::size_t total) noexcept {
    // A zero total is the normal case, not an error: an empty scene, or any frame before the first
    // cull has run. Returning zero keeps the row printable instead of making the caller guard.
    if (total == 0U) {
        return 0.0F;
    }
    return (static_cast<float>(part) / static_cast<float>(total)) * 100.0F;
}

std::vector<EditorRenderStatRowUVE> BuildEditorRenderStatRowsUVE(
    const Render::Renderer3DFrameDiagnosticsUVE& diagnostics) {
    std::vector<EditorRenderStatRowUVE> rows;
    rows.reserve(12U);

    rows.push_back({kSectionSceneUVE, "Mesh items", FormatCountUVE(diagnostics.meshItemsExtracted), false});
    rows.push_back({kSectionSceneUVE, "Primitive items", FormatCountUVE(diagnostics.primitiveItemsExtracted), false});
    rows.push_back({kSectionSceneUVE, "Particle items", FormatCountUVE(diagnostics.particleItemsExtracted), false});

    // Clustering. The rejected count is the whole return on building clusters at all: a rejected
    // cluster skips a plane test for every candidate inside it. Flagged when clusters exist and
    // none are being rejected, because that is the shape of the failure - the clusters are being
    // built and paid for while buying nothing.
    const bool hasClusters = diagnostics.visibilityClusters > 0U;
    rows.push_back({kSectionCullingUVE, "Clusters rejected",
                    hasClusters ? FormatCountOfTotalUVE(diagnostics.visibilityClustersRejected,
                                                        diagnostics.visibilityClusters)
                                : std::string{"no clusters"},
                    hasClusters && diagnostics.visibilityClustersRejected == 0U});

    // Caches. A cache that misses every frame is worse than no cache - it pays for a comparison
    // and then does the original work anyway - and it looks identical from the outside, which is
    // precisely why it is worth a row.
    const std::size_t meshLookups = diagnostics.placementCacheHits + diagnostics.placementCacheMisses;
    rows.push_back({kSectionCachesUVE, "Mesh placement hits",
                    meshLookups > 0U ? FormatCountOfTotalUVE(diagnostics.placementCacheHits, meshLookups)
                                     : std::string{"unused"},
                    meshLookups > 0U && diagnostics.placementCacheHits == 0U});
    const std::size_t primitiveLookups =
        diagnostics.primitivePlacementCacheHits + diagnostics.primitivePlacementCacheMisses;
    rows.push_back({kSectionCachesUVE, "Primitive placement hits",
                    primitiveLookups > 0U
                        ? FormatCountOfTotalUVE(diagnostics.primitivePlacementCacheHits, primitiveLookups)
                        : std::string{"unused"},
                    primitiveLookups > 0U && diagnostics.primitivePlacementCacheHits == 0U});

    rows.push_back({kSectionSubmissionUVE, "Mesh draws", FormatCountUVE(diagnostics.meshDrawCallsRecorded), false});
    rows.push_back({kSectionSubmissionUVE, "Instanced draws",
                    FormatCountUVE(diagnostics.instancedDrawCallsRecorded) + " covering " +
                        FormatCountUVE(diagnostics.instancedObjectsRecorded),
                    false});

    // Shadow batching. Equal batches and items means the batcher merged nothing, which happens
    // when the cascade queue arrives in an order that splits same-mesh runs apart - an ordering
    // change multiplies draw calls while every rendered image stays identical, so nothing except
    // this row would report it.
    rows.push_back({kSectionSubmissionUVE, "Shadow batches",
                    FormatBatchRatioUVE(diagnostics.shadowBatchesRecorded, diagnostics.shadowBatchedItems),
                    diagnostics.shadowBatchedItems > 0U &&
                        diagnostics.shadowBatchesRecorded >= diagnostics.shadowBatchedItems});

    rows.push_back({kSectionSubmissionUVE, "GL draws", FormatCountUVE(diagnostics.glDrawCallsIssued), false});

    // Asset trouble is reported last and flagged, because unlike the rows above these are never
    // normal: a failed load means something the scene references is not going to appear.
    rows.push_back({kSectionSubmissionUVE, "Failed asset loads", FormatCountUVE(diagnostics.failedAssetLoads),
                    diagnostics.failedAssetLoads > 0U});
    rows.push_back({kSectionSubmissionUVE, "Invalid asset refs",
                    FormatCountUVE(diagnostics.invalidAssetReferences),
                    diagnostics.invalidAssetReferences > 0U});

    return rows;
}

} // namespace UVE::Editor
