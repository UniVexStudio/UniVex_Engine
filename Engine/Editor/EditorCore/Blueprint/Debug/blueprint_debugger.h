// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "blueprint_instance.h"
#include "blueprint_nodes.h"
#include "blueprint_value.h"

namespace UVE::Blueprint::Debug {

// ---------------------------------------------------------------------------
// Breakpoints
// ---------------------------------------------------------------------------

struct BlueprintBreakpointUVE final {
    std::uint32_t id = 0U;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    bool enabled = true;
    std::uint64_t hitCount = 0U;
    std::string condition;
};

class BlueprintBreakpointManagerUVE final {
public:
    std::uint32_t AddLineBreakpointUVE(std::uint32_t sourceLine, std::uint32_t nodeId = 0U,
                                       std::string condition = {});
    /// Resolves the first translated source line belonging to `nodeId` in `translation` and adds a breakpoint.
    std::optional<std::uint32_t> AddNodeBreakpointUVE(std::uint32_t nodeId,
                                                      const Asset::BlueprintTranslationUVE& translation,
                                                      std::string condition = {});
    bool RemoveBreakpointUVE(std::uint32_t breakpointId);
    bool SetBreakpointEnabledUVE(std::uint32_t breakpointId, bool enabled);
    bool ToggleLineBreakpointUVE(std::uint32_t sourceLine, std::uint32_t nodeId = 0U);
    void RecordHitAtLineUVE(std::uint32_t sourceLine) noexcept;
    void ClearUVE() noexcept;

    [[nodiscard]] const std::vector<BlueprintBreakpointUVE>& BreakpointsUVE() const noexcept { return m_breakpoints; }
    [[nodiscard]] std::vector<std::uint32_t> ActiveSourceLinesUVE() const;
    [[nodiscard]] const BlueprintBreakpointUVE* FindByLineUVE(std::uint32_t sourceLine) const noexcept;

private:
    std::uint32_t m_nextId = 1U;
    std::vector<BlueprintBreakpointUVE> m_breakpoints;
};

// ---------------------------------------------------------------------------
// Watch & LiveValues
// ---------------------------------------------------------------------------

struct BlueprintWatchEntryUVE final {
    std::string expression;
    bool resolved = false;
    std::optional<ValueUVE> value;
    std::string formattedValue;
};

struct BlueprintLiveValueEntryUVE final {
    std::string name;
    std::string formattedValue;
    bool isField = false;
};

struct BlueprintLiveValuesSnapshotUVE final {
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string functionName;
    std::size_t instructionIndex = 0U;
    std::size_t callDepth = 0U;
    std::vector<BlueprintLiveValueEntryUVE> locals;
    std::vector<BlueprintLiveValueEntryUVE> fields;
    std::vector<BlueprintWatchEntryUVE> watches;
};

// ---------------------------------------------------------------------------
// CallStack
// ---------------------------------------------------------------------------

struct BlueprintCallFrameUVE final {
    std::size_t depth = 0U;
    std::string functionName;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::size_t instructionIndex = 0U;
};

// ---------------------------------------------------------------------------
// ExecutionTrace
// ---------------------------------------------------------------------------

enum class BlueprintTraceEventKindUVE : std::uint8_t {
    Paused = 0,
    StepInto,
    StepOver,
    Continued,
    Completed,
    Waiting,
    Failed,
};

struct BlueprintTraceEntryUVE final {
    std::uint64_t sequence = 0U;
    std::uint64_t timestampMicros = 0U;
    BlueprintTraceEventKindUVE kind = BlueprintTraceEventKindUVE::Paused;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string functionName;
    std::string detail;
};

class BlueprintExecutionTraceUVE final {
public:
    explicit BlueprintExecutionTraceUVE(std::size_t maxEntries = 1024U) noexcept;

