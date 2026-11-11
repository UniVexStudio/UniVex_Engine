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

class EditorPrismHostUVE;

/// The editor's file window, drawn with PRISM in a window of its own: every Load... / Browse... /
/// Quick Import... of the editor (EditorUVE::OpenFilePickerUVE) opens here.
///
///   [title                                               - [] x]
///   [ up | path                                      | search  ]
///   [ Places         | Name             Type     Size  Modified]
///   [  Content       | folders first, then the files of the    ]
///   [  Project       | kinds asked for                         ]
///   [  Home ...      |                                         ]
///   [ File: name                      | kind filter            ]
///   [ Quick Import: Import into <Content folder>               ]
///   [                                       [Load] [Cancel]    ]
///
/// Load picks a file where it is (anywhere on disk; the editor brings it into the project when it
/// must). Quick Import copies the file into the chosen Content folder first and picks the copy.
class EditorPrismFileDialogUVE final {
public:
    EditorPrismFileDialogUVE(EditorUVE& editor, EditorPrismHostUVE& host);
    ~EditorPrismFileDialogUVE();
    EditorPrismFileDialogUVE(const EditorPrismFileDialogUVE&) = delete;
    EditorPrismFileDialogUVE& operator=(const EditorPrismFileDialogUVE&) = delete;

    /// Opens the window for `request` (replacing one already open, whose pick never fires).
    void OpenUVE(FilePickerRequestUVE request);
    /// Closes a window asked to close, and presents the open one. Main context current.
    void FrameUVE();
    [[nodiscard]] bool IsOpenUVE() const noexcept { return m_window != 0; }
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

private:
    struct EntryUVE final {
        std::filesystem::path path;
        bool directory = false;
        std::uintmax_t size = 0U;
        std::filesystem::file_time_type modified{};
    };

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void GoToUVE(const std::filesystem::path& directory);
    void RebuildListUVE();
    void ConfirmUVE();
    void CancelUVE();
    void ShowImportTargetMenuUVE();
    [[nodiscard]] bool MatchesKindUVE(const std::filesystem::path& file) const;
    [[nodiscard]] std::filesystem::path ContentRootUVE() const;

    EditorUVE& m_editor;
    EditorPrismHostUVE& m_host;
    FilePickerRequestUVE m_request;
    int m_window = 0;
    bool m_closePending = false;

    std::filesystem::path m_directory;
    std::vector<EntryUVE> m_entries;
    std::filesystem::path m_importInto;   ///< Content-relative folder Quick Import copies into
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
