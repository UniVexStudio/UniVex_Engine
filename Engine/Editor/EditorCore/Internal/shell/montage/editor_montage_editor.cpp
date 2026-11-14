// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_montage_editor.h"
#include "editor_fit_width.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_host.h"
#include "editor_montage_timeline.h"

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
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>

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

} // namespace

EditorMontageEditorUVE::EditorMontageEditorUVE(EditorUVE& editor, EditorHostUVE& host)
    : m_editor(editor), m_host(host) {}

EditorMontageEditorUVE::~EditorMontageEditorUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

// ------------------------------------------------------------------------------------- helpers

std::filesystem::path EditorMontageEditorUVE::ContentRootUVE() const {
    return m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE().contentRoot;
}

std::string EditorMontageEditorUVE::ClipNameUVE(const Asset::AssetGuidUVE clip) const {
    const std::filesystem::path path = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(clip);
    return path.empty() ? std::string("Missing clip") : path.stem().string();
}

std::vector<std::filesystem::path> EditorMontageEditorUVE::ProjectClipsUVE() const {
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

void EditorMontageEditorUVE::AddClipUVE(const std::filesystem::path& absolute, const std::size_t index) {
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

void EditorMontageEditorUVE::AskNameUVE(const std::string& title, const std::string& initial,
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

std::string EditorMontageEditorUVE::UniqueCurveNameUVE(const Asset::AnimationMontageAssetUVE& montage, const std::string& base) {
    const auto taken = [&montage](const std::string& name) {
        return std::any_of(montage.curves.begin(), montage.curves.end(), [&name](const Asset::MontageCurveUVE& c) { return c.name == name; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int n = 2;; ++n) {
        const std::string name = base + " " + std::to_string(n);
        if (!taken(name)) {
            return name;
        }
    }
}

void EditorMontageEditorUVE::AddAtPlayheadUVE(const AddKindUVE kind) {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    const double at = session->playhead;
    // The track the picked notify is on, else the first.
    std::uint32_t track = 0U;
    if (selection.kind == SelectionUVE::KindUVE::Notify && selection.index < session->montage.notifies.size()) {
        track = session->montage.notifies[selection.index].track;
    } else if (selection.kind == SelectionUVE::KindUVE::State && selection.index < session->montage.notifyStates.size()) {
        track = session->montage.notifyStates[selection.index].track;
    }
    using Kind = SelectionUVE::KindUVE;
    m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& m) {
        switch (kind) {
            case AddKindUVE::Notify: {
                const std::size_t index = Asset::AddMontageNotifyUVE(m, "Notify_" + std::to_string(m.notifies.size() + 1U), at, track);
                selection = SelectionUVE{Kind::Notify, index, 0U};
                break;
            }
            case AddKindUVE::State: {
                const double length = std::max(Asset::kMontageEmptyTimelineSecondsUVE, Asset::MontageLengthUVE(m));
                const std::size_t index = Asset::AddMontageNotifyStateUVE(m, "State_" + std::to_string(m.notifyStates.size() + 1U), at,
                                                                         std::min(length, at + 0.3), track);
                selection = SelectionUVE{Kind::State, index, 0U};
                break;
            }
            case AddKindUVE::Section: {
                const std::string name = Asset::AddMontageSectionUVE(m, Asset::MakeUniqueMontageSectionNameUVE(m, "Section"), at);
                for (std::size_t i = 0U; i < m.sections.size(); ++i) {
                    if (m.sections[i].name == name) {
                        selection = SelectionUVE{Kind::Section, i, 0U};
                    }
                }
                break;
            }
            case AddKindUVE::Curve: {
                const std::string name = UniqueCurveNameUVE(m, "Curve");
                Asset::SetMontageCurveKeyUVE(m, name, at, 0.0F);
                activeCurve = m.curves.size() - 1U;
                selection = SelectionUVE{Kind::CurveKey, activeCurve, 0U};
                break;
            }
            case AddKindUVE::Track:
                m.notifyTracks = std::min(64U, m.notifyTracks + 1U);
                break;
        }
    });
}

// ------------------------------------------------------------------------------------- lifetime

void EditorMontageEditorUVE::SyncUVE() {
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

void EditorMontageEditorUVE::RequestCloseUVE() {
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

prism::Ref<prism::Widget> EditorMontageEditorUVE::BuildUVE() {
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

    // Every notify and state in one list (click picks it), and the curves with their keys.
    m_notifyList = prism::make<prism::TableView>();
    m_notifyList->addColumn(prism::TableColumn{"name", "Notify", 150.0F});
    m_notifyList->addColumn(prism::TableColumn{"time", "Time", 70.0F, 40.0F, true, true, prism::HAlign::Right});
    m_notifyList->addColumn(prism::TableColumn{"track", "Track", 90.0F});
    m_notifyList->cellText = [this](const int row, const int col) -> std::string {
        const EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
        if (session == nullptr || row < 0 || static_cast<std::size_t>(row) >= m_notifyRows.size()) {
            return {};
        }
        const auto [state, index] = m_notifyRows[static_cast<std::size_t>(row)];
        const Asset::AnimationMontageAssetUVE& m = session->montage;
        if (state ? index >= m.notifyStates.size() : index >= m.notifies.size()) {
            return {};
        }
        const std::uint32_t track = state ? m.notifyStates[index].track : m.notifies[index].track;
        switch (col) {
            case 0: return state ? m.notifyStates[index].name + "  [state]" : m.notifies[index].name;
            case 1: return state ? SecondsUVE(m.notifyStates[index].begin) + "-" + SecondsUVE(m.notifyStates[index].end) : SecondsUVE(m.notifies[index].time);
            default: return Asset::MontageNotifyTrackNameUVE(m, track);
        }
    };
    m_notifyList->onSelect = [this](const std::vector<int>& rows) {
        if (rows.empty() || static_cast<std::size_t>(rows.front()) >= m_notifyRows.size()) {
            return;
        }
        const auto [state, index] = m_notifyRows[static_cast<std::size_t>(rows.front())];
        selection = SelectionUVE{state ? SelectionUVE::KindUVE::State : SelectionUVE::KindUVE::Notify, index, 0U};
    };
    m_curveList = prism::make<prism::ScrollBox>();
    auto notifiesPage = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    notifiesPage->add(m_notifyList, prism::Splitter::Pane{300.0F, 100.0F, 1e9F, true});
    notifiesPage->add(m_curveList, prism::Splitter::Pane{220.0F, 100.0F, 1e9F, false});

    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Details", prism::Icon::Properties, PanelUVE(m_details));
    tabs->addTab("Sections", prism::Icon::MontageSections, PanelUVE(m_sections));
    tabs->addTab("Notifies & Curves", prism::Icon::AnimEvent, PanelUVE(notifiesPage));
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

prism::Ref<prism::Widget> EditorMontageEditorUVE::BuildTitleBarUVE() {
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

prism::Ref<prism::Widget> EditorMontageEditorUVE::BuildToolbarUVE() {
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
    row->add(HSeparatorUVE(), CentreUVE());
    // Add at the playhead, no questions asked: named on its own, picked, edited in Details.
    const auto adder = [this, &row](const prism::Icon icon, const char* label, const char* tip, const AddKindUVE kind) {
        auto button = ToolButtonUVE(icon, tip, label);
        button->onClick = [this, kind] { AddAtPlayheadUVE(kind); };
        row->add(button, CentreUVE());
    };
    adder(prism::Icon::AnimEvent, "Notify", "Add a notify at the playhead, on the picked notify's track", AddKindUVE::Notify);
    adder(prism::Icon::NodeTimer, "State", "Add a notify state (a window of time) at the playhead", AddKindUVE::State);
    adder(prism::Icon::MontageSections, "Section", "Add a section at the playhead", AddKindUVE::Section);
    adder(prism::Icon::Curve, "Curve", "Add a curve with a key at the playhead", AddKindUVE::Curve);
    adder(prism::Icon::Layers, "Track", "Add a notify track", AddKindUVE::Track);
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, prism::Margin(12.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

void EditorMontageEditorUVE::ShowPreviewMenuUVE() {
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

void EditorMontageEditorUVE::RefreshUVE() {
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
        RebuildNotifiesUVE();
    }
    const std::vector<std::filesystem::path> clips = ProjectClipsUVE();
    if (clips.size() != m_shownClipCount) {
        m_shownClipCount = clips.size();
        m_assetRows = clips;
        RebuildAssetsUVE();
    }
}

void EditorMontageEditorUVE::RebuildAssetsUVE() {
    if (m_assets) {
        m_assets->setRowCount(static_cast<int>(m_assetRows.size()));
        m_assets->invalidate();
    }
}

void EditorMontageEditorUVE::RebuildDetailsUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr || !m_details) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& m = session->montage;
    auto grid = prism::make<prism::PropertyGrid>();
    int cat = -1;
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

    // A notify track, by name.
    const auto trackPicker = [this, &m](const std::uint32_t track, std::function<void(Asset::AnimationMontageAssetUVE&, std::uint32_t)> set) {
        std::vector<std::string> names;
        const std::uint32_t tracks = std::max<std::uint32_t>(m.notifyTracks, track + 1U);
        for (std::uint32_t t = 0U; t < tracks; ++t) {
            names.push_back(Asset::MontageNotifyTrackNameUVE(m, t));
        }
        auto picker = prism::make<prism::ComboBox>(names, static_cast<int>(track));
        picker->onChange = [this, set = std::move(set)](const int v) {
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) { set(mm, static_cast<std::uint32_t>(std::clamp(v, 0, 63))); });
        };
        return picker;
    };

    // The selection first: it is what was just clicked.
    using Kind = SelectionUVE::KindUVE;
    const std::size_t i = selection.index;
    if (selection.kind == Kind::Segment && i < m.segments.size()) {
        const Asset::MontageSegmentUVE& s = m.segments[i];
        cat = grid->addCategory(ClipNameUVE(s.clip));
        // The clip it plays, swapped for another clip of the project.
        {
            const std::vector<std::filesystem::path> clips = ProjectClipsUVE();
            std::vector<std::string> names;
            int current = -1;
            const std::filesystem::path now = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(s.clip);
            for (std::size_t c = 0U; c < clips.size(); ++c) {
                names.push_back(clips[c].stem().string());
                if (!now.empty() && std::filesystem::path(now).filename() == clips[c].filename() && now.stem() == clips[c].stem()) {
                    current = static_cast<int>(c);
                }
            }
            if (current < 0) {
                names.insert(names.begin(), ClipNameUVE(s.clip));
                current = 0;
            }
            const bool missing = names.size() > clips.size();
            auto clipPicker = prism::make<prism::ComboBox>(names, current);
            clipPicker->onChange = [this, i, clips, missing](const int v) {
                const int at = v - (missing ? 1 : 0);
                if (at < 0 || static_cast<std::size_t>(at) >= clips.size()) {
                    return;
                }
                const Asset::AssetGuidUVE guid = m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(clips[static_cast<std::size_t>(at)]);
                const Asset::AnimationClipAssetUVE* const clip = m_editor.MontageClipUVE(guid);
                if (clip == nullptr) {
                    return;
                }
                const double length = clip->durationSeconds;
                m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
                    if (i < mm.segments.size()) {
                        mm.segments[i].clip = guid;
                        mm.segments[i].clipLength = length;
                        mm.segments[i].clipStart = std::min(mm.segments[i].clipStart, length);
                        mm.segments[i].clipEnd = 0.0;
                    }
                });
            };
            grid->addRow(cat, "Clip", clipPicker, "The clip this part of the slot plays");
        }
        grid->addRow(cat, "Clip Length", prism::make<prism::Label>(SecondsUVE(s.clipLength) + " s"));
        grid->addRow(cat, "Start In Clip", number(s.clipStart, 0.01F, 2, [i](auto& mm, double v) { mm.segments[i].clipStart = std::max(0.0, v); }));
        grid->addRow(cat, "End In Clip", number(s.clipEnd, 0.01F, 2, [i](auto& mm, double v) { mm.segments[i].clipEnd = std::max(0.0, v); }));
        grid->addRow(cat, "Rate", number(s.rate, 0.05F, 2, [i](auto& mm, double v) { mm.segments[i].rate = std::max(0.01, v); }));
        grid->addRow(cat, "Loops", number(s.loops, 1.0F, 0, [i](auto& mm, double v) {
                      mm.segments[i].loops = static_cast<std::uint32_t>(std::clamp(std::lround(v), 1L, 1000L));
                  }));
    } else if (selection.kind == Kind::Section && i < m.sections.size()) {
        const std::string name = m.sections[i].name;
        cat = grid->addCategory(name);
        grid->addRow(cat, "Name", text(name, [name](auto& mm, const std::string& v) { static_cast<void>(Asset::RenameMontageSectionUVE(mm, name, v)); }));
        grid->addRow(cat, "Starts At", number(m.sections[i].time, 0.01F, 2, [name](auto& mm, double v) {
                      static_cast<void>(Asset::MoveMontageSectionUVE(mm, name, v));
                  }));
    } else if (selection.kind == Kind::Notify && i < m.notifies.size()) {
        cat = grid->addCategory(m.notifies[i].name);
        grid->addRow(cat, "Name", text(m.notifies[i].name, [i](auto& mm, const std::string& v) { mm.notifies[i].name = v; }));
        grid->addRow(cat, "Time", number(m.notifies[i].time, 0.01F, 2, [i](auto& mm, double v) { mm.notifies[i].time = std::max(0.0, v); }));
        grid->addRow(cat, "Track", trackPicker(m.notifies[i].track, [i](auto& mm, std::uint32_t t) { mm.notifies[i].track = t; }));
        {
            static const std::vector<std::pair<std::string, std::uint32_t>> kColours{
                {"Track colour", 0U}, {"Red", 0xE05A5AU}, {"Orange", 0xE8964AU}, {"Yellow", 0xE6CF52U}, {"Green", 0x6CC46CU},
                {"Teal", 0x4EC9B0U},   {"Blue", 0x5A9BE6U}, {"Purple", 0xB07CE0U}, {"Pink", 0xE07CB8U}, {"White", 0xE8E8E8U}};
            std::vector<std::string> names;
            int current = 0;
            for (std::size_t c = 0U; c < kColours.size(); ++c) {
                names.push_back(kColours[c].first);
                current = kColours[c].second == m.notifies[i].colour ? static_cast<int>(c) : current;
            }
            auto colour = prism::make<prism::ComboBox>(names, current);
            colour->onChange = [this, i](const int v) {
                if (v < 0 || static_cast<std::size_t>(v) >= kColours.size()) {
                    return;
                }
                m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
                    if (i < mm.notifies.size()) {
                        mm.notifies[i].colour = kColours[static_cast<std::size_t>(v)].second;
                    }
                });
            };
            grid->addRow(cat, "Colour", colour, "Its colour on the timeline");
        }
        grid->addRow(cat, "Trigger Chance", number(m.notifies[i].triggerChance, 0.05F, 2, [i](auto& mm, double v) {
                         mm.notifies[i].triggerChance = static_cast<float>(std::clamp(v, 0.0, 1.0));
                     }),
                     "How likely it is to be raised each time it is passed (1: always)");
        grid->addRow(cat, "Trigger Weight", number(m.notifies[i].minWeight, 0.05F, 2, [i](auto& mm, double v) {
                         mm.notifies[i].minWeight = static_cast<float>(std::clamp(v, 0.0, 1.0));
                     }),
                     "Raised only while the montage weighs at least this much (0: also while it blends)");
    } else if (selection.kind == Kind::State && i < m.notifyStates.size()) {
        cat = grid->addCategory(m.notifyStates[i].name);
        grid->addRow(cat, "Name", text(m.notifyStates[i].name, [i](auto& mm, const std::string& v) { mm.notifyStates[i].name = v; }));
        grid->addRow(cat, "Begin", number(m.notifyStates[i].begin, 0.01F, 2, [i](auto& mm, double v) {
                      mm.notifyStates[i].begin = std::clamp(v, 0.0, mm.notifyStates[i].end);
                  }));
        grid->addRow(cat, "End", number(m.notifyStates[i].end, 0.01F, 2, [i](auto& mm, double v) {
                      mm.notifyStates[i].end = std::max(v, mm.notifyStates[i].begin);
                  }));
        grid->addRow(cat, "Duration", prism::make<prism::Label>(SecondsUVE(m.notifyStates[i].end - m.notifyStates[i].begin) + " s"));
        grid->addRow(cat, "Track", trackPicker(m.notifyStates[i].track, [i](auto& mm, std::uint32_t t) { mm.notifyStates[i].track = t; }));
    } else if (selection.kind == Kind::CurveKey && i < m.curves.size() && selection.key < m.curves[i].keys.size()) {
        const std::size_t k = selection.key;
        cat = grid->addCategory(m.curves[i].name);
        grid->addRow(cat, "Curve", text(m.curves[i].name, [i](auto& mm, const std::string& v) {
                         if (!v.empty()) {
                             mm.curves[i].name = v;
                         }
                     }));
        grid->addRow(cat, "Keys", prism::make<prism::Label>(std::to_string(m.curves[i].keys.size())));
        grid->addRow(cat, "Key Time", number(m.curves[i].keys[k].time, 0.01F, 2, [i, k](auto& mm, double v) { mm.curves[i].keys[k].time = std::max(0.0, v); }));
        grid->addRow(cat, "Key Value", number(m.curves[i].keys[k].value, 0.05F, 2, [i, k](auto& mm, double v) {
                      mm.curves[i].keys[k].value = static_cast<float>(v);
                  }));
    }

    // The asset itself, as the standard Asset Details: what it is made of, and what it is shown on.
    cat = grid->addCategory("Asset");
    {
        std::error_code error;
        const std::filesystem::path relative = std::filesystem::relative(m_openPath, ContentRootUVE(), error);
        grid->addRow(cat, "File", prism::make<prism::Label>(error ? m_openPath.filename().string() : relative.generic_string()));
        grid->addRow(cat, "Length", prism::make<prism::Label>(SecondsUVE(Asset::MontageLengthUVE(m)) + " s"));
        char counts[160];
        std::snprintf(counts, sizeof(counts), "%zu clips, %zu sections, %zu notifies, %zu states, %zu curves, %u tracks", m.segments.size(),
                      m.sections.size(), m.notifies.size(), m.notifyStates.size(), m.curves.size(), static_cast<unsigned>(m.notifyTracks));
        grid->addRow(cat, "Contents", prism::make<prism::Label>(counts));
        // The preview character, picked here as in the toolbar.
        std::vector<std::string> names{"Skeleton of the first clip"};
        std::vector<Asset::AssetGuidUVE> guids{Asset::AssetGuidUVE{}};
        int current = 0;
        const Asset::ProjectFileSnapshotUVE snapshot = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
        for (const Asset::ProjectFileEntryUVE& entry : snapshot.entries) {
            if (entry.kind == Asset::ProjectFileEntryKindUVE::Directory || entry.relativePath.extension() != ".uventity") {
                continue;
            }
            const Asset::AssetGuidUVE guid = m_editor.m_services->GetAssetDatabaseUVE().RegisterUVE(snapshot.contentRoot / entry.relativePath);
            names.push_back(entry.relativePath.generic_string());
            guids.push_back(guid);
            current = guid == m.previewEntity ? static_cast<int>(guids.size()) - 1 : current;
        }
        auto preview = prism::make<prism::ComboBox>(names, current);
        preview->onChange = [this, guids](const int v) {
            if (v >= 0 && static_cast<std::size_t>(v) < guids.size()) {
                m_editor.SetMontagePreviewEntityUVE(guids[static_cast<std::size_t>(v)]);
            }
        };
        grid->addRow(cat, "Preview", preview, "The character the montage is shown on (the game plays it on whoever asks)");
    }

    cat = grid->addCategory("Montage");
    grid->addRow(cat, "Slot", text(m.slot, [](auto& mm, const std::string& v) { mm.slot = v.empty() ? std::string("DefaultSlot") : v; }));
    // A blend: its time, and the shape of its weight over that time.
    const auto blend = [this, &number](const double seconds, const Asset::MontageBlendCurveUVE curve,
                                       std::function<void(Asset::AnimationMontageAssetUVE&, double)> setTime,
                                       std::function<void(Asset::AnimationMontageAssetUVE&, Asset::MontageBlendCurveUVE)> setCurve) {
        auto shape = prism::make<prism::ComboBox>(std::vector<std::string>{"Linear", "Ease In", "Ease Out", "Ease In Out"},
                                                  static_cast<int>(curve));
        shape->onChange = [this, setCurve = std::move(setCurve)](const int v) {
            m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
                setCurve(mm, static_cast<Asset::MontageBlendCurveUVE>(std::clamp(v, 0, 3)));
            });
        };
        auto row = prism::hbox(4.0F);
        row->add(number(seconds, 0.01F, 2, std::move(setTime)), prism::Slot{1.0F});
        row->add(shape, prism::Slot{1.0F});
        return row;
    };
    grid->addRow(cat, "Blend In", blend(m.blendInSeconds, m.blendInCurve,
                                        [](auto& mm, double v) { mm.blendInSeconds = std::max(0.0, v); },
                                        [](auto& mm, Asset::MontageBlendCurveUVE c) { mm.blendInCurve = c; }),
                 "Seconds to blend in, and the shape of the blend");
    grid->addRow(cat, "Blend Out", blend(m.blendOutSeconds, m.blendOutCurve,
                                         [](auto& mm, double v) { mm.blendOutSeconds = std::max(0.0, v); },
                                         [](auto& mm, Asset::MontageBlendCurveUVE c) { mm.blendOutCurve = c; }),
                 "Seconds to blend out, and the shape of the blend");
    grid->addRow(cat, "Blend Out Trigger", number(m.blendOutTriggerSeconds, 0.01F, 2, [](auto& mm, double v) { mm.blendOutTriggerSeconds = v; }));
    auto autoBlend = prism::make<prism::CheckBox>("", m.autoBlendOut);
    autoBlend->onChange = [this](const bool v) {
        m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) { mm.autoBlendOut = v; });
    };
    grid->addRow(cat, "Auto Blend Out", autoBlend);
    grid->addRow(cat, "Rate Scale", number(m.rateScale, 0.05F, 2, [](auto& mm, double v) { mm.rateScale = std::max(0.01, v); }));
    auto rootMotion = prism::make<prism::ComboBox>(std::vector<std::string>{"Off", "In Place", "Apply To Target"},
                                                   static_cast<int>(m.rootMotion));
    rootMotion->onChange = [this](const int v) {
        m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
            mm.rootMotion = static_cast<Asset::MontageRootMotionUVE>(std::clamp(v, 0, 2));
        });
    };
    grid->addRow(cat, "Root Motion", rootMotion);
    m_details->setContent(prism::make<FitWidthUVE>(grid));
}