    void RecordUVE(BlueprintTraceEventKindUVE kind, std::uint32_t sourceLine, std::uint32_t nodeId,
                   std::string functionName, std::string detail = {});
    void ClearUVE() noexcept;
    [[nodiscard]] const std::vector<BlueprintTraceEntryUVE>& EntriesUVE() const noexcept { return m_entries; }
    [[nodiscard]] std::vector<BlueprintTraceEntryUVE> FilterByNodeUVE(std::uint32_t nodeId) const;
    [[nodiscard]] std::string FormatSummaryUVE() const;

private:
    std::size_t m_maxEntries = 1024U;
    std::uint64_t m_nextSequence = 1U;
    std::vector<BlueprintTraceEntryUVE> m_entries;
};

// ---------------------------------------------------------------------------
// Debug Diagnostics & Session Controller
// ---------------------------------------------------------------------------

struct BlueprintDebugDiagnosticUVE final {
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string message;
};

/// High-level Blueprint debugger session wrapping a VM ScriptInstanceUVE and optional source-to-node map.
class BlueprintDebugSessionUVE final {
public:
    BlueprintDebugSessionUVE() = default;

    /// Attaches to `instance` (which must outlive the session or be detached before destruction).
    /// Enables VM debugging on `instance` and synchronizes configured breakpoints.
    [[nodiscard]] bool AttachUVE(ScriptInstanceUVE& instance,
                                 const Asset::BlueprintTranslationUVE* translation = nullptr);
    void DetachUVE() noexcept;
    [[nodiscard]] bool IsAttachedUVE() const noexcept { return m_instance != nullptr; }

    [[nodiscard]] BlueprintBreakpointManagerUVE& BreakpointsUVE() noexcept { return m_breakpoints; }
    [[nodiscard]] const BlueprintBreakpointManagerUVE& BreakpointsUVE() const noexcept { return m_breakpoints; }

    /// Pushes all enabled breakpoints from BreakpointsUVE() to the attached VM instance.
    [[nodiscard]] bool SyncBreakpointsUVE();

    void AddWatchUVE(std::string expression);
    bool RemoveWatchUVE(std::string_view expression);
    void ClearWatchesUVE() noexcept { m_watchExpressions.clear(); }

    /// Captures live locals, fields, and evaluated watch expressions from the paused VM snapshot.
    [[nodiscard]] std::optional<BlueprintLiveValuesSnapshotUVE> CaptureLiveValuesUVE();

    /// Returns the active call stack frames captured while paused.
    [[nodiscard]] std::vector<BlueprintCallFrameUVE> GetCallStackUVE() const;

    [[nodiscard]] ScriptDebugResumeResultUVE ContinueUVE();
    [[nodiscard]] ScriptDebugResumeResultUVE StepIntoUVE();
    [[nodiscard]] ScriptDebugResumeResultUVE StepOverUVE();

    [[nodiscard]] const BlueprintExecutionTraceUVE& TraceUVE() const noexcept { return m_trace; }
    [[nodiscard]] const std::vector<BlueprintDebugDiagnosticUVE>& DiagnosticsUVE() const noexcept {
        return m_diagnostics;
    }

private:
    [[nodiscard]] std::uint32_t NodeForLineUVE(std::uint32_t line) const noexcept;
    void RecordResumeResultUVE(BlueprintTraceEventKindUVE actionKind, const ScriptDebugResumeResultUVE& result);
    void RefreshCallStackFromSnapshotUVE(const ScriptDebugSnapshotUVE& snapshot);

    ScriptInstanceUVE* m_instance = nullptr;
    const Asset::BlueprintTranslationUVE* m_translation = nullptr;
    BlueprintBreakpointManagerUVE m_breakpoints;
    std::vector<std::string> m_watchExpressions;
    BlueprintExecutionTraceUVE m_trace;
    std::vector<BlueprintCallFrameUVE> m_callStack;
    std::vector<BlueprintDebugDiagnosticUVE> m_diagnostics;
};

} // namespace UVE::Blueprint::Debug
