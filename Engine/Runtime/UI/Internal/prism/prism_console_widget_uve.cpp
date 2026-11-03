// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_console_widget_uve.h"
#include "uve/ui/prism/prism_text_uve.h"

#include <algorithm>

namespace UVE::UI::Prism {

namespace {
constexpr float kToolbarHeightUVE = 26.0F;
constexpr float kInputHeightUVE = 26.0F;
} // namespace

PrismConsoleWidgetUVE::PrismConsoleWidgetUVE(std::string debugName)
    : PrismWidgetUVE(std::move(debugName)) {
    m_isFocusable = true;
    m_style.padding = PrismEdgesUVE{4.0F, 4.0F};

    // 1. Toolbar Clear Button
    m_clearButton = std::make_shared<PrismButtonUVE>("Clear", [this]() {
        ClearLogs();
    });
    m_clearButton->GetStyle().width = PrismLengthUVE::Px(55.0F);
    m_clearButton->GetStyle().height = PrismLengthUVE::Px(22.0F);
    AddChild(m_clearButton);

    // 2. Scrollable Log Container
    m_scrollContainer = std::make_shared<PrismScrollContainerUVE>("ConsoleLogs");
    m_scrollContainer->GetStyle().gap = 3.0F;
    AddChild(m_scrollContainer);

    // 3. Command Prompt Input
    m_commandInput = std::make_shared<PrismTextInputUVE>("command  (Up/Down: history)");
    m_commandInput->SetOnCommit([this](const std::string& cmd) {
        SubmitCommand(cmd);
    });
    AddChild(m_commandInput);
}

void PrismConsoleWidgetUVE::AddLog(PrismLogSeverityUVE severity, std::string message) {
    m_logs.push_back(PrismLogEntryUVE{severity, std::move(message)});
    RebuildLogContent();
}

void PrismConsoleWidgetUVE::ClearLogs() noexcept {
    m_logs.clear();
    m_scrollContainer->ClearChildren();
}

void PrismConsoleWidgetUVE::SetSeverityFilter(PrismLogFilterUVE filter) noexcept {
    if (m_filter != filter) {
        m_filter = filter;
        RebuildLogContent();
    }
}

void PrismConsoleWidgetUVE::SubmitCommand(const std::string& commandLine) {
    if (commandLine.empty()) return;

    m_history.push_back(commandLine);
    if (m_history.size() > 128) {
        m_history.erase(m_history.begin());
    }
    m_historyIndex = -1;

    if (m_onCommandSubmitted) {
        m_onCommandSubmitted(commandLine);
    }

    m_commandInput->SetText("");
}

void PrismConsoleWidgetUVE::NavigateHistory(int delta) {
    if (m_history.empty()) return;

    if (delta < 0) { // Up arrow (older)
        if (m_historyIndex == -1) {
            m_historyIndex = static_cast<int>(m_history.size()) - 1;
        } else if (m_historyIndex > 0) {
            --m_historyIndex;
        }
    } else if (delta > 0) { // Down arrow (newer)
        if (m_historyIndex != -1) {
            ++m_historyIndex;
            if (m_historyIndex >= static_cast<int>(m_history.size())) {
                m_historyIndex = -1;
                m_commandInput->SetText("");
                return;
            }
        }
    }

    if (m_historyIndex >= 0 && m_historyIndex < static_cast<int>(m_history.size())) {
        m_commandInput->SetText(m_history[static_cast<std::size_t>(m_historyIndex)]);
    }
}

void PrismConsoleWidgetUVE::RebuildLogContent() {
    m_scrollContainer->ClearChildren();

    const auto& theme = PrismThemeUVE::Dark();

    for (const auto& log : m_logs) {
        // Filter check
        if (m_filter == PrismLogFilterUVE::Info && log.severity != PrismLogSeverityUVE::Info) continue;
        if (m_filter == PrismLogFilterUVE::Warning && log.severity != PrismLogSeverityUVE::Warning) continue;
        if (m_filter == PrismLogFilterUVE::Error && log.severity != PrismLogSeverityUVE::Error) continue;

        PrismColorUVE textColor = theme.textPrimary;
        std::string prefix;
        if (log.severity == PrismLogSeverityUVE::Warning) {
            textColor = PrismColorUVE{0.95F, 0.72F, 0.30F, 1.0F};
            prefix = "[WARN] ";
        } else if (log.severity == PrismLogSeverityUVE::Error) {
            textColor = PrismColorUVE{0.95F, 0.45F, 0.40F, 1.0F};
            prefix = "[ERROR] ";
        } else {
            textColor = theme.textSecondary;
            prefix = "[INFO] ";
        }

        auto lineWidget = std::make_shared<PrismTextUVE>(prefix + log.message, 12.0F, textColor);
        lineWidget->GetStyle().height = PrismLengthUVE::Px(16.0F);
        m_scrollContainer->AddChild(lineWidget);
    }

    if (m_autoScroll) {
        m_scrollContainer->SetScrollOffsetY(m_scrollContainer->GetMaxScrollOffsetY() + 500.0F);
    }
}

PrismEventResultUVE PrismConsoleWidgetUVE::OnKeyDown(const PrismKeyEventUVE& event) {
    if (event.keyCode == Input::KeyCodeUVE::Up) {
        NavigateHistory(-1);
        return PrismEventResultUVE::Handled;
    }
    if (event.keyCode == Input::KeyCodeUVE::Down) {
        NavigateHistory(1);
        return PrismEventResultUVE::Handled;
    }
    return PrismWidgetUVE::OnKeyDown(event);
}

void PrismConsoleWidgetUVE::ArrangeUVE(const Math::RectUVE& finalRect) {
    RelinkChildrenUVE();
    m_layoutRect = finalRect;

    const float startX = finalRect.position.x + m_style.padding.left;
    const float width = std::max(10.0F, finalRect.size.x - m_style.padding.Horizontal());

    // 1. Toolbar Rect
    const Math::RectUVE toolbarRect{
        {startX, finalRect.position.y + m_style.padding.top},
        {width, kToolbarHeightUVE}
    };
    m_clearButton->ArrangeUVE(Math::RectUVE{toolbarRect.position, {55.0F, 22.0F}});

    // 2. Command Prompt Rect (Bottom)
    const float inputY = finalRect.position.y + finalRect.size.y - m_style.padding.bottom - kInputHeightUVE;
    const Math::RectUVE inputRect{
        {startX, inputY},
        {width, kInputHeightUVE}
    };
    m_commandInput->ArrangeUVE(inputRect);

    // 3. Log Container Rect (Middle)
    const float logY = toolbarRect.position.y + kToolbarHeightUVE + 4.0F;
    const float logH = std::max(10.0F, inputY - logY - 4.0F);
    const Math::RectUVE logRect{{startX, logY}, {width, logH}};
    m_scrollContainer->ArrangeUVE(logRect);
}

Math::Vector2UVE PrismConsoleWidgetUVE::OnMeasureContentUVE(Math::Vector2UVE availableSize) {
    const float padH = m_style.padding.Horizontal();
    const float padV = m_style.padding.Vertical();
    const float innerW = std::max(10.0F, availableSize.x - padH);
    const float innerH = std::max(10.0F, availableSize.y - padV);

    m_clearButton->MeasureUVE(Math::Vector2UVE{55.0F, 22.0F});
    m_commandInput->MeasureUVE(Math::Vector2UVE{innerW, kInputHeightUVE});

    const float logH = std::max(10.0F, innerH - kToolbarHeightUVE - kInputHeightUVE - 8.0F);
    m_scrollContainer->MeasureUVE(Math::Vector2UVE{innerW, logH});

    return availableSize;
}

void PrismConsoleWidgetUVE::OnPaintContentUVE(PrismDrawListUVE& drawList) {
    PrismWidgetUVE::OnPaintContentUVE(drawList);

    const auto& theme = PrismThemeUVE::Dark();

    // 1. Console panel background card
    drawList.AddRectFilled(m_layoutRect, theme.bgPanel, theme.radiusSmall);
    drawList.AddRectStroke(m_layoutRect, theme.borderSubtle, 1.0F, theme.radiusSmall);

    // 2. Toolbar separator line
    const float toolbarSepY = m_layoutRect.position.y + m_style.padding.top + kToolbarHeightUVE + 2.0F;
    drawList.AddRectFilled(
        Math::RectUVE{{m_layoutRect.position.x + 4.0F, toolbarSepY}, {m_layoutRect.size.x - 8.0F, 1.0F}},
        theme.borderSubtle
    );

    // 3. Input separator line
    const float inputSepY = m_commandInput->GetLayoutRect().position.y - 3.0F;
    drawList.AddRectFilled(
        Math::RectUVE{{m_layoutRect.position.x + 4.0F, inputSepY}, {m_layoutRect.size.x - 8.0F, 1.0F}},
        theme.borderSubtle
    );
}

} // namespace UVE::UI::Prism
