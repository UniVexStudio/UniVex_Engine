// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_event_router_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

class CustomClickableWidgetUVE : public PrismWidgetUVE {
public:
    using PrismWidgetUVE::PrismWidgetUVE;

    int clickCount = 0;
    int enterCount = 0;
    int leaveCount = 0;

    void OnPointerEnter(const PrismPointerEventUVE& event) override {
        PrismWidgetUVE::OnPointerEnter(event);
        enterCount++;
    }

    void OnPointerLeave(const PrismPointerEventUVE& event) override {
        PrismWidgetUVE::OnPointerLeave(event);
        leaveCount++;
    }

    PrismEventResultUVE OnPointerDown(const PrismPointerEventUVE& event) override {
        PrismWidgetUVE::OnPointerDown(event);
        clickCount++;
        return PrismEventResultUVE::Handled; // Consumes event
    }
};

TEST(PrismEventRouterTest, HitTestDeepestSelectsLeafWidget) {
    PrismEventRouterUVE router;

    auto root = std::make_shared<PrismWidgetUVE>("Root");
    root->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {800.0F, 600.0F}});

    auto child = std::make_shared<PrismWidgetUVE>("Child");
    child->ArrangeUVE(Math::RectUVE{{100.0F, 100.0F}, {200.0F, 200.0F}});

    auto grandchild = std::make_shared<PrismWidgetUVE>("GrandChild");
    grandchild->ArrangeUVE(Math::RectUVE{{120.0F, 120.0F}, {50.0F, 50.0F}});

    child->AddChild(grandchild);
    root->AddChild(child);
    router.SetRoot(root);

    // Coordinate inside grandchild (130, 130)
    auto target = router.HitTestDeepest(Math::Vector2UVE{130.0F, 130.0F});
    EXPECT_EQ(target, grandchild);

    // Coordinate inside child but outside grandchild (250, 250)
    target = router.HitTestDeepest(Math::Vector2UVE{250.0F, 250.0F});
    EXPECT_EQ(target, child);

    // Coordinate outside root (900, 900)
    target = router.HitTestDeepest(Math::Vector2UVE{900.0F, 900.0F});
    EXPECT_EQ(target, nullptr);
}

TEST(PrismEventRouterTest, PointerHoverTransitionsFireOnce) {
    PrismEventRouterUVE router;

    auto root = std::make_shared<PrismWidgetUVE>("Root");
    root->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {500.0F, 500.0F}});

    auto button = std::make_shared<CustomClickableWidgetUVE>("Button");
    button->ArrangeUVE(Math::RectUVE{{50.0F, 50.0F}, {100.0F, 40.0F}});
    root->AddChild(button);

    router.SetRoot(root);

    // Move pointer outside button
    router.DispatchPointerMove(PrismPointerEventUVE{{10.0F, 10.0F}});
    EXPECT_EQ(button->enterCount, 0);
    EXPECT_FALSE(button->IsHovered());

    // Move pointer inside button
    router.DispatchPointerMove(PrismPointerEventUVE{{60.0F, 60.0F}});
    EXPECT_EQ(button->enterCount, 1);
    EXPECT_TRUE(button->IsHovered());

    // Move pointer within button again - enterCount should remain 1
    router.DispatchPointerMove(PrismPointerEventUVE{{70.0F, 65.0F}});
    EXPECT_EQ(button->enterCount, 1);
    EXPECT_EQ(button->leaveCount, 0);

    // Move pointer outside
    router.DispatchPointerMove(PrismPointerEventUVE{{10.0F, 10.0F}});
    EXPECT_EQ(button->leaveCount, 1);
    EXPECT_FALSE(button->IsHovered());
}

TEST(PrismEventRouterTest, EventBubblingAndPointerCapture) {
    PrismEventRouterUVE router;

    auto root = std::make_shared<PrismWidgetUVE>("Root");
    root->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {500.0F, 500.0F}});

    auto clickable = std::make_shared<CustomClickableWidgetUVE>("Clickable");
    clickable->ArrangeUVE(Math::RectUVE{{20.0F, 20.0F}, {80.0F, 80.0F}});
    root->AddChild(clickable);

    router.SetRoot(root);

    // Click inside button
    router.DispatchPointerDown(PrismPointerEventUVE{{30.0F, 30.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_EQ(clickable->clickCount, 1);

    // Capture pointer
    router.CapturePointer(clickable);
    EXPECT_EQ(router.GetCapturedWidget(), clickable);

    // Pointer move far away at (400, 400) should still route directly to captured widget
    router.DispatchPointerMove(PrismPointerEventUVE{{400.0F, 400.0F}});
    EXPECT_EQ(router.GetCapturedWidget(), clickable);

    router.ReleasePointer();
    EXPECT_EQ(router.GetCapturedWidget(), nullptr);
}

} // namespace UVE::UI::Prism::Tests
