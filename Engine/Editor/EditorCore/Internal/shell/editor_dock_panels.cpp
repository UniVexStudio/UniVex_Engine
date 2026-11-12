// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_dock_panels.h"

#include "developer_console.h"
#include "log_sink.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Profiler.h"
#include "prism/TableView.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace UVE::Editor {
namespace {

/// Lines waiting between refreshes beyond this are dropped (and counted), so a log storm while
/// the editor is busy cannot grow without bound.
constexpr std::size_t kMaximumPendingLogLinesUVE = 4096U;
/// Profiler tables refresh at this pace while shown, not every frame.
constexpr std::chrono::milliseconds kProfilerRefreshUVE{250};

/// Hands every engine log message to the Output Log's buffer. The logger owns the sink, so the
/// buffer is shared: the panel may go away first and the sink just keeps filling a buffer nobody
/// reads, bounded by kMaximumPendingLogLinesUVE.
class OutputLogSinkUVE final : public Debug::ILogSinkUVE {
public:
    explicit OutputLogSinkUVE(std::shared_ptr<EditorDockPanelsUVE::LogBufferUVE> buffer)
        : m_buffer(std::move(buffer)) {}
    void Write(const Debug::LogMessageUVE& message) override {
        EditorDockPanelsUVE::LogLineUVE line;
        line.level = message.level;
        line.category = message.category;
        line.text = message.message;
        if (message.sourceFile != nullptr && message.sourceLine > 0) {
            // "File.cpp:42": the Output Log underlines it as the message's source.
            std::string file = message.sourceFile;
            const std::size_t slash = file.find_last_of("/\\");
            if (slash != std::string::npos) {
                file = file.substr(slash + 1U);
            }
            line.text += "  (" + file + ":" + std::to_string(message.sourceLine) + ")";
        }
        const std::lock_guard<std::mutex> lock(m_buffer->mutex);
        if (m_buffer->pending.size() >= kMaximumPendingLogLinesUVE) {
            m_buffer->pending.pop_front();
            ++m_buffer->dropped;
        }
        m_buffer->pending.push_back(std::move(line));
    }
    void Flush() override {}

private:
    std::shared_ptr<EditorDockPanelsUVE::LogBufferUVE> m_buffer;
};

[[nodiscard]] prism::LogLevel ToPrismLevelUVE(const Debug::LogLevelUVE level) {
    switch (level) {
        case Debug::LogLevelUVE::Warning: return prism::LogLevel::Warning;
        case Debug::LogLevelUVE::Error:
        case Debug::LogLevelUVE::Fatal: return prism::LogLevel::Error;
        default: return prism::LogLevel::Info;
    }
}

[[nodiscard]] prism::Ref<prism::Button> BarButtonUVE(const prism::Icon icon, std::string tooltip, std::string text = {}) {
    auto button = prism::make<prism::Button>(std::move(text), icon, prism::ButtonStyle::Toolbar);
    button->setTooltip(std::move(tooltip));
    return button;
}

[[nodiscard]] prism::Ref<prism::Widget> BarUVE(prism::Ref<prism::Widget> row) {
    auto border = prism::make<prism::Border>();
    border->setBackground(prism::ThemeColor(&prism::Theme::header));
    border->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    border->setChild(std::move(row));
    return border;
}

[[nodiscard]] prism::Ref<prism::Label> DimLabelUVE(std::string text = {}) {
    auto label = prism::make<prism::Label>(std::move(text), prism::FontRole::Small);
    label->setColor(prism::ThemeColor(&prism::Theme::textDim));
    return label;
}

constexpr prism::Slot kCentreSlotUVE{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};

/// The console's command line: Enter runs it, Up / Down walk the history, Tab completes.
class ConsoleInputUVE final : public prism::TextInput {
public:
    ConsoleInputUVE() : prism::TextInput("", "command  (Enter: run, Up/Down: history, Tab: complete)") {}
    std::function<void(int)> onHistory;
    std::function<void()> onComplete;
    bool onKeyDown(const prism::KeyEvent& e) override {
        if (e.key == prism::Key::Up && onHistory) {
            onHistory(-1);
            return true;
        }
        if (e.key == prism::Key::Down && onHistory) {
            onHistory(1);
            return true;
        }
        if (e.key == prism::Key::Tab && onComplete) {
            onComplete();
            return true;
        }
        return prism::TextInput::onKeyDown(e);
    }
};

} // namespace

