// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <memory>

namespace prism {
class PropertyGrid;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;

/// Keyboard Shortcuts, drawn with PRISM in a window of its own: every editor command by category,
/// with its shortcut and its alternate. Click a slot and press the new keys (Escape keeps the old
/// ones); a slot another command also uses is drawn in the warning colour; Reset brings a command's
/// own defaults back. Kept with the editor preferences.
class EditorShortcutsWindow final {
public:
    EditorShortcutsWindow(Editor& editor, EditorHost& host);
    ~EditorShortcutsWindow();
    EditorShortcutsWindow(const EditorShortcutsWindow&) = delete;
    EditorShortcutsWindow& operator=(const EditorShortcutsWindow&) = delete;

    void Frame();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    void Rebuild();

    Editor& m_editor;
    EditorHost& m_host;
    int m_window = 0;
    bool m_closePending = false;
    bool m_dirty = false;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::PropertyGrid> m_rows;
};

} // namespace UVE::Editor
