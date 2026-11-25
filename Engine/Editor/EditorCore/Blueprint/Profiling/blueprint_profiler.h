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

struct BlueprintNodeProfileSample final {
    std::uint32_t nodeId = 0U;
    std::string label;
    std::uint64_t callCount = 0U;
    double totalMicros = 0.0;
    double minMicros = 0.0;
    double maxMicros = 0.0;

    [[nodiscard]] double AverageMicros() const noexcept {
        return callCount > 0U ? (totalMicros / static_cast<double>(callCount)) : 0.0;
    }
};

class BlueprintExecutionProfiler final {
public:
    void SetEnabled(bool enabled) noexcept { m_enabled = enabled; }
    [[nodiscard]] bool IsEnabled() const noexcept { return m_enabled; }

    void RecordSample(std::uint32_t nodeId, std::string_view label, double durationMicros);
    void Reset() noexcept;

    [[nodiscard]] std::vector<BlueprintNodeProfileSample> GetSamplesSortedByTotalTime() const;
    [[nodiscard]] const BlueprintNodeProfileSample* FindSample(std::uint32_t nodeId) const noexcept;
    [[nodiscard]] double TotalRecordedMicros() const noexcept;
    [[nodiscard]] std::uint64_t TotalCallCount() const noexcept;

private:
    bool m_enabled = true;
    std::map<std::uint32_t, BlueprintNodeProfileSample> m_samples;
};

/// RAII timer that records elapsed microseconds into `profiler` upon destruction.
class BlueprintScopedProfile final {
public:
    BlueprintScopedProfile(BlueprintExecutionProfiler& profiler, std::uint32_t nodeId,
                              std::string_view label);
    ~BlueprintScopedProfile();
    BlueprintScopedProfile(const BlueprintScopedProfile&) = delete;
    BlueprintScopedProfile& operator=(const BlueprintScopedProfile&) = delete;

private:
    BlueprintExecutionProfiler& m_profiler;
    std::uint32_t m_nodeId = 0U;
    std::string m_label;
    std::chrono::steady_clock::time_point m_start;
    bool m_active = false;
};

// ---------------------------------------------------------------------------
// Memory Profiling
// ---------------------------------------------------------------------------

struct BlueprintMemoryStats final {
    std::size_t variablesBytes = 0U;
    std::size_t nodesBytes = 0U;
    std::size_t wiresBytes = 0U;
    std::size_t commentsBytes = 0U;
    std::size_t totalEstimatedBytes = 0U;
};

[[nodiscard]] BlueprintMemoryStats EstimateBlueprintAssetMemory(const Asset::BlueprintAsset& blueprint);

// ---------------------------------------------------------------------------
// Graph Complexity & Performance Analysis
// ---------------------------------------------------------------------------

struct BlueprintGraphComplexity final {
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

[[nodiscard]] BlueprintGraphComplexity AnalyzeBlueprintGraphComplexity(
    const Asset::BlueprintAsset& blueprint);

struct BlueprintPerformanceReport final {
    BlueprintGraphComplexity complexity;
    BlueprintMemoryStats memory;
    std::vector<BlueprintNodeProfileSample> hottestNodes;
    double totalExecutionMicros = 0.0;
    std::uint64_t totalCalls = 0U;
};

[[nodiscard]] BlueprintPerformanceReport BuildBlueprintPerformanceReport(
    const Asset::BlueprintAsset& blueprint, const BlueprintExecutionProfiler& profiler,
    std::size_t topHotCount = 10U);

[[nodiscard]] std::string FormatBlueprintPerformanceReport(const BlueprintPerformanceReport& report);

} // namespace UVE::Blueprint::Profiling