EditorDockPanelsUVE::EditorDockPanelsUVE(EditorUVE& editor)
    : m_editor(editor), m_logBuffer(std::make_shared<LogBufferUVE>()) {}

EditorDockPanelsUVE::~EditorDockPanelsUVE() = default;

void EditorDockPanelsUVE::AttachLogUVE() {
    if (m_logAttached || m_editor.m_services == nullptr) {
        return;
    }
    m_editor.m_services->GetLoggerUVE().AddSink(std::make_unique<OutputLogSinkUVE>(m_logBuffer));
    m_logAttached = true;
}

// ------------------------------------------------------------------------------------ output log

prism::Ref<prism::Widget> EditorDockPanelsUVE::BuildOutputLogUVE() {
    m_log = prism::make<prism::LogView>();
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 2.0F, 6.0F, 2.0F));
    const auto levelToggle = [this](const prism::Icon icon, std::string tip, const prism::LogLevel level) {
        auto button = BarButtonUVE(icon, std::move(tip));
        button->setCheckable(true);
        button->setChecked(true);
        button->onToggle = [this, level](const bool on) {
            if (m_log) {
                m_log->setLevelEnabled(level, on);
            }
        };
        return button;
    };
    m_logInfo = levelToggle(prism::Icon::Info, "Show messages", prism::LogLevel::Info);
    m_logWarnings = levelToggle(prism::Icon::Warning, "Show warnings", prism::LogLevel::Warning);
    m_logErrors = levelToggle(prism::Icon::Error, "Show errors", prism::LogLevel::Error);
    row->add(m_logInfo, kCentreSlotUVE);
    row->add(m_logWarnings, kCentreSlotUVE);
    row->add(m_logErrors, kCentreSlotUVE);
    auto search = prism::make<prism::TextInput>("", "Search the log");
    search->setLeadingIcon(prism::Icon::Search);
    search->setClearable(true);
    search->onChanged = [this](const std::string& text) {
        if (m_log) {
            m_log->setTextFilter(text);
        }
    };
    row->add(search, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    m_logCounts = DimLabelUVE("0 messages");
    row->add(m_logCounts, kCentreSlotUVE);
    auto copy = BarButtonUVE(prism::Icon::Copy, "Copy the shown lines");
    copy->onClick = [this] {
        if (m_log && prism::Application::exists()) {
            prism::Application::get().platform().setClipboard(m_log->visibleText());
        }
    };
    auto clear = BarButtonUVE(prism::Icon::Trash, "Clear the log");
    clear->onClick = [this] {
        if (m_log) {
            m_log->clear();
            UpdateLogCountsUVE();
        }
    };
    row->add(copy, kCentreSlotUVE);
    row->add(clear, kCentreSlotUVE);

    m_log->onCountsChanged = [this] { UpdateLogCountsUVE(); };
    auto page = prism::vbox();
    page->add(BarUVE(row));
    page->add(m_log, prism::Slot{1.0F});
    PullLogUVE();
    return page;
}

void EditorDockPanelsUVE::UpdateLogCountsUVE() {
    if (!m_log || !m_logCounts) {
        return;
    }
    std::string text = std::to_string(m_log->total()) + " messages";
    const int warnings = m_log->count(prism::LogLevel::Warning);
    const int errors = m_log->count(prism::LogLevel::Error);
    if (warnings > 0) {
        text += ", " + std::to_string(warnings) + " warnings";
    }
    if (errors > 0) {
        text += ", " + std::to_string(errors) + " errors";
    }
    if (m_logDroppedShown > 0U) {
        text += " (" + std::to_string(m_logDroppedShown) + " dropped)";
    }
    m_logCounts->setText(text);
}

