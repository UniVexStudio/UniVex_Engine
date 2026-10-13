// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include <algorithm>
#include <array>
#include <cctype>
#include <string>
#include <utility>

#include <imgui.h>

#include "uve/editor/editor_commands_uve.h"

namespace UVE::Editor {
namespace {

/// The keys a shortcut may use, with the names they are shown and stored by. Dear ImGui's own
/// names are for debugging only and may change; these do not.
struct ShortcutKeyNameUVE final {
    ImGuiKey key;
    std::string_view name;
};

constexpr std::array kShortcutKeysUVE{
    ShortcutKeyNameUVE{ImGuiKey_A, "A"},           ShortcutKeyNameUVE{ImGuiKey_B, "B"},
    ShortcutKeyNameUVE{ImGuiKey_C, "C"},           ShortcutKeyNameUVE{ImGuiKey_D, "D"},
    ShortcutKeyNameUVE{ImGuiKey_E, "E"},           ShortcutKeyNameUVE{ImGuiKey_F, "F"},
    ShortcutKeyNameUVE{ImGuiKey_G, "G"},           ShortcutKeyNameUVE{ImGuiKey_H, "H"},
    ShortcutKeyNameUVE{ImGuiKey_I, "I"},           ShortcutKeyNameUVE{ImGuiKey_J, "J"},
    ShortcutKeyNameUVE{ImGuiKey_K, "K"},           ShortcutKeyNameUVE{ImGuiKey_L, "L"},
    ShortcutKeyNameUVE{ImGuiKey_M, "M"},           ShortcutKeyNameUVE{ImGuiKey_N, "N"},
    ShortcutKeyNameUVE{ImGuiKey_O, "O"},           ShortcutKeyNameUVE{ImGuiKey_P, "P"},
    ShortcutKeyNameUVE{ImGuiKey_Q, "Q"},           ShortcutKeyNameUVE{ImGuiKey_R, "R"},
    ShortcutKeyNameUVE{ImGuiKey_S, "S"},           ShortcutKeyNameUVE{ImGuiKey_T, "T"},
    ShortcutKeyNameUVE{ImGuiKey_U, "U"},           ShortcutKeyNameUVE{ImGuiKey_V, "V"},
    ShortcutKeyNameUVE{ImGuiKey_W, "W"},           ShortcutKeyNameUVE{ImGuiKey_X, "X"},
    ShortcutKeyNameUVE{ImGuiKey_Y, "Y"},           ShortcutKeyNameUVE{ImGuiKey_Z, "Z"},
    ShortcutKeyNameUVE{ImGuiKey_0, "0"},           ShortcutKeyNameUVE{ImGuiKey_1, "1"},
    ShortcutKeyNameUVE{ImGuiKey_2, "2"},           ShortcutKeyNameUVE{ImGuiKey_3, "3"},
    ShortcutKeyNameUVE{ImGuiKey_4, "4"},           ShortcutKeyNameUVE{ImGuiKey_5, "5"},
    ShortcutKeyNameUVE{ImGuiKey_6, "6"},           ShortcutKeyNameUVE{ImGuiKey_7, "7"},
    ShortcutKeyNameUVE{ImGuiKey_8, "8"},           ShortcutKeyNameUVE{ImGuiKey_9, "9"},
    ShortcutKeyNameUVE{ImGuiKey_F1, "F1"},         ShortcutKeyNameUVE{ImGuiKey_F2, "F2"},
    ShortcutKeyNameUVE{ImGuiKey_F3, "F3"},         ShortcutKeyNameUVE{ImGuiKey_F4, "F4"},
    ShortcutKeyNameUVE{ImGuiKey_F5, "F5"},         ShortcutKeyNameUVE{ImGuiKey_F6, "F6"},
    ShortcutKeyNameUVE{ImGuiKey_F7, "F7"},         ShortcutKeyNameUVE{ImGuiKey_F8, "F8"},
    ShortcutKeyNameUVE{ImGuiKey_F9, "F9"},         ShortcutKeyNameUVE{ImGuiKey_F10, "F10"},
    ShortcutKeyNameUVE{ImGuiKey_F11, "F11"},       ShortcutKeyNameUVE{ImGuiKey_F12, "F12"},
    ShortcutKeyNameUVE{ImGuiKey_LeftArrow, "Left"}, ShortcutKeyNameUVE{ImGuiKey_RightArrow, "Right"},
    ShortcutKeyNameUVE{ImGuiKey_UpArrow, "Up"},    ShortcutKeyNameUVE{ImGuiKey_DownArrow, "Down"},
    ShortcutKeyNameUVE{ImGuiKey_Delete, "Delete"}, ShortcutKeyNameUVE{ImGuiKey_Backspace, "Backspace"},
    ShortcutKeyNameUVE{ImGuiKey_Insert, "Insert"}, ShortcutKeyNameUVE{ImGuiKey_Home, "Home"},
    ShortcutKeyNameUVE{ImGuiKey_End, "End"},       ShortcutKeyNameUVE{ImGuiKey_PageUp, "PageUp"},
    ShortcutKeyNameUVE{ImGuiKey_PageDown, "PageDown"}, ShortcutKeyNameUVE{ImGuiKey_Space, "Space"},
    ShortcutKeyNameUVE{ImGuiKey_Enter, "Enter"},   ShortcutKeyNameUVE{ImGuiKey_Tab, "Tab"},
    ShortcutKeyNameUVE{ImGuiKey_Comma, "Comma"},   ShortcutKeyNameUVE{ImGuiKey_Period, "Period"},
    ShortcutKeyNameUVE{ImGuiKey_Slash, "Slash"},   ShortcutKeyNameUVE{ImGuiKey_Minus, "Minus"},
    ShortcutKeyNameUVE{ImGuiKey_Equal, "Equal"},   ShortcutKeyNameUVE{ImGuiKey_LeftBracket, "LeftBracket"},
    ShortcutKeyNameUVE{ImGuiKey_RightBracket, "RightBracket"},
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
