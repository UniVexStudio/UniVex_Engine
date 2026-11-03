// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/prism_imgui_viewport_bridge_uve.h"
#include "uve/ui/prism/prism_button_uve.h"
#include "uve/ui/prism/prism_console_widget_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {

TEST(PrismImGuiViewportBridgeTest, SetsRootWidgetAndRoutesEvents) {
    PrismImGuiViewportBridgeUVE bridge;
    EXPECT_EQ(bridge.GetRootWidget(), nullptr);

    auto console = std::make_shared<UI::Prism::PrismConsoleWidgetUVE>("BridgeConsole");
    bridge.SetRootWidget(console);

    EXPECT_EQ(bridge.GetRootWidget(), console);

    // Verify event router has root
    EXPECT_EQ(bridge.GetEventRouter().GetRoot(), console);

    // Append logs
    console->AddLog(UI::Prism::PrismLogSeverityUVE::Info, "Bridge test log");
    EXPECT_EQ(console->GetLogCount(), 1u);
}

} // namespace UVE::Editor::Tests
