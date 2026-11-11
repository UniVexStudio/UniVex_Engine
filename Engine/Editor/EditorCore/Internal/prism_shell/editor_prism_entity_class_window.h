// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_prism_icons.h"

#include "prism/Widget.h"

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace prism {
class Button;
class Label;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorPrismHostUVE;

/// Entity Class, drawn with PRISM in a window of its own - Unreal's Pick Parent Class for an
/// entity: Content "+ Add" -> Entity -> "Entity Class...".
///
///   [Entity Class                                      - [] x]
///   [ search                                                  ]
///   [ (icon) Player 3D      | Player 3D                       ]
///   [        the summary    | the summary                     ]
///   [ (icon) Character 3D   | what it is for, what to add     ]
///   [ ...                   | Makes: Player.uventity in <dir> ]
///   [ status                              [Create] [Cancel]   ]
///
/// The list is the content catalogue's entity classes; Create is EditorUVE::CreateEntityClassUVE.
class EditorPrismEntityClassWindowUVE final {
public:
    EditorPrismEntityClassWindowUVE(EditorUVE& editor, EditorPrismHostUVE& host);
    ~EditorPrismEntityClassWindowUVE();
    EditorPrismEntityClassWindowUVE(const EditorPrismEntityClassWindowUVE&) = delete;
    EditorPrismEntityClassWindowUVE& operator=(const EditorPrismEntityClassWindowUVE&) = delete;

    /// Opens the window while the editor's picker is open and closes it when the picker ends; the
    /// window's close button cancels. Main context current, between frames.
    void FrameUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

    /// Picks the class at `index` of the shown list (the list widget calls these).
    void PickUVE(int index);
    void CreateUVE();
    void CancelUVE();
    [[nodiscard]] int PickedIndexUVE() const noexcept;
    [[nodiscard]] const std::vector<std::string>& ShownUVE() const noexcept { return m_shown; }
    /// "Player.uventity   in Characters": the file Create would write for `itemId`.
    [[nodiscard]] std::string MakesTextUVE(std::string_view itemId) const;
    [[nodiscard]] EditorPrismIconsUVE& IconsUVE() noexcept { return m_icons; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    void RebuildUVE();
    void RefreshDetailUVE();

    EditorUVE& m_editor;
    EditorPrismHostUVE& m_host;
    EditorPrismIconsUVE m_icons;
    int m_window = 0;
    bool m_closePending = false;
    std::vector<std::string> m_shown;   ///< catalogue ids, in list order, after the search
    std::shared_ptr<prism::TextInput> m_search;
    std::shared_ptr<prism::Widget> m_list;
    std::shared_ptr<prism::Widget> m_detail;
    std::shared_ptr<prism::Label> m_status;
    std::shared_ptr<prism::Button> m_create;
};

} // namespace UVE::Editor
