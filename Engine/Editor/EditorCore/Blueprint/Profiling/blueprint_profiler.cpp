// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_profiler.h"

#include <algorithm>
#include <iomanip>
#include <set>
#include <sstream>
#include <unordered_map>

#include "blueprint_nodes.h"

namespace UVE::Blueprint::Profiling {
namespace {

[[nodiscard]] std::size_t MaxDepthDfs(const std::uint32_t nodeId,
                                         const std::unordered_map<std::uint32_t, std::vector<std::uint32_t>>& adj,
                                         std::unordered_map<std::uint32_t, std::size_t>& memo,
                                         std::set<std::uint32_t>& visiting) {
    if (const auto it = memo.find(nodeId); it != memo.end()) {
        return it->second;
    }
    if (!visiting.insert(nodeId).second) {
        return 0U;
    }
    std::size_t best = 1U;
    if (const auto it = adj.find(nodeId); it != adj.end()) {
        for (const std::uint32_t nxt : it->second) {
            best = std::max(best, 1U + MaxDepthDfs(nxt, adj, memo, visiting));
        }
    }
    visiting.erase(nodeId);
    memo[nodeId] = best;
    return best;
}

} // namespace

// ---------------------------------------------------------------------------
// BlueprintExecutionProfiler
// ---------------------------------------------------------------------------

void BlueprintExecutionProfiler::RecordSample(const std::uint32_t nodeId, const std::string_view label,
                                                    const double durationMicros) {
    if (!m_enabled) {
        return;
    }
    const double clamped = durationMicros < 0.0 ? 0.0 : durationMicros;
    auto it = m_samples.find(nodeId);
    if (it == m_samples.end()) {
        m_samples.emplace(nodeId, BlueprintNodeProfileSample{nodeId, std::string(label), 1U, clamped, clamped,
                                                                clamped});
        return;
    }
    BlueprintNodeProfileSample& s = it->second;
    if (s.label.empty() && !label.empty()) {
        s.label = std::string(label);
    }
    ++s.callCount;
    s.totalMicros += clamped;
    s.minMicros = std::min(s.minMicros, clamped);
    s.maxMicros = std::max(s.maxMicros, clamped);
}

void BlueprintExecutionProfiler::Reset() noexcept { m_samples.clear(); }

std::vector<BlueprintNodeProfileSample> BlueprintExecutionProfiler::GetSamplesSortedByTotalTime() const {
    std::vector<BlueprintNodeProfileSample> out;
    out.reserve(m_samples.size());
    for (const auto& entry : m_samples) {
        out.push_back(entry.second);
    }
    std::sort(out.begin(), out.end(), [](const BlueprintNodeProfileSample& a, const BlueprintNodeProfileSample& b) {
        if (a.totalMicros != b.totalMicros) {
            return a.totalMicros > b.totalMicros;
        }
        return a.nodeId < b.nodeId;
    });
    return out;
}

const BlueprintNodeProfileSample* BlueprintExecutionProfiler::FindSample(
    const std::uint32_t nodeId) const noexcept {
    const auto it = m_samples.find(nodeId);
    return it != m_samples.end() ? &it->second : nullptr;
}

double BlueprintExecutionProfiler::TotalRecordedMicros() const noexcept {
    double sum = 0.0;
    for (const auto& entry : m_samples) {
        sum += entry.second.totalMicros;
    }
    return sum;
}

std::uint64_t BlueprintExecutionProfiler::TotalCallCount() const noexcept {
    std::uint64_t sum = 0U;
    for (const auto& entry : m_samples) {
        sum += entry.second.callCount;
    }
    return sum;
}

// ---------------------------------------------------------------------------
// BlueprintScopedProfile
// ---------------------------------------------------------------------------

BlueprintScopedProfile::BlueprintScopedProfile(BlueprintExecutionProfiler& profiler,
                                                     const std::uint32_t nodeId,
                                                     const std::string_view label)
    : m_profiler(profiler),
      m_nodeId(nodeId),
      m_label(label),
      m_start(std::chrono::steady_clock::now()),
      m_active(profiler.IsEnabled()) {}

BlueprintScopedProfile::~BlueprintScopedProfile() {
    if (!m_active) {
        return;
    }
    const auto end = std::chrono::steady_clock::now();
    const double micros = std::chrono::duration<double, std::micro>(end - m_start).count();
    m_profiler.RecordSample(m_nodeId, m_label, micros);
}

// ---------------------------------------------------------------------------
// Memory Profiling
// ---------------------------------------------------------------------------

BlueprintMemoryStats EstimateBlueprintAssetMemory(const Asset::BlueprintAsset& blueprint) {
    BlueprintMemoryStats stats;
    for (const Asset::BlueprintVariable& v : blueprint.variables) {
        stats.variablesBytes += sizeof(Asset::BlueprintVariable) + v.name.size() + v.defaultText.size();
    }
    for (const Asset::BlueprintNode& n : blueprint.nodes) {
        stats.nodesBytes += sizeof(Asset::BlueprintNode) + n.type.size() + n.variable.size();
        for (const std::string& val : n.inputValues) {
            stats.nodesBytes += sizeof(std::string) + val.size();
        }
    }
    stats.wiresBytes = blueprint.wires.size() * sizeof(Asset::BlueprintWire);
    for (const Asset::BlueprintComment& c : blueprint.comments) {
        stats.commentsBytes += sizeof(Asset::BlueprintComment) + c.title.size();
    }
    stats.totalEstimatedBytes = sizeof(Asset::BlueprintAsset) + blueprint.parentKind.size() + stats.variablesBytes +
                                stats.nodesBytes + stats.wiresBytes + stats.commentsBytes;
    return stats;
}

// ---------------------------------------------------------------------------
// Graph Complexity & Performance Report
// ---------------------------------------------------------------------------

BlueprintGraphComplexity AnalyzeBlueprintGraphComplexity(const Asset::BlueprintAsset& blueprint) {
    BlueprintGraphComplexity c;
    c.nodeCount = blueprint.nodes.size();
    c.wireCount = blueprint.wires.size();
    c.variableCount = blueprint.variables.size();
    c.commentCount = blueprint.comments.size();

    std::set<std::uint32_t> connectedNodes;
    std::unordered_map<std::uint32_t, std::vector<std::uint32_t>> adj;
    for (const Asset::BlueprintWire& wire : blueprint.wires) {
        connectedNodes.insert(wire.fromNode);
        connectedNodes.insert(wire.toNode);
        adj[wire.fromNode].push_back(wire.toNode);
    }

    for (const Asset::BlueprintNode& node : blueprint.nodes) {
        if (!connectedNodes.contains(node.id)) {
            ++c.orphanNodeCount;
        }
        const Asset::BlueprintNodeType* const spec = Asset::FindBlueprintNodeType(node.type);
        if (spec == nullptr) {
            continue;
        }
        if (spec->role == Asset::BlueprintNodeRole::Event) {
            ++c.eventEntryCount;
        } else if (spec->role == Asset::BlueprintNodeRole::Pure) {
            ++c.pureNodeCount;
        } else if (spec->role == Asset::BlueprintNodeRole::Branch ||
                   spec->role == Asset::BlueprintNodeRole::While ||
                   spec->role == Asset::BlueprintNodeRole::ForLoop ||
                   spec->role == Asset::BlueprintNodeRole::ForEach ||
                   spec->role == Asset::BlueprintNodeRole::SwitchInt ||
                   spec->role == Asset::BlueprintNodeRole::SwitchString) {
            ++c.branchNodeCount;
        }
    }

    std::unordered_map<std::uint32_t, std::size_t> memo;
    std::set<std::uint32_t> visiting;
    for (const Asset::BlueprintNode& node : blueprint.nodes) {
        c.maxChainDepth = std::max(c.maxChainDepth, MaxDepthDfs(node.id, adj, memo, visiting));
    }

    c.cyclomaticComplexity = 1U + c.branchNodeCount;
    return c;
}

BlueprintPerformanceReport BuildBlueprintPerformanceReport(const Asset::BlueprintAsset& blueprint,
                                                                 const BlueprintExecutionProfiler& profiler,
                                                                 const std::size_t topHotCount) {
    BlueprintPerformanceReport report;
    report.complexity = AnalyzeBlueprintGraphComplexity(blueprint);
    report.memory = EstimateBlueprintAssetMemory(blueprint);
    report.hottestNodes = profiler.GetSamplesSortedByTotalTime();
    if (report.hottestNodes.size() > topHotCount) {
        report.hottestNodes.resize(topHotCount);
    }
    report.totalExecutionMicros = profiler.TotalRecordedMicros();
    report.totalCalls = profiler.TotalCallCount();
    return report;
}

std::string FormatBlueprintPerformanceReport(const BlueprintPerformanceReport& report) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(2);
    oss << "Blueprint Performance Report\n"
        << "  Nodes: " << report.complexity.nodeCount << " (Events: " << report.complexity.eventEntryCount
        << ", Pure: " << report.complexity.pureNodeCount << ", Orphans: " << report.complexity.orphanNodeCount << ")\n"
        << "  Wires: " << report.complexity.wireCount << ", Max Depth: " << report.complexity.maxChainDepth
        << ", Cyclomatic Complexity: " << report.complexity.cyclomaticComplexity << "\n"
        << "  Estimated Memory: " << report.memory.totalEstimatedBytes << " bytes\n"
        << "  Execution Total: " << report.totalExecutionMicros << " us across " << report.totalCalls << " call(s)\n";
    for (const BlueprintNodeProfileSample& s : report.hottestNodes) {
        oss << "    - Node #" << s.nodeId << " (" << s.label << "): " << s.totalMicros << " us total, "
            << s.AverageMicros() << " us avg (" << s.callCount << " calls)\n";
    }
    return oss.str();
}

} // namespace UVE::Blueprint::Profiling
