// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "blueprint_debugger.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <sstream>
#include <utility>

namespace UVE::Blueprint::Debug {
namespace {

[[nodiscard]] std::uint64_t NowMicrosUVE() noexcept {
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::microseconds>(now).count());
}

[[nodiscard]] std::string_view TraceKindNameUVE(const BlueprintTraceEventKindUVE kind) noexcept {
    switch (kind) {
        case BlueprintTraceEventKindUVE::Paused: return "Paused";
        case BlueprintTraceEventKindUVE::StepInto: return "StepInto";
        case BlueprintTraceEventKindUVE::StepOver: return "StepOver";
        case BlueprintTraceEventKindUVE::Continued: return "Continued";
        case BlueprintTraceEventKindUVE::Completed: return "Completed";
        case BlueprintTraceEventKindUVE::Waiting: return "Waiting";
        case BlueprintTraceEventKindUVE::Failed: return "Failed";
    }
    return "Paused";
}

[[nodiscard]] BlueprintWatchEntryUVE EvaluateWatchUVE(const std::string& expression,
                                                      const ScriptDebugSnapshotUVE& snapshot) {
    BlueprintWatchEntryUVE entry;
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
            entry.formattedValue = FormatValueUVE(field.second);
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
            entry.formattedValue = FormatValueUVE(snapshot.locals[idx]);
            return entry;
        }
    }

    entry.resolved = false;
    entry.formattedValue = "<unresolved>";
    return entry;
}

} // namespace

// ---------------------------------------------------------------------------
// BlueprintBreakpointManagerUVE
// ---------------------------------------------------------------------------

