// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_render_stats_uve.h"

#include <algorithm>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

/// Returns a COPY of the named row, not a pointer into the vector.
///
/// A pointer would be the obvious signature and would be a trap: every call site here passes
/// BuildEditorRenderStatRowsUVE(...) directly, so the vector is a temporary that dies at the end
/// of the full expression and the pointer dangles immediately. Returning by value makes the
/// mistake unwritable rather than relying on each call site to remember.
[[nodiscard]] std::optional<EditorRenderStatRowUVE> FindRowUVE(const std::vector<EditorRenderStatRowUVE>& rows,
                                                               const std::string& label) {
    const auto match = std::find_if(rows.cbegin(), rows.cend(),
                                    [&label](const EditorRenderStatRowUVE& row) { return row.label == label; });
    if (match == rows.cend()) {
        return std::nullopt;
    }
    return *match;
}

TEST(EditorRenderStatsUVETest, ComputeEditorStatPercentageUVE_ZeroTotalIsZeroNotADivisionByZero) {
    // Reached constantly rather than theoretically: an empty scene, or any frame before the first
    // cull has run, has a zero total for most of these ratios.
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentageUVE(0U, 0U), 0.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentageUVE(5U, 0U), 0.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentageUVE(1U, 4U), 25.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentageUVE(4U, 4U), 100.0F);
}

TEST(EditorRenderStatsUVETest, EmptyFrameProducesRowsWithoutFlaggingAnything) {
    // A default-constructed frame is what the editor shows before anything has rendered. It must
    // print cleanly: flagging an idle editor teaches the reader to ignore the highlight, which
    // costs the highlight its entire value.
    const std::vector<EditorRenderStatRowUVE> rows =
        BuildEditorRenderStatRowsUVE(Render::Renderer3DFrameDiagnosticsUVE{});

    EXPECT_FALSE(rows.empty());
    for (const EditorRenderStatRowUVE& row : rows) {
        EXPECT_FALSE(row.isConcerning) << "an idle frame flagged '" << row.label << "'";
        EXPECT_FALSE(row.label.empty());
        EXPECT_FALSE(row.value.empty()) << row.label << " printed an empty value";
        EXPECT_FALSE(row.section.empty());
    }
}

TEST(EditorRenderStatsUVETest, RowsAreGroupedSoEachSectionAppearsOnce) {
    // The panel prints a heading whenever the section changes, so interleaved sections would
    // print the same heading repeatedly. Cheap to get wrong by appending a row in the wrong place.
    Render::Renderer3DFrameDiagnosticsUVE diagnostics{};
    diagnostics.meshItemsExtracted = 10U;
    const std::vector<EditorRenderStatRowUVE> rows = BuildEditorRenderStatRowsUVE(diagnostics);

    std::vector<std::string> sectionOrder;
    for (const EditorRenderStatRowUVE& row : rows) {
        if (sectionOrder.empty() || sectionOrder.back() != row.section) {
            sectionOrder.push_back(row.section);
        }
    }
    std::vector<std::string> unique = sectionOrder;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    EXPECT_EQ(sectionOrder.size(), unique.size()) << "a section's rows are not contiguous";
}

TEST(EditorRenderStatsUVETest, ClustersBuiltButNoneRejectedIsFlagged) {
    // The shape of the clustering failure. Clusters get built every frame whether or not they
    // help; if the frustum rejects none of them the engine is paying to build them and getting
    // nothing, and the rendered image is identical either way.
    Render::Renderer3DFrameDiagnosticsUVE diagnostics{};
    diagnostics.visibilityClusters = 32U;
    diagnostics.visibilityClustersRejected = 0U;

    const std::optional<EditorRenderStatRowUVE> row =
        FindRowUVE(BuildEditorRenderStatRowsUVE(diagnostics), "Clusters rejected");
    ASSERT_TRUE(row.has_value());
    EXPECT_TRUE(row.value().isConcerning);

    // Rejecting some is the healthy case and must not be flagged.
    diagnostics.visibilityClustersRejected = 14U;
    const std::optional<EditorRenderStatRowUVE> healthy =
        FindRowUVE(BuildEditorRenderStatRowsUVE(diagnostics), "Clusters rejected");
    ASSERT_TRUE(healthy.has_value());
    EXPECT_FALSE(healthy.value().isConcerning);
    EXPECT_NE(healthy.value().value.find("14"), std::string::npos);
    EXPECT_NE(healthy.value().value.find("32"), std::string::npos);
}

