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

struct BlueprintBreakpoint final {
    std::uint32_t id = 0U;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    bool enabled = true;
    std::uint64_t hitCount = 0U;
    std::string condition;
};

class BlueprintBreakpointManager final {
public:
    std::uint32_t AddLineBreakpoint(std::uint32_t sourceLine, std::uint32_t nodeId = 0U,
                                       std::string condition = {});
    /// A breakpoint on a graph's node: a graph compiles with each instruction's position being its node's id, so the
    /// "line" of node `nodeId` is `nodeId` itself.
    std::optional<std::uint32_t> AddNodeBreakpoint(std::uint32_t nodeId, std::string condition = {});
    bool RemoveBreakpoint(std::uint32_t breakpointId);
    bool SetBreakpointEnabled(std::uint32_t breakpointId, bool enabled);
    bool ToggleLineBreakpoint(std::uint32_t sourceLine, std::uint32_t nodeId = 0U);
    void RecordHitAtLine(std::uint32_t sourceLine) noexcept;
    void Clear() noexcept;

    [[nodiscard]] const std::vector<BlueprintBreakpoint>& Breakpoints() const noexcept { return m_breakpoints; }
    [[nodiscard]] std::vector<std::uint32_t> ActiveSourceLines() const;
    [[nodiscard]] const BlueprintBreakpoint* FindByLine(std::uint32_t sourceLine) const noexcept;

private:
    std::uint32_t m_nextId = 1U;
    std::vector<BlueprintBreakpoint> m_breakpoints;
};

// ---------------------------------------------------------------------------
// Watch & LiveValues
// ---------------------------------------------------------------------------

struct BlueprintWatchEntry final {
    std::string expression;
    bool resolved = false;
    std::optional<Value> value;
    std::string formattedValue;
};

struct BlueprintLiveValueEntry final {
    std::string name;
    std::string formattedValue;
    bool isField = false;
};

struct BlueprintLiveValuesSnapshot final {
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string functionName;
    std::size_t instructionIndex = 0U;
    std::size_t callDepth = 0U;
    std::vector<BlueprintLiveValueEntry> locals;
    std::vector<BlueprintLiveValueEntry> fields;
    std::vector<BlueprintWatchEntry> watches;
};

// ---------------------------------------------------------------------------
// CallStack
// ---------------------------------------------------------------------------

struct BlueprintCallFrame final {
    std::size_t depth = 0U;
    std::string functionName;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::size_t instructionIndex = 0U;
};

// ---------------------------------------------------------------------------
// ExecutionTrace
// ---------------------------------------------------------------------------

enum class BlueprintTraceEventKind : std::uint8_t {
    Paused = 0,
    StepInto,
    StepOver,
    Continued,
    Completed,
    Waiting,
    Failed,
};

struct BlueprintTraceEntry final {
    std::uint64_t sequence = 0U;
    std::uint64_t timestampMicros = 0U;
    BlueprintTraceEventKind kind = BlueprintTraceEventKind::Paused;
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string functionName;
    std::string detail;
};

class BlueprintExecutionTrace final {
public:
    explicit BlueprintExecutionTrace(std::size_t maxEntries = 1024U) noexcept;

    void Record(BlueprintTraceEventKind kind, std::uint32_t sourceLine, std::uint32_t nodeId,
                   std::string functionName, std::string detail = {});
    void Clear() noexcept;
    [[nodiscard]] const std::vector<BlueprintTraceEntry>& Entries() const noexcept { return m_entries; }
    [[nodiscard]] std::vector<BlueprintTraceEntry> FilterByNode(std::uint32_t nodeId) const;
    [[nodiscard]] std::string FormatSummary() const;

private:
    std::size_t m_maxEntries = 1024U;
    std::uint64_t m_nextSequence = 1U;
    std::vector<BlueprintTraceEntry> m_entries;
};

// ---------------------------------------------------------------------------
// Debug Diagnostics & Session Controller
// ---------------------------------------------------------------------------

struct BlueprintDebugDiagnostic final {
    std::uint32_t sourceLine = 0U;
    std::uint32_t nodeId = 0U;
    std::string message;
};

/// High-level Blueprint debugger session wrapping a VM ScriptInstance and optional position-to-node meaning.
class BlueprintDebugSession final {
public:
    BlueprintDebugSession() = default;

    /// Attaches to `instance` (which must outlive the session or be detached before destruction).
    /// Enables VM debugging on `instance` and synchronizes configured breakpoints.
    /// `positionsAreNodes`: the instance runs a program compiled from a graph, whose instruction positions are
    /// node ids (so a stop reports its node).
    [[nodiscard]] bool Attach(ScriptInstance& instance, bool positionsAreNodes = false);
    void Detach() noexcept;
    [[nodiscard]] bool IsAttached() const noexcept { return m_instance != nullptr; }

    [[nodiscard]] BlueprintBreakpointManager& Breakpoints() noexcept { return m_breakpoints; }
    [[nodiscard]] const BlueprintBreakpointManager& Breakpoints() const noexcept { return m_breakpoints; }

    /// Pushes all enabled breakpoints from Breakpoints() to the attached VM instance.
    [[nodiscard]] bool SyncBreakpoints();

    void AddWatch(std::string expression);
    bool RemoveWatch(std::string_view expression);
    void ClearWatches() noexcept { m_watchExpressions.clear(); }

    /// Captures live locals, fields, and evaluated watch expressions from the paused VM snapshot.
    [[nodiscard]] std::optional<BlueprintLiveValuesSnapshot> CaptureLiveValues();

    /// Returns the active call stack frames captured while paused.
    [[nodiscard]] std::vector<BlueprintCallFrame> GetCallStack() const;

    [[nodiscard]] ScriptDebugResumeResult Continue();
    [[nodiscard]] ScriptDebugResumeResult StepInto();
    [[nodiscard]] ScriptDebugResumeResult StepOver();

    [[nodiscard]] const BlueprintExecutionTrace& Trace() const noexcept { return m_trace; }
    [[nodiscard]] const std::vector<BlueprintDebugDiagnostic>& Diagnostics() const noexcept {
        return m_diagnostics;
    }

private:
    [[nodiscard]] std::uint32_t NodeForLine(std::uint32_t line) const noexcept;
    void RecordResumeResult(BlueprintTraceEventKind actionKind, const ScriptDebugResumeResult& result);
    void RefreshCallStackFromSnapshot(const ScriptDebugSnapshot& snapshot);

    ScriptInstance* m_instance = nullptr;
    bool m_positionsAreNodes = false;
    BlueprintBreakpointManager m_breakpoints;
    std::vector<std::string> m_watchExpressions;
    BlueprintExecutionTrace m_trace;
    std::vector<BlueprintCallFrame> m_callStack;
    std::vector<BlueprintDebugDiagnostic> m_diagnostics;
};

} // namespace UVE::Blueprint::Debug
