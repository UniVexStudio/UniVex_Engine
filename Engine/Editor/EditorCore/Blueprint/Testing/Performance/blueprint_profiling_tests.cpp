// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_profiler.h"
#include "blueprint_resources.h"

namespace UVE::Blueprint::Tests {
namespace {

TEST(BlueprintProfilingUVETest, ExecutionProfilerAndScopedSamples) {
    Profiling::BlueprintExecutionProfilerUVE profiler;
    profiler.RecordSampleUVE(10U, "EventTick", 120.0);
    profiler.RecordSampleUVE(10U, "EventTick", 80.0);
    profiler.RecordSampleUVE(20U, "HeavyMath", 450.0);

    {
        Profiling::BlueprintScopedProfileUVE scoped(profiler, 30U, "ScopedWork");
    }

    EXPECT_EQ(profiler.TotalCallCountUVE(), 4U);
    EXPECT_GE(profiler.TotalRecordedMicrosUVE(), 650.0);

    const auto sorted = profiler.GetSamplesSortedByTotalTimeUVE();
    ASSERT_EQ(sorted.size(), 3U);
    EXPECT_EQ(sorted[0].nodeId, 20U);
    EXPECT_EQ(sorted[1].nodeId, 10U);
    EXPECT_DOUBLE_EQ(sorted[1].AverageMicrosUVE(), 100.0);
    EXPECT_DOUBLE_EQ(sorted[1].minMicros, 80.0);
    EXPECT_DOUBLE_EQ(sorted[1].maxMicros, 120.0);

    profiler.ResetUVE();
    EXPECT_EQ(profiler.TotalCallCountUVE(), 0U);
}

TEST(BlueprintProfilingUVETest, ComplexityMemoryAndFormattedReport) {
    Asset::BlueprintAssetUVE bp =
        Resources::InstantiateBlueprintTemplateUVE(Resources::BlueprintTemplateKindUVE::NetworkRpcClient);
    // Add an unconnected orphan branch node to exercise branch + orphan metrics.
    bp.nodes.push_back(Asset::BlueprintNodeUVE{99U, "branch", {}, 500.0F, 400.0F, {}});

    const Profiling::BlueprintGraphComplexityUVE complexity = Profiling::AnalyzeBlueprintGraphComplexityUVE(bp);
    EXPECT_EQ(complexity.nodeCount, 4U);
    EXPECT_EQ(complexity.wireCount, 2U);
    EXPECT_EQ(complexity.eventEntryCount, 1U);
    EXPECT_EQ(complexity.pureNodeCount, 1U);
    EXPECT_EQ(complexity.branchNodeCount, 1U);
    EXPECT_EQ(complexity.orphanNodeCount, 1U);
    EXPECT_EQ(complexity.maxChainDepth, 2U);
    EXPECT_EQ(complexity.cyclomaticComplexity, 2U);

    const Profiling::BlueprintMemoryStatsUVE mem = Profiling::EstimateBlueprintAssetMemoryUVE(bp);
    EXPECT_GT(mem.totalEstimatedBytes, 0U);
    EXPECT_GT(mem.nodesBytes, 0U);

    Profiling::BlueprintExecutionProfilerUVE profiler;
    profiler.RecordSampleUVE(2U, "RPC Begin", 35.5);
    const auto report = Profiling::BuildBlueprintPerformanceReportUVE(bp, profiler, 5U);
    const std::string formatted = Profiling::FormatBlueprintPerformanceReportUVE(report);
    EXPECT_NE(formatted.find("Blueprint Performance Report"), std::string::npos);
    EXPECT_NE(formatted.find("RPC Begin"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
