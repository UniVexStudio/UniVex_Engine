// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_montage_editor.h"

#include "editor.h"
#include "editor_prism_chrome.h"
#include "editor_prism_host.h"
#include "editor_prism_montage_timeline.h"

#include "animation_montage_editing.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TableView.h"
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kWindowWidthUVE = 1280;
constexpr int kWindowHeightUVE = 800;

[[nodiscard]] prism::Slot CentreUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string SecondsUVE(const double t) {
    char text[32];
    std::snprintf(text, sizeof(text), "%.2f", t);
    return text;
}

/// One Details row: the label, then the value's widget.
[[nodiscard]] prism::Ref<prism::Widget> RowUVE(const std::string& label, prism::Ref<prism::Widget> value) {
    auto row = prism::hbox(6.0F);
    row->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 2.0F));
    auto name = prism::make<prism::Label>(label);
    name->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto nameBox = prism::make<prism::SizeBox>();
    nameBox->setWidthOverride(130.0F);
    nameBox->setContent(name);
    row->add(nameBox, CentreUVE());
    row->add(std::move(value), prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    return row;
}

[[nodiscard]] prism::Ref<prism::Widget> HeadingUVE(const std::string& text) {
    auto label = prism::make<prism::Label>(text);
    label->setColor(prism::ThemeColor(&prism::Theme::textBright));
    auto box = prism::make<prism::Border>();
    box->setBackground(prism::ThemeColor(&prism::Theme::header));
    box->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    box->setChild(label);
    return box;
}

} // namespace

EditorPrismMontageEditorUVE::EditorPrismMontageEditorUVE(EditorUVE& editor, EditorPrismHostUVE& host)
    : m_editor(editor), m_host(host) {}

EditorPrismMontageEditorUVE::~EditorPrismMontageEditorUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

// ------------------------------------------------------------------------------------- helpers

std::filesystem::path EditorPrismMontageEditorUVE::ContentRootUVE() const {
    return m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
}

std::string EditorPrismMontageEditorUVE::ClipNameUVE(const Asset::AssetGuidUVE clip) const {
    const std::filesystem::path path = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(clip);
    return path.empty() ? std::string("Missing clip") : path.stem().string();
}

std::vector<std::filesystem::path> EditorPrismMontageEditorUVE::ProjectClipsUVE() const {
    std::vector<std::filesystem::path> clips;
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::Directory && entry.relativePath.extension() == ".uvanim") {
            clips.push_back(snapshot.contentRoot / entry.relativePath);
        }
    }
    std::sort(clips.begin(), clips.end());
    return clips;
}

void EditorPrismMontageEditorUVE::AddClipUVE(const std::filesystem::path& absolute, const std::size_t index) {
    const Asset::AssetGuidUVE guid = m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
    const Asset::AnimationClipAssetUVE* const clip = m_editor.MontageClipUVE(guid);
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    if (clip == nullptr) {
        session->status = "Could not read " + absolute.filename().string() + " as an animation clip.";
        return;
    }
    const double length = clip->durationSeconds;
    m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
        const std::size_t at = Asset::AddMontageSegmentUVE(m, guid, length, index);
        selection = SelectionUVE{SelectionUVE::KindUVE::Segment, at, 0U};
    });
    if (!clip->IsSkeletalUVE()) {
        session->status = absolute.filename().string() + " moves an object, not bones: the preview character will not move.";
    }
}

void EditorPrismMontageEditorUVE::AskNameUVE(const std::string& title, const std::string& initial,
                                             std::function<void(const std::string&)> done) {
    if (m_window == 0) {
        return;
    }
    m_host.AppUVE().runInWindow(m_window, [title, initial, done = std::move(done)] {
        auto field = prism::make<prism::TextInput>(initial, "Name");
        std::weak_ptr<prism::TextInput> weak = field;
        const auto apply = [weak, done] {
            if (auto f = weak.lock(); f && !f->text().empty() && done) {
                done(f->text());
            }
        };
        // Enter takes the name, as OK does.
        field->onCommit = [apply](const std::string&) {
            apply();
            if (prism::Application::exists()) {
                prism::Application::get().closeAllPopups();
            }
        };
        prism::showDialog(title, field, {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"OK", true, apply}}, 340.0F);
        if (prism::Application::exists()) {
            prism::Application::get().setFocus(field.get());
        }
    });
}

