// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <memory>
#include "uve/ui/prism/prism_widget_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

namespace UVE::Editor {

/// Host bridge that embeds and renders pure Prism UI widgets inside Dear ImGui editor windows.
/// Translates PrismDrawList commands directly to ImDrawList, and forwards ImGui mouse/keyboard IO
/// into PrismEventRouter. This enables gradual, zero-regression migration of Editor panels.
class PrismImGuiViewportBridgeUVE {
public:
    PrismImGuiViewportBridgeUVE();

    void SetRootWidget(std::shared_ptr<UI::Prism::PrismWidgetUVE> root);
    [[nodiscard]] std::shared_ptr<UI::Prism::PrismWidgetUVE> GetRootWidget() const noexcept { return m_rootWidget; }

    /// Renders the root widget inside the current ImGui window.
    /// Uses ImGui::GetCursorScreenPos() and ImGui::GetContentRegionAvail().
    void RenderImGuiHost();

    [[nodiscard]] UI::Prism::PrismEventRouterUVE& GetEventRouter() noexcept { return m_eventRouter; }

private:
    std::shared_ptr<UI::Prism::PrismWidgetUVE> m_rootWidget;
    UI::Prism::PrismEventRouterUVE             m_eventRouter;
    UI::Prism::PrismDrawListUVE                m_drawList;
};

} // namespace UVE::Editor
