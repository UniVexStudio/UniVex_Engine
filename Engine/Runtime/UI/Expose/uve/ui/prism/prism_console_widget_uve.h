// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "uve/ui/prism/prism_button_uve.h"
#include "uve/ui/prism/prism_scroll_container_uve.h"
#include "uve/ui/prism/prism_text_input_uve.h"
#include "uve/ui/prism/prism_theme_uve.h"
#include "uve/ui/prism/prism_widget_uve.h"

namespace UVE::UI::Prism {

enum class PrismLogSeverityUVE : std::uint8_t {
    Info = 0,
    Warning,
    Error,
};

enum class PrismLogFilterUVE : std::uint8_t {
    All = 0,
    Info,
    Warning,
    Error,
};

struct PrismLogEntryUVE final {
    PrismLogSeverityUVE severity = PrismLogSeverityUVE::Info;
    std::string         message;
};

/// High-performance modern Developer Console Panel.
/// Features a filter toolbar, smooth scrolling log viewer with color-coded severity,
/// command history navigation, and an interactive command prompt.
class PrismConsoleWidgetUVE : public PrismWidgetUVE {
public:
    explicit PrismConsoleWidgetUVE(std::string debugName = "DeveloperConsole");

    void AddLog(PrismLogSeverityUVE severity, std::string message);
    void ClearLogs() noexcept;

    [[nodiscard]] const std::vector<PrismLogEntryUVE>& GetLogs() const noexcept { return m_logs; }
    [[nodiscard]] std::size_t GetLogCount() const noexcept { return m_logs.size(); }

    void SetSeverityFilter(PrismLogFilterUVE filter) noexcept;
    [[nodiscard]] PrismLogFilterUVE GetSeverityFilter() const noexcept { return m_filter; }

    void SetAutoScroll(bool autoScroll) noexcept { m_autoScroll = autoScroll; }
    [[nodiscard]] bool IsAutoScroll() const noexcept { return m_autoScroll; }

    void SetOnCommandSubmitted(std::function<void(const std::string&)> callback) {
        m_onCommandSubmitted = std::move(callback);
    }

    void SubmitCommand(const std::string& commandLine);

    [[nodiscard]] const std::vector<std::string>& GetHistory() const noexcept { return m_history; }

    // --- State overrides ---
    void ArrangeUVE(const Math::RectUVE& finalRect) override;
    PrismEventResultUVE OnKeyDown(const PrismKeyEventUVE& event) override;

protected:
    Math::Vector2UVE OnMeasureContentUVE(Math::Vector2UVE availableSize) override;
    void OnPaintContentUVE(PrismDrawListUVE& drawList) override;

private:
    void RebuildLogContent();
    void NavigateHistory(int delta);

    std::vector<PrismLogEntryUVE>                  m_logs;
    std::vector<std::string>                       m_history;
    int                                            m_historyIndex = -1;
    PrismLogFilterUVE                              m_filter = PrismLogFilterUVE::All;
    bool                                           m_autoScroll = true;

    // Sub-components
    std::shared_ptr<PrismButtonUVE>                m_clearButton;
    std::shared_ptr<PrismScrollContainerUVE>       m_scrollContainer;
    std::shared_ptr<PrismTextInputUVE>             m_commandInput;

    std::function<void(const std::string&)>        m_onCommandSubmitted;
};

} // namespace UVE::UI::Prism