void EditorMontageEditorUVE::RebuildSectionsUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr || !m_sections) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& m = session->montage;
    // Each section with what follows it (End, Loop, or another section), as the standard Montage Sections.
    auto grid = prism::make<prism::PropertyGrid>();
    const int cat = grid->addCategory("Sections");
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
        auto row = prism::hbox(4.0F);
        row->add(next, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        row->add(preview, CentreUVE());
        grid->addRow(cat, section.name, row, "Starts at " + SecondsUVE(section.time) + " s");
    }
    auto add = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    add->setTooltip("Add a section at the playhead");
    add->onClick = [this] {
        const EditorUVE::MontageEditSessionUVE* const s = m_editor.MontageEditSessionPtrUVE();
        const double at = s != nullptr ? s->playhead : 0.0;
        static_cast<void>(at);
        AddAtPlayheadUVE(AddKindUVE::Section);
    };
    grid->addRow(cat, "", add);
    m_sections->setContent(prism::make<FitWidthUVE>(grid));
}

void EditorMontageEditorUVE::RebuildNotifiesUVE() {
    EditorUVE::MontageEditSessionUVE* const session = m_editor.MontageEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    const Asset::AnimationMontageAssetUVE& m = session->montage;
    // Notifies and states together, in time order.
    m_notifyRows.clear();
    for (std::size_t i = 0U; i < m.notifies.size(); ++i) {
        m_notifyRows.emplace_back(false, i);
    }
    for (std::size_t i = 0U; i < m.notifyStates.size(); ++i) {
        m_notifyRows.emplace_back(true, i);
    }
    std::stable_sort(m_notifyRows.begin(), m_notifyRows.end(), [&m](const auto& a, const auto& b) {
        const double ta = a.first ? m.notifyStates[a.second].begin : m.notifies[a.second].time;
        const double tb = b.first ? m.notifyStates[b.second].begin : m.notifies[b.second].time;
        return ta < tb;
    });
    if (m_notifyList) {
        m_notifyList->setRowCount(static_cast<int>(m_notifyRows.size()));
        for (std::size_t row = 0U; row < m_notifyRows.size(); ++row) {
            const bool state = m_notifyRows[row].first;
            if (selection.index == m_notifyRows[row].second &&
                selection.kind == (state ? SelectionUVE::KindUVE::State : SelectionUVE::KindUVE::Notify)) {
                m_notifyList->select({static_cast<int>(row)});
            }
        }
        m_notifyList->invalidate();
    }
    // The curves: pick the one the Curves track edits; rename, delete, add.
    if (m_curveList) {
        auto grid = prism::make<prism::PropertyGrid>();
        const int cat = grid->addCategory("Curves");
        if (activeCurve >= m.curves.size() && !m.curves.empty()) {
            activeCurve = 0U;
        }
        for (std::size_t ci = 0U; ci < m.curves.size(); ++ci) {
            auto row = prism::hbox(4.0F);
            auto pick = prism::make<prism::Button>(ci == activeCurve ? "Editing" : "Edit", prism::Icon::Curve,
                                                   ci == activeCurve ? prism::ButtonStyle::Primary : prism::ButtonStyle::Default);
            pick->onClick = [this, ci] {
                activeCurve = ci;
                m_shownValid = false;
            };
            auto remove = ToolButtonUVE(prism::Icon::Trash, "Delete this curve");
            remove->onClick = [this, ci] {
                m_editor.EditMontageUVE([ci](Asset::AnimationMontageAssetUVE& mm) {
                    if (ci < mm.curves.size()) {
                        mm.curves.erase(mm.curves.begin() + static_cast<std::ptrdiff_t>(ci));
                    }
                });
                selection = SelectionUVE{};
            };
            auto name = prism::make<prism::TextInput>(m.curves[ci].name, "Name");
            name->onCommit = [this, ci](const std::string& v) {
                if (v.empty()) {
                    return;
                }
                m_editor.EditMontageUVE([&](Asset::AnimationMontageAssetUVE& mm) {
                    if (ci < mm.curves.size() && mm.curves[ci].name != v) {
                        mm.curves[ci].name = UniqueCurveNameUVE(mm, v);   // two curves never share a name
                    }
                });
            };
            row->add(name, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            row->add(pick, CentreUVE());
            row->add(remove, CentreUVE());
            grid->addRow(cat, std::to_string(m.curves[ci].keys.size()) + " keys", row);
        }
        auto add = prism::make<prism::Button>("Add Curve", prism::Icon::Plus, prism::ButtonStyle::Default);
        add->onClick = [this] { AddAtPlayheadUVE(AddKindUVE::Curve); };
        grid->addRow(cat, "", add);
        m_curveList->setContent(prism::make<FitWidthUVE>(grid));
    }
}

// ---------------------------------------------------------------------------------------- render

void EditorMontageEditorUVE::RenderUVE() {
    if (m_window == 0 || m_editor.MontageEditSessionPtrUVE() == nullptr) {
        return;
    }
    std::vector<EditorHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInputUVE input = m_host.CaptureViewInputUVE(m_window, rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture =
                m_editor.RenderMontagePreviewUVE(Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used, input);
            m_host.ApplyViewCursorUVE(m_window, input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                EditorHostUVE::ViewImageUVE view{};
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
