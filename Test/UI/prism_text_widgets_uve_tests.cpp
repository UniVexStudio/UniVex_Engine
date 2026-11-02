// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/ui/prism/prism_text_uve.h"
#include "uve/ui/prism/prism_text_input_uve.h"
#include "uve/ui/prism/prism_event_router_uve.h"

#include <gtest/gtest.h>

namespace UVE::UI::Prism::Tests {

TEST(PrismTextTest, MeasureComputesWidthBasedOnLengthAndFontSize) {
    PrismTextUVE label("UniVex Engine", 16.0F);
    const Math::Vector2UVE measured = label.MeasureUVE({800.0F, 600.0F});

    // 13 chars * (16 * 0.55 = 8.8) ~ 114.4
    EXPECT_GT(measured.x, 100.0F);
    EXPECT_GT(measured.y, 16.0F);
}

TEST(PrismTextInputTest, TypingAndBackspaceUpdatesText) {
    PrismEventRouterUVE router;

    auto input = std::make_shared<PrismTextInputUVE>("Type here...");
    input->ArrangeUVE(Math::RectUVE{{10.0F, 10.0F}, {200.0F, 28.0F}});
    router.SetRoot(input);

    // Click to focus
    router.DispatchPointerDown(PrismPointerEventUVE{{50.0F, 20.0F}, {}, Input::MouseButtonUVE::Left});
    EXPECT_TRUE(input->IsFocused());

    // Type "ABC"
    router.DispatchCharInput(U'A');
    router.DispatchCharInput(U'B');
    router.DispatchCharInput(U'C');
    EXPECT_EQ(input->GetText(), "ABC");

    // Press Backspace
    router.DispatchKeyDown(PrismKeyEventUVE{Input::KeyCodeUVE::Backspace});
    EXPECT_EQ(input->GetText(), "AB");

    // Click outside to lose focus and commit
    std::string committedText;
    input->SetOnCommit([&](const std::string& text) { committedText = text; });

    router.ClearFocus();
    EXPECT_FALSE(input->IsFocused());
    EXPECT_EQ(committedText, "AB");
}

} // namespace UVE::UI::Prism::Tests
