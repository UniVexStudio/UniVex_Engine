// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <string>
#include <string_view>
#include <vector>

#include <gtest/gtest.h>

#include "uve/strings/string_utils_uve.h"

namespace UVE::Strings::Tests {
namespace {

TEST(StringUtilsUVETest, Trim_StripsAsciiWhitespaceBothEnds) {
    EXPECT_EQ(TrimUVE("  hello\t\n"), "hello");
    EXPECT_EQ(TrimUVE("hello"), "hello");
    EXPECT_EQ(TrimUVE(""), "");
    EXPECT_EQ(TrimUVE("   "), "");
    EXPECT_EQ(TrimUVE("  a  b  "), "a  b"); // Interior space survives.
}

TEST(StringUtilsUVETest, Split_KeepsEmptyParts) {
    const std::vector<std::string_view> parts = SplitUVE("a,,b,", ',');
    ASSERT_EQ(parts.size(), 4U);
    EXPECT_EQ(parts[0U], "a");
    EXPECT_EQ(parts[1U], "");
    EXPECT_EQ(parts[2U], "b");
    EXPECT_EQ(parts[3U], "");
    const std::vector<std::string_view> single = SplitUVE("abc", ',');
    ASSERT_EQ(single.size(), 1U);
    EXPECT_EQ(single[0U], "abc");
}

TEST(StringUtilsUVETest, Join_InsertsDelimitersBetweenOnly) {
    const std::vector<std::string_view> parts{"a", "b", "c"};
    EXPECT_EQ(JoinUVE(parts, ", "), "a, b, c");
    const std::vector<std::string_view> one{"solo"};
    EXPECT_EQ(JoinUVE(one, ", "), "solo");
    const std::vector<std::string_view> none;
    EXPECT_EQ(JoinUVE(none, ", "), "");
}

TEST(StringUtilsUVETest, Case_ConvertsAsciiAndPassesTheRestThrough) {
    EXPECT_EQ(ToLowerUVE("HeLLo World! 123"), "hello world! 123");
    EXPECT_EQ(ToUpperUVE("HeLLo World! 123"), "HELLO WORLD! 123");
    EXPECT_EQ(ToLowerUVE(""), "");
}

} // namespace
} // namespace UVE::Strings::Tests
