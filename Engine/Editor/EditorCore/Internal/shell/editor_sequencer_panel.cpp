// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_sequencer_panel.h"

#include "editor_chrome.h"
#include "editor_keyframe_view.h"
#include "editor_timeline_tracks.h"

#include "animation_sequencer_component.h"
#include "i_asset_database.h"
#include "i_project_file_index.h"
#include "name_component.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <filesystem>
#include <functional>

namespace UVE::Editor {
namespace {

[[nodiscard]] prism::Slot CentreSlotUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string NameOfUVE(const Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity) {
    return em.HasComponentUVE<Scene::NameComponentUVE>(entity) ? em.GetComponentUVE<Scene::NameComponentUVE>(entity).name
                                                               : std::string{"AnimationSequencer"};
}

/// A colour swatch and the channel's name, for the legend.
class LegendUVE final : public prism::Widget {
public:
    prism::Vec2 measure(prism::Vec2) override {
        const prism::Theme& t = prism::theme();
        float w = 0.0F;
        for (int channel = 0; channel < 3; ++channel) {
            w += 14.0F + t.small->width(TimelineChannelNameUVE(channel)) + 12.0F;
        }
        return m_desired = constrain({w + 14.0F + t.small->width("Event"), 20.0F});
    }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        float x = m_rect.x;
        const auto entry = [&](const prism::Color color, const std::string& name) {
            c.fillRoundRect(prism::Rect{x, m_rect.y + m_rect.h * 0.5F - 4.0F, 8.0F, 8.0F}, 2.0F, color);
            x += 12.0F;
            c.textIn(*t.small, prism::Rect{x, m_rect.y + m_rect.h * 0.5F - 10.0F, t.small->width(name) + 4.0F, 20.0F}, name, t.textDim);
            x += t.small->width(name) + 12.0F;
        };
        for (int channel = 0; channel < 3; ++channel) {
            entry(TimelineChannelColorUVE(channel), TimelineChannelNameUVE(channel));
        }
        entry(TimelineEventColorUVE(), "Event");
    }
};

} // namespace

EditorSequencerPanelUVE::EditorSequencerPanelUVE(EditorUVE& editor) : m_editor(editor) {}
EditorSequencerPanelUVE::~EditorSequencerPanelUVE() = default;

const EditorUVE::AnimationTimelineViewUVE& EditorSequencerPanelUVE::ResyncUVE() {
    m_view = m_editor.SyncAnimationTimelineUVE();
    return m_view;
}

void EditorSequencerPanelUVE::AskNameUVE(const std::string& title, const std::string& initial,
                                             std::function<void(const std::string&)> done) {
    auto field = prism::make<prism::TextInput>(initial, "Name");
    std::weak_ptr<prism::TextInput> weak = field;
    const auto apply = [weak, done = std::move(done)] {
        if (auto f = weak.lock(); f && !f->text().empty() && done) {
            done(f->text());
        }
    };
    field->onCommit = [apply](const std::string&) {
        apply();
        if (prism::Application::exists()) {
            prism::Application::get().closeAllPopups();
        }
    };
    prism::showDialog(title, field, {prism::DialogButton{"Cancel", false, {}}, prism::DialogButton{"OK", true, apply}}, 380.0F);
    if (prism::Application::exists()) {
        prism::Application::get().setFocus(field.get());
    }
}

// ---------------------------------------------------------------------------------------- build

prism::Ref<prism::Widget> EditorSequencerPanelUVE::BuildUVE() {
    m_keys = prism::make<KeyframeViewUVE>(*this);
    auto page = prism::vbox();
    page->add(BuildToolbarUVE());
    page->add(m_keys, prism::Slot{1.0F});
    page->add(BuildStatusBarUVE());
    m_shownPlayer.clear();
    m_shownClip.clear();
    m_shownClock.clear();
    m_shownInfo.clear();
    m_shownStatus.clear();
    return page;
}