TEST(EditorRenderStatsUVETest, ShadowBatcherThatMergedNothingIsFlagged) {
    // One batch per item means the batcher merged nothing. That happens when the cascade queue
    // arrives ordered so same-mesh runs are split apart - which multiplies draw calls while every
    // rendered pixel stays identical, so no visual check would ever catch it.
    Render::Renderer3DFrameDiagnosticsUVE diagnostics{};
    diagnostics.shadowBatchesRecorded = 1668U;
    diagnostics.shadowBatchedItems = 1668U;

    const std::optional<EditorRenderStatRowUVE> unmerged =
        FindRowUVE(BuildEditorRenderStatRowsUVE(diagnostics), "Shadow batches");
    ASSERT_TRUE(unmerged.has_value());
    EXPECT_TRUE(unmerged.value().isConcerning);

    // Merging well is not flagged, and the ratio is spelled out rather than left to the reader.
    diagnostics.shadowBatchesRecorded = 3U;
    const std::optional<EditorRenderStatRowUVE> merged =
        FindRowUVE(BuildEditorRenderStatRowsUVE(diagnostics), "Shadow batches");
    ASSERT_TRUE(merged.has_value());
    EXPECT_FALSE(merged.value().isConcerning);
    EXPECT_NE(merged.value().value.find("556.0x"), std::string::npos) << "value was: " << merged.value().value;
}

TEST(EditorRenderStatsUVETest, PlacementCacheMissingEveryFrameIsFlaggedButAnUnusedCacheIsNot) {
    // A cache that misses every frame is worse than no cache: it pays for the comparison and then
    // does the original work anyway, and looks identical from outside. An UNUSED cache - a frame
    // with no meshes at all - is not a fault and must read differently.
    Render::Renderer3DFrameDiagnosticsUVE allMisses{};
    allMisses.placementCacheHits = 0U;
    allMisses.placementCacheMisses = 2000U;
    const std::optional<EditorRenderStatRowUVE> missing =
        FindRowUVE(BuildEditorRenderStatRowsUVE(allMisses), "Mesh placement hits");
    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing.value().isConcerning);

    const std::optional<EditorRenderStatRowUVE> unused =
        FindRowUVE(BuildEditorRenderStatRowsUVE(Render::Renderer3DFrameDiagnosticsUVE{}), "Mesh placement hits");
    ASSERT_TRUE(unused.has_value());
    EXPECT_FALSE(unused.value().isConcerning) << "a frame with no meshes is not a broken cache";
    EXPECT_EQ(unused.value().value, "unused");

    Render::Renderer3DFrameDiagnosticsUVE healthy{};
    healthy.placementCacheHits = 1990U;
    healthy.placementCacheMisses = 10U;
    const std::optional<EditorRenderStatRowUVE> warm =
        FindRowUVE(BuildEditorRenderStatRowsUVE(healthy), "Mesh placement hits");
    ASSERT_TRUE(warm.has_value());
    EXPECT_FALSE(warm.value().isConcerning);
    EXPECT_NE(warm.value().value.find("100%"), std::string::npos) << "value was: " << warm.value().value;
}

TEST(EditorRenderStatsUVETest, AssetFailuresAreAlwaysFlagged) {
    // Unlike the performance rows, these are never a normal reading: a failed load means
    // something the scene references will not appear.
    Render::Renderer3DFrameDiagnosticsUVE diagnostics{};
    diagnostics.failedAssetLoads = 1U;
    diagnostics.invalidAssetReferences = 2U;
    const std::vector<EditorRenderStatRowUVE> rows = BuildEditorRenderStatRowsUVE(diagnostics);

    const std::optional<EditorRenderStatRowUVE> failed = FindRowUVE(rows, "Failed asset loads");
    ASSERT_TRUE(failed.has_value());
    EXPECT_TRUE(failed.value().isConcerning);
    const std::optional<EditorRenderStatRowUVE> invalid = FindRowUVE(rows, "Invalid asset refs");
    ASSERT_TRUE(invalid.has_value());
    EXPECT_TRUE(invalid.value().isConcerning);
}

} // namespace
} // namespace UVE::Editor::Tests