// ------------------------------------------------------------------------------------- lifetime

void EditorPrismMontageEditorUVE::SyncUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    const std::filesystem::path path = session != nullptr ? session->path : std::filesystem::path{};
    if (m_window != 0 && (session == nullptr || path != m_openPath)) {
        m_host.CloseWindowUVE(m_window);
        m_window = 0;
        m_openPath.clear();
        m_timeline.reset();
        m_viewport.reset();
        m_details.reset();
        m_sections.reset();
        m_assets.reset();
        m_tab.reset();
        m_shownValid = false;
    }
    if (session == nullptr) {
        return;
    }
    if (m_window == 0) {
        selection = SelectionUVE{};
        m_closeAsked = false;
        m_shownValid = false;
        m_shownClipCount = static_cast<std::size_t>(-1);
        m_window = m_host.OpenWindowUVE(BuildUVE(), "Montage Editor - " + path.filename().string(), kWindowWidthUVE,
                                        kWindowHeightUVE);
        if (m_window == 0) {
            m_editor.CloseMontageEditorUVE();
            return;
        }
        m_openPath = path;
        m_host.FocusWindowUVE(m_window);
    }
    if (session->focusPending) {
        session->focusPending = false;
        m_host.FocusWindowUVE(m_window);
    }
    if (m_host.WindowCloseRequestedUVE(m_window)) {
        m_host.ClearCloseRequestUVE(m_window);
        RequestCloseUVE();
    }
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTick.time_since_epoch().count() == 0 ? 0.0 : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    m_editor.TickMontageEditorUVE(std::min(dt, 0.1));
    m_host.AppUVE().runInWindow(m_window, [this] { RefreshUVE(); });
}

void EditorPrismMontageEditorUVE::RequestCloseUVE() {
    if (!m_editor.IsMontageEditorDirtyUVE()) {
        m_editor.CloseMontageEditorUVE();
        return;
    }
    if (m_closeAsked) {
        return;
    }
    m_closeAsked = true;
    const std::string name = m_openPath.filename().string();
    m_host.AppUVE().runInWindow(m_window, [this, name] {
        auto text = prism::make<prism::Label>("Save the changes to " + name + " before closing?");
        prism::showDialog("Close Montage", text,
                          {prism::DialogButton{"Save", true, [this] {
                               m_closeAsked = false;
                               if (m_editor.SaveMontageEditorUVE()) {
                                   m_editor.CloseMontageEditorUVE();
                               }
                           }},
                           prism::DialogButton{"Don't Save", false, [this] {
                               m_closeAsked = false;
                               m_editor.CloseMontageEditorUVE();
                           }},
                           prism::DialogButton{"Cancel", false, [this] { m_closeAsked = false; }}},
                          420.0F);
    });
}

// --------------------------------------------------------------------------------------- layout

