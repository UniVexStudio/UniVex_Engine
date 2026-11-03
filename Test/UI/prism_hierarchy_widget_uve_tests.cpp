// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_hierarchy_widget_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismHierarchyWidgetTest, SyncTreeAndSelection) {
    auto hierarchy = std::make_shared<PrismHierarchyWidgetUVE>("Outliner");
    hierarchy->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {250.0F, 500.0F}});

    std::vector<PrismHierarchyItemUVE> items;
    items.push_back(PrismHierarchyItemUVE{101, "Main Camera", {}});
    items.push_back(PrismHierarchyItemUVE{102, "Directional Light", {}});

    PrismHierarchyItemUVE player{103, "Player Character", {}};
    player.children.push_back(PrismHierarchyItemUVE{104, "Body Mesh", {}});
    player.children.push_back(PrismHierarchyItemUVE{105, "Sword", {}});
    items.push_back(player);

    std::uint64_t selectedEntity = 0;
    hierarchy->SetOnEntitySelected([&](std::uint64_t id) {
        selectedEntity = id;
    });

    hierarchy->SyncTree(items, 104);
    EXPECT_EQ(hierarchy->GetSelectedEntityId(), 104u);

    // Filter update
    std::string filter;
    hierarchy->SetOnFilterChanged([&](const std::string& str) {
        filter = str;
    });

    // Paint generates commands
    PrismDrawListUVE drawList;
    hierarchy->PaintUVE(drawList);
    // Same-state quads batch into one command, so assert on emitted geometry (more than 2 quads).
    EXPECT_GT(drawList.GetTotalIndexCount(), 2u * 6u);
}

TEST(PrismHierarchyWidgetTest, AddButtonCallback) {
    auto hierarchy = std::make_shared<PrismHierarchyWidgetUVE>("Outliner");
    hierarchy->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {250.0F, 500.0F}});

    bool addRequested = false;
    hierarchy->SetOnAddObjectRequested([&]() {
        addRequested = true;
    });

    PrismEventRouterUVE router;
    router.SetRoot(hierarchy);

    // Add button is at x=4..28, y=4..26. Click at (14, 14)
    router.DispatchPointerDown(PrismPointerEventUVE{{14.0F, 14.0F}, {}, Input::MouseButtonUVE::Left});
    router.DispatchPointerUp(PrismPointerEventUVE{{14.0F, 14.0F}, {}, Input::MouseButtonUVE::Left});

    EXPECT_TRUE(addRequested);
}

} // namespace UVE::UI::Prism::Tests
