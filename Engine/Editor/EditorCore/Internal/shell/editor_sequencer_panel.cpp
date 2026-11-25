// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_sequencer_panel.h"
#include "model_animations.h"

#include "animation_clip_editing.h"
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
#include "prism/SearchMenu.h"
#include "prism/Canvas.h"

#include "editor_asset_table.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

[[nodiscard]] std::string Utf8Of(const std::filesystem::path& path) {
    const std::u8string u8 = path.u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] std::string Utf8GenericOf(const std::filesystem::path& path) {
    const std::u8string u8 = path.generic_u8string();
    return std::string(reinterpret_cast<const char*>(u8.data()), u8.size());
}

[[nodiscard]] prism::Slot CentreSlot() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string NameOf(const Scene::IEntityManager& em, const Scene::Entity entity) {
    if (entity == Scene::kInvalidEntity || !em.IsAlive(entity)) {
        return "AnimationSequencer";
    }
    return em.HasComponent<Scene::NameComponent>(entity) ? em.GetComponent<Scene::NameComponent>(entity).name
                                                               : std::string{"AnimationSequencer"};
}

/// A colour swatch and the channel's name, for the legend.
class Legend final : public prism::Widget {
public:
    prism::Vec2 measure(prism::Vec2) override {
        const prism::Theme& t = prism::theme();
        float w = 0.0F;
        for (int channel = 0; channel < 3; ++channel) {
            w += 14.0F + t.small->width(TimelineChannelName(channel)) + 12.0F;
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
            entry(TimelineChannelColor(channel), TimelineChannelName(channel));
        }
        entry(TimelineEventColor(), "Event");
    }
};

} // namespace

EditorSequencerPanel::EditorSequencerPanel(Editor& editor) : m_editor(editor) {}
EditorSequencerPanel::~EditorSequencerPanel() = default;

const Editor::AnimationTimelineView& EditorSequencerPanel::Resync() {
    m_view = m_editor.SyncAnimationTimeline();
    return m_view;
}

namespace {

/// The panel's frame: Animation Libraries and clips dropped from the Asset Table land here.
class SequencerDrop final : public prism::Border {
public:
    std::function<bool(const std::string&)> accepts;
    std::function<bool(const std::string&)> dropped;
    bool onDragOver(const prism::DragDropEvent& e) override {
        const std::string* path = e.operation && e.operation->tag() == EditorAssetTable::kDragTag ? e.operation->getPayload<std::string>() : nullptr;
        const bool ok = path != nullptr && accepts && accepts(*path);
        if (ok != m_hot) {
            m_hot = ok;
            invalidate();
        }
        return ok;
    }
    void onDragLeave() override {
        m_hot = false;
        invalidate();
    }
    bool onDrop(const prism::DragDropEvent& e) override {
        const bool ok = onDragOver(e);
        m_hot = false;
        invalidate();
        return ok && dropped && dropped(*e.operation->getPayload<std::string>());
    }
    void paint(prism::Canvas& c) override {
        prism::Border::paint(c);
        if (m_hot) {
            c.strokeRoundRect(rect().deflated(2.0F), 4.0F, prism::theme().highlight, 2);
        }
    }

private:
    bool m_hot = false;
};

} // namespace

