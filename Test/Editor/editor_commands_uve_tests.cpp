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

TEST(EditorCommandPaletteMatchUVETest, FuzzyReadsTheWholeQueryAsOnePattern) {
    const auto fuzzy = [](const std::string_view query) {
        return ScoreCommandPaletteMatchUVE("Copy Node Path", "Edit: Copy Node Path", query,
                                           CommandPaletteMatchModeUVE::Fuzzy);
    };
    EXPECT_EQ(fuzzy(""), 0); // nothing to match on leaves every command in the list
    EXPECT_GE(fuzzy("copy"), 0);
    EXPECT_EQ(fuzzy("zzz"), -1);
    // The characters of the query have to appear in order, so reversed words do not match.
    EXPECT_EQ(fuzzy("node copy"), -1);
}

TEST(EditorCommandPaletteMatchUVETest, WordsModeNeedsEveryWordAndAnyOrder) {
    const auto words = [](const std::string_view query) {
        return ScoreCommandPaletteMatchUVE("Copy Node Path", "Edit: Copy Node Path", query,
                                           CommandPaletteMatchModeUVE::Words);
    };
    EXPECT_EQ(words(""), 0);
    // Words need not sit together nor in the label's order.
    EXPECT_GT(words("node copy"), 0);
    EXPECT_EQ(words("copy node"), words("node copy"));
    // One word that matches nothing rejects the command, as one unmatched character does in fuzzy.
    EXPECT_EQ(words("copy zzz"), -1);
    // Leading, trailing and repeated spaces are not words.
    EXPECT_EQ(words("  copy   node  "), words("copy node"));
    // A one-word query is what fuzzy would have scored.
    EXPECT_EQ(words("copy"), ScoreCommandPaletteMatchUVE("Copy Node Path", "Edit: Copy Node Path",
                                                         "copy", CommandPaletteMatchModeUVE::Fuzzy));
}

TEST(EditorCommandPaletteSettingsUVETest, DefaultsOpenFuzzilyWithMemory) {
    const CommandPaletteSettingsUVE defaults{};
    EXPECT_TRUE(defaults.enabled);
    EXPECT_EQ(defaults.matchMode, CommandPaletteMatchModeUVE::Fuzzy);
    EXPECT_GT(defaults.recentCount, 0U);
    EXPECT_LE(defaults.recentCount, kMaximumRecentCommandsUVE);
}

} // namespace
} // namespace UVE::Editor::Tests
