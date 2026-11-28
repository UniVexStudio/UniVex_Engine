// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_debugger.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <sstream>
#include <utility>

namespace UVE::Blueprint::Debug {
namespace {

[[nodiscard]] std::uint64_t NowMicros() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

[[nodiscard]] std::string_view TraceKindName(const BlueprintTraceEventKind kind) noexcept {
    switch (kind) {
        case BlueprintTraceEventKind::Paused: return "Paused";
        case BlueprintTraceEventKind::StepInto: return "StepInto";
        case BlueprintTraceEventKind::StepOver: return "StepOver";
        case BlueprintTraceEventKind::Continued: return "Continued";
        case BlueprintTraceEventKind::Completed: return "Completed";
        case BlueprintTraceEventKind::Waiting: return "Waiting";
        case BlueprintTraceEventKind::Failed: return "Failed";
    }
    return "Paused";
}

[[nodiscard]] BlueprintWatchEntry EvaluateWatch(const std::string& expression,
                                                      const ScriptDebugSnapshot& snapshot) {
    BlueprintWatchEntry entry;
    entry.expression = expression;

    // 1. Named field lookup (e.g. "count" or "self.count")
    std::string_view key = expression;
    if (key.starts_with("self.")) {
        key.remove_prefix(5U);
    }
    for (const auto& field : snapshot.fields) {
        if (field.first == key) {
            entry.resolved = true;
            entry.value = field.second;
            entry.formattedValue = FormatValue(field.second);
            return entry;
        }
    }

    // 2. Local slot lookup (e.g. "local[0]" or "$0")
    std::string_view slotText;
    if (key.starts_with("local[") && key.ends_with(']')) {
        slotText = key.substr(6U, key.size() - 7U);
    } else if (key.starts_with('$')) {
        slotText = key.substr(1U);
    }
    if (!slotText.empty()) {
        std::size_t idx = 0U;
        const auto [ptr, ec] = std::from_chars(slotText.data(), slotText.data() + slotText.size(), idx);
        if (ec == std::errc{} && ptr == slotText.data() + slotText.size() && idx < snapshot.locals.size()) {
            entry.resolved = true;
            entry.value = snapshot.locals[idx];
            entry.formattedValue = FormatValue(snapshot.locals[idx]);
            return entry;
        }
    }

    entry.resolved = false;
    entry.formattedValue = "<unresolved>";
    return entry;
}

} // namespace

// ---------------------------------------------------------------------------
// BlueprintBreakpointManager
// ---------------------------------------------------------------------------

std::uint32_t BlueprintBreakpointManager::AddLineBreakpoint(const std::uint32_t sourceLine,
                                                                  const std::uint32_t nodeId,
                                                                  std::string condition) {
    if (sourceLine == 0U) {
        return 0U;
    }
    for (BlueprintBreakpoint& existing : m_breakpoints) {
        if (existing.sourceLine == sourceLine) {
            existing.enabled = true;
            if (nodeId != 0U) {
                existing.nodeId = nodeId;
            }
            if (!condition.empty()) {
                existing.condition = std::move(condition);
            }
            return existing.id;
        }
    }
    const std::uint32_t id = m_nextId++;
    m_breakpoints.push_back(BlueprintBreakpoint{id, sourceLine, nodeId, true, 0U, std::move(condition)});
    return id;
}

std::optional<std::uint32_t> BlueprintBreakpointManager::AddNodeBreakpoint(const std::uint32_t nodeId, std::string condition) {
    if (nodeId == 0U) {
        return std::nullopt;
    }
    return AddLineBreakpoint(nodeId, nodeId, std::move(condition));
}

bool BlueprintBreakpointManager::RemoveBreakpoint(const std::uint32_t breakpointId) {
    const auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                 [breakpointId](const BlueprintBreakpoint& bp) { return bp.id == breakpointId; });
    if (it == m_breakpoints.end()) {
        return false;
    }
    m_breakpoints.erase(it);
    return true;
}

