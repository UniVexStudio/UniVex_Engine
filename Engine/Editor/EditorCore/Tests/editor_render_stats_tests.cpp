// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_render_stats.h"

#include <algorithm>
#include <optional>
#include <string>

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

/// Returns a COPY of the named row, not a pointer into the vector.
///
/// A pointer would be the obvious signature and would be a trap: every call site here passes
/// BuildEditorRenderStatRows(...) directly, so the vector is a temporary that dies at the end
/// of the full expression and the pointer dangles immediately. Returning by value makes the
/// mistake unwritable rather than relying on each call site to remember.
[[nodiscard]] std::optional<EditorRenderStatRow> FindRow(const std::vector<EditorRenderStatRow>& rows,
                                                               const std::string& label) {
    const auto match = std::find_if(rows.cbegin(), rows.cend(),
                                    [&label](const EditorRenderStatRow& row) { return row.label == label; });
    if (match == rows.cend()) {
        return std::nullopt;
    }
    return *match;
}

TEST(EditorRenderStatsTest, ComputeEditorStatPercentage_ZeroTotalIsZeroNotADivisionByZero) {
    // Reached constantly rather than theoretically: an empty scene, or any frame before the first
    // cull has run, has a zero total for most of these ratios.
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentage(0U, 0U), 0.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentage(5U, 0U), 0.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentage(1U, 4U), 25.0F);
    EXPECT_FLOAT_EQ(ComputeEditorStatPercentage(4U, 4U), 100.0F);
}

TEST(EditorRenderStatsTest, EmptyFrameProducesRowsWithoutFlaggingAnything) {
    // A default-constructed frame is what the editor shows before anything has rendered. It must
    // print cleanly: flagging an idle editor teaches the reader to ignore the highlight, which
    // costs the highlight its entire value.
    const std::vector<EditorRenderStatRow> rows =
        BuildEditorRenderStatRows(Render::Renderer3DFrameDiagnostics{});

    EXPECT_FALSE(rows.empty());
    for (const EditorRenderStatRow& row : rows) {
        EXPECT_FALSE(row.isConcerning) << "an idle frame flagged '" << row.label << "'";
        EXPECT_FALSE(row.label.empty());
        EXPECT_FALSE(row.value.empty()) << row.label << " printed an empty value";
        EXPECT_FALSE(row.section.empty());
    }
}

TEST(EditorRenderStatsTest, RowsAreGroupedSoEachSectionAppearsOnce) {
    // The panel prints a heading whenever the section changes, so interleaved sections would
    // print the same heading repeatedly. Cheap to get wrong by appending a row in the wrong place.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.meshItemsExtracted = 10U;
    const std::vector<EditorRenderStatRow> rows = BuildEditorRenderStatRows(diagnostics);

    std::vector<std::string> sectionOrder;
    for (const EditorRenderStatRow& row : rows) {
        if (sectionOrder.empty() || sectionOrder.back() != row.section) {
            sectionOrder.push_back(row.section);
        }
    }
    std::vector<std::string> unique = sectionOrder;
    std::sort(unique.begin(), unique.end());
    unique.erase(std::unique(unique.begin(), unique.end()), unique.end());
    EXPECT_EQ(sectionOrder.size(), unique.size()) << "a section's rows are not contiguous";
}

TEST(EditorRenderStatsTest, ClustersBuiltButNoneRejectedIsFlagged) {
    // The shape of the clustering failure. Clusters get built every frame whether or not they
    // help; if the frustum rejects none of them the engine is paying to build them and getting
    // nothing, and the rendered image is identical either way.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.visibilityClusters = 32U;
    diagnostics.visibilityClustersRejected = 0U;

    const std::optional<EditorRenderStatRow> row =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Clusters rejected");
    ASSERT_TRUE(row.has_value());
    EXPECT_TRUE(row.value().isConcerning);

    // Rejecting some is the healthy case and must not be flagged.
    diagnostics.visibilityClustersRejected = 14U;
    const std::optional<EditorRenderStatRow> healthy =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Clusters rejected");
    ASSERT_TRUE(healthy.has_value());
    EXPECT_FALSE(healthy.value().isConcerning);
    EXPECT_NE(healthy.value().value.find("14"), std::string::npos);
    EXPECT_NE(healthy.value().value.find("32"), std::string::npos);
}