prism::Ref<prism::Widget> EditorPrismMontageEditorUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());
    window->add(BuildToolbarUVE());

    // Centre: the preview over the timeline. Right: Details, Sections, Asset Browser.
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};
    m_timeline = prism::make<MontageTimelineUVE>(*this);
    auto timelineScroll = prism::make<prism::ScrollBox>();
    timelineScroll->setContent(m_timeline);
    auto centre = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centre->add(m_viewport, prism::Splitter::Pane{480.0F, 160.0F, 1e9F, true});
    centre->add(PanelUVE(timelineScroll), prism::Splitter::Pane{230.0F, 120.0F, 1e9F, false});

    m_details = prism::make<prism::ScrollBox>();
    m_sections = prism::make<prism::ScrollBox>();
    m_assets = prism::make<prism::TableView>();
    m_assets->addColumn(prism::TableColumn{"name", "Clip", 180.0F});
    m_assets->addColumn(prism::TableColumn{"length", "Length", 70.0F, 40.0F, true, true, prism::HAlign::Right});
    m_assets->addColumn(prism::TableColumn{"bones", "Bones", 60.0F, 40.0F, true, true, prism::HAlign::Right});
    m_assets->cellText = [this](const int row, const int col) -> std::string {
        if (row < 0 || static_cast<std::size_t>(row) >= m_assetRows.size()) {
            return {};
        }
        const std::filesystem::path& path = m_assetRows[static_cast<std::size_t>(row)];
        if (col == 0) {
            return path.stem().string();
        }
        const Asset::AnimationClipAssetUVE* const clip =
            m_editor.MontageClipUVE(m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(path));
        if (clip == nullptr) {
            return "-";
        }
        return col == 1 ? SecondsUVE(clip->durationSeconds) + " s" : std::to_string(clip->bones.size());
    };
    m_assets->onActivate = [this](const int row) {
        if (row >= 0 && static_cast<std::size_t>(row) < m_assetRows.size()) {
            AddClipUVE(m_assetRows[static_cast<std::size_t>(row)], static_cast<std::size_t>(-1));
        }
    };
    auto assetsPage = prism::vbox(4.0F);
    auto assetsNote = prism::make<prism::Label>("Double-click a clip to add it to the Slot track.", prism::FontRole::Small);
    assetsNote->setColor(prism::ThemeColor(&prism::Theme::textDim));
    assetsNote->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 0.0F));
    assetsPage->add(assetsNote);
    assetsPage->add(m_assets, prism::Slot{1.0F});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details));
    tabs->addTab("Sections", prism::Icon::MontageSections, PanelUVE(m_sections));
    tabs->addTab("Asset Browser", prism::Icon::AssetBrowser, PanelUVE(assetsPage));

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(centre, prism::Splitter::Pane{900.0F, 400.0F, 1e9F, true});
    columns->add(tabs, prism::Splitter::Pane{360.0F, 260.0F, 800.0F, false});
    window->add(columns, prism::Slot{1.0F});

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorPrismMontageEditorUVE::BuildTitleBarUVE() {
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(12.0F);
    menus->addMenu("File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item("Save", [this] { static_cast<void>(m_editor.SaveMontageEditorUVE()); }, "", prism::Icon::Save));
        items.push_back(prism::MenuItem::Item("Show in Content Browser", [this] {
            const Asset::ProjectFileSnapshotUVE now = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(m_openPath, now.contentRoot, error);
            if (!error) {
                m_editor.m_contentBrowserDirectory = relative.parent_path();
            }
        }, "", prism::Icon::Folder));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { RequestCloseUVE(); }, "", prism::Icon::Close));
        return items;
    });
    menus->addMenu("Edit", [this] {
        const EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
        prism::MenuItem undo = prism::MenuItem::Item("Undo", [this] { static_cast<void>(m_editor.UndoMontageEditUVE()); }, "", prism::Icon::Undo);
        undo.enabled = session != nullptr && !session->undo.empty();
        prism::MenuItem redo = prism::MenuItem::Item("Redo", [this] { static_cast<void>(m_editor.RedoMontageEditUVE()); }, "", prism::Icon::Redo);
        redo.enabled = session != nullptr && !session->redo.empty();
        return std::vector<prism::MenuItem>{std::move(undo), std::move(redo)};
    });
    menus->addMenu("Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Play it from a script: montage.play(\"<file name>\")", {}).disabled(),
            prism::MenuItem::Item("Events: montage_notify, montage_notify_begin / _end, montage_ended", {}).disabled()};
    });
    row->add(menus, CentreUVE());
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*row, [this] { RequestCloseUVE(); });

    auto tabRow = prism::hbox();
    m_tab = prism::make<DocumentTabUVE>();
    m_tab->setFallbackIcon(prism::Icon::AnimMontage);
    m_tab->setName(m_openPath.empty() && m_editor.MontageEditSessionPtrUVE() != nullptr
                       ? m_editor.MontageEditSessionPtrUVE()->path.stem().string()
                       : m_openPath.stem().string());
    tabRow->add(m_tab, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Fill});
    tabRow->add(prism::make<DragStripUVE>(true), prism::Slot{1.0F});
    auto titleRow = prism::make<prism::SizeBox>();
    titleRow->setHeightOverride(kTitleRowUVE);
    titleRow->setContent(row);
    auto tabRowBox = prism::make<prism::SizeBox>();
    tabRowBox->setHeightOverride(kTabRowUVE);
    tabRowBox->setContent(tabRow);
    auto right = prism::vbox();
    right->add(titleRow);
    right->add(tabRowBox);
    auto top = prism::hbox();
    top->add(prism::make<LogoBoxUVE>());
    top->add(right, prism::Slot{1.0F});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(top);
    return bar;
}

