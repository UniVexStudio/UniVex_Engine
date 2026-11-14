// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "prism/Widget.h"

#include <memory>
#include <string>

namespace prism {
class Button;
class Label;
class TextInput;
} // namespace prism

namespace UVE::Editor {

class KeyframeViewUVE;

/// The Entity Editor dock's Animation Sequencer page, in PRISM:
///
///   [Player v][Walk v] | |< < > >| [] Loop | 12 / 30  0.40 s | Key  Save  Undo Redo | Dope Sheet Curves Fit
///   [ Tracks / filter ][ ruler with the playhead                                                         ]
///   [ Events          ][  ^hit                ^step                                                      ]
///   [ Transform       ][  <>        <>                <>                                                 ]
///   [ > Hips          ][  <>  <>  <>  <>  <>  <>  <>                                                     ]
///   [ Filter bones | Position Rotation Scale | Walk  1.00 s  30 frames  30 fps | status                  ]
///
/// Everything it shows and does is EditorUVE's Animation Sequencer session
/// (SyncAnimationTimelineUVE and the calls beside it); the keys are KeyframeViewUVE's.
class EditorSequencerPanelUVE final {
public:
    explicit EditorSequencerPanelUVE(EditorUVE& editor);
    ~EditorSequencerPanelUVE();
    EditorSequencerPanelUVE(const EditorSequencerPanelUVE&) = delete;
    EditorSequencerPanelUVE& operator=(const EditorSequencerPanelUVE&) = delete;

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Advances the preview by `deltaSeconds` while the page is `shown` and refreshes the toolbar;
    /// hidden, a playing preview pauses. Inside the Entity Editor's window.
    void RefreshUVE(double deltaSeconds, bool shown);

    [[nodiscard]] EditorUVE& EditorRefUVE() noexcept { return m_editor; }
    /// The session as it is now (re-resolved: call after any edit before reading the clip).
    const EditorUVE::AnimationTimelineViewUVE& ResyncUVE();
    [[nodiscard]] const EditorUVE::AnimationTimelineViewUVE& ViewUVE() const noexcept { return m_view; }
    /// Asks for a name in a small dialog; `done` gets it.
    static void AskNameUVE(const std::string& title, const std::string& initial, std::function<void(const std::string&)> done);

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildStatusBarUVE();
    void ShowPlayerMenuUVE();
    void ShowClipMenuUVE();
    /// Load Library: import a project's .uvanimlib into this player's list (or link it live).
    void ShowLoadLibraryMenuUVE();
    /// Export Library: this player's animations to a new .uvanimlib, named in a dialog.
    void ExportLibraryUVE();
    /// A file dropped on the panel: a library is loaded, a clip is added.
    bool AcceptDroppedUVE(const std::string& contentRelative);

    EditorUVE& m_editor;
    EditorUVE::AnimationTimelineViewUVE m_view;
    std::shared_ptr<KeyframeViewUVE> m_keys;
    std::shared_ptr<prism::Button> m_playerButton;
    std::shared_ptr<prism::Button> m_clipButton;
    std::shared_ptr<prism::Button> m_loadLibraryButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_loopButton;
    std::shared_ptr<prism::Button> m_keyButton;
    std::shared_ptr<prism::Button> m_saveButton;
    std::shared_ptr<prism::Button> m_dopeButton;
    std::shared_ptr<prism::Button> m_curvesButton;
    std::shared_ptr<prism::Label> m_clock;
    std::shared_ptr<prism::Label> m_info;
    std::shared_ptr<prism::Label> m_status;
    std::shared_ptr<prism::TextInput> m_filter;
    /// What the toolbar last showed, so its labels relayout only when they change.
    std::string m_shownPlayer;
    std::string m_shownClip;
    std::string m_shownClock;
    std::string m_shownInfo;
    std::string m_shownStatus;
};

} // namespace UVE::Editor