void EditorDockPanelsUVE::PullLogUVE() {
    std::deque<LogLineUVE> lines;
    std::uint64_t dropped = 0U;
    {
        const std::lock_guard<std::mutex> lock(m_logBuffer->mutex);
        lines.swap(m_logBuffer->pending);
        dropped = m_logBuffer->dropped;
    }
    if (!m_log) {
        // Not built yet: keep them for when it is.
        if (!lines.empty()) {
            const std::lock_guard<std::mutex> lock(m_logBuffer->mutex);
            for (auto it = lines.rbegin(); it != lines.rend(); ++it) {
                m_logBuffer->pending.push_front(std::move(*it));
            }
        }
        return;
    }
    if (lines.empty() && dropped == m_logDroppedShown) {
        return;
    }
    const bool follow = m_log->atBottom();
    for (LogLineUVE& line : lines) {
        m_log->add(ToPrismLevelUVE(line.level), std::move(line.text), std::move(line.category));
    }
    m_logDroppedShown = dropped;
    if (follow) {
        m_log->scrollToEnd();
    }
    UpdateLogCountsUVE();
}

// --------------------------------------------------------------------------------------- console

prism::Ref<prism::Widget> EditorDockPanelsUVE::BuildConsoleUVE() {
    m_consoleOutput = prism::make<prism::LogView>();
    auto input = prism::make<ConsoleInputUVE>();
    input->setLeadingIcon(prism::Icon::Terminal);
    input->onCommit = [this](const std::string&) { ExecuteConsoleUVE(); };
    input->onHistory = [this](const int delta) { ShowConsoleHistoryUVE(delta); };
    input->onComplete = [this] { CompleteConsoleUVE(); };
    m_consoleInput = input;
    m_consoleHint = DimLabelUVE();

    auto clear = BarButtonUVE(prism::Icon::Trash, "Clear the console");
    clear->onClick = [this] {
        static_cast<void>(m_editor.m_developerConsole.ClearUVE());
        RefreshConsoleUVE();
    };
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    row->add(m_consoleInput, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    row->add(clear, kCentreSlotUVE);

    auto page = prism::vbox();
    page->add(m_consoleOutput, prism::Slot{1.0F});
    auto hint = prism::hbox();
    hint->setPadding(prism::Margin(8.0F, 0.0F, 8.0F, 0.0F));
    hint->add(m_consoleHint, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    page->add(hint);
    page->add(BarUVE(row));
    RefreshConsoleUVE();
    return page;
}

void EditorDockPanelsUVE::RefreshConsoleUVE() {
    if (!m_consoleOutput) {
        return;
    }
    const DeveloperConsoleSnapshotUVE snapshot = m_editor.m_developerConsole.GetSnapshotUVE();
    if (snapshot.generation == m_consoleGeneration) {
        return;
    }
    m_consoleGeneration = snapshot.generation;
    m_consoleOutput->clear();
    for (const DeveloperConsoleEntryUVE& entry : snapshot.output) {
        prism::LogLevel level = prism::LogLevel::Info;
        if (entry.severity == DeveloperConsoleSeverityUVE::Warning) {
            level = prism::LogLevel::Warning;
        } else if (entry.severity == DeveloperConsoleSeverityUVE::Error) {
            level = prism::LogLevel::Error;
        } else if (!entry.text.empty() && entry.text.front() == '>') {
            level = prism::LogLevel::Command;
        }
        m_consoleOutput->add(level, entry.text, "Console");
    }
    if (snapshot.output.empty()) {
        m_consoleOutput->add(prism::LogLevel::Info, "Nothing yet. Type help and press Enter.", "Console");
    }
    if (!snapshot.available) {
        m_consoleHint->setText("The console is not available in this build.");
    }
    m_consoleOutput->scrollToEnd();
}

void EditorDockPanelsUVE::ExecuteConsoleUVE() {
    if (!m_consoleInput) {
        return;
    }
    const std::string command = m_consoleInput->text();
    if (command.empty()) {
        return;
    }
    static_cast<void>(m_editor.m_developerConsole.ExecuteUVE(command));
    m_consoleInput->setText("");
    m_consoleHint->setText("");
    RefreshConsoleUVE();
}

void EditorDockPanelsUVE::ShowConsoleHistoryUVE(const int delta) {
    if (!m_editor.m_developerConsole.MoveHistoryUVE(delta)) {
        return;
    }
    const DeveloperConsoleSnapshotUVE snapshot = m_editor.m_developerConsole.GetSnapshotUVE();
    m_consoleInput->setText(snapshot.historyEntry);
}

void EditorDockPanelsUVE::CompleteConsoleUVE() {
    const std::string typed = m_consoleInput->text();
    if (typed.find(' ') != std::string::npos ||
        !m_editor.m_developerConsole.SetCompletionPrefixUVE(typed)) {
        return;
    }
    const DeveloperConsoleSnapshotUVE snapshot = m_editor.m_developerConsole.GetSnapshotUVE();
    if (snapshot.completions.size() == 1U) {
        m_consoleInput->setText(snapshot.completions.front().identifier + " ");
        m_consoleHint->setText(snapshot.completions.front().help);
        return;
    }
    std::string hint;
    for (const DeveloperConsoleCompletionUVE& completion : snapshot.completions) {
        hint += (hint.empty() ? "" : "   ") + completion.identifier;
        if (hint.size() > 240U) {
            hint += "   ...";
            break;
        }
    }
    m_consoleHint->setText(hint.empty() ? "No match." : hint);
}

// -------------------------------------------------------------------------------------- profiler

prism::Ref<prism::Widget> EditorDockPanelsUVE::BuildProfilerUVE() {
    m_captureButton = BarButtonUVE(prism::Icon::Play, "Begin or end a capture of spans, counters and breadcrumbs",
                                   "Begin capture");
    m_captureButton->onClick = [this] {
        if (m_editor.m_profilerCapture.IsActiveUVE()) {
            m_editor.EndProfilerCaptureUVE();
        } else {
            m_editor.BeginProfilerCaptureUVE();
        }
        m_lastProfilerRefresh = {};
        RefreshProfilerUVE();
    };
    m_profilerStatus = DimLabelUVE();
    m_profilerSummary = DimLabelUVE();
    auto row = prism::hbox(6.0F);
    row->setPadding(prism::Margin(6.0F, 2.0F, 6.0F, 2.0F));
    row->add(m_captureButton, kCentreSlotUVE);
    row->add(m_profilerStatus, kCentreSlotUVE);
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    row->add(m_profilerSummary, kCentreSlotUVE);

    const auto table = [](std::initializer_list<std::pair<const char*, float>> columns) {
        auto t = prism::make<prism::TableView>();
        for (const auto& [title, width] : columns) {
            prism::TableColumn column;
            column.id = title;
            column.title = title;
            column.width = width;
            t->addColumn(column);
        }
        return t;
    };
    // Frame: the renderer's last-frame diagnostics, by section; concerning rows in amber.
    m_frameTable = table({{"Section", 140.0F}, {"Statistic", 260.0F}, {"Value", 220.0F}});
    m_frameTable->cellText = [this](const int r, const int c) -> std::string {
        if (r < 0 || static_cast<std::size_t>(r) >= m_frameRows.size()) {
            return {};
        }
        const EditorRenderStatRowUVE& row = m_frameRows[static_cast<std::size_t>(r)];
        return c == 0 ? row.section : c == 1 ? row.label : row.value;
    };
    m_frameTable->paintCell = [this](prism::Canvas& canvas, const int r, const int c, const prism::Rect rect, bool) {
        if (r < 0 || static_cast<std::size_t>(r) >= m_frameRows.size()) {
            return;
        }
        const EditorRenderStatRowUVE& row = m_frameRows[static_cast<std::size_t>(r)];
        const prism::Theme& t = prism::theme();
        const std::string& text = c == 0 ? row.section : c == 1 ? row.label : row.value;
        const prism::Color colour = row.isConcerning && c != 0 ? prism::Color(242, 178, 64)
                                    : c == 0                   ? t.textDim
                                                               : t.text;
        canvas.textIn(*t.ui, prism::Rect{rect.x + 6.0F, rect.y, rect.w - 8.0F, rect.h}, text, colour);
    };
    m_spanTable = table({{"Span", 260.0F}, {"Category", 110.0F}, {"Begin (ns)", 150.0F}, {"Duration", 120.0F}});
    m_spanTable->cellText = [this](const int r, const int c) -> std::string {
        if (r < 0 || static_cast<std::size_t>(r) >= m_spanRows.size()) {
            return {};
        }
        const Core::ProfilerSpanRowUVE& row = m_spanRows[static_cast<std::size_t>(r)];
        switch (c) {
            case 0: return row.name;
            case 1: return std::string(Core::ProfilerCategoryNameUVE(row.category));
            case 2: return std::to_string(row.beginNanoseconds);
            default: return Core::FormatProfilerDurationUVE(row.durationNanoseconds);
        }
    };
    m_counterTable = table({{"Counter", 260.0F}, {"Category", 110.0F}, {"Value", 150.0F}});
    m_counterTable->cellText = [this](const int r, const int c) -> std::string {
        if (r < 0 || static_cast<std::size_t>(r) >= m_counterRows.size()) {
            return {};
        }
        const Core::ProfilerCounterRowUVE& row = m_counterRows[static_cast<std::size_t>(r)];
        switch (c) {
            case 0: return row.name;
            case 1: return std::string(Core::ProfilerCategoryNameUVE(row.category));
            default: return Core::FormatProfilerCounterUVE(row.value);
        }
    };
    m_breadcrumbs = prism::make<prism::LogView>();
    m_profilerRuntime = DimLabelUVE();

    m_timeline = prism::make<prism::Profiler>();
    m_timeline->setTargetMs(1000.0F / 60.0F);
    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Timeline", prism::Icon::ProfTimeline, m_timeline);
    tabs->addTab("Frame", prism::Icon::ProfFrame, m_frameTable);
    tabs->addTab("Spans", prism::Icon::ProfSpans, m_spanTable);
    tabs->addTab("Counters", prism::Icon::ProfCounters, m_counterTable);
    tabs->addTab("Breadcrumbs", prism::Icon::ProfBreadcrumbs, m_breadcrumbs);

    auto runtime = prism::hbox();
    runtime->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 3.0F));
    runtime->add(m_profilerRuntime, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});

    auto page = prism::vbox();
    page->add(BarUVE(row));
    page->add(tabs, prism::Slot{1.0F});
    page->add(runtime);
    RefreshProfilerUVE();
    return page;
}