prism::Ref<prism::Widget> EditorPrismMontageEditorUVE::BuildToolbarUVE() {
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    auto save = ToolButtonUVE(prism::Icon::Save, "Save the montage", "Save");
    save->onClick = [this] { static_cast<void>(m_editor.SaveMontageEditorUVE()); };
    row->add(save, CentreUVE());
    row->add(HSeparatorUVE(), CentreUVE());
    m_previewButton = ToolButtonUVE(prism::Icon::PreviewCharacter, "The character the montage is previewed on", "Preview");
    m_previewButton->onClick = [this] { ShowPreviewMenuUVE(); };
    row->add(m_previewButton, CentreUVE());
    row->add(HSeparatorUVE(), CentreUVE());
    m_playButton = ToolButtonUVE(prism::Icon::Play, "Play from the playhead's section (Space)");
    m_playButton->onClick = [this] { m_editor.PlayMontagePreviewUVE(); };
    auto pause = ToolButtonUVE(prism::Icon::Pause, "Pause");
    pause->onClick = [this] { m_editor.PauseMontagePreviewUVE(); };
    auto stop = ToolButtonUVE(prism::Icon::Stop, "Stop and go back to the start");
    stop->onClick = [this] { m_editor.StopMontagePreviewUVE(); };
    m_loopButton = ToolButtonUVE(prism::Icon::NodeLoop, "Loop the preview", "Loop");
    m_loopButton->onClick = [this] {
        if (EditorUVE::MontageEditSessionUVE* const s = m_editor.MontageEditSessionPtrUVE()) {
            s->loop = !s->loop;
        }
    };
    m_speed = prism::make<prism::ComboBox>(std::vector<std::string>{"0.25x", "0.5x", "1x", "2x"}, 2);
    m_speed->onChange = [this](const int i) {
        static constexpr double kSpeeds[] = {0.25, 0.5, 1.0, 2.0};
        if (EditorUVE::MontageEditSessionUVE* const s = m_editor.MontageEditSessionPtrUVE(); s != nullptr && i >= 0 && i < 4) {
            s->speed = kSpeeds[i];
        }
    };
    for (const auto& button : {m_playButton, pause, stop, m_loopButton}) {
        row->add(button, CentreUVE());
    }
    auto speedBox = prism::make<prism::SizeBox>();
    speedBox->setWidthOverride(80.0F);
    speedBox->setContent(m_speed);
    row->add(speedBox, CentreUVE());
    row->add(HSeparatorUVE(), CentreUVE());
    auto undo = ToolButtonUVE(prism::Icon::Undo, "Undo");
    undo->onClick = [this] { static_cast<void>(m_editor.UndoMontageEditUVE()); };
    auto redo = ToolButtonUVE(prism::Icon::Redo, "Redo");
    redo->onClick = [this] { static_cast<void>(m_editor.RedoMontageEditUVE()); };
    row->add(undo, CentreUVE());
    row->add(redo, CentreUVE());
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, prism::Margin(12.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

void EditorPrismMontageEditorUVE::ShowPreviewMenuUVE() {
    std::vector<prism::MenuItem> items;
    const EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    const Asset::AssetGuidUVE current = session != nullptr ? session->montage.previewEntity : Asset::AssetGuidUVE{};
    items.push_back(prism::MenuItem::Check("Skeleton of the first clip", current == Asset::kInvalidAssetGuidUVE,
                                           [this] { m_editor.SetMontagePreviewEntityUVE(Asset::AssetGuidUVE{}); }));
    items.push_back(prism::MenuItem::Sep());
    const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    bool any = false;
    for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
        if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory || entry.relativePath.extension() != ".uventity") {
            continue;
        }
        any = true;
        const std::filesystem::path absolute = snapshot.contentRoot / entry.relativePath;
        const Asset::AssetGuidUVE guid = m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(absolute);
        items.push_back(prism::MenuItem::Check(entry.relativePath.generic_string(), guid == current,
                                               [this, guid] { m_editor.SetMontagePreviewEntityUVE(guid); }));
    }
    if (!any) {
        items.push_back(prism::MenuItem::Item("No entities in this project yet", {}).disabled());
    }
    prism::showMenu(std::move(items), m_previewButton ? m_previewButton->rect() : prism::Rect{}, prism::PopupPlacement::Below);
}

