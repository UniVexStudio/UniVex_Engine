// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_button_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismButtonTest, ClickTriggersCallback) {
    PrismEventRouterUVE router;

    auto button = std::make_shared<PrismButtonUVE>("Save File");
    button->ArrangeUVE(Math::RectUVE{{10.0F, 10.0F}, {120.0F, 30.0F}});
    router.SetRoot(button);

    bool clicked = false;
    button->SetOnClick([&]() { clicked = true; });

    // Pointer down inside button
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(button->IsPressed());
    EXPECT_FALSE(clicked);

    // Pointer up inside button -> triggers click
    router.DispatchPointerUp(PrismPointerEventUVE{{50.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_FALSE(button->IsPressed());
    EXPECT_TRUE(clicked);
}

TEST(PrismButtonTest, ReleaseOutsideDoesNotTriggerClick) {
    PrismEventRouterUVE router;

    auto root = std::make_shared<PrismWidgetUVE>("Root");
    root->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {400.0F, 400.0F}});

    auto button = std::make_shared<PrismButtonUVE>("Cancel");
    button->ArrangeUVE(Math::RectUVE{{10.0F, 10.0F}, {100.0F, 30.0F}});
    root->AddChild(button);
    router.SetRoot(root);

    bool clicked = false;
    button->SetOnClick([&]() { clicked = true; });

    // Pointer down inside button
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(button->IsPressed());

    // Move mouse outside button
    router.DispatchPointerMove(PrismPointerEventUVE{{200.0F, 200.0F}});
    EXPECT_FALSE(button->IsHovered());

    // Pointer up outside -> cancel click
    router.DispatchPointerUp(PrismPointerEventUVE{{200.0F, 200.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_FALSE(clicked);
}

} // namespace UVE::UI::Prism::Tests
