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
struct ShortcutKeyName final {
    EditorKey key;
    std::string_view name;
};

constexpr std::array kShortcutKeys{
    ShortcutKeyName{EditorKey::A, "A"},           ShortcutKeyName{EditorKey::B, "B"},
    ShortcutKeyName{EditorKey::C, "C"},           ShortcutKeyName{EditorKey::D, "D"},
    ShortcutKeyName{EditorKey::E, "E"},           ShortcutKeyName{EditorKey::F, "F"},
    ShortcutKeyName{EditorKey::G, "G"},           ShortcutKeyName{EditorKey::H, "H"},
    ShortcutKeyName{EditorKey::I, "I"},           ShortcutKeyName{EditorKey::J, "J"},
    ShortcutKeyName{EditorKey::K, "K"},           ShortcutKeyName{EditorKey::L, "L"},
    ShortcutKeyName{EditorKey::M, "M"},           ShortcutKeyName{EditorKey::N, "N"},
    ShortcutKeyName{EditorKey::O, "O"},           ShortcutKeyName{EditorKey::P, "P"},
    ShortcutKeyName{EditorKey::Q, "Q"},           ShortcutKeyName{EditorKey::R, "R"},
    ShortcutKeyName{EditorKey::S, "S"},           ShortcutKeyName{EditorKey::T, "T"},
    ShortcutKeyName{EditorKey::U, "U"},           ShortcutKeyName{EditorKey::V, "V"},
    ShortcutKeyName{EditorKey::W, "W"},           ShortcutKeyName{EditorKey::X, "X"},
    ShortcutKeyName{EditorKey::Y, "Y"},           ShortcutKeyName{EditorKey::Z, "Z"},
    ShortcutKeyName{EditorKey::Digit0, "0"},           ShortcutKeyName{EditorKey::Digit1, "1"},
    ShortcutKeyName{EditorKey::Digit2, "2"},           ShortcutKeyName{EditorKey::Digit3, "3"},
    ShortcutKeyName{EditorKey::Digit4, "4"},           ShortcutKeyName{EditorKey::Digit5, "5"},
    ShortcutKeyName{EditorKey::Digit6, "6"},           ShortcutKeyName{EditorKey::Digit7, "7"},
    ShortcutKeyName{EditorKey::Digit8, "8"},           ShortcutKeyName{EditorKey::Digit9, "9"},
    ShortcutKeyName{EditorKey::F1, "F1"},         ShortcutKeyName{EditorKey::F2, "F2"},
    ShortcutKeyName{EditorKey::F3, "F3"},         ShortcutKeyName{EditorKey::F4, "F4"},
    ShortcutKeyName{EditorKey::F5, "F5"},         ShortcutKeyName{EditorKey::F6, "F6"},
    ShortcutKeyName{EditorKey::F7, "F7"},         ShortcutKeyName{EditorKey::F8, "F8"},
    ShortcutKeyName{EditorKey::F9, "F9"},         ShortcutKeyName{EditorKey::F10, "F10"},
    ShortcutKeyName{EditorKey::F11, "F11"},       ShortcutKeyName{EditorKey::F12, "F12"},
    ShortcutKeyName{EditorKey::Left, "Left"}, ShortcutKeyName{EditorKey::Right, "Right"},
    ShortcutKeyName{EditorKey::Up, "Up"},    ShortcutKeyName{EditorKey::Down, "Down"},
    ShortcutKeyName{EditorKey::Delete, "Delete"}, ShortcutKeyName{EditorKey::Backspace, "Backspace"},
    ShortcutKeyName{EditorKey::Insert, "Insert"}, ShortcutKeyName{EditorKey::Home, "Home"},
    ShortcutKeyName{EditorKey::End, "End"},       ShortcutKeyName{EditorKey::PageUp, "PageUp"},
    ShortcutKeyName{EditorKey::PageDown, "PageDown"}, ShortcutKeyName{EditorKey::Space, "Space"},
    ShortcutKeyName{EditorKey::Enter, "Enter"},   ShortcutKeyName{EditorKey::Tab, "Tab"},
    ShortcutKeyName{EditorKey::Comma, "Comma"},   ShortcutKeyName{EditorKey::Period, "Period"},
    ShortcutKeyName{EditorKey::Slash, "Slash"},   ShortcutKeyName{EditorKey::Minus, "Minus"},
    ShortcutKeyName{EditorKey::Equal, "Equal"},   ShortcutKeyName{EditorKey::LeftBracket, "LeftBracket"},
    ShortcutKeyName{EditorKey::RightBracket, "RightBracket"},
};

[[nodiscard]] char Lower(const char c) noexcept {
    return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
}

} // namespace

bool IsShortcutKey(const int key) noexcept {
    return std::any_of(kShortcutKeys.begin(), kShortcutKeys.end(),
                       [key](const ShortcutKeyName& entry) { return static_cast<int>(entry.key) == key; });
}

std::string FormatEditorShortcut(const EditorShortcut& shortcut) {
    const auto entry = std::find_if(kShortcutKeys.begin(), kShortcutKeys.end(), [&](const ShortcutKeyName& e) {
        return static_cast<int>(e.key) == shortcut.key;
    });
    if (entry == kShortcutKeys.end()) {
        return {};
    }
    std::string text;
    text += shortcut.ctrl ? "Ctrl+" : "";
    text += shortcut.shift ? "Shift+" : "";
    text += shortcut.alt ? "Alt+" : "";
    text += entry->name;
    return text;
}

std::string DisplayEditorShortcut(const EditorShortcut& shortcut) {
    std::string text = FormatEditorShortcut(shortcut);
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 7> kPunctuation{{
        {"Comma", ","}, {"Period", "."}, {"Slash", "/"}, {"Minus", "-"}, {"Equal", "="},
        {"LeftBracket", "["}, {"RightBracket", "]"}}};
    for (const auto& [name, symbol] : kPunctuation) {
        if (text.ends_with(name) && (text.size() == name.size() || text[text.size() - name.size() - 1U] == '+')) {
            text.replace(text.size() - name.size(), name.size(), symbol);
            break;
        }
    }
    return text;
}

std::optional<EditorShortcut> ParseEditorShortcut(const std::string_view text) {
    EditorShortcut shortcut;
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
    const auto entry = std::find_if(kShortcutKeys.begin(), kShortcutKeys.end(),
                                    [rest](const ShortcutKeyName& e) { return e.name == rest; });
    if (entry == kShortcutKeys.end()) {
        return std::nullopt;
    }
    shortcut.key = static_cast<int>(entry->key);
    return shortcut;
}

int ScoreFuzzyMatch(const std::string_view text, const std::string_view query) {
    std::string needle;
    for (const char c : query) {
        if (c != ' ') {
            needle.push_back(Lower(c));
        }
    }
    if (needle.empty()) {
        return 0;
    }
    std::string haystack;
    haystack.reserve(text.size());
    for (const char c : text) {
        haystack.push_back(Lower(c));
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