// -------------------------------------------------------------------------------------- refresh

void EditorPrismMontageEditorUVE::RefreshUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    if (m_tab) {
        m_tab->setDirty(m_editor.IsMontageEditorDirtyUVE());
    }
    if (m_loopButton) {
        m_loopButton->setChecked(session->loop);
    }
    if (m_playButton) {
        m_playButton->setChecked(session->playing);
    }
    if (m_previewButton) {
        const std::filesystem::path preview = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(session->montage.previewEntity);
        m_previewButton->setText("Preview: " + (preview.empty() ? std::string("Skeleton") : preview.stem().string()));
    }
    if (m_status) {
        const Asset::MontageSectionUVE* const at = Asset::MontageSectionAtUVE(session->montage, session->playhead);
        std::string text = SecondsUVE(session->playhead) + " / " + SecondsUVE(Asset::MontageLengthUVE(session->montage)) + " s";
        if (at != nullptr) {
            text += "   " + at->name;
        }
        if (!session->lastEvent.empty()) {
            text += "   " + session->lastEvent;
        }
        if (!session->status.empty()) {
            text += "   |  " + session->status;
        }
        m_status->setText(text);
    }
    if (m_timeline) {
        m_timeline->relayout();
        m_timeline->invalidate();
    }
    if (!m_shownValid || !(m_shownMontage == session->montage) || !(m_shownSelection == selection)) {
        m_shownMontage = session->montage;
        m_shownSelection = selection;
        m_shownValid = true;
        RebuildDetailsUVE();
        RebuildSectionsUVE();
    }
    const std::vector<std::filesystem::path> clips = ProjectClipsUVE();
    if (clips.size() != m_shownClipCount) {
        m_shownClipCount = clips.size();
        m_assetRows = clips;
        RebuildAssetsUVE();
    }
}

void EditorPrismMontageEditorUVE::RebuildAssetsUVE() {
    if (m_assets) {
        m_assets->setRowCount(static_cast<int>(m_assetRows.size()));
        m_assets->invalidate();
    }
}

