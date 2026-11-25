// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "setting_descriptor.h"
#include "settings_registry.h"

#include "prism/Widget.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace prism {
class Button;
class CheckBox;
class Label;
class PropertyGrid;
class TextInput;
class TreeView;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;

/// A settings window drawn with PRISM in a window of its own: the Editor Preferences, or the
/// project's Project Settings. Every visible setting of a registry, drawn from its descriptor, so a
/// newly declared setting appears in the right category with the right control:
///
///   [Editor Preferences                                        - [] x]
///   [ search                         [ ] Modified only  [ ] Advanced ]
///   [ All Settings  | Viewport / Grid                                ]
///   [  Viewport     |   Opacity            [ 0.6        ]  (reset)   ]
///   [   Grid        |   ...                                          ]
///   [ note                                          [Save]  [Close]  ]
///
/// Changes apply as they are made (`set`); `save` (Project Settings) writes the file.
class EditorSettingsWindow final {
public:
    struct Source final {
        std::string title;
        const Config::SettingsRegistry* registry = nullptr;
        std::function<std::optional<Config::SettingValue>(std::string_view id)> get;
        std::function<bool(std::string_view id, const Config::SettingValue& value)> set;
        /// Under the rows: where the changes go.
        std::function<std::string()> note;
        /// A Save button when set (and enabled while `dirty`).
        std::function<bool()> save;
        std::function<bool()> dirty;
    };

    /// `visible` is the editor's flag for this window (opened by its command, cleared on close).
    EditorSettingsWindow(Editor& editor, EditorHost& host, bool& visible, std::function<Source()> source,
                            bool* focusPending = nullptr);
    ~EditorSettingsWindow();
    EditorSettingsWindow(const EditorSettingsWindow&) = delete;
    EditorSettingsWindow& operator=(const EditorSettingsWindow&) = delete;

    /// Opens, draws and closes the window with the editor's flag. Main context current, between frames.
    void Frame();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    void RebuildTree();
    void RebuildRows();
    [[nodiscard]] prism::Ref<prism::Widget> EditorFor(const Config::SettingDescriptor& descriptor,
                                                         const Config::SettingValue& value);
    void Set(const std::string& id, const Config::SettingValue& value);
    void Close();

    Editor& m_editor;
    EditorHost& m_host;
    bool& m_visible;
    bool* m_focusPending = nullptr;
    std::function<Source()> m_sourceOf;
    Source m_source;
    int m_window = 0;
    bool m_closePending = false;
    bool m_rowsDirty = false;
    std::string m_category;
    std::vector<std::string> m_categoryPaths;   ///< tree node id - 1 -> category path
    std::unordered_map<std::string, Config::SettingValue> m_restartBaseline;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::CheckBox> m_modifiedOnly;
    std::shared_ptr<prism::CheckBox> m_advanced;
    std::shared_ptr<prism::TreeView> m_tree;
    std::shared_ptr<prism::PropertyGrid> m_rows;
    std::shared_ptr<prism::Label> m_restart;
    std::shared_ptr<prism::Label> m_note;
    std::shared_ptr<prism::Button> m_save;
};

} // namespace UVE::Editor
