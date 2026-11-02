// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_tab_group_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismTabGroupTest, AddTabsAndSwitchActiveTab) {
    PrismEventRouterUVE router;

    auto tabGroup = std::make_shared<PrismTabGroupUVE>("EditorTabs");
    tabGroup->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {600.0F, 400.0F}});

    auto sceneContent = std::make_shared<PrismWidgetUVE>("SceneContent");
    auto inspectorContent = std::make_shared<PrismWidgetUVE>("InspectorContent");
    auto consoleContent = std::make_shared<PrismWidgetUVE>("ConsoleContent");

    tabGroup->AddTab("Scene", sceneContent);
    tabGroup->AddTab("Inspector", inspectorContent);
    tabGroup->AddTab("Console", consoleContent);

    EXPECT_EQ(tabGroup->GetTabs().size(), 3u);
    EXPECT_EQ(tabGroup->GetActiveTabIndex(), 0u);
    EXPECT_EQ(tabGroup->GetActiveContent(), sceneContent);

    std::size_t selectedTab = 999;
    tabGroup->SetOnTabSelected([&](std::size_t idx) { selectedTab = idx; });

    router.SetRoot(tabGroup);

    // Tab 0 is approx x: 4 to ~85. Tab 1 ("Inspector") is approx x: ~87 to ~180.
    // Click on Tab 1 at x=120, y=14
    router.DispatchPointerDown(PrismPointerEventUVE{{120.0F, 14.0F}, {}, Input::MouseButtonUVE::Left});
    router.DispatchPointerUp(PrismPointerEventUVE{{120.0F, 14.0F}, {}, Input::MouseButtonUVE::Left});

    EXPECT_EQ(tabGroup->GetActiveTabIndex(), 1u);
    EXPECT_EQ(selectedTab, 1u);
    EXPECT_EQ(tabGroup->GetActiveContent(), inspectorContent);

    // Check content arranged rect has y >= 28.0F (below tab bar)
    EXPECT_GE(inspectorContent->GetLayoutRect().position.y, 28.0F);
}

TEST(PrismTabGroupTest, CloseTabRemovesItAndAdjustsActive) {
    auto tabGroup = std::make_shared<PrismTabGroupUVE>("EditorTabs");
    tabGroup->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {600.0F, 400.0F}});

    tabGroup->AddTab("Tab1", std::make_shared<PrismWidgetUVE>("C1"));
    tabGroup->AddTab("Tab2", std::make_shared<PrismWidgetUVE>("C2"));

    tabGroup->SetActiveTabIndex(1);
    EXPECT_EQ(tabGroup->GetActiveTabIndex(), 1u);

    // Remove Tab 1
    tabGroup->RemoveTab(1);
    EXPECT_EQ(tabGroup->GetTabs().size(), 1u);
    EXPECT_EQ(tabGroup->GetActiveTabIndex(), 0u);
}

TEST(PrismTabGroupTest, DragThresholdTriggersDragStart) {
    PrismEventRouterUVE router;

    auto tabGroup = std::make_shared<PrismTabGroupUVE>("EditorTabs");
    tabGroup->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {600.0F, 400.0F}});
    tabGroup->AddTab("Tab1", std::make_shared<PrismWidgetUVE>("C1"));

    bool dragStarted = false;
    std::size_t draggedIdx = 999;
    tabGroup->SetOnTabDragStarted([&](std::size_t idx, Math::Vector2UVE /*pos*/) {
        dragStarted = true;
        draggedIdx = idx;
    });

    router.SetRoot(tabGroup);

    // Pointer down on tab 0
    router.DispatchPointerDown(PrismPointerEventUVE{{40.0F, 14.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_FALSE(dragStarted);

    // Pointer move > 6px
    router.DispatchPointerMove(PrismPointerEventUVE{{50.0F, 25.0F}, {10.0F, 11.0F}});
    EXPECT_TRUE(dragStarted);
    EXPECT_EQ(draggedIdx, 0u);
}

} // namespace UVE::UI::Prism::Tests
