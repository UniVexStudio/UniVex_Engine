// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_scroll_container_uve.h"
#include "uve/ui/prism/prism_checkbox_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismScrollContainerTest, MeasuresAndCalculatesMaxScrollOffset) {
    auto scroll = std::make_shared<PrismScrollContainerUVE>("TestScroll");
    scroll->GetStyle().gap = 10.0F;

    // Add 5 children, each 50px tall
    for (int i = 0; i < 5; ++i) {
        auto child = std::make_shared<PrismWidgetUVE>("Child");
        child->GetStyle().height = PrismLengthUVE::Px(50.0F);
        child->GetStyle().width = PrismLengthUVE::Px(100.0F);
        scroll->AddChild(child);
    }

    // Container available height is 150px
    // Total content height = 5 * 50 + 4 * 10 = 290px
    scroll->MeasureUVE(Math::Vector2UVE{200.0F, 150.0F});
    scroll->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 150.0F}});

    EXPECT_FLOAT_EQ(scroll->GetMaxScrollOffsetY(), 140.0F); // 290 - 150 = 140
    EXPECT_FLOAT_EQ(scroll->GetScrollOffsetY(), 0.0F);

    // Dispatch scroll down
    PrismScrollEventUVE scrollEvent{{100.0F, 75.0F}, {0.0F, -2.0F}}; // wheel delta -2 -> +60px
    const auto result = scroll->OnScroll(scrollEvent);
    EXPECT_EQ(result, PrismEventResultUVE::Handled);
    EXPECT_FLOAT_EQ(scroll->GetScrollOffsetY(), 60.0F);

    // Over-scroll down should clamp to max
    scroll->OnScroll(PrismScrollEventUVE{{100.0F, 75.0F}, {0.0F, -5.0F}}); // +150px -> clamps to 140
    EXPECT_FLOAT_EQ(scroll->GetScrollOffsetY(), 140.0F);

    // Over-scroll up should clamp to 0
    scroll->OnScroll(PrismScrollEventUVE{{100.0F, 75.0F}, {0.0F, 10.0F}}); // -300px -> clamps to 0
    EXPECT_FLOAT_EQ(scroll->GetScrollOffsetY(), 0.0F);
}

TEST(PrismScrollContainerTest, PaintsScrollbarWhenContentOverflows) {
    auto scroll = std::make_shared<PrismScrollContainerUVE>("TestScroll");
    auto tallChild = std::make_shared<PrismWidgetUVE>("TallChild");
    tallChild->GetStyle().height = PrismLengthUVE::Px(500.0F);
    scroll->AddChild(tallChild);

    scroll->MeasureUVE(Math::Vector2UVE{200.0F, 100.0F});
    scroll->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 100.0F}});

    PrismDrawListUVE drawList;
    scroll->PaintUVE(drawList);

    // Should emit commands for container, child, plus scrollbar track & thumb
    EXPECT_GT(drawList.GetCommandCount(), 2u);
}

TEST(PrismCheckboxTest, ClickTogglesStateAndTriggersCallback) {
    PrismEventRouterUVE router;

    bool callbackState = false;
    int toggleCount = 0;

    auto checkbox = std::make_shared<PrismCheckboxUVE>("Enable VSync", false);
    checkbox->ArrangeUVE(Math::RectUVE{{10.0F, 10.0F}, {150.0F, 24.0F}});
    checkbox->SetOnToggled([&](bool checked) {
        callbackState = checked;
        ++toggleCount;
    });

    router.SetRoot(checkbox);

    EXPECT_FALSE(checkbox->IsChecked());

    // Click checkbox
    router.DispatchPointerDown(PrismPointerEventUVE{{20.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(checkbox->IsChecked());
    EXPECT_TRUE(callbackState);
    EXPECT_EQ(toggleCount, 1);

    // Click again to uncheck
    router.DispatchPointerDown(PrismPointerEventUVE{{20.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_FALSE(checkbox->IsChecked());
    EXPECT_FALSE(callbackState);
    EXPECT_EQ(toggleCount, 2);
}

TEST(PrismCheckboxTest, SpacebarTogglesWhenFocused) {
    PrismEventRouterUVE router;

    auto checkbox = std::make_shared<PrismCheckboxUVE>("HDR Mode", false);
    checkbox->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 24.0F}});
    router.SetRoot(checkbox);

    // Focus checkbox
    router.DispatchPointerDown(PrismPointerEventUVE{{10.0F, 10.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(checkbox->IsFocused());
    EXPECT_TRUE(checkbox->IsChecked()); // First click toggled to true

    // Spacebar toggles to false
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Space});
    EXPECT_FALSE(checkbox->IsChecked());

    // Enter toggles back to true
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Enter});
    EXPECT_TRUE(checkbox->IsChecked());
}

} // namespace UVE::UI::Prism::Tests
