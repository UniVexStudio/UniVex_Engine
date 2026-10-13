// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <type_traits>

#include <gtest/gtest.h>

#include "uve/utilities/non_copyable_uve.h"

namespace UVE::Utilities::Tests {
namespace {

struct WidgetUVE : public NonCopyableUVE {
    int value = 0;
};

static_assert(!std::is_copy_constructible_v<WidgetUVE>);
static_assert(!std::is_copy_assignable_v<WidgetUVE>);
static_assert(!std::is_move_constructible_v<WidgetUVE>);
static_assert(!std::is_move_assignable_v<WidgetUVE>);
static_assert(std::is_default_constructible_v<WidgetUVE>);

TEST(NonCopyableUVETest, Derived_IsConstructibleButNotCopyableOrMovable) {
    // The static_asserts above are the test; this case exists so the file runs green and the
    // inheritance reads naturally (a default-constructed derived still works).
    const WidgetUVE widget;
    EXPECT_EQ(widget.value, 0);
}

} // namespace
} // namespace UVE::Utilities::Tests
