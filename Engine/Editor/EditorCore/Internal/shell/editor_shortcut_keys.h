// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_commands.h"

#include "prism/Core.h"

#include <cstdint>
#include <optional>
#include <string>

namespace UVE::Editor {

/// The editor shortcut a PRISM key press makes ("Ctrl+S"): its key by the editor's stable name and
/// its modifiers. Nothing for a key no shortcut may use (Escape, a modifier alone).
[[nodiscard]] inline std::optional<EditorShortcutUVE> ShortcutFromPrismKeyUVE(const prism::Key key, const std::uint8_t mods) {
    std::string name;
    const int k = static_cast<int>(key);
    if (key >= prism::Key::A && key <= prism::Key::Z) {
        name = std::string(1, static_cast<char>('A' + (k - static_cast<int>(prism::Key::A))));
    } else if (key >= prism::Key::N0 && key <= prism::Key::N9) {
        name = std::string(1, static_cast<char>('0' + (k - static_cast<int>(prism::Key::N0))));
    } else if (key >= prism::Key::F1 && key <= prism::Key::F12) {
        name = "F" + std::to_string(1 + (k - static_cast<int>(prism::Key::F1)));
    } else {
        switch (key) {
            case prism::Key::Delete: name = "Delete"; break;
            case prism::Key::Backspace: name = "Backspace"; break;
            case prism::Key::Home: name = "Home"; break;
            case prism::Key::End: name = "End"; break;
            case prism::Key::PageUp: name = "PageUp"; break;
            case prism::Key::PageDown: name = "PageDown"; break;
            case prism::Key::Space: name = "Space"; break;
            case prism::Key::Enter: name = "Enter"; break;
            case prism::Key::Tab: name = "Tab"; break;
            case prism::Key::Left: name = "Left"; break;
            case prism::Key::Right: name = "Right"; break;
            case prism::Key::Up: name = "Up"; break;
            case prism::Key::Down: name = "Down"; break;
            default: return std::nullopt;
        }
    }
    std::string text;
    text += (mods & prism::ModCtrl) != 0 ? "Ctrl+" : "";
    text += (mods & prism::ModShift) != 0 ? "Shift+" : "";
    text += (mods & prism::ModAlt) != 0 ? "Alt+" : "";
    text += name;
    return ParseEditorShortcutUVE(text);
}

} // namespace UVE::Editor
