// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_uvscript_highlight_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

using Kind = UVScriptTokenKindUVE;

TEST(UVScriptHighlightUVETest, ColoursEachKindOfWord) {
    const std::string_view line = "entity Player : Character3D";
    const std::vector<UVScriptTokenSpanUVE> spans = HighlightUVScriptLineUVE(line);
    ASSERT_EQ(spans.size(), 3U);
    EXPECT_EQ(spans[0], (UVScriptTokenSpanUVE{0U, 6U, Kind::Declaration}));
    EXPECT_EQ(spans[1], (UVScriptTokenSpanUVE{7U, 6U, Kind::Type}));
    EXPECT_EQ(spans[2], (UVScriptTokenSpanUVE{16U, 15U, Kind::Type}));
}

TEST(UVScriptHighlightUVETest, HandlerAndFunctionNamesAreDefinitions) {
    const auto on = HighlightUVScriptLineUVE("on body_entered(other):");
    ASSERT_EQ(on.size(), 2U);
    EXPECT_EQ(on[1], (UVScriptTokenSpanUVE{3U, 12U, Kind::Definition}));
    const auto fn = HighlightUVScriptLineUVE("fn jump(height: float) -> bool:");
    ASSERT_GE(fn.size(), 4U);
    EXPECT_EQ(fn[1].kind, Kind::Definition);
    EXPECT_EQ(fn[2], (UVScriptTokenSpanUVE{16U, 5U, Kind::Type}));
    EXPECT_EQ(fn[3].kind, Kind::Type); // bool
}

TEST(UVScriptHighlightUVETest, NumbersWithUnitsStringsCommentsAndLiterals) {
    const std::string_view line = "    wait 200ms  # \"not a string\"";
    const auto spans = HighlightUVScriptLineUVE(line);
    ASSERT_EQ(spans.size(), 3U);
    EXPECT_EQ(spans[0].kind, Kind::Keyword);
    EXPECT_EQ(spans[1], (UVScriptTokenSpanUVE{9U, 5U, Kind::Number}));
    EXPECT_EQ(spans[2], (UVScriptTokenSpanUVE{16U, line.size() - 16U, Kind::Comment}));

    const auto text = HighlightUVScriptLineUVE(R"(print("a \" # b") if true)");
    ASSERT_EQ(text.size(), 3U);
    EXPECT_EQ(text[0], (UVScriptTokenSpanUVE{6U, 10U, Kind::String})) << "an escaped quote and a '#' stay inside";
    EXPECT_EQ(text[1].kind, Kind::Keyword);
    EXPECT_EQ(text[2].kind, Kind::Literal);

    const auto open = HighlightUVScriptLineUVE("x = \"never closed");
    ASSERT_EQ(open.size(), 1U);
    EXPECT_EQ(open[0].start + open[0].length, 17U) << "an open string runs to the end of the line";
    EXPECT_TRUE(HighlightUVScriptLineUVE("speed = velocity").empty()) << "plain names are not listed";
}

TEST(UVScriptHighlightUVETest, NewLineKeepsTheIndentAndOpensABlockAfterAColon) {
    EXPECT_EQ(ComputeUVScriptNewLineIndentUVE("on ready:"), "    ");
    EXPECT_EQ(ComputeUVScriptNewLineIndentUVE("    if x > 1:  # big"), "        ");
    EXPECT_EQ(ComputeUVScriptNewLineIndentUVE("    speed = 3"), "    ");
    EXPECT_EQ(ComputeUVScriptNewLineIndentUVE("    print(\"a:\")"), "    ");
    EXPECT_EQ(ComputeUVScriptNewLineIndentUVE(""), "");
}

} // namespace
} // namespace UVE::Editor::Tests