void EditorDockPanelsUVE::RefreshProfilerUVE() {
    if (!m_frameTable) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    if (now - m_lastProfilerRefresh < kProfilerRefreshUVE) {
        return;
    }
    m_lastProfilerRefresh = now;

    m_frameRows = BuildEditorRenderStatRowsUVE(m_editor.m_services->GetRenderer3DUVE().GetLastFrameDiagnosticsUVE());
    m_frameTable->setRowCount(static_cast<int>(m_frameRows.size()));

    const bool active = m_editor.m_profilerCapture.IsActiveUVE();
    m_captureButton->setText(active ? "End capture" : "Begin capture");
    m_captureButton->setIcon(active ? prism::Icon::Stop : prism::Icon::Play);
    m_profilerStatus->setText(m_editor.ProfilerStatusUVE());

    // Live while capturing, frozen on the last completed bundle otherwise.
    const Core::DiagnosticCaptureBundleUVE live =
        active ? m_editor.m_profilerCapture.SnapshotUVE() : Core::DiagnosticCaptureBundleUVE{};
    const Core::DiagnosticCaptureBundleUVE& shown =
        active ? live : (m_editor.m_profilerHasBundle ? m_editor.m_profilerBundle : live);
    if (shown.sessionId.empty()) {
        m_spanRows.clear();
        m_counterRows.clear();
        m_profilerSummary->setText("No capture yet: begin one to profile this session.");
        m_profilerRuntime->setText("");
    } else {
        m_spanRows = Core::BuildProfilerSpanRowsUVE(shown.spans);
        m_counterRows = Core::BuildProfilerCounterRowsUVE(shown.counters);
        m_profilerSummary->setText(shown.sessionId + (active ? " (live): " : " (ended): ") +
                                   std::to_string(shown.spans.size()) + " spans, " +
                                   std::to_string(shown.counters.size()) + " counters, " +
                                   std::to_string(shown.breadcrumbs.size()) + " breadcrumbs (" +
                                   std::to_string(shown.droppedSpanCount) + "/" +
                                   std::to_string(shown.droppedCounterCount) + "/" +
                                   std::to_string(shown.droppedBreadcrumbCount) + " dropped)");
        m_profilerRuntime->setText(FormatProfilerRuntimeLineUVE(shown.runtimeSnapshot));
    }
    m_spanTable->setRowCount(static_cast<int>(m_spanRows.size()));
    m_counterTable->setRowCount(static_cast<int>(m_counterRows.size()));

    if (shown.sessionId != m_breadcrumbSession || shown.breadcrumbs.size() < m_breadcrumbsShown) {
        m_breadcrumbs->clear();
        m_breadcrumbsShown = 0U;
        m_breadcrumbSession = shown.sessionId;
    }
    for (std::size_t i = m_breadcrumbsShown; i < shown.breadcrumbs.size(); ++i) {
        const Core::DiagnosticBreadcrumbUVE& crumb = shown.breadcrumbs[i];
        m_breadcrumbs->add(prism::LogLevel::Info, "[" + std::to_string(crumb.sequence) + "] " + crumb.message,
                           std::string(Core::ProfilerCategoryNameUVE(crumb.category)));
    }
    m_breadcrumbsShown = shown.breadcrumbs.size();
}