void EditorSequencerPanel::AskName(const std::string& title, const std::string& initial,
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

prism::Ref<prism::Widget> EditorSequencerPanel::Build() {
    m_keys = prism::make<KeyframeView>(*this);
    auto column = prism::vbox();
    column->add(BuildToolbar());
    column->add(m_keys, prism::Slot{1.0F});
    column->add(BuildStatusBar());
    auto page = prism::make<SequencerDrop>();
    page->accepts = [](const std::string& path) {
        const std::string ext = Utf8GenericOf(std::filesystem::path{path}.extension());
        return ext == ".uvanimlib" || ext == ".uvanim";
    };
    page->dropped = [this](const std::string& path) { return AcceptDropped(path); };
    page->setChild(column);
    m_shownPlayer.clear();
    m_shownClip.clear();
    m_shownClock.clear();
    m_shownInfo.clear();
    m_shownStatus.clear();
    return page;
}

prism::Ref<prism::Widget> EditorSequencerPanel::BuildToolbar() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    m_playerButton = ToolButton(prism::Icon::None, "The AnimationSequencer shown (selecting one in Components picks it too)",
                                   "Player");
    m_playerButton->setMaxSize(120.0F, 26.0F);
    m_playerButton->onClick = [this] { ShowPlayerMenu(); };
    row->add(m_playerButton, CentreSlot());
    m_clipButton = ToolButton(prism::Icon::AnimClip, "This player's animations: switch, new, add, load or export a library, rename",
                                 "No animation");
    m_clipButton->setMaxSize(168.0F, 26.0F);
    m_clipButton->onClick = [this] { ShowClipMenu(); };
    row->add(m_clipButton, CentreSlot());
    row->add(HSeparator(), CentreSlot());

    const auto transport = [&](const prism::Icon icon, const char* tip, std::function<void()> action) {
        auto button = ToolButton(icon, tip);
        button->onClick = [this, action = std::move(action)] {
            action();
            if (m_keys) {
                m_keys->invalidate();
            }
        };
        row->add(button, CentreSlot());
        return button;
    };
    transport(prism::Icon::SkipStart, "First frame (Home)", [this] { m_editor.SetAnimationTimelineTime(0.0); });
    transport(prism::Icon::StepBack, "Previous frame (Left)", [this] { m_editor.StepAnimationTimelineFrame(-1); });
    m_playButton = transport(prism::Icon::Play, "Play / Pause the preview on this entity (Space)",
                             [this] { m_editor.ToggleAnimationTimelinePlay(); });
    transport(prism::Icon::Stop, "Stop and go back to the start", [this] { m_editor.StopAnimationTimelinePlayback(); });
    transport(prism::Icon::StepForward, "Next frame (Right)", [this] { m_editor.StepAnimationTimelineFrame(1); });
    transport(prism::Icon::SkipEnd, "Last frame (End)", [this] { m_editor.SetAnimationTimelineTime(m_view.durationSeconds); });
    m_loopButton = ToolButton(prism::Icon::NodeLoop, "Loop (the player's loop mode)");
    m_loopButton->setCheckable(true);
    m_loopButton->onClick = [this] { m_editor.SetAnimationTimelineLoop(!m_editor.m_timeline.loop); };
    row->add(m_loopButton, CentreSlot());
    row->add(HSeparator(), CentreSlot());
    m_clock = prism::make<prism::Label>("0 / 0", prism::FontRole::Mono);
    m_clock->setTooltip("The playhead's frame / the clip's frames, and its time");
    auto clockBox = prism::make<prism::SizeBox>();
    clockBox->setWidthOverride(128.0F);
    clockBox->setContent(m_clock);
    row->add(clockBox, CentreSlot());
    row->add(HSeparator(), CentreSlot());
    m_recordButton = ToolButton(prism::Icon::Dot, "Record: move the animated object (gizmo or Inspector) and its transform is keyed "
                                                     "at the playhead", "Rec");
    m_recordButton->setCheckable(true);
    m_recordButton->onClick = [this] { m_editor.SetAnimationTimelineRecording(!m_editor.m_timeline.recording); };
    row->add(m_recordButton, CentreSlot());
    m_keyButton = ToolButton(prism::Icon::Key, "Add a key at the playhead: the selected bone, else the selected object (the animated one or one under it) (K)", "Key");
    m_keyButton->onClick = [this] {
        // A bone's row selected keys that bone; otherwise the object the clip moves, as it stands.
        if (m_editor.m_timeline.clip != nullptr && !m_editor.m_selectedSkeletonBone.empty()) {
            static_cast<void>(m_editor.KeyAnimationTimelineBone(m_editor.m_selectedSkeletonBone));
        } else {
            static_cast<void>(m_editor.KeyAnimationTimelineObject());
        }
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    row->add(m_keyButton, CentreSlot());
    m_saveButton = ToolButton(prism::Icon::Save, "Save: write the edited clip to its .uvanim (Ctrl+S)");
    m_saveButton->onClick = [this] { m_editor.SaveAnimationTimelineClip(); };
    row->add(m_saveButton, CentreSlot());
    auto undo = ToolButton(prism::Icon::Undo, "Undo a key edit (Ctrl+Z)");
    undo->onClick = [this] {
        m_editor.UndoAnimationTimeline();
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    auto redo = ToolButton(prism::Icon::Redo, "Redo (Ctrl+Y)");
    redo->onClick = [this] {
        m_editor.RedoAnimationTimeline();
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    row->add(undo, CentreSlot());
    row->add(redo, CentreSlot());
    row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_dopeButton = ToolButton(prism::Icon::DopeSheet, "Dope Sheet: keys per bone and channel", "Dope");
    m_dopeButton->setCheckable(true);
    m_dopeButton->onClick = [this] {
        m_editor.m_timeline.curves = false;
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    m_curvesButton = ToolButton(prism::Icon::Curve, "The selected bone's values over time; drag a point to change it", "Curves");
    m_curvesButton->setCheckable(true);
    m_curvesButton->onClick = [this] {
        m_editor.m_timeline.curves = true;
        if (m_keys) {
            m_keys->invalidate();
        }
    };
    row->add(m_dopeButton, CentreSlot());
    row->add(m_curvesButton, CentreSlot());
    row->add(HSeparator(), CentreSlot());
    m_settingsButton = ToolButton(prism::Icon::Settings, "Sequencer settings: playback speed, follow the playhead, snapping, time display");
    m_settingsButton->onClick = [this] { ShowSettingsMenu(); };
    row->add(m_settingsButton, CentreSlot());


    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

prism::Ref<prism::Widget> EditorSequencerPanel::BuildStatusBar() {
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
    row->add(filterBox, CentreSlot());
    row->add(prism::make<Legend>(), CentreSlot());
    m_info = prism::make<prism::Label>("", prism::FontRole::Small);
    row->add(m_info, CentreSlot());
    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
    // Zoom, at the keys' own edge: out, in, and the whole clip.
    auto fit = ToolButton(prism::Icon::Frame, "Fit: show the whole clip (F). Ctrl + wheel zooms, Shift + wheel pans");
    fit->onClick = [this] {
        if (m_keys) {
            m_keys->Fit();
        }
    };
    auto zoomOut = ToolButton(prism::Icon::Minus, "Zoom out of the keys (Ctrl + wheel)");
    zoomOut->onClick = [this] {
        if (m_keys) {
            m_keys->Zoom(0.8);
        }
    };
    auto zoomIn = ToolButton(prism::Icon::Plus, "Zoom in on the keys, round the playhead (Ctrl + wheel)");
    zoomIn->onClick = [this] {
        if (m_keys) {
            m_keys->Zoom(1.25);
        }
    };
    row->add(zoomOut, CentreSlot());
    row->add(zoomIn, CentreSlot());
    row->add(fit, CentreSlot());
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::statusBar));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 0.0F));
    bar->setChild(row);
    return bar;
}

// -------------------------------------------------------------------------------------- refresh

void EditorSequencerPanel::Refresh(const double deltaSeconds, const bool shown) {
    m_editor.m_timeline.panelShown = shown;
    if (!shown) {
        if (m_editor.m_timeline.playing) {
            m_editor.ToggleAnimationTimelinePlay();   // a hidden preview does not run on
        }
        return;
    }
    m_editor.StepAnimationTimeline(deltaSeconds);
    const Editor::AnimationTimelineView& view = Resync();
    const Editor::AnimationTimelineState& state = m_editor.m_timeline;
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();

    if (m_playerButton) {
        m_playerButton->setVisible(view.players.size() > 1U);
        if (view.player != Scene::kInvalidEntity && em.IsAlive(view.player)) {
            const std::string playerName = NameOf(em, view.player);
            if (m_shownPlayer != playerName) {
                m_shownPlayer = playerName;
                m_playerButton->setText(playerName);
                m_playerButton->setTooltip(playerName + " — The AnimationSequencer shown (selecting one in Components picks it too)");
            }
        }
    }
    if (m_clipButton) {
        const bool hasValidPlayer =
            view.player != Scene::kInvalidEntity && em.IsAlive(view.player) &&
            em.HasComponent<Scene::AnimationSequencerComponent>(view.player);
        const std::string clip =
            view.clip != nullptr ? Asset::AssetDisplayName(m_editor.m_services->GetAssetDatabase().Resolve(state.clipGuid))
            : hasValidPlayer && em.GetComponent<Scene::AnimationSequencerComponent>(view.player).clip != Asset::AssetGuid{}
                ? std::string{"(unreadable clip)"}
                : std::string{"No animation"};
        const std::string buttonLabel = clip + (state.dirty ? "  *" : "");
        if (m_shownClip != buttonLabel) {
            m_shownClip = buttonLabel;
            m_clipButton->setText(buttonLabel);
            m_clipButton->setTooltip(
                clip + (state.dirty ? " (unsaved edits)" : "") +
                " — This player's animations: switch, new, add, load or export a library, rename");
        }
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
    if (m_recordButton) {
        m_recordButton->setEnabled(view.clip != nullptr);
        if (m_recordButton->checked() != state.recording) {
            m_recordButton->setChecked(state.recording);
        }
    }
    if (m_dopeButton && m_dopeButton->checked() == state.curves) {
        m_dopeButton->setChecked(!state.curves);
    }
    if (m_curvesButton && m_curvesButton->checked() != state.curves) {
        m_curvesButton->setChecked(state.curves);
    }
    char clock[64];
    if (state.clockInSeconds) {
        std::snprintf(clock, sizeof(clock), "%.2f / %.2f s", state.timeSeconds, view.durationSeconds);
    } else {
        std::snprintf(clock, sizeof(clock), "%ld / %ld   %.2f s", std::lround(state.timeSeconds * view.frameRate),
                      std::lround(view.durationSeconds * view.frameRate), state.timeSeconds);
    }
    // While playing, the keys follow the playhead: a page on when it runs off the right edge.
    if (state.playing && state.followPlayhead && m_keys) {
        m_keys->KeepPlayheadInView();
    }
    if (m_clock && m_shownClock != clock) {
        m_shownClock = clock;
        m_clock->setText(clock);
    }
    std::string info;
    if (view.clip != nullptr) {
        char text[160];
        std::snprintf(text, sizeof(text), "%.2f s  |  %ld frames  |  %.0f fps  |  %zu tracks", view.clip->durationSeconds,
                      std::lround(view.durationSeconds * view.frameRate), view.frameRate, view.clip->bones.size() + view.clip->objects.size());
        info = text;
        if (view.clip->HasMorphTracks()) {
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

void EditorSequencerPanel::ShowPlayerMenu() {
    const Editor::AnimationTimelineView& view = Resync();
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    std::vector<prism::MenuItem> items;
    for (const Scene::Entity player : view.players) {
        items.push_back(prism::MenuItem::Check(NameOf(em, player), player == view.player,
                                               [this, player] { m_editor.SelectEntity(player); }));
    }
    if (m_playerButton) {
        const prism::Rect r = m_playerButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

void EditorSequencerPanel::ShowClipMenu() {
    const Editor::AnimationTimelineView& view = Resync();
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (view.player == Scene::kInvalidEntity || !em.IsAlive(view.player) ||
        !em.HasComponent<Scene::AnimationSequencerComponent>(view.player)) {
        return;
    }
    const Scene::Entity player = view.player;
    const Scene::Entity skeleton = view.skeleton;
    m_editor.RefreshSequencerLinkedClips(player);
    const Asset::AssetGuid activeClip = em.GetComponent<Scene::AnimationSequencerComponent>(player).clip;
    const Asset::AssetGuid activeLibraryRef = em.GetComponent<Scene::AnimationSequencerComponent>(player).libraryRef;

    const auto clipItem = [&](const Asset::AssetGuid guid) {
        const Editor::AnimationTimelineState::ClipCard& card = m_editor.AnimationTimelineClipCard(guid);
        char right[48];
        std::snprintf(right, sizeof(right), "%s  %.2f s", card.skeletal ? "bones" : "object", card.durationSeconds);
        return prism::MenuItem::Check(card.name, guid == activeClip,
                                      [this, player, guid] { m_editor.ChooseAnimationTimelineClip(player, guid); }, right);
    };
    std::vector<prism::MenuItem> items;
    const std::vector<Asset::AssetGuid> own = m_editor.AnimationTimelineOwnClips(player);
    for (const Asset::AssetGuid guid : own) {
        items.push_back(clipItem(guid));
    }
    if (own.empty()) {
        items.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    if (!m_editor.m_timeline.linkedLibraryName.empty()) {
        const std::vector<Asset::AssetGuid> linked = m_editor.AnimationTimelineLinkedClips(player);
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item(m_editor.m_timeline.linkedLibraryName, {}, "", prism::Icon::AnimLibrary).disabled());
        for (const Asset::AssetGuid guid : linked) {
            items.push_back(clipItem(guid));
        }
    }

    // The current clip's own commands.
    if (activeClip != Asset::AssetGuid{}) {
        const Asset::AssetGuid current = activeClip;
        const bool readable = m_editor.AnimationTimelineClipCard(current).readable;
        std::vector<prism::MenuItem> clipMenu;
        clipMenu.push_back(prism::MenuItem::Item("Rename...", [this] {
            const std::string name = Asset::AssetDisplayName(m_editor.m_services->GetAssetDatabase().Resolve(m_editor.m_timeline.clipGuid));
            AskName("Animation Name", name, [this](const std::string& to) { static_cast<void>(m_editor.RenameAnimationTimelineClip(to)); });
        }));
        clipMenu.back().enabled = m_editor.m_timeline.clip != nullptr;
        clipMenu.push_back(prism::MenuItem::Item("Duplicate", [this, player, current] { m_editor.DuplicateAnimationTimelineClip(player, current); },
                                                 "", prism::Icon::Duplicate));
        clipMenu.back().enabled = readable;
        clipMenu.push_back(prism::MenuItem::Sep());
        clipMenu.push_back(prism::MenuItem::Item("Remove from Player", [this, player, current] { m_editor.RemoveAnimationTimelineClip(player, current); },
                                                 "", prism::Icon::Trash));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Sub("Current Animation", std::move(clipMenu), prism::Icon::AnimClip));
    }

    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Item("New Animation", [this, player, skeleton] {
        if (m_editor.CreateAnimationTimelineClip(player, skeleton)) {
            Resync();
            const std::string name = Asset::AssetDisplayName(m_editor.m_services->GetAssetDatabase().Resolve(m_editor.m_timeline.clipGuid));
            AskName("Animation Name", name, [this](const std::string& to) { static_cast<void>(m_editor.RenameAnimationTimelineClip(to)); });
        }
    }, "", prism::Icon::Plus));

    // Any clip or library in Content: the file dialog, or straight from the quick list.
    const Asset::ProjectFileSnapshot project = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
    std::vector<prism::MenuItem> addMenu;
    addMenu.push_back(prism::MenuItem::Item("Browse...", [this, player] {
        m_editor.BrowseAnimationTimeline(player, Editor::AnimationTimelineBrowse::AddClip);
    }, "", prism::Icon::Browse));
    std::vector<prism::MenuItem> libraryMenu;
    libraryMenu.push_back(prism::MenuItem::Item("Browse...", [this, player] {
        m_editor.BrowseAnimationTimeline(player, Editor::AnimationTimelineBrowse::ImportLibrary);
    }, "", prism::Icon::Browse));
    bool anyClip = false;
    bool anyLibrary = false;
    for (const Asset::ProjectFileEntry& entry : project.entries) {
        if (entry.kind != Asset::ProjectFileEntryKind::File) {
            continue;
        }
        const std::string folder = Utf8GenericOf(entry.relativePath.parent_path());
        const std::filesystem::path absolute = project.contentRoot / entry.relativePath;
        const std::string ext = Utf8GenericOf(entry.relativePath.extension());
        if (ext == ".uvanim") {
            const bool listed = entry.registeredAssetGuid.has_value() && std::ranges::find(own, *entry.registeredAssetGuid) != own.end();
            if (listed) {
                continue;
            }
            if (!anyClip) {
                addMenu.push_back(prism::MenuItem::Sep());
                anyClip = true;
            }
            addMenu.push_back(prism::MenuItem::Item(Utf8Of(entry.relativePath.stem()), [this, player, absolute] {
                static_cast<void>(m_editor.AddClipToAnimationSequencer(player, absolute));
            }, folder.empty() ? "Content" : folder));
        } else if (ext == ".uvanimlib") {
            if (!anyLibrary) {
                libraryMenu.push_back(prism::MenuItem::Sep());
                anyLibrary = true;
            }
            libraryMenu.push_back(prism::MenuItem::Item(Utf8Of(entry.relativePath.stem()), [this, player, absolute] {
                static_cast<void>(m_editor.ImportAnimationLibraryIntoSequencer(player, absolute));
            }, folder.empty() ? "Content" : folder));
        }
    }
    // The takes kept inside the project's models.
    for (const Asset::AssetRecord& record : m_editor.m_services->GetAssetDatabase().GetRegisteredAssets()) {
        std::filesystem::path model;
        std::string take;
        if (!Asset::SplitEmbeddedClipPath(record.path, model, take) || std::ranges::find(own, record.guid) != own.end()) {
            continue;
        }
        if (!anyClip) {
            addMenu.push_back(prism::MenuItem::Sep());
            anyClip = true;
        }
        const std::filesystem::path absolute = record.path;
        addMenu.push_back(prism::MenuItem::Item(Asset::AssetDisplayName(absolute), [this, player, absolute] {
            static_cast<void>(m_editor.AddClipToAnimationSequencer(player, absolute));
        }, "Models"));
    }
    if (!anyClip) {
        addMenu.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    if (!anyLibrary) {
        libraryMenu.push_back(prism::MenuItem::Item("None", {}).disabled());
    }
    // A project may hold thousands of clips: a searchable list, not a submenu.
    items.push_back(prism::MenuItem::Item("Add Animation...", [this, addMenu = std::move(addMenu)]() mutable {
        if (m_clipButton) {
            const prism::Rect r = m_clipButton->rect();
            prism::showSearchMenu(std::move(addMenu), r, prism::PopupPlacement::Below, "Search animations");
        }
    }, "", prism::Icon::FolderOpen));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Sub("Load Library", std::move(libraryMenu), prism::Icon::AnimLibrary));
    if (activeLibraryRef == Asset::AssetGuid{}) {
        items.push_back(prism::MenuItem::Item("Link Library...", [this, player] {
            m_editor.BrowseAnimationTimeline(player, Editor::AnimationTimelineBrowse::LinkLibrary);
        }));
    } else {
        const std::filesystem::path linked = m_editor.m_services->GetAssetDatabase().Resolve(activeLibraryRef);
        items.push_back(prism::MenuItem::Item(linked.empty() ? std::string{"Unlink Library"} : "Unlink " + Utf8Of(linked.stem()),
                                              [this, player] { static_cast<void>(m_editor.UnlinkAnimationLibraryFromSequencer(player)); }));
    }
    items.push_back(prism::MenuItem::Item("Export Library...", [this] { ExportLibrary(); }, "", prism::Icon::Save));
    if (m_clipButton) {
        const prism::Rect r = m_clipButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

void EditorSequencerPanel::ShowSettingsMenu() {
    Editor::AnimationTimelineState& state = m_editor.m_timeline;
    std::vector<prism::MenuItem> speeds;
    for (const double rate : {0.1, 0.25, 0.5, 1.0, 1.5, 2.0}) {
        char label[16];
        std::snprintf(label, sizeof(label), "%gx", rate);
        speeds.push_back(prism::MenuItem::Check(label, std::abs(state.playbackRate - rate) < 1e-6, [this, rate] { m_editor.m_timeline.playbackRate = rate; }));
    }
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Sub("Playback Speed", std::move(speeds), prism::Icon::Play));
    items.push_back(prism::MenuItem::Check("Follow Playhead", state.followPlayhead,
                                           [this] { m_editor.m_timeline.followPlayhead = !m_editor.m_timeline.followPlayhead; }));
    items.push_back(prism::MenuItem::Check("Snap to Frames", state.snapToFrames,
                                           [this] { m_editor.m_timeline.snapToFrames = !m_editor.m_timeline.snapToFrames; }));
    items.push_back(prism::MenuItem::Check("Show Time in Seconds", state.clockInSeconds,
                                           [this] { m_editor.m_timeline.clockInSeconds = !m_editor.m_timeline.clockInSeconds; }));
    items.push_back(prism::MenuItem::Sep());
    items.push_back(prism::MenuItem::Check("Record (key what is moved)", state.recording,
                                           [this] { m_editor.SetAnimationTimelineRecording(!m_editor.m_timeline.recording); }));
    if (m_settingsButton) {
        const prism::Rect r = m_settingsButton->rect();
        prism::showContextMenu(std::move(items), prism::Vec2{r.x, r.bottom()});
    }
}

void EditorSequencerPanel::ExportLibrary() {
    const Editor::AnimationTimelineView& view = Resync();
    if (view.player == Scene::kInvalidEntity) {
        return;
    }
    const Scene::Entity player = view.player;
    AskName("Export Animation Library", "Animation Library", [this, player](const std::string& name) {
        if (name.empty()) {
            return;
        }
        // Beside the player's current clip, else at the top of Content.
        const Asset::ProjectFileSnapshot project = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
        std::filesystem::path folder = project.contentRoot;
        const std::filesystem::path clip = m_editor.m_services->GetAssetDatabase().Resolve(m_editor.m_timeline.clipGuid);
        // (A take inside a model has no folder in Content: its library goes to the top.)
        std::filesystem::path model;
        std::string take;
        if (!clip.empty() && !Asset::SplitEmbeddedClipPath(clip, model, take)) {
            folder = clip.parent_path();
        }
        std::filesystem::path target = folder / (name + ".uvanimlib");
        for (int n = 2; std::filesystem::exists(target); ++n) {
            target = folder / (name + " " + std::to_string(n) + ".uvanimlib");
        }
        static_cast<void>(m_editor.ExportAnimationSequencerToLibrary(player, target));
    });
}

bool EditorSequencerPanel::AcceptDropped(const std::string& contentRelative) {
    const Editor::AnimationTimelineView& view = Resync();
    if (view.player == Scene::kInvalidEntity) {
        return false;
    }
    const std::filesystem::path absolute = m_editor.m_services->GetProjectFileIndex().GetSnapshot().contentRoot / contentRelative;
    if (absolute.extension() == ".uvanimlib") {
        return m_editor.ImportAnimationLibraryIntoSequencer(view.player, absolute);
    }
    return m_editor.AddClipToAnimationSequencer(view.player, absolute);
}

} // namespace UVE::Editor
