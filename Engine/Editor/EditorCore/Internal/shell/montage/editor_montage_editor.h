// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_icons.h"

#include "animation_montage_asset.h"
#include "asset_guid.h"

#include "prism/Widget.h"

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace prism {
class Button;
class ComboBox;
class EngineView;
class Label;
class ScrollBox;
class TableView;
} // namespace prism

namespace UVE::Editor {

class EditorUVE;
class EditorHostUVE;
class DocumentTabUVE;
class MontageTimelineUVE;

/// The Montage Editor, Unreal's Animation Montage editor in a window of its own:
///
///   [#1 logo] [title bar: File Edit Window Help | drag strip | - [] x]
///   [       ] [#2 the montage's tab                                 ]
///   [ Save | Preview: <character> | > || [] Loop 1x | + Notify + State + Section + Curve + Track | status ]
///   [ preview of the character (the montage at the playhead) | Details (asset, montage, picked)  ]
///   [                                                         | Sections                         ]
///   [ timeline: ruler, Sections, Slot, Notifies, Curves       | Notifies & Curves | Asset Browser]
///
/// Everything it shows and does is EditorUVE's montage session (OpenMontageEditorUVE ...).
class EditorMontageEditorUVE final {
public:
    EditorMontageEditorUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorMontageEditorUVE();
    EditorMontageEditorUVE(const EditorMontageEditorUVE&) = delete;
    EditorMontageEditorUVE& operator=(const EditorMontageEditorUVE&) = delete;

    /// Opens the window while a montage is open, closes it when the session ends; advances the
    /// preview and refreshes the panels. Main context current, before BeginFrame.
    void SyncUVE();
    /// Renders the preview and presents the window (after BeginFrame).
    void RenderUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }

    /// What the timeline has picked; Details edits it.
    struct SelectionUVE final {
        enum class KindUVE : std::uint8_t { None, Segment, Section, Notify, State, CurveKey };
        KindUVE kind = KindUVE::None;
        std::size_t index = 0U;
        std::size_t key = 0U;   ///< CurveKey: the key within curve `index`
        [[nodiscard]] bool operator==(const SelectionUVE&) const = default;
    };
    SelectionUVE selection;
    /// The curve the Curves track edits (its keys are shown and added to).
    std::size_t activeCurve = 0U;
    EditorUVE& EditorRefUVE() noexcept { return m_editor; }
    /// Adds one at the playhead with a name of its own, and picks it (Details edits it).
    enum class AddKindUVE : std::uint8_t { Notify, State, Section, Curve, Track };
    void AddAtPlayheadUVE(AddKindUVE kind);
    /// A curve name not used yet: `base`, else "base 2", "base 3"...
    [[nodiscard]] static std::string UniqueCurveNameUVE(const Asset::AnimationMontageAssetUVE& montage, const std::string& base);
    /// Asks for a name in a small dialog of this window; `done` gets it.
    void AskNameUVE(const std::string& title, const std::string& initial, std::function<void(const std::string&)> done);
    /// The project's clips (content-relative .uvanim), for the Slot track's menus.
    [[nodiscard]] std::vector<std::filesystem::path> ProjectClipsUVE() const;
    /// Adds the clip at `absolute` to the slot track at `index` (past the end: last).
    void AddClipUVE(const std::filesystem::path& absolute, std::size_t index);
    /// A clip's name (its file's stem), or "Missing clip".
    [[nodiscard]] std::string ClipNameUVE(Asset::AssetGuidUVE clip) const;
    [[nodiscard]] std::filesystem::path ContentRootUVE() const;

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarUVE();
    void ShowPreviewMenuUVE();
    void RequestCloseUVE();
    void RefreshUVE();
    void RebuildDetailsUVE();
    void RebuildSectionsUVE();
    void RebuildAssetsUVE();
    void RebuildNotifiesUVE();

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    EditorIconsUVE m_icons;
    int m_window = 0;
    std::filesystem::path m_openPath;
    bool m_closeAsked = false;
    std::chrono::steady_clock::time_point m_lastTick{};

    std::shared_ptr<DocumentTabUVE> m_tab;
    std::shared_ptr<prism::Button> m_previewButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_loopButton;
    std::shared_ptr<prism::ComboBox> m_speed;
    std::shared_ptr<prism::Label> m_status;
    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<MontageTimelineUVE> m_timeline;
    std::shared_ptr<prism::ScrollBox> m_details;
    std::shared_ptr<prism::ScrollBox> m_sections;
    std::shared_ptr<prism::TableView> m_notifyList;
    std::shared_ptr<prism::ScrollBox> m_curveList;
    /// The Notifies list's rows: (is a state, index).
    std::vector<std::pair<bool, std::size_t>> m_notifyRows;
    std::shared_ptr<prism::TableView> m_assets;
    std::vector<std::filesystem::path> m_assetRows;

    /// What the panels last showed, so they are rebuilt only when it changes.
    Asset::AnimationMontageAssetUVE m_shownMontage;
    SelectionUVE m_shownSelection;
    bool m_shownValid = false;
    std::size_t m_shownClipCount = static_cast<std::size_t>(-1);
};

} // namespace UVE::Editor
