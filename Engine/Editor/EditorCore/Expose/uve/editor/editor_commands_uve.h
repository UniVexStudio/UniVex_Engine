// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

namespace UVE::Editor {

/// A key and its modifiers. `key` is a Dear ImGui key value, 0 for none; it is stored and shown
/// by the editor's own stable names (FormatEditorShortcutUVE), never by that value.
struct EditorShortcutUVE final {
    int key = 0;
    bool ctrl = false;
    bool shift = false;
    bool alt = false;

    [[nodiscard]] bool IsEmptyUVE() const noexcept { return key == 0; }
    [[nodiscard]] bool operator==(const EditorShortcutUVE&) const = default;
};

/// "Ctrl+Shift+Z", "F5", "Ctrl+Comma"; empty for no shortcut. The stored form: stable names.
[[nodiscard]] std::string FormatEditorShortcutUVE(const EditorShortcutUVE& shortcut);
/// The same shortcut as a person reads it on a menu: punctuation as itself, "Ctrl+,".
[[nodiscard]] std::string DisplayEditorShortcutUVE(const EditorShortcutUVE& shortcut);
/// The shortcut `text` names, in FormatEditorShortcutUVE's form; an empty text is no shortcut.
/// Nothing for a key it does not know, a repeated or unknown modifier, or modifiers alone.
[[nodiscard]] std::optional<EditorShortcutUVE> ParseEditorShortcutUVE(std::string_view text);
/// Whether `key` is one a shortcut may use: letters, digits, function keys, arrows and the named
/// editing keys - not a modifier on its own.
[[nodiscard]] bool IsShortcutKeyUVE(int key) noexcept;

/// How well `text` matches a command palette `query`: -1 for no match (the query's characters,
/// ignoring case and spaces, must all appear in order), higher for a better one - a prefix beats
/// a whole-word substring, which beats one scattered through the text.
[[nodiscard]] int ScoreFuzzyMatchUVE(std::string_view text, std::string_view query);

/// How the command palette reads a query. Fuzzy (the shipped default) treats the whole query as
/// one pattern, exactly like ScoreFuzzyMatchUVE. Words requires every whitespace-separated word
/// to match on its own, so "cop node" finds "Copy Node Path" even though the words are apart and
/// in a different order than the label would need.
enum class CommandPaletteMatchModeUVE {
    Fuzzy,
    Words,
};

/// How well a command matches a palette `query` under `mode`: -1 when it does not match at all,
/// otherwise a score where higher is a better match. A command is looked up by its `label` and by
/// its `title` ("Edit: Duplicate"), the two things the palette shows, and the better of the two
/// is what counts.
[[nodiscard]] int ScoreCommandPaletteMatchUVE(std::string_view label, std::string_view title,
                                              std::string_view query, CommandPaletteMatchModeUVE mode);

/// The palette's options, kept as editor settings (see RegisterEditorSettingsUVE).
struct CommandPaletteSettingsUVE final {
    /// Whether Ctrl+Shift+P and the File > Command Palette item open the palette at all. Off is
    /// for an author who lives in the menus: the command stays declared, and stays rebindable.
    bool enabled = true;
    CommandPaletteMatchModeUVE matchMode = CommandPaletteMatchModeUVE::Fuzzy;
    /// How many recently run commands are remembered and offered first; at most the maximum below.
    std::size_t recentCount = 8U;
};

/// The most a recentCount setting may ask for. Beyond a couple of dozen the list stops being a
/// shortcut and starts being a second, stale menu.
inline constexpr std::size_t kMaximumRecentCommandsUVE = 16U;

/// One thing the editor can do, found by the menu bar, the command palette and keyboard
/// shortcuts alike. Each has a primary and an alternate shortcut.
struct EditorCommandUVE final {
    std::string id;
    std::string label;
    std::string category;
    std::array<EditorShortcutUVE, 2> defaultShortcuts{};
    std::array<EditorShortcutUVE, 2> shortcuts{};
    std::function<bool()> isEnabled;
    std::function<void()> run;
    /// Whether its shortcut works while a text field has the keyboard. Only for commands with a
    /// modifier that no text field uses - the palette, Save - never for Undo, which a field needs.
    bool worksWhileTyping = false;
};

} // namespace UVE::Editor
