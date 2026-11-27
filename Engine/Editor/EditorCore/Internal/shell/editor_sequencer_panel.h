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

class KeyframeView;

/// The Entity Editor dock's Animation Sequencer page, in PRISM:
///
///   [Player v][Walk v] | |< < > >| [] Loop | 12 / 30  0.40 s | Key  Save  Undo Redo | Dope Sheet Curves Fit
///   [ Tracks / filter ][ ruler with the playhead                                                         ]
///   [ Events          ][  ^hit                ^step                                                      ]
///   [ Transform       ][  <>        <>                <>                                                 ]
///   [ > Hips          ][  <>  <>  <>  <>  <>  <>  <>                                                     ]
///   [ Filter bones | Position Rotation Scale | Walk  1.00 s  30 frames  30 fps | status                  ]
///
/// Everything it shows and does is Editor's Animation Sequencer session
/// (SyncAnimationTimeline and the calls beside it); the keys are KeyframeView's.
class EditorSequencerPanel final {
public:
    explicit EditorSequencerPanel(Editor& editor);
    ~EditorSequencerPanel();
    EditorSequencerPanel(const EditorSequencerPanel&) = delete;
    EditorSequencerPanel& operator=(const EditorSequencerPanel&) = delete;

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    /// Advances the preview by `deltaSeconds` while the page is `shown` and refreshes the toolbar;
    /// hidden, a playing preview pauses. Inside the Entity Editor's window.
    void Refresh(double deltaSeconds, bool shown);

    [[nodiscard]] Editor& EditorRef() noexcept { return m_editor; }
    /// The session as it is now (re-resolved: call after any edit before reading the clip).
    const Editor::AnimationTimelineView& Resync();
    [[nodiscard]] const Editor::AnimationTimelineView& View() const noexcept { return m_view; }
    /// Asks for a name in a small dialog; `done` gets it.
    static void AskName(const std::string& title, const std::string& initial, std::function<void(const std::string&)> done);

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildStatusBar();
    void ShowPlayerMenu();
    void ShowClipMenu();
    /// Load Library: import a project's .uvanimlib into this player's list (or link it live).
    /// Export Library: this player's animations to a new .uvanimlib, named in a dialog.
    void ExportLibrary();
    void ShowSettingsMenu();
    /// A file dropped on the panel: a library is loaded, a clip is added.
    bool AcceptDropped(const std::string& contentRelative);

    Editor& m_editor;
    Editor::AnimationTimelineView m_view;
    std::shared_ptr<KeyframeView> m_keys;
    std::shared_ptr<prism::Button> m_playerButton;
    std::shared_ptr<prism::Button> m_clipButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_loopButton;
    std::shared_ptr<prism::Button> m_keyButton;
    std::shared_ptr<prism::Button> m_recordButton;
    std::shared_ptr<prism::Button> m_settingsButton;
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
    bool m_compactToolbar = false;
    std::shared_ptr<bool> m_alive = std::make_shared<bool>(true);
    const void* m_lastKeysClip = nullptr;
    double m_lastKeysTime = -1.0;
    bool m_lastKeysCurves = false;
    bool m_lastKeysPlaying = false;
    bool m_lastKeysDirty = false;
    std::size_t m_lastKeysSelection = 0U;
    int m_lastKeysEvent = -1;
    std::string m_lastKeysFilter;
    std::string m_lastKeysError;
    Scene::Entity m_lastKeysPlayer = Scene::kInvalidEntity;
    std::uint64_t m_lastKeysRevision = 0U;
    std::string m_lastKeysBone;
};

} // namespace UVE::Editor
