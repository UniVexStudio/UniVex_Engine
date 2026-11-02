// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_slider_uve.h"
#include "uve/ui/prism/prism_numeric_drag_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"
#include "uve/ui/prism/prism_draw_list_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismSliderTest, PointerDragAdjustsValueAndNotifies) {
    PrismEventRouterUVE router;

    float changedValue = -1.0F;
    int changeCount = 0;

    auto slider = std::make_shared<PrismSliderUVE>(0.0F, 100.0F, 25.0F, "Speed");
    slider->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {200.0F, 24.0F}});
    slider->SetOnValueChanged([&](float val) {
        changedValue = val;
        ++changeCount;
    });

    router.SetRoot(slider);
    EXPECT_FLOAT_EQ(slider->GetValue(), 25.0F);

    // Pointer down at right edge of slider track -> should reach near 100.0F
    router.DispatchPointerDown(PrismPointerEventUVE{{190.0F, 12.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_GT(slider->GetValue(), 80.0F);
    EXPECT_GT(changeCount, 0);

    // Keyboard arrow left nudges down
    const float beforeKey = slider->GetValue();
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Left});
    EXPECT_LT(slider->GetValue(), beforeKey);

    // Paint generates draw list commands
    PrismDrawListUVE drawList;
    slider->PaintUVE(drawList);
    EXPECT_GT(drawList.GetCommandCount(), 2u);
}

TEST(PrismNumericDragTest, DragScrubAdjustsValue) {
    PrismEventRouterUVE router;

    float trackedValue = 0.0F;
    auto drag = std::make_shared<PrismNumericDragUVE>("X", 10.0F, 0.1F);
    drag->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 24.0F}});
    drag->SetOnValueChanged([&](float v) { trackedValue = v; });

    router.SetRoot(drag);
    EXPECT_FLOAT_EQ(drag->GetValue(), 10.0F);

    // Click down at x=50
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 12.0F}, {}, Input::MouseButtonUVE::Left});

    // Drag to x=70 (+20px * 0.1 speed = +2.0)
    router.DispatchPointerMove(PrismPointerEventUVE{{70.0F, 12.0F}, {20.0F, 0.0F}});
    EXPECT_FLOAT_EQ(drag->GetValue(), 12.0F);
    EXPECT_FLOAT_EQ(trackedValue, 12.0F);

    // Release mouse
    router.DispatchPointerUp(PrismPointerEventUVE{{70.0F, 12.0F}, {}, Input::MouseButtonUVE::Left});
}

TEST(PrismNumericDragTest, ClickEntersDirectTypingModeAndCommits) {
    PrismEventRouterUVE router;

    auto drag = std::make_shared<PrismNumericDragUVE>("Z", 0.0F, 0.1F);
    drag->ArrangeUVE(Math::RectUVE{{0.0F, 0.0F}, {100.0F, 24.0F}});
    router.SetRoot(drag);

    // Click without moving (<2px) triggers text typing mode
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 12.0F}, {}, Input::MouseButtonUVE::Left});
    router.DispatchPointerUp(PrismPointerEventUVE{{50.0F, 12.0F}, {}, Input::MouseButtonUVE::Left});

    // Backspace existing "0.00"
    for (int i = 0; i < 5; ++i) {
        router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Backspace});
    }

    // Type "42.5"
    router.DispatchCharInput(U'4');
    router.DispatchCharInput(U'2');
    router.DispatchCharInput(U'.');
    router.DispatchCharInput(U'5');

    // Press Enter to commit
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Enter});

    EXPECT_FLOAT_EQ(drag->GetValue(), 42.5F);
}

} // namespace UVE::UI::Prism::Tests
