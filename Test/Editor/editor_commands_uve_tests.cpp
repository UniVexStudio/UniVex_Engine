// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/editor_commands_uve.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

TEST(EditorShortcutUVETest, FormatsAndParsesBackTheWayItIsWritten) {
    for (const char* text : {"Ctrl+Shift+Z", "F5", "Delete", "Ctrl+Comma", "Alt+Left", "Shift+F5", "Ctrl+S", "0"}) {
        const std::optional<EditorShortcutUVE> shortcut = ParseEditorShortcutUVE(text);
        ASSERT_TRUE(shortcut.has_value()) << text;
        EXPECT_FALSE(shortcut->IsEmptyUVE()) << text;
        EXPECT_EQ(FormatEditorShortcutUVE(*shortcut), text);
    }
    // Modifiers in another order still parse, and format in the one canonical order.
    const std::optional<EditorShortcutUVE> reordered = ParseEditorShortcutUVE("Shift+Ctrl+Z");
    ASSERT_TRUE(reordered.has_value());
    EXPECT_EQ(FormatEditorShortcutUVE(*reordered), "Ctrl+Shift+Z");
}

TEST(EditorShortcutUVETest, EmptyIsNoShortcutAndMalformedIsNothing) {
    const std::optional<EditorShortcutUVE> none = ParseEditorShortcutUVE("");
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->IsEmptyUVE());
    EXPECT_EQ(FormatEditorShortcutUVE(EditorShortcutUVE{}), "");
    for (const char* text : {"Ctrl+", "Ctrl+Ctrl+Z", "Shift", "Hyper+Z", "Pause", "ctrl+z", "Ctrl++"}) {
        EXPECT_FALSE(ParseEditorShortcutUVE(text).has_value()) << text;
    }
}

TEST(EditorShortcutUVETest, PunctuationIsShownAsItselfButStoredByName) {
    const std::optional<EditorShortcutUVE> comma = ParseEditorShortcutUVE("Ctrl+Comma");
    ASSERT_TRUE(comma.has_value());
    EXPECT_EQ(DisplayEditorShortcutUVE(*comma), "Ctrl+,");
    EXPECT_EQ(FormatEditorShortcutUVE(*comma), "Ctrl+Comma");
    EXPECT_EQ(DisplayEditorShortcutUVE(*ParseEditorShortcutUVE("RightBracket")), "]");
    EXPECT_EQ(DisplayEditorShortcutUVE(*ParseEditorShortcutUVE("Shift+F5")), "Shift+F5");
}

TEST(EditorShortcutUVETest, ModifiersAloneAreNotShortcutKeys) {
    EXPECT_TRUE(IsShortcutKeyUVE(ParseEditorShortcutUVE("Z")->key));
    EXPECT_FALSE(IsShortcutKeyUVE(0));
    EXPECT_FALSE(IsShortcutKeyUVE(1));
}

TEST(EditorFuzzyMatchUVETest, PrefixBeatsWordBeatsInsideBeatsScattered) {
    EXPECT_EQ(ScoreFuzzyMatchUVE("Anything", ""), 0);
    const int prefix = ScoreFuzzyMatchUVE("Save Scene", "save");
    const int word = ScoreFuzzyMatchUVE("File: Save Scene", "save");
    const int inside = ScoreFuzzyMatchUVE("Autosave", "save");
    const int scattered = ScoreFuzzyMatchUVE("Save Scene", "ssn");
    EXPECT_GT(prefix, word);
    EXPECT_GT(word, inside);
    EXPECT_GT(inside, scattered);
    EXPECT_GE(scattered, 0);
    EXPECT_EQ(ScoreFuzzyMatchUVE("Save Scene", "xyz"), -1);
    EXPECT_GE(ScoreFuzzyMatchUVE("Editor Preferences", "ED PREF"), 0);
}

} // namespace
} // namespace UVE::Editor::Tests