std::uint32_t BlueprintBreakpointManagerUVE::AddLineBreakpointUVE(const std::uint32_t sourceLine,
                                                                  const std::uint32_t nodeId,
                                                                  std::string condition) {
    if (sourceLine == 0U) {
        return 0U;
    }
    for (BlueprintBreakpointUVE& existing : m_breakpoints) {
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
    m_breakpoints.push_back(BlueprintBreakpointUVE{id, sourceLine, nodeId, true, 0U, std::move(condition)});
    return id;
}

std::optional<std::uint32_t> BlueprintBreakpointManagerUVE::AddNodeBreakpointUVE(
    const std::uint32_t nodeId, const Asset::BlueprintTranslationUVE& translation, std::string condition) {
    if (nodeId == 0U) {
        return std::nullopt;
    }
    for (std::size_t i = 0U; i < translation.lineNodes.size(); ++i) {
        if (translation.lineNodes[i] == nodeId) {
            return AddLineBreakpointUVE(static_cast<std::uint32_t>(i + 1U), nodeId, std::move(condition));
        }
    }
    return std::nullopt;
}

bool BlueprintBreakpointManagerUVE::RemoveBreakpointUVE(const std::uint32_t breakpointId) {
    const auto it = std::find_if(m_breakpoints.begin(), m_breakpoints.end(),
                                 [breakpointId](const BlueprintBreakpointUVE& bp) { return bp.id == breakpointId; });
    if (it == m_breakpoints.end()) {
        return false;
    }
    m_breakpoints.erase(it);
    return true;
}

bool BlueprintBreakpointManagerUVE::SetBreakpointEnabledUVE(const std::uint32_t breakpointId, const bool enabled) {
    for (BlueprintBreakpointUVE& bp : m_breakpoints) {
        if (bp.id == breakpointId) {
            bp.enabled = enabled;
            return true;
        }
    }
    return false;
}

bool BlueprintBreakpointManagerUVE::ToggleLineBreakpointUVE(const std::uint32_t sourceLine,
                                                            const std::uint32_t nodeId) {
    for (auto it = m_breakpoints.begin(); it != m_breakpoints.end(); ++it) {
        if (it->sourceLine == sourceLine) {
            m_breakpoints.erase(it);
            return false;
        }
    }
    return AddLineBreakpointUVE(sourceLine, nodeId) != 0U;
}

void BlueprintBreakpointManagerUVE::RecordHitAtLineUVE(const std::uint32_t sourceLine) noexcept {
    for (BlueprintBreakpointUVE& bp : m_breakpoints) {
        if (bp.enabled && bp.sourceLine == sourceLine) {
            ++bp.hitCount;
        }
    }
}

void BlueprintBreakpointManagerUVE::ClearUVE() noexcept { m_breakpoints.clear(); }

std::vector<std::uint32_t> BlueprintBreakpointManagerUVE::ActiveSourceLinesUVE() const {
    std::vector<std::uint32_t> lines;
    for (const BlueprintBreakpointUVE& bp : m_breakpoints) {
        if (bp.enabled && bp.sourceLine > 0U) {
            lines.push_back(bp.sourceLine);
        }
    }
    return lines;
}

const BlueprintBreakpointUVE* BlueprintBreakpointManagerUVE::FindByLineUVE(
    const std::uint32_t sourceLine) const noexcept {
    for (const BlueprintBreakpointUVE& bp : m_breakpoints) {
        if (bp.sourceLine == sourceLine) {
            return &bp;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// BlueprintExecutionTraceUVE
// ---------------------------------------------------------------------------

BlueprintExecutionTraceUVE::BlueprintExecutionTraceUVE(const std::size_t maxEntries) noexcept
    : m_maxEntries(maxEntries == 0U ? 1U : maxEntries) {}

void BlueprintExecutionTraceUVE::RecordUVE(const BlueprintTraceEventKindUVE kind, const std::uint32_t sourceLine,
                                           const std::uint32_t nodeId, std::string functionName, std::string detail) {
    if (m_entries.size() >= m_maxEntries) {
        m_entries.erase(m_entries.begin());
    }
    m_entries.push_back(BlueprintTraceEntryUVE{m_nextSequence++, NowMicrosUVE(), kind, sourceLine, nodeId,
                                               std::move(functionName), std::move(detail)});
}

void BlueprintExecutionTraceUVE::ClearUVE() noexcept { m_entries.clear(); }

std::vector<BlueprintTraceEntryUVE> BlueprintExecutionTraceUVE::FilterByNodeUVE(const std::uint32_t nodeId) const {
    std::vector<BlueprintTraceEntryUVE> out;
    for (const BlueprintTraceEntryUVE& e : m_entries) {
        if (e.nodeId == nodeId) {
            out.push_back(e);
        }
    }
    return out;
}

std::string BlueprintExecutionTraceUVE::FormatSummaryUVE() const {
    std::ostringstream oss;
    for (const BlueprintTraceEntryUVE& e : m_entries) {
        oss << "#" << e.sequence << " [" << TraceKindNameUVE(e.kind) << "] " << e.functionName << " line="
            << e.sourceLine << " node=" << e.nodeId;
        if (!e.detail.empty()) {
            oss << " (" << e.detail << ")";
        }
        oss << "\n";
    }
    return oss.str();
}

// ---------------------------------------------------------------------------
// BlueprintDebugSessionUVE
// ---------------------------------------------------------------------------

bool BlueprintDebugSessionUVE::AttachUVE(ScriptInstanceUVE& instance,
                                         const Asset::BlueprintTranslationUVE* const translation) {
    if (!instance.EnableDebuggerUVE()) {
        m_diagnostics.push_back(BlueprintDebugDiagnosticUVE{
            0U, 0U, "Could not enable debugger (instance may be running linked native code)."});
        return false;
    }
    m_instance = &instance;
    m_translation = translation;
    m_callStack.clear();
    return SyncBreakpointsUVE();
}

void BlueprintDebugSessionUVE::DetachUVE() noexcept {
    if (m_instance != nullptr) {
        m_instance->DisableDebuggerUVE();
        m_instance = nullptr;
    }
    m_translation = nullptr;
    m_callStack.clear();
}

bool BlueprintDebugSessionUVE::SyncBreakpointsUVE() {
    if (m_instance == nullptr) {
        return false;
    }
    const std::vector<std::uint32_t> lines = m_breakpoints.ActiveSourceLinesUVE();
    return m_instance->SetDebuggerBreakpointsUVE(lines);
}

void BlueprintDebugSessionUVE::AddWatchUVE(std::string expression) {
    if (expression.empty()) {
        return;
    }
    if (std::find(m_watchExpressions.begin(), m_watchExpressions.end(), expression) == m_watchExpressions.end()) {
        m_watchExpressions.push_back(std::move(expression));
    }
}

bool BlueprintDebugSessionUVE::RemoveWatchUVE(const std::string_view expression) {
    const auto it = std::find(m_watchExpressions.begin(), m_watchExpressions.end(), expression);
    if (it == m_watchExpressions.end()) {
        return false;
    }
    m_watchExpressions.erase(it);
    return true;
}

std::uint32_t BlueprintDebugSessionUVE::NodeForLineUVE(const std::uint32_t line) const noexcept {
    return m_translation != nullptr ? m_translation->NodeAtLineUVE(line) : 0U;
}

void BlueprintDebugSessionUVE::RefreshCallStackFromSnapshotUVE(const ScriptDebugSnapshotUVE& snapshot) {
    const std::uint32_t nodeId = NodeForLineUVE(snapshot.line);
    const BlueprintCallFrameUVE currentFrame{snapshot.callDepth, snapshot.functionName, snapshot.line, nodeId,
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

std::optional<BlueprintLiveValuesSnapshotUVE> BlueprintDebugSessionUVE::CaptureLiveValuesUVE() {
    if (m_instance == nullptr) {
        return std::nullopt;
    }
    const std::optional<ScriptDebugSnapshotUVE> raw = m_instance->GetDebugSnapshotUVE();
    if (!raw.has_value()) {
        return std::nullopt;
    }

    const std::uint32_t nodeId = NodeForLineUVE(raw->line);
    m_breakpoints.RecordHitAtLineUVE(raw->line);
    RefreshCallStackFromSnapshotUVE(*raw);

    BlueprintLiveValuesSnapshotUVE out;
    out.sourceLine = raw->line;
    out.nodeId = nodeId;
    out.functionName = raw->functionName;
    out.instructionIndex = raw->instructionIndex;
    out.callDepth = raw->callDepth;

    out.locals.reserve(raw->locals.size());
    for (std::size_t i = 0U; i < raw->locals.size(); ++i) {
        out.locals.push_back(
            BlueprintLiveValueEntryUVE{"local[" + std::to_string(i) + "]", FormatValueUVE(raw->locals[i]), false});
    }

    out.fields.reserve(raw->fields.size());
    for (const auto& field : raw->fields) {
        out.fields.push_back(BlueprintLiveValueEntryUVE{field.first, FormatValueUVE(field.second), true});
    }

    out.watches.reserve(m_watchExpressions.size());
    for (const std::string& expr : m_watchExpressions) {
        out.watches.push_back(EvaluateWatchUVE(expr, *raw));
    }

    return out;
}

std::vector<BlueprintCallFrameUVE> BlueprintDebugSessionUVE::GetCallStackUVE() const { return m_callStack; }

void BlueprintDebugSessionUVE::RecordResumeResultUVE(const BlueprintTraceEventKindUVE actionKind,
                                                     const ScriptDebugResumeResultUVE& result) {
    if (m_instance == nullptr) {
        return;
    }
    if (result.state == ScriptDebugStateUVE::Paused) {
        if (const auto snap = m_instance->GetDebugSnapshotUVE(); snap.has_value()) {
            const std::uint32_t nodeId = NodeForLineUVE(snap->line);
            RefreshCallStackFromSnapshotUVE(*snap);
            m_trace.RecordUVE(actionKind, snap->line, nodeId, snap->functionName, "paused");
        }
    } else if (result.state == ScriptDebugStateUVE::Completed) {
        m_trace.RecordUVE(BlueprintTraceEventKindUVE::Completed, 0U, 0U,
                          m_callStack.empty() ? std::string{} : m_callStack.front().functionName,
                          result.value.has_value() ? FormatValueUVE(*result.value) : std::string{});
        m_callStack.clear();
    } else if (result.state == ScriptDebugStateUVE::Waiting) {
        m_trace.RecordUVE(BlueprintTraceEventKindUVE::Waiting, 0U, 0U,
                          m_callStack.empty() ? std::string{} : m_callStack.back().functionName, "yield");
    } else if (result.state == ScriptDebugStateUVE::Failed) {
        const std::string err = m_instance->GetLastErrorUVE();
        m_diagnostics.push_back(BlueprintDebugDiagnosticUVE{0U, 0U, err});
        m_trace.RecordUVE(BlueprintTraceEventKindUVE::Failed, 0U, 0U, {}, err);
        m_callStack.clear();
    }
}

ScriptDebugResumeResultUVE BlueprintDebugSessionUVE::ContinueUVE() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResultUVE result = m_instance->ContinueDebugUVE();
    RecordResumeResultUVE(BlueprintTraceEventKindUVE::Continued, result);
    return result;
}

ScriptDebugResumeResultUVE BlueprintDebugSessionUVE::StepIntoUVE() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResultUVE result = m_instance->StepIntoDebugUVE();
    RecordResumeResultUVE(BlueprintTraceEventKindUVE::StepInto, result);
    return result;
}

ScriptDebugResumeResultUVE BlueprintDebugSessionUVE::StepOverUVE() {
    if (m_instance == nullptr) {
        return {};
    }
    const ScriptDebugResumeResultUVE result = m_instance->StepOverDebugUVE();
    RecordResumeResultUVE(BlueprintTraceEventKindUVE::StepOver, result);
    return result;
}

} // namespace UVE::Blueprint::Debug
