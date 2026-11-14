// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

#include "editor_commands.h"

namespace UVE::Editor {
namespace {

/// The keys a shortcut may use, with the names they are shown and stored by.
struct ShortcutKeyNameUVE final {
    EditorKeyUVE key;
    std::string_view name;
};

constexpr std::array kShortcutKeysUVE{
    ShortcutKeyNameUVE{EditorKeyUVE::A, "A"},           ShortcutKeyNameUVE{EditorKeyUVE::B, "B"},
    ShortcutKeyNameUVE{EditorKeyUVE::C, "C"},           ShortcutKeyNameUVE{EditorKeyUVE::D, "D"},
    ShortcutKeyNameUVE{EditorKeyUVE::E, "E"},           ShortcutKeyNameUVE{EditorKeyUVE::F, "F"},
    ShortcutKeyNameUVE{EditorKeyUVE::G, "G"},           ShortcutKeyNameUVE{EditorKeyUVE::H, "H"},
    ShortcutKeyNameUVE{EditorKeyUVE::I, "I"},           ShortcutKeyNameUVE{EditorKeyUVE::J, "J"},
    ShortcutKeyNameUVE{EditorKeyUVE::K, "K"},           ShortcutKeyNameUVE{EditorKeyUVE::L, "L"},
    ShortcutKeyNameUVE{EditorKeyUVE::M, "M"},           ShortcutKeyNameUVE{EditorKeyUVE::N, "N"},
    ShortcutKeyNameUVE{EditorKeyUVE::O, "O"},           ShortcutKeyNameUVE{EditorKeyUVE::P, "P"},
    ShortcutKeyNameUVE{EditorKeyUVE::Q, "Q"},           ShortcutKeyNameUVE{EditorKeyUVE::R, "R"},
    ShortcutKeyNameUVE{EditorKeyUVE::S, "S"},           ShortcutKeyNameUVE{EditorKeyUVE::T, "T"},
    ShortcutKeyNameUVE{EditorKeyUVE::U, "U"},           ShortcutKeyNameUVE{EditorKeyUVE::V, "V"},
    ShortcutKeyNameUVE{EditorKeyUVE::W, "W"},           ShortcutKeyNameUVE{EditorKeyUVE::X, "X"},
    ShortcutKeyNameUVE{EditorKeyUVE::Y, "Y"},           ShortcutKeyNameUVE{EditorKeyUVE::Z, "Z"},
    ShortcutKeyNameUVE{EditorKeyUVE::Digit0, "0"},           ShortcutKeyNameUVE{EditorKeyUVE::Digit1, "1"},
    ShortcutKeyNameUVE{EditorKeyUVE::Digit2, "2"},           ShortcutKeyNameUVE{EditorKeyUVE::Digit3, "3"},
    ShortcutKeyNameUVE{EditorKeyUVE::Digit4, "4"},           ShortcutKeyNameUVE{EditorKeyUVE::Digit5, "5"},
    ShortcutKeyNameUVE{EditorKeyUVE::Digit6, "6"},           ShortcutKeyNameUVE{EditorKeyUVE::Digit7, "7"},
    ShortcutKeyNameUVE{EditorKeyUVE::Digit8, "8"},           ShortcutKeyNameUVE{EditorKeyUVE::Digit9, "9"},
    ShortcutKeyNameUVE{EditorKeyUVE::F1, "F1"},         ShortcutKeyNameUVE{EditorKeyUVE::F2, "F2"},
    ShortcutKeyNameUVE{EditorKeyUVE::F3, "F3"},         ShortcutKeyNameUVE{EditorKeyUVE::F4, "F4"},
    ShortcutKeyNameUVE{EditorKeyUVE::F5, "F5"},         ShortcutKeyNameUVE{EditorKeyUVE::F6, "F6"},
    ShortcutKeyNameUVE{EditorKeyUVE::F7, "F7"},         ShortcutKeyNameUVE{EditorKeyUVE::F8, "F8"},
    ShortcutKeyNameUVE{EditorKeyUVE::F9, "F9"},         ShortcutKeyNameUVE{EditorKeyUVE::F10, "F10"},
    ShortcutKeyNameUVE{EditorKeyUVE::F11, "F11"},       ShortcutKeyNameUVE{EditorKeyUVE::F12, "F12"},
    ShortcutKeyNameUVE{EditorKeyUVE::Left, "Left"}, ShortcutKeyNameUVE{EditorKeyUVE::Right, "Right"},
    ShortcutKeyNameUVE{EditorKeyUVE::Up, "Up"},    ShortcutKeyNameUVE{EditorKeyUVE::Down, "Down"},
    ShortcutKeyNameUVE{EditorKeyUVE::Delete, "Delete"}, ShortcutKeyNameUVE{EditorKeyUVE::Backspace, "Backspace"},
    ShortcutKeyNameUVE{EditorKeyUVE::Insert, "Insert"}, ShortcutKeyNameUVE{EditorKeyUVE::Home, "Home"},
    ShortcutKeyNameUVE{EditorKeyUVE::End, "End"},       ShortcutKeyNameUVE{EditorKeyUVE::PageUp, "PageUp"},
    ShortcutKeyNameUVE{EditorKeyUVE::PageDown, "PageDown"}, ShortcutKeyNameUVE{EditorKeyUVE::Space, "Space"},
    ShortcutKeyNameUVE{EditorKeyUVE::Enter, "Enter"},   ShortcutKeyNameUVE{EditorKeyUVE::Tab, "Tab"},
    ShortcutKeyNameUVE{EditorKeyUVE::Comma, "Comma"},   ShortcutKeyNameUVE{EditorKeyUVE::Period, "Period"},
    ShortcutKeyNameUVE{EditorKeyUVE::Slash, "Slash"},   ShortcutKeyNameUVE{EditorKeyUVE::Minus, "Minus"},
    ShortcutKeyNameUVE{EditorKeyUVE::Equal, "Equal"},   ShortcutKeyNameUVE{EditorKeyUVE::LeftBracket, "LeftBracket"},
    ShortcutKeyNameUVE{EditorKeyUVE::RightBracket, "RightBracket"},
};

[[nodiscard]] char LowerUVE(const char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

bool IsShortcutKeyUVE(const int key) noexcept {
    return std::any_of(kShortcutKeysUVE.begin(), kShortcutKeysUVE.end(),
                       [key](const ShortcutKeyNameUVE& entry) { return static_cast<int>(entry.key) == key; });
}

std::string FormatEditorShortcutUVE(const EditorShortcutUVE& shortcut) {
    const auto entry = std::find_if(kShortcutKeysUVE.begin(), kShortcutKeysUVE.end(), [&](const ShortcutKeyNameUVE& e) {
        return static_cast<int>(e.key) == shortcut.key;
    });
    if (entry == kShortcutKeysUVE.end()) {
        return {};
    }
    std::string text;
    text += shortcut.ctrl ? "Ctrl+" : "";
    text += shortcut.shift ? "Shift+" : "";
    text += shortcut.alt ? "Alt+" : "";
    text += entry->name;
    return text;
}

std::string DisplayEditorShortcutUVE(const EditorShortcutUVE& shortcut) {
    std::string text = FormatEditorShortcutUVE(shortcut);
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kPunctuationUVE{{
        {"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Minus", "-"}, {"Equal", "="},
        {"LeftBracket", "["}, {"RightBracket", "]"}}};
    for (const auto& [name, symbol] : kPunctuationUVE) {
        if (text.ends_with(name) && (text.size() == name.size() || text[text.size() - name.size() - 1U] == '+')) {
            text.replace(text.size() - name.size(), name.size(), symbol);
            break;
        }
    }
    return text;
}

std::optional<EditorShortcutUVE> ParseEditorShortcutUVE(const std::string_view text) {
    EditorShortcutUVE shortcut;
    if (text.empty()) {
        return shortcut;
    }
    std::string_view rest = text;
    // Modifiers first, each once, in any order; the last part is the key.
    for (std::size_t plus = rest.find('+'); plus != std::string_view::npos && plus + 1U < rest.size();
         plus = rest.find('+')) {
        const std::string_view modifier = rest.substr(0U, plus);
        bool* flag = modifier == "Ctrl" ? &shortcut.ctrl
                     : modifier == "Shift" ? &shortcut.shift
                     : modifier == "Alt"   ? &shortcut.alt
                                           : nullptr;
        if (flag == nullptr || *flag) {
            return std::nullopt;
        }
        *flag = true;
        rest.remove_prefix(plus + 1U);
    }
    const auto entry = std::find_if(kShortcutKeysUVE.begin(), kShortcutKeysUVE.end(),
                                    [rest](const ShortcutKeyNameUVE& e) { return e.name == rest; });
    if (entry == kShortcutKeysUVE.end()) {
        return std::nullopt;
    }
    shortcut.key = static_cast<int>(entry->key);
    return shortcut;
}

int ScoreFuzzyMatchUVE(const std::string_view text, const std::string_view query) {
    std::string needle;
    for (const char c : query) {
        if (c != ' ') {
            needle.push_back(LowerUVE(c));
        }
    }
    if (needle.empty()) {
        return 0;
    }
    std::string haystack;
    haystack.reserve(text.size());
    for (const char c : text) {
        haystack.push_back(LowerUVE(c));
    }
    if (haystack.starts_with(needle)) {
        return 300;
    }
    if (const std::size_t found = haystack.find(needle); found != std::string::npos) {
        // A match that starts a word ("save" in "Scene: Save") beats one inside a word.
        const bool wordStart = found == 0U || !std::isalnum(static_cast<unsigned char>(haystack[found - 1U]));
        return wordStart ? 200 : 150;
    }
    // Scattered: every character in order, scored by how tightly they sit together.
    std::size_t position = 0U;
    std::size_t first = std::string::npos;
    for (const char c : needle) {
        position = haystack.find(c, position);
        if (position == std::string::npos) {
            return -1;
        }
        first = first == std::string::npos ? position : first;
        ++position;
    }
    const std::size_t span = position - first;
    return 100 - static_cast<int>(std::min<std::size_t>(span - needle.size(), 99U));
}

} // namespace UVE::Editor
