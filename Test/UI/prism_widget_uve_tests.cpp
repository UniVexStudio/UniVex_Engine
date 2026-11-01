// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_widget_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismWidgetTest, HierarchyParentChildAttachment) {
    auto parent = std::make_shared<PrismWidgetUVE>("Parent");
    auto child = std::make_shared<PrismWidgetUVE>("Child");

    parent->AddChild(child);
    EXPECT_EQ(parent->GetChildren().size(), 1U);
    EXPECT_EQ(child->GetParent(), parent);

    parent->RemoveChild(child);
    EXPECT_EQ(parent->GetChildren().size(), 0U);
    EXPECT_EQ(child->GetParent(), nullptr);
}

TEST(PrismWidgetTest, MeasureComputesExplicitFixedSize) {
    auto widget = std::make_shared<PrismWidgetUVE>("FixedBox");
    widget->GetStyle().width = PrismLengthUVE::Px(250.0F);
    widget->GetStyle().height = PrismLengthUVE::Px(120.0F);

    const Math::Vector2UVE available{800.0F, 600.0F};
    const Math::Vector2UVE desired = widget->MeasureUVE(available);

    EXPECT_FLOAT_EQ(desired.x, 250.0F);
    EXPECT_FLOAT_EQ(desired.y, 120.0F);
}

TEST(PrismWidgetTest, FlexColumnLayoutArrangesChildrenSequentially) {
    auto container = std::make_shared<PrismWidgetUVE>("Container");
    container->GetStyle().flexDirection = PrismFlexDirectionUVE::Column;
    container->GetStyle().width = PrismLengthUVE::Px(300.0F);
    container->GetStyle().height = PrismLengthUVE::Px(400.0F);
    container->GetStyle().padding = PrismEdgesUVE{10.0F};
    container->GetStyle().gap = 5.0F;

    auto child1 = std::make_shared<PrismWidgetUVE>("Child1");
    child1->GetStyle().width = PrismLengthUVE::Px(100.0F);
    child1->GetStyle().height = PrismLengthUVE::Px(50.0F);

    auto child2 = std::make_shared<PrismWidgetUVE>("Child2");
    child2->GetStyle().width = PrismLengthUVE::Px(150.0F);
    child2->GetStyle().height = PrismLengthUVE::Px(60.0F);

    container->AddChild(child1);
    container->AddChild(child2);

    container->MeasureUVE({800.0F, 600.0F});
    container->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {300.0F, 400.0F}});

    // Child1 should be placed at (10, 10)
    EXPECT_FLOAT_EQ(child1->GetLayoutRect().position.x, 10.0F);
    EXPECT_FLOAT_EQ(child1->GetLayoutRect().position.y, 10.0F);

    // Child2 should be placed at y = 10 (pad) + 50 (child1) + 5 (gap) = 65
    EXPECT_FLOAT_EQ(child2->GetLayoutRect().position.x, 10.0F);
    EXPECT_FLOAT_EQ(child2->GetLayoutRect().position.y, 65.0F);
}

TEST(PrismWidgetTest, FlexRowGrowExpandsProportionally) {
    auto row = std::make_shared<PrismWidgetUVE>("Row");
    row->GetStyle().flexDirection = PrismFlexDirectionUVE::Row;
    row->GetStyle().width = PrismLengthUVE::Px(300.0F);
    row->GetStyle().height = PrismLengthUVE::Px(100.0F);

    auto itemA = std::make_shared<PrismWidgetUVE>("ItemA");
    itemA->GetStyle().width = PrismLengthUVE::Px(100.0F);
    itemA->GetStyle().flexGrow = 1.0F;

    auto itemB = std::make_shared<PrismWidgetUVE>("ItemB");
    itemB->GetStyle().width = PrismLengthUVE::Px(100.0F);
    itemB->GetStyle().flexGrow = 1.0F;

    row->AddChild(itemA);
    row->AddChild(itemB);

    row->MeasureUVE({800.0F, 600.0F});
    row->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {300.0F, 100.0F}});

    // Total initial = 200, container = 300, remaining = 100. Each grows by 50 -> 150 each
    EXPECT_FLOAT_EQ(itemA->GetLayoutRect().size.x, 150.0F);
    EXPECT_FLOAT_EQ(itemB->GetLayoutRect().size.x, 150.0F);
    EXPECT_FLOAT_EQ(itemB->GetLayoutRect().position.x, 150.0F);
}

TEST(PrismWidgetTest, HitTestRespectsFinalLayoutRect) {
    auto widget = std::make_shared<PrismWidgetUVE>("Box");
    widget->ArrangeUVE(Math::RectUVE{{50.0F, 50.0F}, {100.0F, 100.0F}});

    EXPECT_TRUE(widget->HitTestUVE(Math::Vector2UVE{55.0F, 55.0F}));
    EXPECT_TRUE(widget->HitTestUVE(Math::Vector2UVE{150.0F, 150.0F}));
    EXPECT_FALSE(widget->HitTestUVE(Math::Vector2UVE{45.0F, 50.0F}));
    EXPECT_FALSE(widget->HitTestUVE(Math::Vector2UVE{151.0F, 150.0F}));
}

} // namespace UVE::UI::Prism::Tests
