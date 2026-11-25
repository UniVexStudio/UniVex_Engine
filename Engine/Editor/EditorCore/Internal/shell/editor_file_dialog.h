// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "prism/Widget.h"

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class Button;
class ComboBox;
class Label;
class TableView;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class EditorHost;

/// The editor's file window, drawn with PRISM in a window of its own: every Load... / Browse... /
/// Import... of the editor (Editor::OpenFilePicker) opens here.
///
///   [title                                               - [] x]
///   [ up | path                                      | search  ]
///   [ Places         | Name             Type     Size  Modified]
///   [  Content       | folders first, then the files of the    ]
///   [  Project       | kinds asked for                         ]
///   [  Home ...      |                                         ]
///   [ File: name                      | kind filter            ]
///   [ Import: Import into <Content folder>                     ]
///   [                                       [Load] [Cancel]    ]
///
/// Load picks a file where it is (anywhere on disk; the editor brings it into the project when it
/// must). Import copies the file into the chosen Content folder first and picks the copy.
class EditorFileDialog final {
public:
    EditorFileDialog(Editor& editor, EditorHost& host);
    ~EditorFileDialog();
    EditorFileDialog(const EditorFileDialog&) = delete;
    EditorFileDialog& operator=(const EditorFileDialog&) = delete;

    /// Opens the window for `request` (replacing one already open, whose pick never fires).
    void Open(FilePickerRequest request);
    /// Closes a window asked to close, and presents the open one. Main context current.
    void Frame();
    [[nodiscard]] bool IsOpen() const noexcept { return m_window != 0; }
    [[nodiscard]] int WindowId() const noexcept { return m_window; }

private:
    struct Entry final {
        std::filesystem::path path;
        bool directory = false;
        std::uintmax_t size = 0U;
        std::filesystem::file_time_type modified{};
    };

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    void GoTo(const std::filesystem::path& directory);
    void RebuildList();
    void Confirm();
    void Cancel();
    void ShowImportTargetMenu();
    [[nodiscard]] bool MatchesKind(const std::filesystem::path& file) const;
    [[nodiscard]] std::filesystem::path ContentRoot() const;

    Editor& m_editor;
    EditorHost& m_host;
    FilePickerRequest m_request;
    int m_window = 0;
    bool m_closePending = false;

    std::filesystem::path m_directory;
    std::vector<Entry> m_entries;
    std::filesystem::path m_importInto;   ///< Content-relative folder Import copies into
    bool m_allKinds = false;

    std::shared_ptr<prism::TextInput> m_pathInput;
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::TableView> m_table;
    std::shared_ptr<prism::TextInput> m_fileName;
    std::shared_ptr<prism::ComboBox> m_kind;
    std::shared_ptr<prism::Button> m_importTarget;
    std::shared_ptr<prism::Label> m_status;
};

} // namespace UVE::Editor
