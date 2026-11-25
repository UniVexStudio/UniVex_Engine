// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "monitor_selection_policy.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(MonitorSelectionPolicyTest, SelectsAnExactMonitorNameMatch) {
    constexpr std::array<std::string_view, 3U> monitors{"Built-in", "Display A", "Display B"};

    const std::optional<std::size_t> index = FindMonitorIndex("Display A", monitors);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 1U);
}

TEST(MonitorSelectionPolicyTest, UsesTheFirstExactMatchAndDoesNotNormalizeNames) {
    constexpr std::array<std::string_view, 3U> monitors{"Display A", "Display B", "Display A"};

    const std::optional<std::size_t> index = FindMonitorIndex("Display A", monitors);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 0U);
    EXPECT_FALSE(FindMonitorIndex("display a", monitors).has_value());
}

TEST(MonitorSelectionPolicyTest, EmptyOrUnknownNameRequestsPrimaryMonitorFallback) {
    constexpr std::array<std::string_view, 2U> monitors{"Built-in", "Display A"};

    EXPECT_FALSE(FindMonitorIndex({}, monitors).has_value());
    EXPECT_FALSE(FindMonitorIndex("Disconnected display", monitors).has_value());
    EXPECT_FALSE(FindMonitorIndex("Display A", std::span<const std::string_view>{}).has_value());
}

} // namespace
} // namespace UVE::Window::Tests
