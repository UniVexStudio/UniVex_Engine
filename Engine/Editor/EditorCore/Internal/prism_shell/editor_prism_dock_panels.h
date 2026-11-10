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
class Label;
class LogView;
class TableView;
class TextInput;
} // namespace prism

namespace UVE::Editor {

/// The dock's Output Log, Console and Profiler pages drawn with PRISM widgets, over the engine's own
/// data:
///
///   * Output Log: every message the engine logs (Debug::ILoggerUVE), received by a log sink this
///     class installs, with level toggles, a text filter, copy and clear;
///   * Console: the editor's DeveloperConsoleUVE (its output, command line, history and completion);
///   * Profiler: the editor's ProfilerCaptureUVE sessions (spans, counters, breadcrumbs, runtime
///     snapshot) and the renderer's last-frame diagnostics (BuildEditorRenderStatRowsUVE).
class EditorPrismDockPanelsUVE final {
public:
    explicit EditorPrismDockPanelsUVE(EditorUVE& editor);
    ~EditorPrismDockPanelsUVE();
    EditorPrismDockPanelsUVE(const EditorPrismDockPanelsUVE&) = delete;
    EditorPrismDockPanelsUVE& operator=(const EditorPrismDockPanelsUVE&) = delete;

    /// Installs the log sink (once); messages logged from then on reach the Output Log.
    void AttachLogUVE();

    [[nodiscard]] prism::Ref<prism::Widget> BuildOutputLogUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildConsoleUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildProfilerUVE();

    /// Pulls new log lines (always, so nothing is missed while the page is hidden), and refreshes
    /// the console and profiler while their page is the one showing.
    void RefreshUVE(bool consoleShown, bool profilerShown);

    /// A message as the sink received it (any thread), waiting for the next refresh.
    struct LogLineUVE final {
        Debug::LogLevelUVE level = Debug::LogLevelUVE::Info;
        std::string category;
        std::string text;
    };
    /// Shared with the sink, which the logger owns and may outlive this panel.
    struct LogBufferUVE final {
        std::mutex mutex;
        std::deque<LogLineUVE> pending;
        std::uint64_t dropped = 0U;
    };

private:
    void PullLogUVE();
    void RefreshConsoleUVE();
    void RefreshProfilerUVE();
    void UpdateLogCountsUVE();
    void ExecuteConsoleUVE();
    void ShowConsoleHistoryUVE(int delta);
    void CompleteConsoleUVE();

    EditorUVE& m_editor;
    std::shared_ptr<LogBufferUVE> m_logBuffer;
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
    std::vector<EditorRenderStatRowUVE> m_frameRows;
    std::vector<Core::ProfilerSpanRowUVE> m_spanRows;
    std::vector<Core::ProfilerCounterRowUVE> m_counterRows;
    std::size_t m_breadcrumbsShown = 0U;
    std::string m_breadcrumbSession;
    std::chrono::steady_clock::time_point m_lastProfilerRefresh{};
};

} // namespace UVE::Editor
