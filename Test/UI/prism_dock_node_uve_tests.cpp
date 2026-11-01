// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_dock_node_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismDockNodeTest, InitialNodeIsLeaf) {
    auto node = std::make_shared<PrismDockNodeUVE>("Root");
    EXPECT_TRUE(node->IsLeaf());
    EXPECT_EQ(node->GetSplitDirection(), PrismDockSplitDirectionUVE::None);
    EXPECT_EQ(node->GetFirstChild(), nullptr);
    EXPECT_EQ(node->GetSecondChild(), nullptr);
}

TEST(PrismDockNodeTest, SplitHorizontalDividesBoundsAccurately) {
    auto root = std::make_shared<PrismDockNodeUVE>("Root");
    auto hierarchyPanel = std::make_shared<PrismWidgetUVE>("Hierarchy");
    auto inspectorPanel = std::make_shared<PrismWidgetUVE>("Inspector");

    root->AddPanel(hierarchyPanel);

    // Split horizontally with 30% / 70% ratio
    const bool splitSuccess = root->Split(PrismDockSplitDirectionUVE::Horizontal, 0.3F, inspectorPanel);
    EXPECT_TRUE(splitSuccess);
    EXPECT_FALSE(root->IsLeaf());

    // Allocate 1000 x 600 area
    root->ArrangeLayout(Math::RectUVE{{0.0F, 0.0F}, {1000.0F, 600.0F}});

    // First child (Hierarchy) should be 300px wide
    auto first = root->GetFirstChild();
    ASSERT_NE(first, nullptr);
    EXPECT_FLOAT_EQ(first->GetBounds().position.x, 0.0F);
    EXPECT_FLOAT_EQ(first->GetBounds().size.x, 300.0F);
    EXPECT_FLOAT_EQ(first->GetBounds().size.y, 600.0F);

    // Second child (Inspector) should be 700px wide starting at x = 300
    auto second = root->GetSecondChild();
    ASSERT_NE(second, nullptr);
    EXPECT_FLOAT_EQ(second->GetBounds().position.x, 300.0F);
    EXPECT_FLOAT_EQ(second->GetBounds().size.x, 700.0F);
    EXPECT_FLOAT_EQ(second->GetBounds().size.y, 600.0F);
}

TEST(PrismDockNodeTest, CalculateDropQuadrantEdgeZones) {
    auto node = std::make_shared<PrismDockNodeUVE>("DockSlot");
    node->ArrangeLayout(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}});

    // Left edge (x = 40 / 400 = 0.10)
    EXPECT_EQ(node->CalculateDropQuadrant({40.0F, 200.0F}), PrismDockQuadrantUVE::Left);

    // Right edge (x = 360 / 400 = 0.90)
    EXPECT_EQ(node->CalculateDropQuadrant({360.0F, 200.0F}), PrismDockQuadrantUVE::Right);

    // Top edge (y = 50 / 400 = 0.125)
    EXPECT_EQ(node->CalculateDropQuadrant({200.0F, 50.0F}), PrismDockQuadrantUVE::Top);

    // Bottom edge (y = 350 / 400 = 0.875)
    EXPECT_EQ(node->CalculateDropQuadrant({200.0F, 350.0F}), PrismDockQuadrantUVE::Bottom);

    // Center zone (x = 200, y = 200)
    EXPECT_EQ(node->CalculateDropQuadrant({200.0F, 200.0F}), PrismDockQuadrantUVE::Center);
}

} // namespace UVE::UI::Prism::Tests
