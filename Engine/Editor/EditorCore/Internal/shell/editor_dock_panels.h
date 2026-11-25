// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"
#include "editor_render_stats.h"
#include "profiler_view.h"

#include "prism/Widget.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace prism {
class Button;
class Profiler;
class Label;
class LogView;
class TableView;
class TextInput;
} // namespace prism

namespace UVE::Editor {

/// The dock's Output Log, Console and Profiler pages drawn with PRISM widgets, over the engine's own
/// data:
///
///   * Output Log: every message the engine logs (Debug::ILogger), received by a log sink this
///     class installs, with level toggles, a text filter, copy and clear;
///   * Console: the editor's DeveloperConsole (its output, command line, history and completion);
///   * Profiler: the editor's ProfilerCapture sessions (spans, counters, breadcrumbs, runtime
///     snapshot) and the renderer's last-frame diagnostics (BuildEditorRenderStatRows).
class EditorDockPanels final {
public:
    explicit EditorDockPanels(Editor& editor);
    ~EditorDockPanels();
    EditorDockPanels(const EditorDockPanels&) = delete;
    EditorDockPanels& operator=(const EditorDockPanels&) = delete;

    /// Installs the log sink (once); messages logged from then on reach the Output Log.
    void AttachLog();

    [[nodiscard]] prism::Ref<prism::Widget> BuildOutputLog();
    [[nodiscard]] prism::Ref<prism::Widget> BuildConsole();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProfiler();
    [[nodiscard]] prism::Ref<prism::Widget> BuildSearch();

    /// Pulls new log lines (always, so nothing is missed while the page is hidden), and refreshes
    /// the console and profiler while their page is the one showing. `gpuMs`: the GPU time of the
    /// latest frame measured (0 when unknown), for the Timeline.
    void Refresh(bool consoleShown, bool profilerShown, float gpuMs = 0.0F);

    /// A message as the sink received it (any thread), waiting for the next refresh.
    struct LogLine final {
        Debug::LogLevel level = Debug::LogLevel::Info;
        std::string category;
        std::string text;
    };
    /// Shared with the sink, which the logger owns and may outlive this panel.
    struct LogBuffer final {
        std::mutex mutex;
        std::deque<LogLine> pending;
        std::uint64_t dropped = 0U;
    };

private:
    void PullLog();
    void RefreshConsole();
    void RefreshProfiler();
    void UpdateLogCounts();
    void ExecuteConsole();
    void ShowConsoleHistory(int delta);
    void CompleteConsole();

    Editor& m_editor;
    std::shared_ptr<LogBuffer> m_logBuffer;
    bool m_logAttached = false;

    // Output Log.
    std::shared_ptr<prism::LogView> m_log;
    std::shared_ptr<prism::Label> m_logCounts;
    std::shared_ptr<prism::Button> m_logInfo;
    std::shared_ptr<prism::Button> m_logWarnings;
    std::shared_ptr<prism::Button> m_logErrors;
    std::uint64_t m_logDroppedShown = 0U;

    // Console.
    std::shared_ptr<prism::LogView> m_consoleOutput;
    std::shared_ptr<prism::TextInput> m_consoleInput;
    std::shared_ptr<prism::Label> m_consoleHint;
    std::uint64_t m_consoleGeneration = ~std::uint64_t{0};

    // Profiler.
    std::shared_ptr<prism::Button> m_captureButton;
    std::shared_ptr<prism::Label> m_profilerStatus;
    std::shared_ptr<prism::Label> m_profilerSummary;
    std::shared_ptr<prism::Label> m_profilerRuntime;
    std::shared_ptr<prism::TableView> m_frameTable;
    std::shared_ptr<prism::TableView> m_spanTable;
    std::shared_ptr<prism::TableView> m_counterTable;
    std::shared_ptr<prism::LogView> m_breadcrumbs;
    // The Timeline: PRISM's frame profiler, fed every frame with the engine's numbers.
    std::shared_ptr<prism::Profiler> m_timeline;
    std::chrono::steady_clock::time_point m_lastFrame{};
    void FeedTimeline(bool shown, float gpuMs);
    std::vector<EditorRenderStatRow> m_frameRows;
    std::vector<Core::ProfilerSpanRow> m_spanRows;
    std::vector<Core::ProfilerCounterRow> m_counterRows;
    std::size_t m_breadcrumbsShown = 0U;
    std::string m_breadcrumbSession;
    std::chrono::steady_clock::time_point m_lastProfilerRefresh{};
};

} // namespace UVE::Editor