TEST(EditorRenderStatsTest, ShadowBatcherThatMergedNothingIsFlagged) {
    // One batch per item means the batcher merged nothing. That happens when the cascade queue
    // arrives ordered so same-mesh runs are split apart - which multiplies draw calls while every
    // rendered pixel stays identical, so no visual check would ever catch it.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.shadowBatchesRecorded = 1668U;
    diagnostics.shadowBatchedItems = 1668U;

    const std::optional<EditorRenderStatRow> unmerged =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Shadow batches");
    ASSERT_TRUE(unmerged.has_value());
    EXPECT_TRUE(unmerged.value().isConcerning);

    // Merging well is not flagged, and the ratio is spelled out rather than left to the reader.
    diagnostics.shadowBatchesRecorded = 3U;
    const std::optional<EditorRenderStatRow> merged =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Shadow batches");
    ASSERT_TRUE(merged.has_value());
    EXPECT_FALSE(merged.value().isConcerning);
    EXPECT_NE(merged.value().value.find("556.0x"), std::string::npos) << "value was: " << merged.value().value;
}

TEST(EditorRenderStatsTest, PlacementCacheMissingEveryFrameIsFlaggedButAnUnusedCacheIsNot) {
    // A cache that misses every frame is worse than no cache: it pays for the comparison and then
    // does the original work anyway, and looks identical from outside. An UNUSED cache - a frame
    // with no meshes at all - is not a fault and must read differently.
    Render::Renderer3DFrameDiagnostics allMisses{};
    allMisses.placementCacheHits = 0U;
    allMisses.placementCacheMisses = 2000U;
    const std::optional<EditorRenderStatRow> missing =
        FindRow(BuildEditorRenderStatRows(allMisses), "Mesh placement hits");
    ASSERT_TRUE(missing.has_value());
    EXPECT_TRUE(missing.value().isConcerning);

    const std::optional<EditorRenderStatRow> unused =
        FindRow(BuildEditorRenderStatRows(Render::Renderer3DFrameDiagnostics{}), "Mesh placement hits");
    ASSERT_TRUE(unused.has_value());
    EXPECT_FALSE(unused.value().isConcerning) << "a frame with no meshes is not a broken cache";
    EXPECT_EQ(unused.value().value, "unused");

    Render::Renderer3DFrameDiagnostics healthy{};
    healthy.placementCacheHits = 1990U;
    healthy.placementCacheMisses = 10U;
    const std::optional<EditorRenderStatRow> warm =
        FindRow(BuildEditorRenderStatRows(healthy), "Mesh placement hits");
    ASSERT_TRUE(warm.has_value());
    EXPECT_FALSE(warm.value().isConcerning);
    EXPECT_NE(warm.value().value.find("100%"), std::string::npos) << "value was: " << warm.value().value;
}

TEST(EditorRenderStatsTest, AssetFailuresAreAlwaysFlagged) {
    // Unlike the performance rows, these are never a normal reading: a failed load means
    // something the scene references will not appear.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.failedAssetLoads = 1U;
    diagnostics.invalidAssetReferences = 2U;
    const std::vector<EditorRenderStatRow> rows = BuildEditorRenderStatRows(diagnostics);

    const std::optional<EditorRenderStatRow> failed = FindRow(rows, "Failed asset loads");
    ASSERT_TRUE(failed.has_value());
    EXPECT_TRUE(failed.value().isConcerning);
    const std::optional<EditorRenderStatRow> invalid = FindRow(rows, "Invalid asset refs");
    ASSERT_TRUE(invalid.has_value());
    EXPECT_TRUE(invalid.value().isConcerning);
}

