// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_console_widget_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismConsoleWidgetTest, LogsAndFiltering) {
    auto console = std::make_shared<PrismConsoleWidgetUVE>("ConsoleTest");
    console->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {500.0F, 300.0F}});

    console->AddLog(PrismLogSeverityUVE::Info, "Engine initialized");
    console->AddLog(PrismLogSeverityUVE::Warning, "High memory pressure");
    console->AddLog(PrismLogSeverityUVE::Error, "Shader compile failed");

    EXPECT_EQ(console->GetLogCount(), 3u);

    // Filter to Warning only
    console->SetSeverityFilter(PrismLogFilterUVE::Warning);
    EXPECT_EQ(console->GetSeverityFilter(), PrismLogFilterUVE::Warning);

    // Clear logs
    console->ClearLogs();
    EXPECT_EQ(console->GetLogCount(), 0u);
}

TEST(PrismConsoleWidgetTest, CommandSubmissionAndHistory) {
    auto console = std::make_shared<PrismConsoleWidgetUVE>("ConsoleTest");
    console->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {500.0F, 300.0F}});

    std::string executedCommand;
    console->SetOnCommandSubmitted([&](const std::string& cmd) {
        executedCommand = cmd;
    });

    console->SubmitCommand("stat fps");
    EXPECT_EQ(executedCommand, "stat fps");
    EXPECT_EQ(console->GetHistory().size(), 1u);
    EXPECT_EQ(console->GetHistory()[0], "stat fps");

    console->SubmitCommand("r.wireframe 1");
    EXPECT_EQ(executedCommand, "r.wireframe 1");
    EXPECT_EQ(console->GetHistory().size(), 2u);

    // Paint generates draw commands
    PrismDrawListUVE drawList;
    console->PaintUVE(drawList);
    EXPECT_GT(drawList.GetCommandCount(), 2u);
}

} // namespace UVE::UI::Prism::Tests