void EditorPrismMontageEditorUVE::RebuildDetailsUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr || !m_details) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& m = session->montage;
    auto page = prism::vbox(2.0F);
    const auto number = [this](const double value, const float step, const int decimals,
                               std::function<void(Asset::AnimationMontageAssetUVE&, double)> set) {
        auto field = prism::make<prism::NumberField>(static_cast<float>(value), step, decimals);
        field->onCommit = [this, set = std::move(set)](const float v) {
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& montage) { set(montage, static_cast<double>(v)); });
        };
        return field;
    };
    const auto text = [this](const std::string& value, std::function<void(Asset::AnimationMontageAssetUVE&, const std::string&)> set) {
        auto field = prism::make<prism::TextInput>(value, "");
        field->onCommit = [this, set = std::move(set)](const std::string& v) {
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& montage) { set(montage, v); });
        };
        return field;
    };

    // The selection first: it is what was just clicked.
    using Kind = SelectionUVE::KindUVE;
    const std::size_t i = selection.index;
    if (selection.kind == Kind::Segment && i < m.segments.size()) {
        const Asset::MontageSegmentUVE& s = m.segments[i];
        page->add(HeadingUVE("Clip: " + ClipNameUVE(s.clip)));
        page->add(RowUVE("Clip Length", prism::make<prism::Label>(SecondsUVE(s.clipLength) + " s")));
        page->add(RowUVE("Start In Clip", number(s.clipStart, 0.01F, 2, [i](auto& mm, double v) { mm.segments[i].clipStart = std::max(0.0, v); })));
        page->add(RowUVE("End In Clip", number(s.clipEnd, 0.01F, 2, [i](auto& mm, double v) { mm.segments[i].clipEnd = std::max(0.0, v); })));
        page->add(RowUVE("Rate", number(s.rate, 0.05F, 2, [i](auto& mm, double v) { mm.segments[i].rate = std::max(0.01, v); })));
        page->add(RowUVE("Loops", number(s.loops, 1.0F, 0, [i](auto& mm, double v) {
                      mm.segments[i].loops = static_cast<std::uint32_t>(std::clamp(std::lround(v), 1L, 1000L));
                  })));
    } else if (selection.kind == Kind::Section && i < m.sections.size()) {
        const std::string name = m.sections[i].name;
        page->add(HeadingUVE("Section: " + name));
        page->add(RowUVE("Name", text(name, [name](auto& mm, const std::string& v) { static_cast<void>(Asset::RenameMontageSectionUVE(mm, name, v)); })));
        page->add(RowUVE("Starts At", number(m.sections[i].time, 0.01F, 2, [name](auto& mm, double v) {
                      static_cast<void>(Asset::MoveMontageSectionUVE(mm, name, v));
                  })));
    } else if (selection.kind == Kind::Notify && i < m.notifies.size()) {
        page->add(HeadingUVE("Notify: " + m.notifies[i].name));
        page->add(RowUVE("Name", text(m.notifies[i].name, [i](auto& mm, const std::string& v) { mm.notifies[i].name = v; })));
        page->add(RowUVE("Time", number(m.notifies[i].time, 0.01F, 2, [i](auto& mm, double v) { mm.notifies[i].time = std::max(0.0, v); })));
        page->add(RowUVE("Track", number(m.notifies[i].track + 1U, 1.0F, 0, [i](auto& mm, double v) {
                      mm.notifies[i].track = static_cast<std::uint32_t>(std::clamp(std::lround(v), 1L, 16L) - 1);
                  })));
    } else if (selection.kind == Kind::State && i < m.notifyStates.size()) {
        page->add(HeadingUVE("Notify State: " + m.notifyStates[i].name));
        page->add(RowUVE("Name", text(m.notifyStates[i].name, [i](auto& mm, const std::string& v) { mm.notifyStates[i].name = v; })));
        page->add(RowUVE("Begin", number(m.notifyStates[i].begin, 0.01F, 2, [i](auto& mm, double v) {
                      mm.notifyStates[i].begin = std::clamp(v, 0.0, mm.notifyStates[i].end);
                  })));
        page->add(RowUVE("End", number(m.notifyStates[i].end, 0.01F, 2, [i](auto& mm, double v) {
                      mm.notifyStates[i].end = std::max(v, mm.notifyStates[i].begin);
                  })));
    } else if (selection.kind == Kind::CurveKey && i < m.curves.size() && selection.key < m.curves[i].keys.size()) {
        const std::size_t k = selection.key;
        page->add(HeadingUVE("Curve: " + m.curves[i].name));
        page->add(RowUVE("Name", text(m.curves[i].name, [i](auto& mm, const std::string& v) { mm.curves[i].name = v; })));
        page->add(RowUVE("Key Time", number(m.curves[i].keys[k].time, 0.01F, 2, [i, k](auto& mm, double v) { mm.curves[i].keys[k].time = std::max(0.0, v); })));
        page->add(RowUVE("Key Value", number(m.curves[i].keys[k].value, 0.05F, 2, [i, k](auto& mm, double v) {
                      mm.curves[i].keys[k].value = static_cast<float>(v);
                  })));
    }

    page->add(HeadingUVE("Montage"));
    page->add(RowUVE("Slot", text(m.slot, [](auto& mm, const std::string& v) { mm.slot = v.empty() ? std::string("DefaultSlot") : v; })));
    page->add(RowUVE("Blend In", number(m.blendInSeconds, 0.01F, 2, [](auto& mm, double v) { mm.blendInSeconds = std::max(0.0, v); })));
    page->add(RowUVE("Blend Out", number(m.blendOutSeconds, 0.01F, 2, [](auto& mm, double v) { mm.blendOutSeconds = std::max(0.0, v); })));
    page->add(RowUVE("Blend Out Trigger", number(m.blendOutTriggerSeconds, 0.01F, 2, [](auto& mm, double v) { mm.blendOutTriggerSeconds = v; })));
    auto autoBlend = prism::make<prism::CheckBox>("", m.autoBlendOut);
    autoBlend->onChange = [this](const bool v) {
        m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) { mm.autoBlendOut = v; });
    };
    page->add(RowUVE("Auto Blend Out", autoBlend));
    page->add(RowUVE("Rate Scale", number(m.rateScale, 0.05F, 2, [](auto& mm, double v) { mm.rateScale = std::max(0.01, v); })));
    auto rootMotion = prism::make<prism::ComboBox>(std::vector<std::string>{"Off", "In Place", "Apply To Target"},
                                                   static_cast<int>(m.rootMotion));
    rootMotion->onChange = [this](const int v) {
        m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
            mm.rootMotion = static_cast<Asset::MontageRootMotionUVE>(std::clamp(v, 0, 2));
        });
    };
    page->add(RowUVE("Root Motion", rootMotion));
    auto note = prism::make<prism::Label>("Length " + SecondsUVE(Asset::MontageLengthUVE(m)) + " s, " +
                                              std::to_string(m.segments.size()) + " clips, " + std::to_string(m.sections.size()) +
                                              " sections, " + std::to_string(m.notifies.size() + m.notifyStates.size()) + " notifies",
                                          prism::FontRole::Small);
    note->setColor(prism::ThemeColor(&prism::Theme::textDim));
    note->setPadding(prism::Margin(8.0F, 6.0F, 8.0F, 6.0F));
    page->add(note);
    m_details->setContent(page);
}

