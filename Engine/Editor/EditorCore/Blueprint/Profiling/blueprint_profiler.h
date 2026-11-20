// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "blueprint_asset.h"

namespace UVE::Blueprint::Profiling {

// ---------------------------------------------------------------------------
// Execution Profiling
// ---------------------------------------------------------------------------

struct BlueprintNodeProfileSampleUVE final {
    std::uint32_t nodeId = 0U;
    std::string label;
    std::uint64_t callCount = 0U;
    double totalMicros = 0.0;
    double minMicros = 0.0;
    double maxMicros = 0.0;

    [[nodiscard]] double AverageMicrosUVE() const noexcept {
        return callCount > 0U ? (totalMicros / static_cast<double>(callCount)) : 0.0;
    }
};

class BlueprintExecutionProfilerUVE final {
public:
    void SetEnabledUVE(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabledUVE() const noexcept { return m_enabled; }

    void RecordSampleUVE(std::uint32_t nodeId, std::string_view label, double durationMicros);
    void ResetUVE() noexcept;

    [[nodiscard]] std::vector<BlueprintNodeProfileSampleUVE> GetSamplesSortedByTotalTimeUVE() const;
    [[nodiscard]] const BlueprintNodeProfileSampleUVE* FindSampleUVE(std::uint32_t nodeId) const noexcept;
    [[nodiscard]] double TotalRecordedMicrosUVE() const noexcept;
    [[nodiscard]] std::uint64_t TotalCallCountUVE() const noexcept;

private:
    bool m_enabled = true;
    std::map<std::uint32_t, BlueprintNodeProfileSampleUVE> m_samples;
};

/// RAII timer that records elapsed microseconds into `profiler` upon destruction.
class BlueprintScopedProfileUVE final {
public:
    BlueprintScopedProfileUVE(BlueprintExecutionProfilerUVE& profiler, std::uint32_t nodeId,
                              std::string_view label);
    ~BlueprintScopedProfileUVE();
    BlueprintScopedProfileUVE(const BlueprintScopedProfileUVE&) = delete;
    BlueprintScopedProfileUVE& operator=(const BlueprintScopedProfileUVE&) = delete;

private:
    BlueprintExecutionProfilerUVE& m_profiler;
    std::uint32_t m_nodeId = 0U;
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
    bool m_active = false;
};

// ---------------------------------------------------------------------------
// Memory Profiling
// ---------------------------------------------------------------------------

struct BlueprintMemoryStatsUVE final {
    std::size_t variablesBytes = 0U;
    std::size_t nodesBytes = 0U;
    std::size_t wiresBytes = 0U;
    std::size_t commentsBytes = 0U;
    std::size_t totalEstimatedBytes = 0U;
};

[[nodiscard]] BlueprintMemoryStatsUVE EstimateBlueprintAssetMemoryUVE(const Asset::BlueprintAssetUVE& blueprint);

// ---------------------------------------------------------------------------
// Graph Complexity & Performance Analysis
// ---------------------------------------------------------------------------

struct BlueprintGraphComplexityUVE final {
    std::size_t nodeCount = 0U;
    std::size_t wireCount = 0U;
    std::size_t variableCount = 0U;
    std::size_t commentCount = 0U;
    std::size_t eventEntryCount = 0U;
    std::size_t pureNodeCount = 0U;
    std::size_t branchNodeCount = 0U;
    std::size_t orphanNodeCount = 0U;
    std::size_t maxChainDepth = 0U;
    std::size_t cyclomaticComplexity = 1U;
};

[[nodiscard]] BlueprintGraphComplexityUVE AnalyzeBlueprintGraphComplexityUVE(
    const Asset::BlueprintAssetUVE& blueprint);

struct BlueprintPerformanceReportUVE final {
    BlueprintGraphComplexityUVE complexity;
    BlueprintMemoryStatsUVE memory;
    std::vector<BlueprintNodeProfileSampleUVE> hottestNodes;
    double totalExecutionMicros = 0.0;
    std::uint64_t totalCalls = 0U;
};

[[nodiscard]] BlueprintPerformanceReportUVE BuildBlueprintPerformanceReportUVE(
    const Asset::BlueprintAssetUVE& blueprint, const BlueprintExecutionProfilerUVE& profiler,
    std::size_t topHotCount = 10U);

[[nodiscard]] std::string FormatBlueprintPerformanceReportUVE(const BlueprintPerformanceReportUVE& report);

} // namespace UVE::Blueprint::Profiling
