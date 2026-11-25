// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "string_utils.h"

namespace UVE::Strings::Tests {
namespace {

TEST(StringUtilsTest, Trim_StripsAsciiWhitespaceBothEnds) {
    EXPECT_EQ(Trim("  hello\t\n"), "hello");
    EXPECT_EQ(Trim("hello"), "hello");
    EXPECT_EQ(Trim(""), "");
    EXPECT_EQ(Trim("   "), "");
    EXPECT_EQ(Trim("  a  b  "), "a  b"); // Interior space survives.
}

TEST(StringUtilsTest, Split_KeepsEmptyParts) {
    const std::vector<std::string_view> parts = Split("a,,b,", ',');
    ASSERT_EQ(parts.size(), 4U);
    EXPECT_EQ(parts[0U], "a");
    EXPECT_EQ(parts[1U], "");
    EXPECT_EQ(parts[2U], "b");
    EXPECT_EQ(parts[3U], "");
    const std::vector<std::string_view> single = Split("abc", ',');
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single[0U], "abc");
}

TEST(StringUtilsTest, Join_InsertsDelimitersBetweenOnly) {
    const std::vector<std::string_view> parts{"a", "b", "c"};
    EXPECT_EQ(Join(parts, ", "), "a, b, c");
    const std::vector<std::string_view> one{"solo"};
    EXPECT_EQ(Join(one, ", "), "solo");
    const std::vector<std::string_view> none;
    EXPECT_EQ(Join(none, ", "), "");
}

TEST(StringUtilsTest, Case_ConvertsAsciiAndPassesTheRestThrough) {
    EXPECT_EQ(ToLower("HeLLo World! 123"), "hello world! 123");
    EXPECT_EQ(ToUpper("HeLLo World! 123"), "HELLO WORLD! 123");
    EXPECT_EQ(ToLower(""), "");
}

} // namespace
} // namespace UVE::Strings::Tests