prism::Ref<prism::Widget> EditorSequencerPanelUVE::BuildToolbarUVE() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    m_playerButton = ToolButtonUVE(prism::Icon::ChevronDown, "The AnimationSequencer shown (selecting one in Components picks it too)",
                                   "Player");
    m_playerButton->onClick = [this] { ShowPlayerMenuUVE(); };
    row->add(m_playerButton, CentreSlotUVE());
    m_clipButton = ToolButtonUVE(prism::Icon::AnimClip, "This player's animations: switch, new, add, import, link, export, rename",
                                 "No animation");
    m_clipButton->onClick = [this] { ShowClipMenuUVE(); };
    row->add(m_clipButton, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());

    const auto transport = [&](const prism::Icon icon, const char* tip, std::function<void()> action) {
        auto button = ToolButtonUVE(icon, tip);
        button->onClick = [this, action = std::move(action)] {
            action();
            if (m_keys) {
                m_keys->invalidate();
            }
        };
        row->add(button, CentreSlotUVE());
        return button;
    };
    transport(prism::Icon::SkipStart, "First frame (Home)", [this] { m_editor.SetAnimationTimelineTimeUVE(0.0); });
    transport(prism::Icon::StepBack, "Previous frame (Left)", [this] { m_editor.StepAnimationTimelineFrameUVE(-1); });
    m_playButton = transport(prism::Icon::Play, "Play / Pause the preview on this entity (Space)",
                             [this] { m_editor.ToggleAnimationTimelinePlayUVE(); });
    transport(prism::Icon::Stop, "Stop and go back to the start", [this] { m_editor.StopAnimationTimelinePlaybackUVE(); });
    transport(prism::Icon::StepForward, "Next frame (Right)", [this] { m_editor.StepAnimationTimelineFrameUVE(1); });
    transport(prism::Icon::SkipEnd, "Last frame (End)", [this] { m_editor.SetAnimationTimelineTimeUVE(m_view.durationSeconds); });
    m_loopButton = ToolButtonUVE(prism::Icon::NodeLoop, "Loop (the player's loop mode)");
    m_loopButton->setCheckable(true);
    m_loopButton->onClick = [this] { m_editor.SetAnimationTimelineLoopUVE(!m_editor.m_timeline.loop); };
    row->add(m_loopButton, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());
    m_clock = prism::make<prism::Label>("0 / 0", prism::FontRole::Mono);
    m_clock->setTooltip("The playhead's frame / the clip's frames, and its time");
    auto clockBox = prism::make<prism::SizeBox>();
    clockBox->setWidthOverride(128.0F);
    clockBox->setContent(m_clock);
    row->add(clockBox, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());
    m_keyButton = ToolButtonUVE(prism::Icon::Key, "Add a key to the selected bone at the playhead (K)", "Key");
    m_keyButton->onClick = [this] {
        m_editor.InsertAnimationTimelineKeyUVE();
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    row->add(m_keyButton, CentreSlotUVE());
    m_saveButton = ToolButtonUVE(prism::Icon::Save, "Save: write the edited clip to its .uvanim (Ctrl+S)");
    m_saveButton->onClick = [this] { m_editor.SaveAnimationTimelineClipUVE(); };
    row->add(m_saveButton, CentreSlotUVE());
    auto undo = ToolButtonUVE(prism::Icon::Undo, "Undo a key edit (Ctrl+Z)");
    undo->onClick = [this] {
        m_editor.UndoAnimationTimelineUVE();
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    auto redo = ToolButtonUVE(prism::Icon::Redo, "Redo (Ctrl+Y)");
    redo->onClick = [this] {
        m_editor.RedoAnimationTimelineUVE();
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    row->add(undo, CentreSlotUVE());
    row->add(redo, CentreSlotUVE());
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_dopeButton = ToolButtonUVE(prism::Icon::DopeSheet, "Dope Sheet: keys per bone and channel", "Dope");
    m_dopeButton->setCheckable(true);
    m_dopeButton->onClick = [this] {
        m_editor.m_timeline.curves = false;
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    m_curvesButton = ToolButtonUVE(prism::Icon::Curve, "The selected bone's values over time; drag a point to change it", "Curves");
    m_curvesButton->setCheckable(true);
    m_curvesButton->onClick = [this] {
        m_editor.m_timeline.curves = true;
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    auto fit = ToolButtonUVE(prism::Icon::Frame, "Fit: show the whole clip (F). Ctrl + wheel zooms, Shift + wheel pans");
    fit->onClick = [this] {
        if (m_keys) {
            m_keys->FitUVE();
        }
    };
    row->add(m_dopeButton, CentreSlotUVE());
    row->add(m_curvesButton, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());
    row->add(fit, CentreSlotUVE());

    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

prism::Ref<prism::Widget> EditorSequencerPanelUVE::BuildStatusBarUVE() {
    auto row = prism::hbox(10.0F);
    row->setPadding(prism::Margin(6.0F, 2.0F, 8.0F, 2.0F));
    m_filter = prism::make<prism::TextInput>(m_editor.m_timeline.filter, "Filter bones");
    m_filter->setLeadingIcon(prism::Icon::Search);
    m_filter->onChanged = [this](const std::string& text) {
        m_editor.m_timeline.filter = text;
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    auto filterBox = prism::make<prism::SizeBox>();
    filterBox->setWidthOverride(218.0F);
    filterBox->setContent(m_filter);
    row->add(filterBox, CentreSlotUVE());
    row->add(prism::make<LegendUVE>(), CentreSlotUVE());
    m_info = prism::make<prism::Label>("", prism::FontRole::Small);
    row->add(m_info, CentreSlotUVE());
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    bar->setChild(row);
    return bar;
}

// -------------------------------------------------------------------------------------- refresh

void EditorSequencerPanelUVE::RefreshUVE(const double deltaSeconds, const bool shown) {
    if (!shown) {
        if (m_editor.m_timeline.playing) {
            m_editor.ToggleAnimationTimelinePlayUVE();   // a hidden preview does not run on
        }
        return;
    }
    m_editor.StepAnimationTimelineUVE(deltaSeconds);
    const EditorUVE::AnimationTimelineViewUVE& view = ResyncUVE();
    const EditorUVE::AnimationTimelineStateUVE& state = m_editor.m_timeline;
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();

    const auto setText = [](prism::Button& button, std::string& shown, const std::string& text) {
        if (shown != text) {
            shown = text;
            button.setText(text);
        }
    };
    if (m_playerButton) {
        m_playerButton->setVisible(view.players.size() > 1U);
        if (view.player != Scene::kInvalidEntityUVE) {
            setText(*m_playerButton, m_shownPlayer, NameOfUVE(em, view.player));
        }
    }
    if (m_clipButton) {
        const std::string clip =
            view.clip != nullptr ? m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(state.clipGuid).stem().string()
            : view.player != Scene::kInvalidEntityUVE &&
                    em.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(view.player).clip != Asset::AssetGuidUVE{}
                ? std::string{"(unreadable clip)"}
                : std::string{"No animation"};
        setText(*m_clipButton, m_shownClip, clip + (state.dirty ? "  *" : ""));
    }
    if (m_playButton) {
        m_playButton->setIcon(state.playing ? prism::Icon::Pause : prism::Icon::Play);
    }
    if (m_loopButton && m_loopButton->checked() != state.loop) {
        m_loopButton->setChecked(state.loop);
    }
    if (m_saveButton) {
        m_saveButton->setEnabled(state.dirty);
    }
    if (m_keyButton) {
        m_keyButton->setEnabled(view.clip != nullptr);
    }
    if (m_dopeButton && m_dopeButton->checked() == state.curves) {
        m_dopeButton->setChecked(!state.curves);
    }
    if (m_curvesButton && m_curvesButton->checked() != state.curves) {
        m_curvesButton->setChecked(state.curves);
    }
    char clock[64];
    std::snprintf(clock, sizeof(clock), "%ld / %ld   %.2f s", std::lround(state.timeSeconds * view.frameRate),
                  std::lround(view.durationSeconds * view.frameRate), state.timeSeconds);
    if (m_clock && m_shownClock != clock) {
        m_shownClock = clock;
        m_clock->setText(clock);
    }
    std::string info;
    if (view.clip != nullptr) {
        char text[160];
        std::snprintf(text, sizeof(text), "%.2f s  |  %ld frames  |  %.0f fps  |  %zu tracks", view.clip->durationSeconds,
                      std::lround(view.durationSeconds * view.frameRate), view.frameRate, view.clip->bones.size());
        info = text;
        if (view.clip->HasMorphTracksUVE()) {
            info += "  |  " + std::to_string(view.clip->morphWeights.size()) + " blend shapes";
        }
    }
    if (m_info && m_shownInfo != info) {
        m_shownInfo = info;
        m_info->setText(info);
    }
    const std::string status = !state.status.empty()         ? state.status
                               : !state.selectedKeys.empty() ? std::to_string(state.selectedKeys.size()) + " selected"
                                                             : std::string{};
    if (m_status && m_shownStatus != status) {
        m_shownStatus = status;
        m_status->setText(status);
    }
    if (m_keys) {
        m_keys->invalidate();
    }
}

// ---------------------------------------------------------------------------------------- menus

void EditorSequencerPanelUVE::ShowPlayerMenuUVE() {
    const EditorUVE::AnimationTimelineViewUVE& view = ResyncUVE();
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    std::vector<prism::MenuItem> items;
    for (const Scene::EntityUVE player : view.players) {
        items.push_back(prism::MenuItem::Check(NameOfUVE(em, player), player == view.player,
                                               [this, player] { m_editor.SelectEntityUVE(player); }));
    }
    if (m_playerButton) {
        const prism::Rect r = m_playerButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

void EditorSequencerPanelUVE::ShowClipMenuUVE() {
    const EditorUVE::AnimationTimelineViewUVE& view = ResyncUVE();
    if (view.player == Scene::kInvalidEntityUVE) {
        return;
    }
    const Scene::EntityUVE player = view.player;
    const Scene::EntityUVE skeleton = view.skeleton;
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const auto& component = em.GetComponentUVE<Scene::AnimationSequencerComponentUVE>(player);
    m_editor.m_timeline.clipCards.clear();   // names and lengths as the files are now
    m_editor.RefreshSequencerLinkedClipsUVE(player);

    const auto clipItem = [&](const Asset::AssetGuidUVE guid) {
        const EditorUVE::AnimationTimelineStateUVE::ClipCardUVE& card = m_editor.AnimationTimelineClipCardUVE(guid);
        char right[48];
        std::snprintf(right, sizeof(right), "%s  %.2f s", card.skeletal ? "bones" : "object", card.durationSeconds);
        return prism::MenuItem::Check(card.name, guid == component.clip,
                                      [this, player, guid] { m_editor.ChooseAnimationTimelineClipUVE(player, guid); }, right);
    };
    std::vector<prism::MenuItem> items;
    const std::vector<Asset::AssetGuidUVE> own = m_editor.AnimationTimelineOwnClipsUVE(player);
    for (const Asset::AssetGuidUVE guid : own) {
        items.push_back(clipItem(guid));
    }
    if (own.empty()) {
        items.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    if (!m_editor.m_timeline.linkedLibraryName.empty()) {
        const std::vector<Asset::AssetGuidUVE> linked = m_editor.AnimationTimelineLinkedClipsUVE(player);
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item(m_editor.m_timeline.linkedLibraryName, {}, "", prism::Icon::AnimLibrary).disabled());
        for (const Asset::AssetGuidUVE guid : linked) {
            items.push_back(clipItem(guid));
        }
    }

    // The current clip's own commands.
    if (component.clip != Asset::AssetGuidUVE{}) {
        const Asset::AssetGuidUVE current = component.clip;
        const bool readable = m_editor.AnimationTimelineClipCardUVE(current).readable;
        std::vector<prism::MenuItem> clipMenu;
        clipMenu.push_back(prism::MenuItem::Item("Rename...", [this] {
            const std::string name = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(m_editor.m_timeline.clipGuid).stem().string();
            AskNameUVE("Animation Name", name, [this](const std::string& to) { static_cast<void>(m_editor.RenameAnimationTimelineClipUVE(to)); });
        }));
        clipMenu.back().enabled = m_editor.m_timeline.clip != nullptr;
        clipMenu.push_back(prism::MenuItem::Item("Duplicate", [this, player, current] { m_editor.DuplicateAnimationTimelineClipUVE(player, current); },
                                                 "", prism::Icon::Duplicate));
        clipMenu.back().enabled = readable;
        clipMenu.push_back(prism::MenuItem::Sep());
        clipMenu.push_back(prism::MenuItem::Item("Remove from Player", [this, player, current] { m_editor.RemoveAnimationTimelineClipUVE(player, current); },
                                                 "", prism::Icon::Trash));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Sub("Current Animation", std::move(clipMenu), prism::Icon::AnimClip));
    }

    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("New Animation", [this, player, skeleton] {
        if (m_editor.CreateAnimationTimelineClipUVE(player, skeleton)) {
            ResyncUVE();
            const std::string name = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(m_editor.m_timeline.clipGuid).stem().string();
            AskNameUVE("Animation Name", name, [this](const std::string& to) { static_cast<void>(m_editor.RenameAnimationTimelineClipUVE(to)); });
        }
    }, "", prism::Icon::Plus));

    // Any clip or library in Content: the file dialog, or straight from the quick list.
    const Asset::ProjectFileSnapshotUVE project = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
    std::vector<prism::MenuItem> addMenu;
    addMenu.push_back(prism::MenuItem::Item("Browse...", [this, player] {
        m_editor.BrowseAnimationTimelineUVE(player, EditorUVE::AnimationTimelineBrowseUVE::AddClip);
    }, "", prism::Icon::Browse));
    std::vector<prism::MenuItem> libraryMenu;
    libraryMenu.push_back(prism::MenuItem::Item("Browse...", [this, player] {
        m_editor.BrowseAnimationTimelineUVE(player, EditorUVE::AnimationTimelineBrowseUVE::ImportLibrary);
    }, "", prism::Icon::Browse));
    bool anyClip = false;
    bool anyLibrary = false;
    for (const Asset::ProjectFileEntryUVE& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKindUVE::File) {
            continue;
        }
        const std::string folder = entry.relativePath.parent_path().generic_string();
        const std::filesystem::path absolute = project.contentRoot / entry.relativePath;
        if (entry.relativePath.extension() == ".uvanim") {
            const bool listed = entry.registeredAssetGuid.has_value() && std::ranges::find(own, *entry.registeredAssetGuid) != own.end();
            if (listed) {
                continue;
            }
            if (!anyClip) {
                addMenu.push_back(prism::MenuItem::Sep());
                anyClip = true;
            }
            addMenu.push_back(prism::MenuItem::Item(entry.relativePath.stem().string(), [this, player, absolute] {
                static_cast<void>(m_editor.AddClipToAnimationSequencerUVE(player, absolute));
            }, folder.empty() ? "Content" : folder));
        } else if (entry.relativePath.extension() == ".uvanimlib") {
            if (!anyLibrary) {
                libraryMenu.push_back(prism::MenuItem::Sep());
                anyLibrary = true;
            }
            libraryMenu.push_back(prism::MenuItem::Item(entry.relativePath.stem().string(), [this, player, absolute] {
                static_cast<void>(m_editor.ImportAnimationLibraryIntoSequencerUVE(player, absolute));
            }, folder.empty() ? "Content" : folder));
        }
    }
    if (!anyClip) {
        addMenu.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    if (!anyLibrary) {
        libraryMenu.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    items.push_back(prism::MenuItem::Sub("Add from Project", std::move(addMenu), prism::Icon::FolderOpen));
    items.push_back(prism::MenuItem::Sub("Import Library", std::move(libraryMenu), prism::Icon::AnimLibrary));
    if (component.libraryRef == Asset::AssetGuidUVE{}) {
        items.push_back(prism::MenuItem::Item("Link Library...", [this, player] {
            m_editor.BrowseAnimationTimelineUVE(player, EditorUVE::AnimationTimelineBrowseUVE::LinkLibrary);
        }));
    } else {
        const std::filesystem::path linked = m_editor.m_services->GetAssetDatabaseUVE().ResolveUVE(component.libraryRef);
        items.push_back(prism::MenuItem::Item(linked.empty() ? std::string{"Unlink Library"} : "Unlink " + linked.stem().string(),
                                              [this, player] { static_cast<void>(m_editor.UnlinkAnimationLibraryFromSequencerUVE(player)); }));
    }
    items.push_back(prism::MenuItem::Item("Export to Library...", [this, player] {
        m_editor.BrowseAnimationTimelineUVE(player, EditorUVE::AnimationTimelineBrowseUVE::ExportLibrary);
    }, "", prism::Icon::Save));
    if (m_clipButton) {
        const prism::Rect r = m_clipButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

} // namespace UVE::Editor
