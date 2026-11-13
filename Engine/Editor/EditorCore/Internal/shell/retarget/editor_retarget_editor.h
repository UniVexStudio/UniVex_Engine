// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "prism/Widget.h"

#include <cstddef>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace prism {
class Button;
class CheckBox;
class ComboBox;
class EngineView;
class Label;
class ProgressBar;
class TableView;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorHostUVE;
class DocumentTabUVE;
class RetargetSkeletonDiagramUVE;

/// The Retarget window, in a window of its own beside the level (which stays open and editable):
///
///   [#1 logo] [title bar: File View Help | drag strip | - [] x]
///   [       ] [#2 Retarget                                  ]
///   [ Character: <rig>  | 47 good 5 check 0 broken 44 missing | Generate  Undo Last ]
///   [ Source: the skeleton the  | preview: the character, alone,   | Humanoid bones ]
///   [ character answers, drawn  | in its own world                 | (filter, map)  ]
///   [ flat with its joints      |                                  |                ]
///   [ Animations / Kept bones   |                                  |                ]
///   [ status / progress                                                             ]
///
/// Picking a joint in the source box, a row of the bones, or the bone in the preview is the same
/// pick: all three follow it.
///
/// Everything it shows and does is EditorUVE's Retarget window state (OpenRetargetWindowUVE ...).
class EditorRetargetWindowUVE final {
public:
    EditorRetargetWindowUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorRetargetWindowUVE();
    EditorRetargetWindowUVE(const EditorRetargetWindowUVE&) = delete;
    EditorRetargetWindowUVE& operator=(const EditorRetargetWindowUVE&) = delete;

    /// Opens the window while the Retarget window is open and closes it after; runs its work and
    /// refreshes the panels. Main context current, before BeginFrame.
    void SyncUVE();
    /// Renders the preview and presents the window (after BeginFrame).
    void RenderUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarUVE();
    void RefreshUVE();
    void RebuildRowsUVE();
    /// The source box: the humanoid's skeleton or the animations' own, coloured by the plan.
    void RebuildSourceUVE(bool refit);
    /// One humanoid bone picked (from the box or the list): the others follow.
    void PickReferenceBoneUVE(int reference, bool fromList);

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    int m_window = 0;
    std::size_t m_shownPlan = static_cast<std::size_t>(-1);
    std::string m_shownFilter;
    bool m_shownProblemsOnly = false;
    std::vector<std::filesystem::path> m_candidates;
    std::vector<std::size_t> m_boneRows;

    std::shared_ptr<prism::ComboBox> m_character;
    std::shared_ptr<prism::Label> m_legend;
    std::shared_ptr<prism::Button> m_generate;
    std::shared_ptr<prism::Button> m_undo;
    std::shared_ptr<prism::TableView> m_animations;
    std::shared_ptr<prism::TableView> m_kept;
    std::shared_ptr<prism::TableView> m_bones;
    std::shared_ptr<prism::TextInput> m_filter;
    std::shared_ptr<prism::CheckBox> m_problemsOnly;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<RetargetSkeletonDiagramUVE> m_source;
    std::shared_ptr<prism::ComboBox> m_sourceMode;
    std::shared_ptr<prism::Label> m_sourceCaption;
    int m_shownSourceMode = -1;
    /// Per joint of the box in Animation mode: the humanoid bone it answers (-1: none).
    std::vector<int> m_sourceReference;
    /// The humanoid bone picked, or -1.
    int m_pickedReference = -1;
    std::shared_ptr<prism::Label> m_status;
    std::shared_ptr<prism::ProgressBar> m_progress;
    std::shared_ptr<DocumentTabUVE> m_tab;
};

} // namespace UVE::Editor