bool BlueprintBreakpointManager::SetBreakpointEnabled(const std::uint32_t breakpointId, const bool enabled) {
    for (BlueprintBreakpoint& bp : m_breakpoints) {
        if (bp.id == breakpointId) {
            bp.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool BlueprintBreakpointManager::ToggleLineBreakpoint(const std::uint32_t sourceLine,
                                                            const std::uint32_t nodeId) {
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end(); ++it) {
        if (it->sourceLine == sourceLine) {
            m_breakpoints.erase(it);
            return false;
        }
    }
    return AddLineBreakpoint(sourceLine, nodeId) != 0U;
}

void BlueprintBreakpointManager::RecordHitAtLine(const std::uint32_t sourceLine) noexcept {
    for (BlueprintBreakpoint& bp : m_breakpoints) {
        if (bp.enabled && bp.sourceLine == sourceLine) {
            ++bp.hitCount;
        }
    }
}

void BlueprintBreakpointManager::Clear() noexcept { m_breakpoints.clear(); }

std::vector<std::uint32_t> BlueprintBreakpointManager::ActiveSourceLines() const {
    std::vector<std::uint32_t> lines;
    for (const BlueprintBreakpoint& bp : m_breakpoints) {
        if (bp.enabled && bp.sourceLine > 0U) {
            lines.push_back(bp.sourceLine);
        }
    }
    return lines;
}

const BlueprintBreakpoint* BlueprintBreakpointManager::FindByLine(
    const std::uint32_t sourceLine) const noexcept {
    for (const BlueprintBreakpoint& bp : m_breakpoints) {
        if (bp.sourceLine == sourceLine) {
            return &bp;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BlueprintExecutionTrace
// ---------------------------------------------------------------------------

BlueprintExecutionTrace::BlueprintExecutionTrace(const std::size_t maxEntries) noexcept
    : m_maxEntries(maxEntries == 0U ? 1U : maxEntries) {}

void BlueprintExecutionTrace::Record(const BlueprintTraceEventKind kind, const std::uint32_t sourceLine,
                                           const std::uint32_t nodeId, std::string functionName, std::string detail) {
    if (m_entries.size() >= m_maxEntries) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(BlueprintTraceEntry{m_nextSequence++, NowMicros(), kind, sourceLine, nodeId,
                                               std::move(functionName), std::move(detail)});
}

void BlueprintExecutionTrace::Clear() noexcept { m_entries.clear(); }

std::vector<BlueprintTraceEntry> BlueprintExecutionTrace::FilterByNode(const std::uint32_t nodeId) const {
    std::vector<BlueprintTraceEntry> out;
    for (const BlueprintTraceEntry& e : m_entries) {
        if (e.nodeId == nodeId) {
            out.push_back(e);
        }
    }
    return out;
}

std::string BlueprintExecutionTrace::FormatSummary() const {
    std::ostringstream oss;
    for (const BlueprintTraceEntry& e : m_entries) {
        oss << "#" << e.sequence << " [" << TraceKindName(e.kind) << "] " << e.functionName << " line="
            << e.sourceLine << " node=" << e.nodeId;
        if (!e.detail.empty()) {
            oss << " (" << e.detail << ")";
        }
        oss << "\n";
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// BlueprintDebugSession
// ---------------------------------------------------------------------------

bool BlueprintDebugSession::Attach(ScriptInstance& instance, const bool positionsAreNodes) {
    if (!instance.EnableDebugger()) {
        m_diagnostics.push_back(BlueprintDebugDiagnostic{
            0U, 0U, "Could not enable debugger (instance may be running linked native code)."});
        return false;
    }
    m_instance = &instance;
    m_positionsAreNodes = positionsAreNodes;
    m_callStack.clear();
    return SyncBreakpoints();
}

void BlueprintDebugSession::Detach() noexcept {
    if (m_instance != nullptr) {
        m_instance->DisableDebugger();
        m_instance = nullptr;
    }
    m_positionsAreNodes = false;
    m_callStack.clear();
}

bool BlueprintDebugSession::SyncBreakpoints() {
    if (m_instance == nullptr) {
        return false;
    }
    const std::vector<std::uint32_t> lines = m_breakpoints.ActiveSourceLines();
    return m_instance->SetDebuggerBreakpoints(lines);
}

void BlueprintDebugSession::AddWatch(std::string expression) {
    if (expression.empty()) {
        return;
    }
    if (std::find(m_watchExpressions.begin(), m_watchExpressions.end(), expression) == m_watchExpressions.end()) {
        m_watchExpressions.push_back(std::move(expression));
    }
}

bool BlueprintDebugSession::RemoveWatch(const std::string_view expression) {
    const auto it = std::find(m_watchExpressions.begin(), m_watchExpressions.end(), expression);
    if (it == m_watchExpressions.end()) {
        return false;
    }
    m_watchExpressions.erase(it);
    return true;
}

std::uint32_t BlueprintDebugSession::NodeForLine(const std::uint32_t line) const noexcept {
    return m_positionsAreNodes ? line : 0U;
}

void BlueprintDebugSession::RefreshCallStackFromSnapshot(const ScriptDebugSnapshot& snapshot) {
    const std::uint32_t nodeId = NodeForLine(snapshot.line);
    const BlueprintCallFrame currentFrame{snapshot.callDepth, snapshot.functionName, snapshot.line, nodeId,
                                             snapshot.instructionIndex};
    if (snapshot.callDepth == 0U) {
        m_callStack.clear();
        m_callStack.push_back(currentFrame);
        return;
    }
    if (m_callStack.size() > snapshot.callDepth) {
        m_callStack.resize(snapshot.callDepth);
    }
    if (m_callStack.size() == snapshot.callDepth) {
        m_callStack.push_back(currentFrame);
    } else if (!m_callStack.empty() && m_callStack.back().depth == snapshot.callDepth) {
        m_callStack.back() = currentFrame;
    } else {
        m_callStack.push_back(currentFrame);
    }
}

std::optional<BlueprintLiveValuesSnapshot> BlueprintDebugSession::CaptureLiveValues() {
    if (m_instance == nullptr) {
        return std::nullopt;
    }
    const std::optional<ScriptDebugSnapshot> raw = m_instance->GetDebugSnapshot();
    if (!raw.has_value()) {
        return std::nullopt;
    }

    const std::uint32_t nodeId = NodeForLine(raw->line);
    m_breakpoints.RecordHitAtLine(raw->line);
    RefreshCallStackFromSnapshot(*raw);

    BlueprintLiveValuesSnapshot out;
    out.sourceLine = raw->line;
    out.nodeId = nodeId;
    out.functionName = raw->functionName;
    out.instructionIndex = raw->instructionIndex;
    out.callDepth = raw->callDepth;

    out.locals.reserve(raw->locals.size());
    for (std::size_t i = 0U; i < raw->locals.size(); ++i) {
        out.locals.push_back(
            BlueprintLiveValueEntry{"local[" + std::to_string(i) + "]", FormatValue(raw->locals[i]), false});
    }

    out.fields.reserve(raw->fields.size());
    for (const auto& field : raw->fields) {
        out.fields.push_back(BlueprintLiveValueEntry{field.first, FormatValue(field.second), true});
    }

    out.watches.reserve(m_watchExpressions.size());
    for (const std::string& expr : m_watchExpressions) {
        out.watches.push_back(EvaluateWatch(expr, *raw));
    }

    return out;
}

std::vector<BlueprintCallFrame> BlueprintDebugSession::GetCallStack() const { return m_callStack; }

void BlueprintDebugSession::RecordResumeResult(const BlueprintTraceEventKind actionKind,
                                                     const ScriptDebugResumeResult& result) {
    if (m_instance == nullptr) {
        return;
    }
    if (result.state == ScriptDebugState::Paused) {
        if (const auto snap = m_instance->GetDebugSnapshot(); snap.has_value()) {
            const std::uint32_t nodeId = NodeForLine(snap->line);
            RefreshCallStackFromSnapshot(*snap);
            m_trace.Record(actionKind, snap->line, nodeId, snap->functionName, "paused");
        }
    } else if (result.state == ScriptDebugState::Completed) {
        m_trace.Record(BlueprintTraceEventKind::Completed, 0U, 0U,
                          m_callStack.empty() ? std::string{} : m_callStack.front().functionName,
                          result.value.has_value() ? FormatValue(*result.value) : std::string{});
        m_callStack.clear();
    } else if (result.state == ScriptDebugState::Waiting) {
        m_trace.Record(BlueprintTraceEventKind::Waiting, 0U, 0U,
                          m_callStack.empty() ? std::string{} : m_callStack.back().functionName, "yield");
    } else if (result.state == ScriptDebugState::Failed) {
        const std::string err = m_instance->GetLastError();
        m_diagnostics.push_back(BlueprintDebugDiagnostic{0U, 0U, err});
        m_trace.Record(BlueprintTraceEventKind::Failed, 0U, 0U, {}, err);
        m_callStack.clear();
    }
}

ScriptDebugResumeResult BlueprintDebugSession::Continue() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResult result = m_instance->ContinueDebug();
    RecordResumeResult(BlueprintTraceEventKind::Continued, result);
    return result;
}

ScriptDebugResumeResult BlueprintDebugSession::StepInto() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResult result = m_instance->StepIntoDebug();
    RecordResumeResult(BlueprintTraceEventKind::StepInto, result);
    return result;
}

ScriptDebugResumeResult BlueprintDebugSession::StepOver() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResult result = m_instance->StepOverDebug();
    RecordResumeResult(BlueprintTraceEventKind::StepOver, result);
    return result;
}

} // namespace UVE::Blueprint::Debug