void EditorPrismMontageEditorUVE::RebuildSectionsUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr || !m_sections) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& m = session->montage;
    auto page = prism::vbox(2.0F);
    page->add(HeadingUVE("Sections: what follows each one"));
    for (const Asset::MontageSectionUVE& section : m.sections) {
        // Next: End, Loop (itself), or another section.
        std::vector<std::string> choices{"End", "Loop"};
        int current = section.next.empty() ? 0 : section.next == section.name ? 1 : 0;
        for (const Asset::MontageSectionUVE& other : m.sections) {
            if (other.name == section.name) {
                continue;
            }
            choices.push_back("> " + other.name);
            if (other.name == section.next) {
                current = static_cast<int>(choices.size()) - 1;
            }
        }
        auto next = prism::make<prism::ComboBox>(choices, current);
        next->onChange = [this, name = section.name, choices](const int v) {
            const std::string target = v <= 0 ? std::string() : v == 1 ? name : choices[static_cast<std::size_t>(v)].substr(2);
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
                static_cast<void>(Asset::SetMontageSectionNextUVE(mm, name, target));
            });
        };
        auto preview = ToolButtonUVE(prism::Icon::Play, "Preview from this section");
        preview->onClick = [this, name = section.name] { m_editor.PlayMontagePreviewUVE(name); };
        auto row = prism::hbox(6.0F);
        row->setPadding(prism::Margin(8.0F, 2.0F, 8.0F, 2.0F));
        auto label = prism::make<prism::Label>(section.name + "  (" + SecondsUVE(section.time) + " s)");
        auto labelBox = prism::make<prism::SizeBox>();
        labelBox->setWidthOverride(150.0F);
        labelBox->setContent(label);
        row->add(labelBox, CentreUVE());
        row->add(next, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        row->add(preview, CentreUVE());
        page->add(row);
    }
    auto add = prism::make<prism::Button>("Add Section at the Playhead", prism::Icon::Plus, prism::ButtonStyle::Default);
    add->onClick = [this] {
        const EditorUVE::MontageEditSessionUVE* const s = m_editor.MontageEditSessionPtrUVE();
        const double at = s != nullptr ? s->playhead : 0.0;
        AskNameUVE("New Section", "Attack", [this, at](const std::string& name) {
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) { static_cast<void>(Asset::AddMontageSectionUVE(mm, name, at)); });
        });
    };
    auto addRow = prism::hbox();
    addRow->setPadding(prism::Margin(8.0F, 8.0F, 8.0F, 8.0F));
    addRow->add(add, CentreUVE());
    page->add(addRow);
    m_sections->setContent(page);
}

// ---------------------------------------------------------------------------------------- render

void EditorPrismMontageEditorUVE::RenderUVE() {
    if (m_window == 0 || m_editor.MontageEditSessionPtrUVE() == nullptr) {
        return;
    }
    std::vector<EditorPrismHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInputUVE input = m_host.CaptureViewInputUVE(m_window, rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture =
                m_editor.RenderMontagePreviewUVE(Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used, input);
            m_host.ApplyViewCursorUVE(m_window, input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorPrismHostUVE::ViewImageUVE view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x, used.y};
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
            }
        }
    }
    m_host.RenderWindowUVE(m_window, views);
}

} // namespace UVE::Editor
