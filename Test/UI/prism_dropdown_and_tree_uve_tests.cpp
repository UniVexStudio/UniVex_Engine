// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_dropdown_uve.h"
#include "uve/ui/prism/prism_tree_view_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismDropdownTest, OpenSelectAndClose) {
    PrismEventRouterUVE router;

    std::vector<std::string> options = {"Lit", "Unlit", "Wireframe", "Path Traced"};
    auto dropdown = std::make_shared<PrismDropdownUVE>(options, 0);
    dropdown->ArrangeUVE(Math::RectUVE{{10.0F, 10.0F}, {160.0F, 24.0F}});

    int selectedIdx = -1;
    std::string selectedText;
    dropdown->SetOnSelectionChanged([&](int idx, const std::string& str) {
        selectedIdx = idx;
        selectedText = str;
    });

    router.SetRoot(dropdown);
    EXPECT_FALSE(dropdown->IsOpen());
    EXPECT_EQ(dropdown->GetSelectedItem(), "Lit");

    // Click header to open
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(dropdown->IsOpen());

    // Click 3rd item ("Wireframe", index 2)
    // list starts at y = 10 + 24 + 2 = 36. Each item is 22px high.
    // Index 2 is at y = 36 + 2 * 22 + 10 = 90px
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 90.0F}, {}, Input::MouseButtonUVE::Left});

    EXPECT_FALSE(dropdown->IsOpen());
    EXPECT_EQ(dropdown->GetSelectedIndex(), 2);
    EXPECT_EQ(selectedIdx, 2);
    EXPECT_EQ(selectedText, "Wireframe");
}

TEST(PrismTreeViewTest, TreeExpandCollapseAndSelection) {
    PrismEventRouterUVE router;

    auto tree = std::make_shared<PrismTreeViewUVE>("SceneHierarchy");
    tree->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {250.0F, 500.0F}});

    auto root1 = std::make_shared<PrismTreeNodeUVE>("Main Camera", 101);
    auto root2 = std::make_shared<PrismTreeNodeUVE>("Directional Light", 102);
    auto parent = std::make_shared<PrismTreeNodeUVE>("Player Character", 103);
    auto child1 = std::make_shared<PrismTreeNodeUVE>("Mesh", 104);
    auto child2 = std::make_shared<PrismTreeNodeUVE>("Weapon Socket", 105);

    parent->AddChild(child1);
    parent->AddChild(child2);

    tree->AddRootNode(root1);
    tree->AddRootNode(root2);
    tree->AddRootNode(parent);

    std::uint64_t selectedEntity = 0;
    tree->SetOnSelectionChanged([&](const std::shared_ptr<PrismTreeNodeUVE>& node) {
        if (node) selectedEntity = node->GetEntityId();
    });

    router.SetRoot(tree);

    // Initial measurement (parent collapsed) -> 3 visible rows
    Math::Vector2UVE size = tree->MeasureUVE(Math::Vector2UVE{250.0F, 500.0F});
    EXPECT_FLOAT_EQ(size.y, 3 * 22.0F + 4.0F);

    // Expand parent node
    parent->SetExpanded(true);
    size = tree->MeasureUVE(Math::Vector2UVE{250.0F, 500.0F});
    // Now 5 visible rows: root1, root2, parent, child1, child2
    EXPECT_FLOAT_EQ(size.y, 5 * 22.0F + 4.0F);

    // Click on 4th row (child1, y = 2 + 3 * 22 + 10 = 78)
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 78.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_EQ(selectedEntity, 104ULL);
    EXPECT_EQ(tree->GetSelectedNode(), child1);

    // Keyboard arrow up moves to parent (entity 103)
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Up});
    EXPECT_EQ(selectedEntity, 103ULL);
    EXPECT_EQ(tree->GetSelectedNode(), parent);

    // Left arrow collapses parent
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Left});
    EXPECT_FALSE(parent->IsExpanded());
}

} // namespace UVE::UI::Prism::Tests
