// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>

#include <gtest/gtest.h>

#include "blueprint_asset.h"
#include "blueprint_profiler.h"
#include "blueprint_resources.h"

namespace UVE::Blueprint::Tests {
namespace {

TEST(BlueprintProfilingTest, ExecutionProfilerAndScopedSamples) {
    Profiling::BlueprintExecutionProfiler profiler;
    profiler.RecordSample(10U, "EventTick", 120.0);
    profiler.RecordSample(10U, "EventTick", 80.0);
    profiler.RecordSample(20U, "HeavyMath", 450.0);

    {
        Profiling::BlueprintScopedProfile scoped(profiler, 30U, "ScopedWork");
    }

    EXPECT_EQ(profiler.TotalCallCount(), 4U);
    EXPECT_GE(profiler.TotalRecordedMicros(), 650.0);

    const auto sorted = profiler.GetSamplesSortedByTotalTime();
    ASSERT_EQ(sorted.size(), 3U);
    EXPECT_EQ(sorted[0].nodeId, 20U);
    EXPECT_EQ(sorted[1].nodeId, 10U);
    EXPECT_DOUBLE_EQ(sorted[1].AverageMicros(), 100.0);
    EXPECT_DOUBLE_EQ(sorted[1].minMicros, 80.0);
    EXPECT_DOUBLE_EQ(sorted[1].maxMicros, 120.0);

    profiler.Reset();
    EXPECT_EQ(profiler.TotalCallCount(), 0U);
}

TEST(BlueprintProfilingTest, ComplexityMemoryAndFormattedReport) {
    Asset::BlueprintAsset bp =
        Resources::InstantiateBlueprintTemplate(Resources::BlueprintTemplateKind::NetworkRpcClient);
    // Add an unconnected orphan branch node to exercise branch + orphan metrics.
    bp.nodes.push_back(Asset::BlueprintNode{99U, "branch", {}, 500.0F, 400.0F, {}});

    const Profiling::BlueprintGraphComplexity complexity = Profiling::AnalyzeBlueprintGraphComplexity(bp);
    EXPECT_EQ(complexity.nodeCount, 4U);
    EXPECT_EQ(complexity.wireCount, 2U);
    EXPECT_EQ(complexity.eventEntryCount, 1U);
    EXPECT_EQ(complexity.pureNodeCount, 1U);
    EXPECT_EQ(complexity.branchNodeCount, 1U);
    EXPECT_EQ(complexity.orphanNodeCount, 1U);
    EXPECT_EQ(complexity.maxChainDepth, 2U);
    EXPECT_EQ(complexity.cyclomaticComplexity, 2U);

    const Profiling::BlueprintMemoryStats mem = Profiling::EstimateBlueprintAssetMemory(bp);
    EXPECT_GT(mem.totalEstimatedBytes, 0U);
    EXPECT_GT(mem.nodesBytes, 0U);

    Profiling::BlueprintExecutionProfiler profiler;
    profiler.RecordSample(2U, "RPC Begin", 35.5);
    const auto report = Profiling::BuildBlueprintPerformanceReport(bp, profiler, 5U);
    const std::string formatted = Profiling::FormatBlueprintPerformanceReport(report);
    EXPECT_NE(formatted.find("Blueprint Performance Report"), std::string::npos);
    EXPECT_NE(formatted.find("RPC Begin"), std::string::npos);
}

} // namespace
} // namespace UVE::Blueprint::Tests