void EditorDockPanelsUVE::FeedTimelineUVE() {
    // One frame for PRISM's profiler: the frame's time, the renderer's draw counts, and - while a
    // capture runs - the spans the engine recorded for its latest frame (CPU / Render / GPU tracks).
    if (!m_timeline) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    const bool first = m_lastFrame == std::chrono::steady_clock::time_point{};
    const float frameMs = first ? 0.0F : std::chrono::duration<float, std::milli>(now - m_lastFrame).count();
    m_lastFrame = now;
    if (first) {
        return;
    }
    const Render::Renderer3DFrameDiagnosticsUVE diagnostics =
        m_editor.m_services->GetRenderer3DUVE().GetLastFrameDiagnosticsUVE();
    prism::ProfileFrame frame;
    frame.cpuMs = frameMs;
    frame.drawCalls = static_cast<int>(diagnostics.meshDrawCallsRecorded + diagnostics.decalDrawCallsRecorded +
                                       diagnostics.primitiveItemsExtracted);
    frame.triangles = static_cast<int>(diagnostics.decalTrianglesExtracted);
    if (m_editor.m_profilerCapture.IsActiveUVE()) {
        const Core::DiagnosticCaptureBundleUVE live = m_editor.m_profilerCapture.SnapshotUVE();
        std::uint64_t latest = 0U;
        for (const Core::DiagnosticSpanUVE& span : live.spans) {
            latest = std::max(latest, span.frameNumber);
        }
        std::uint64_t start = ~std::uint64_t{0};
        for (const Core::DiagnosticSpanUVE& span : live.spans) {
            if (span.frameNumber == latest) {
                start = std::min(start, span.beginTimestampNanoseconds);
            }
        }
        for (const Core::DiagnosticSpanUVE& span : live.spans) {
            if (span.frameNumber != latest || span.endTimestampNanoseconds < span.beginTimestampNanoseconds) {
                continue;
            }
            prism::ProfileScope scope;
            scope.name = span.name;
            scope.track = span.category == Core::DiagnosticCategoryUVE::Render ? 1 : 0;
            scope.startMs = static_cast<float>(span.beginTimestampNanoseconds - start) / 1.0e6F;
            scope.durMs = static_cast<float>(span.endTimestampNanoseconds - span.beginTimestampNanoseconds) / 1.0e6F;
            frame.scopes.push_back(std::move(scope));
        }
    }
    m_timeline->pushFrame(std::move(frame));
}

void EditorDockPanelsUVE::RefreshUVE(const bool consoleShown, const bool profilerShown) {
    PullLogUVE();
    FeedTimelineUVE();
    if (consoleShown) {
        RefreshConsoleUVE();
    }
    if (profilerShown) {
        RefreshProfilerUVE();
    }
}

} // namespace UVE::Editor
