// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <memory>

namespace prism {
class PropertyGrid;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorHostUVE;

/// Keyboard Shortcuts, drawn with PRISM in a window of its own: every editor command by category,
/// with its shortcut and its alternate. Click a slot and press the new keys (Escape keeps the old
/// ones); a slot another command also uses is drawn in the warning colour; Reset brings a command's
/// own defaults back. Kept with the editor preferences.
class EditorShortcutsWindowUVE final {
public:
    EditorShortcutsWindowUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorShortcutsWindowUVE();
    EditorShortcutsWindowUVE(const EditorShortcutsWindowUVE&) = delete;
    EditorShortcutsWindowUVE& operator=(const EditorShortcutsWindowUVE&) = delete;

    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void RebuildUVE();

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    int m_window = 0;
    bool m_closePending = false;
    bool m_dirty = false;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::PropertyGrid> m_rows;
};

} // namespace UVE::Editor
