// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/window/monitor_selection_policy_uve.h"

#include <array>
#include <cstddef>
#include <optional>
#include <span>
#include <string_view>

#include <gtest/gtest.h>

namespace UVE::Window::Tests {
namespace {

TEST(MonitorSelectionPolicyUVETest, SelectsAnExactMonitorNameMatch) {
    constexpr std::array<std::string_view, 3U> monitors{"Built-in", "Display A", "Display B"};

    const std::optional<std::size_t> index = FindMonitorIndexUVE("Display A", monitors);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 1U);
}

TEST(MonitorSelectionPolicyUVETest, UsesTheFirstExactMatchAndDoesNotNormalizeNames) {
    constexpr std::array<std::string_view, 3U> monitors{"Display A", "Display B", "Display A"};

    const std::optional<std::size_t> index = FindMonitorIndexUVE("Display A", monitors);
    ASSERT_TRUE(index.has_value());
    EXPECT_EQ(*index, 0U);
    EXPECT_FALSE(FindMonitorIndexUVE("display a", monitors).has_value());
}

TEST(MonitorSelectionPolicyUVETest, EmptyOrUnknownNameRequestsPrimaryMonitorFallback) {
    constexpr std::array<std::string_view, 2U> monitors{"Built-in", "Display A"};

    EXPECT_FALSE(FindMonitorIndexUVE({}, monitors).has_value());
    EXPECT_FALSE(FindMonitorIndexUVE("Disconnected display", monitors).has_value());
    EXPECT_FALSE(FindMonitorIndexUVE("Display A", std::span<const std::string_view>{}).has_value());
}

} // namespace
} // namespace UVE::Window::Tests