TEST(EditorRenderStatsTest, ADecalFrameReportsWhatThePassBuiltAndFlagsOnlyWhatLandedOnNothing) {
    // The decal pass counts what it considered, what it drew, what those draws cost in polygons, and
    // how many decals reached receivers but landed on none of them. A decal that projected onto
    // nothing is worth a look - it is authored, enabled, in range and still costs the walk - while
    // the polygon counts are just numbers.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.decalsConsidered = 4U;
    diagnostics.decalDrawsExtracted = 3U;
    diagnostics.decalPatchesExtracted = 5U;
    diagnostics.decalTrianglesExtracted = 9U;
    diagnostics.decalsWithoutReceivers = 1U;
    const std::vector<EditorRenderStatRow> rows = BuildEditorRenderStatRows(diagnostics);

    const std::optional<EditorRenderStatRow> considered = FindRow(rows, "Decals considered");
    ASSERT_TRUE(considered.has_value());
    EXPECT_EQ(considered.value().value, "4");
    EXPECT_FALSE(considered.value().isConcerning);

    const std::optional<EditorRenderStatRow> draws = FindRow(rows, "Decal draws");
    ASSERT_TRUE(draws.has_value());
    EXPECT_EQ(draws.value().value, "3");
    EXPECT_FALSE(draws.value().isConcerning) << "a decal that drew is not a problem to report";

    const std::optional<EditorRenderStatRow> patches = FindRow(rows, "Decal patches");
    ASSERT_TRUE(patches.has_value());
    EXPECT_EQ(patches.value().value, "5");

    const std::optional<EditorRenderStatRow> triangles = FindRow(rows, "Decal triangles");
    ASSERT_TRUE(triangles.has_value());
    EXPECT_EQ(triangles.value().value, "9");

    const std::optional<EditorRenderStatRow> nothing = FindRow(rows, "Decals on nothing");
    ASSERT_TRUE(nothing.has_value());
    EXPECT_EQ(nothing.value().value, "1");
    EXPECT_TRUE(nothing.value().isConcerning) << "a decal that projected onto nothing is worth a look";
}

TEST(EditorRenderStatsTest, DecalsThatExtractButNeverRecordAreFlagged) {
    // The frame the two rows exist for: the projection pass found geometry (so every other decal
    // row reads healthy) and not one draw reached the GPU. That is a decal nobody can see, and no
    // other row can tell it apart from a frame with no decals at all.
    Render::Renderer3DFrameDiagnostics diagnostics{};
    diagnostics.decalsConsidered = 1U;
    diagnostics.decalDrawsExtracted = 1U;
    diagnostics.decalPatchesExtracted = 1U;
    diagnostics.decalTrianglesExtracted = 2U;

    const std::optional<EditorRenderStatRow> drawCalls =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Decal draw calls");
    ASSERT_TRUE(drawCalls.has_value());
    EXPECT_TRUE(drawCalls.value().isConcerning);

    // Recorded is the healthy case, and the row is not flagged for it.
    diagnostics.decalDrawCallsRecorded = 1U;
    const std::optional<EditorRenderStatRow> recorded =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Decal draw calls");
    ASSERT_TRUE(recorded.has_value());
    EXPECT_FALSE(recorded.value().isConcerning);

    // Dropped draws are flagged on their own: a decal the plan refused is not the same event as a
    // decal the pass never extracted, and a scene that hits the frame's budget needs to know.
    diagnostics.decalDrawsDropped = 3U;
    const std::optional<EditorRenderStatRow> dropped =
        FindRow(BuildEditorRenderStatRows(diagnostics), "Decal draws dropped");
    ASSERT_TRUE(dropped.has_value());
    EXPECT_TRUE(dropped.value().isConcerning);
}

} // namespace
} // namespace UVE::Editor::Tests
