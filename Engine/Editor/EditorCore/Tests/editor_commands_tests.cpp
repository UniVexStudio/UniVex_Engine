// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "editor_commands.h"

#include <gtest/gtest.h>

namespace UVE::Editor::Tests {
namespace {

TEST(EditorShortcutTest, FormatsAndParsesBackTheWayItIsWritten) {
    for (const char* text : {"Ctrl+Shift+Z", "F5", "Delete", "Ctrl+Comma", "Alt+Left", "Shift+F5", "Ctrl+S", "0"}) {
        const std::optional<EditorShortcut> shortcut = ParseEditorShortcut(text);
        ASSERT_TRUE(shortcut.has_value()) << text;
        EXPECT_FALSE(shortcut->IsEmpty()) << text;
        EXPECT_EQ(FormatEditorShortcut(*shortcut), text);
    }
    // Modifiers in another order still parse, and format in the one canonical order.
    const std::optional<EditorShortcut> reordered = ParseEditorShortcut("Shift+Ctrl+Z");
    ASSERT_TRUE(reordered.has_value());
    EXPECT_EQ(FormatEditorShortcut(*reordered), "Ctrl+Shift+Z");
}

TEST(EditorShortcutTest, EmptyIsNoShortcutAndMalformedIsNothing) {
    const std::optional<EditorShortcut> none = ParseEditorShortcut("");
    ASSERT_TRUE(none.has_value());
    EXPECT_TRUE(none->IsEmpty());
    EXPECT_EQ(FormatEditorShortcut(EditorShortcut{}), "");
    for (const char* text : {"Ctrl+", "Ctrl+Ctrl+Z", "Shift", "Hyper+Z", "Pause", "ctrl+z", "Ctrl++"}) {
        EXPECT_FALSE(ParseEditorShortcut(text).has_value()) << text;
    }
}

TEST(EditorShortcutTest, PunctuationIsShownAsItselfButStoredByName) {
    const std::optional<EditorShortcut> comma = ParseEditorShortcut("Ctrl+Comma");
    ASSERT_TRUE(comma.has_value());
    EXPECT_EQ(DisplayEditorShortcut(*comma), "Ctrl+,");
    EXPECT_EQ(FormatEditorShortcut(*comma), "Ctrl+Comma");
    EXPECT_EQ(DisplayEditorShortcut(*ParseEditorShortcut("RightBracket")), "]");
    EXPECT_EQ(DisplayEditorShortcut(*ParseEditorShortcut("Shift+F5")), "Shift+F5");
}

TEST(EditorShortcutTest, ModifiersAloneAreNotShortcutKeys) {
    EXPECT_TRUE(IsShortcutKey(ParseEditorShortcut("Z")->key));
    EXPECT_FALSE(IsShortcutKey(0));
    EXPECT_FALSE(IsShortcutKey(1));
}

TEST(EditorFuzzyMatchTest, PrefixBeatsWordBeatsInsideBeatsScattered) {
    EXPECT_EQ(ScoreFuzzyMatch("Anything", ""), 0);
    const int prefix = ScoreFuzzyMatch("Save Scene", "save");
    const int word = ScoreFuzzyMatch("File: Save Scene", "save");
    const int inside = ScoreFuzzyMatch("Autosave", "save");
    const int scattered = ScoreFuzzyMatch("Save Scene", "ssn");
    EXPECT_GT(prefix, word);
    EXPECT_GT(word, inside);
    EXPECT_GT(inside, scattered);
    EXPECT_GE(scattered, 0);
    EXPECT_EQ(ScoreFuzzyMatch("Save Scene", "xyz"), -1);
    EXPECT_GE(ScoreFuzzyMatch("Editor Preferences", "ED PREF"), 0);
}

} // namespace
} // namespace UVE::Editor::Tests
