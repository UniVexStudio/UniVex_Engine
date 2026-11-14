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

class EditorUVE;
class EditorHostUVE;

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
class EditorSettingsWindowUVE final {
public:
    struct SourceUVE final {
        std::string title;
        const Config::SettingsRegistryUVE* registry = nullptr;
        std::function<std::optional<Config::SettingValueUVE>(std::string_view id)> get;
        std::function<bool(std::string_view id, const Config::SettingValueUVE& value)> set;
        /// Under the rows: where the changes go.
        std::function<std::string()> note;
        /// A Save button when set (and enabled while `dirty`).
        std::function<bool()> save;
        std::function<bool()> dirty;
    };

    /// `visible` is the editor's flag for this window (opened by its command, cleared on close).
    EditorSettingsWindowUVE(EditorUVE& editor, EditorHostUVE& host, bool& visible, std::function<SourceUVE()> source);
    ~EditorSettingsWindowUVE();
    EditorSettingsWindowUVE(const EditorSettingsWindowUVE&) = delete;
    EditorSettingsWindowUVE& operator=(const EditorSettingsWindowUVE&) = delete;

    /// Opens, draws and closes the window with the editor's flag. Main context current, between frames.
    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void RebuildTreeUVE();
    void RebuildRowsUVE();
    [[nodiscard]] prism::Ref<prism::Widget> EditorForUVE(const Config::SettingDescriptorUVE& descriptor,
                                                         const Config::SettingValueUVE& value);
    void SetUVE(const std::string& id, const Config::SettingValueUVE& value);
    void CloseUVE();

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    bool& m_visible;
    std::function<SourceUVE()> m_sourceOf;
    SourceUVE m_source;
    int m_window = 0;
    bool m_closePending = false;
    bool m_rowsDirty = false;
    std::string m_category;
    std::vector<std::string> m_categoryPaths;   ///< tree node id - 1 -> category path
    std::unordered_map<std::string, Config::SettingValueUVE> m_restartBaseline;
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
