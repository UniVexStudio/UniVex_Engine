// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_editor.h"
#include "editor_layout_store.h"

#include "editor.h"
#include "editor_chrome.h"
#include "editor_fit_width.h"
#include "editor_touch_controls.h"
#include "editor_host.h"
#include "editor_vfx_graph.h"
#include "editor_vfx_inspector.h"
#include "editor_vfx_preview.h"

#include "i_project_file_index.h"
#include "vfx_templates.h"

#include "prism/Application.h"
#include "prism/BlueprintGraph.h"
#include "prism/ColorPicker.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/EngineView.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TableView.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kVfxWindowWidth = 1440;
constexpr int kVfxWindowHeight = 860;

constexpr std::uint8_t kPreviewStateStopped = 0U;
constexpr std::uint8_t kPreviewStatePaused = 1U;
constexpr std::uint8_t kPreviewStatePlaying = 2U;

/// Root window border that intercepts VFX Editor shortcuts (Save, Compile, Undo/Redo, Play/Pause)
/// during PRISM's tunnel phase while respecting focused text inputs and modal popups.
class VfxWindowFrame final : public prism::Border {
public:
    std::function<bool(const prism::KeyEvent&)> onShortcut;

    prism::Reply onPreviewKeyDown(const prism::KeyEvent& e) override {
        if (onShortcut && onShortcut(e)) {
            return prism::Reply::handled();
        }
        return prism::Border::onPreviewKeyDown(e);
    }
};

[[nodiscard]] prism::Slot CentreSlot() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string SecondsText(const double t) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", t);
    return buf;
}

[[nodiscard]] prism::Ref<prism::Border> MakeSectionHeader(prism::Ref<prism::Box> row) {
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(std::move(row));
    return bar;
}

[[nodiscard]] std::string MakeUniqueEmitterName(const Asset::ParticleSystemAsset& sys,
                                                   const std::string& baseName) {
    const auto exists = [&sys](const std::string& candidate) {
        return std::any_of(sys.emitters.begin(), sys.emitters.end(),
                           [&candidate](const Asset::ParticleEmitterAsset& e) { return e.name == candidate; });
    };
    if (!exists(baseName)) {
        return baseName;
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = baseName + "_" + std::to_string(n);
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return baseName + "_Copy";
}

[[nodiscard]] std::string MakeUniqueParameterName(const Asset::ParticleSystemAsset& sys,
                                                     const std::string& baseName) {
    const auto exists = [&sys](const std::string& candidate) {
        return std::any_of(sys.parameters.begin(), sys.parameters.end(),
                           [&candidate](const Asset::ParticleUserParameter& p) { return p.name == candidate; });
    };
    if (!exists(baseName)) {
        return baseName;
    }
    for (int n = 2; n < 1000; ++n) {
        const std::string candidate = baseName + "_" + std::to_string(n);
        if (!exists(candidate)) {
            return candidate;
        }
    }
    return baseName + "_Param";
}

[[nodiscard]] std::uint32_t NextFreeModuleIdInSystem(const Asset::ParticleSystemAsset& sys) noexcept {
    std::uint32_t maxId = 1U;
    for (const Asset::ParticleEmitterAsset& emitter : sys.emitters) {
        for (const auto* stageVec : {&emitter.emitterUpdate, &emitter.particleSpawn, &emitter.particleUpdate, &emitter.render, &emitter.detached}) {
            for (const Asset::ParticleModule& mod : *stageVec) {
                maxId = std::max(maxId, mod.id);
            }
        }
        for (const Asset::ParticleMathNode& mathNode : emitter.mathNodes) {
            maxId = std::max(maxId, mathNode.id);
        }
    }
    return maxId + 1U;
}

} // namespace

EditorVfxEditor::EditorVfxEditor(Editor& editor, EditorHost& host)
    : m_editor(editor), m_host(host) {}

EditorVfxEditor::~EditorVfxEditor() {
    if (m_window != 0 && m_host.IsReady()) {
        m_host.CloseWindow(m_window);
    }
}

bool EditorVfxEditor::SaveScreenshot(const std::filesystem::path& path) {
    if (m_window == 0 || !m_host.IsReady()) {
        return false;
    }
    return m_host.SaveWindowScreenshot(m_window, path);
}

void EditorVfxEditor::SelectModuleId(const std::uint32_t moduleId) {
    m_selectedModuleId = moduleId;
    m_shownModuleId = moduleId;
    if (m_graph && moduleId != 0U) {
        m_graph->select(static_cast<int>(moduleId));
    }
    if (m_window != 0 && m_host.IsReady()) {
        m_host.App().runInWindow(m_window, [this] { RebuildInspector(); });
    }
}

void EditorVfxEditor::Sync() {
    if (!m_host.IsReady()) {
        return;
    }
    Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    const std::filesystem::path path = session != nullptr ? session->path : std::filesystem::path{};
    if (m_window != 0 && (session == nullptr || path != m_openPath)) {
        m_host.CloseWindow(m_window);
        m_window = 0;
        m_openPath.clear();
        m_viewport.reset();
        m_statsOverlayLabel.reset();
        m_graph.reset();
        m_timeline.reset();
        m_compileTable.reset();
        m_compileSummaryLabel.reset();
        m_emittersScroll.reset();
        m_addEmitterButton.reset();
        m_parametersScroll.reset();
        m_inspectorScroll.reset();
        m_playButton.reset();
        m_stopButton.reset();
        m_loopButton.reset();
        m_seedButton.reset();
        m_gridButton.reset();
        m_boundsButton.reset();
        m_speedCombo.reset();
        m_previewStatePill.reset();
        m_previewStateBadge.reset();
        m_timecodeLabel.reset();
        m_status.reset();
        m_tab.reset();
        m_shownPreviewState = 0xFFU;
        m_shownPlayingInit = false;
        m_shownValid = false;
    }
    if (session == nullptr) {
        return;
    }
    if (m_window == 0) {
        m_selectedModuleId = 0U;
        m_shownModuleId = 0U;
        m_closeAsked = false;
        m_shownValid = false;
        m_shownPlayingInit = false;
        m_shownPreviewState = 0xFFU;
        m_shownCompileCount = static_cast<std::size_t>(-1);
        m_window = m_host.OpenWindow(
            Build(),
            "Particle System Editor - " + path.filename().string(),
            kVfxWindowWidth,
            kVfxWindowHeight);
        if (m_window == 0) {
            m_editor.CloseVfxEditor();
            return;
        }
        m_openPath = path;
        m_host.FocusWindow(m_window);
    }
    if (session->focusPending) {
        session->focusPending = false;
        m_host.FocusWindow(m_window);
    }
    if (m_host.WindowCloseRequested(m_window)) {
        m_host.ClearCloseRequest(m_window);
        RequestClose();
    }
    const auto now = std::chrono::steady_clock::now();
    const double dt = m_lastTick.time_since_epoch().count() == 0
                          ? (1.0 / 60.0)
                          : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    m_editor.TickVfxEditor(std::min(dt, 0.1));
    m_host.App().runInWindow(m_window, [this] { Refresh(); });
}

void EditorVfxEditor::RequestClose() {
    if (!m_editor.IsVfxEditorDirty()) {
        m_editor.CloseVfxEditor();
        return;
    }
    if (m_closeAsked) {
        return;
    }
    m_closeAsked = true;
    const std::string name = m_openPath.filename().string();
    m_host.App().runInWindow(m_window, [this, name] {
        auto text = prism::make<prism::Label>("Save the changes to " + name + " before closing?");
        prism::showDialog(
            "Close Particle System",
            text,
            {prism::DialogButton{"Save", true, [this] {
                 m_closeAsked = false;
                 if (m_editor.SaveVfxEditor()) {
                     m_editor.CloseVfxEditor();
                 }
             }},
             prism::DialogButton{"Don't Save", false, [this] {
                 m_closeAsked = false;
                 m_editor.CloseVfxEditor();
             }},
             prism::DialogButton{"Cancel", false, [this] { m_closeAsked = false; }}},
            420.0F);
    });
}

prism::Ref<prism::Widget> EditorVfxEditor::Build() {
    auto window = prism::vbox();
    window->add(BuildTitleBar());

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    m_leftColumn = BuildLeftColumn();
    m_rightColumn = BuildRightColumn();
    columns->add(m_leftColumn, prism::Splitter::Pane{395.0F, 330.0F, 640.0F, false});
    columns->add(BuildCentreColumn(), prism::Splitter::Pane{695.0F, 420.0F, 1e9F, true});
    columns->add(m_rightColumn, prism::Splitter::Pane{350.0F, 260.0F, 600.0F, false});
    EditorLayoutStore::Instance().BindSplitter("vfx.columns", *columns);
    window->add(columns, prism::Slot{1.0F});

    auto frame = prism::make<VfxWindowFrame>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->onShortcut = [this](const prism::KeyEvent& e) -> bool {
        const bool ctrl = (e.mods & prism::ModCtrl) != 0U;
        const bool shift = (e.mods & prism::ModShift) != 0U;
        const bool alt = (e.mods & prism::ModAlt) != 0U;
        if (ctrl && !alt && !shift && e.key == prism::Key::S) {
            static_cast<void>(m_editor.SaveVfxEditor());
            return true;
        }
        if (!ctrl && !alt && !shift && e.key == prism::Key::F7) {
            static_cast<void>(m_editor.CompileVfxEditor());
            m_shownCompileCount = static_cast<std::size_t>(-1);
            return true;
        }
        const bool modalOrTyping = prism::Application::exists() && prism::Application::get().wantsKeyboard();
        if (modalOrTyping) {
            return false;
        }
        if (ctrl && !alt && e.key == prism::Key::Z) {
            static_cast<void>(shift ? m_editor.RedoVfxEdit() : m_editor.UndoVfxEdit());
            return true;
        }
        if (ctrl && !alt && !shift && e.key == prism::Key::Y) {
            static_cast<void>(m_editor.RedoVfxEdit());
            return true;
        }
        if (!ctrl && !alt && !shift && !e.repeat && e.key == prism::Key::Space) {
            const prism::Widget* const focused =
                prism::Application::exists() ? prism::Application::get().focus() : nullptr;
            const bool buttonFocused =
                focused != nullptr &&
                (focused->accessRole() == prism::AccessRole::Button ||
                 focused->accessRole() == prism::AccessRole::ToggleButton ||
                 focused->accessRole() == prism::AccessRole::CheckBox);
            if (!buttonFocused) {
                if (const Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                    if (s->playing) {
                        m_editor.PauseVfxPreview();
                    } else {
                        m_editor.PlayVfxPreview();
                    }
                    return true;
                }
            }
        }
        return false;
    };
    frame->setChild(window);
    return frame;
}

prism::Ref<prism::Widget> EditorVfxEditor::BuildTitleBar() {
    m_tab = prism::make<DocumentTab>();
    m_tab->setFallbackIcon(prism::Icon::VfxGraph);
    m_tab->setName(m_openPath.empty() && m_editor.VfxEditSessionPtr() != nullptr
                       ? m_editor.VfxEditSessionPtr()->path.stem().string()
                       : m_openPath.stem().string());

    EditorHeaderDesc desc;
    desc.documentTab = m_tab;
    desc.toolbarRow = BuildToolbar();
    desc.onClose = [this] { RequestClose(); };
    desc.menus.push_back({"File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item(
            "Save", [this] { static_cast<void>(m_editor.SaveVfxEditor()); }, "Ctrl+S", prism::Icon::Save));
        items.push_back(prism::MenuItem::Item(
            "Compile",
            [this] {
                static_cast<void>(m_editor.CompileVfxEditor());
                m_shownCompileCount = static_cast<std::size_t>(-1);
            },
            "F7", prism::Icon::Check));
        items.push_back(prism::MenuItem::Item("Show in Asset Table", [this] {
            const Asset::ProjectFileSnapshot now = m_editor.m_services->GetProjectFileIndex().GetSnapshot();
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(m_openPath, now.contentRoot, error);
            if (!error) {
                m_editor.m_assetTableDirectory = relative.parent_path();
            }
        }, "", prism::Icon::Folder));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { RequestClose(); }, "", prism::Icon::Close));
        return items;
    }});
    desc.menus.push_back({"Edit", [this] {
        const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
        prism::MenuItem undo = prism::MenuItem::Item(
            "Undo", [this] { static_cast<void>(m_editor.UndoVfxEdit()); }, "Ctrl+Z", prism::Icon::Undo);
        undo.enabled = session != nullptr && !session->undo.empty();
        prism::MenuItem redo = prism::MenuItem::Item(
            "Redo", [this] { static_cast<void>(m_editor.RedoVfxEdit()); }, "Ctrl+Y", prism::Icon::Redo);
        redo.enabled = session != nullptr && !session->redo.empty();
        return std::vector<prism::MenuItem>{std::move(undo), std::move(redo)};
    }});
    desc.menus.push_back({"Window", [this] {
        const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item(
                "Frame All Nodes", [this] {
                    if (m_graph) {
                        m_graph->frameAll();
                    }
                },
                "Home", prism::Icon::Frame),
            prism::MenuItem::Item(
                "Frame Selected Nodes", [this] {
                    if (m_graph) {
                        m_graph->frameSelection();
                    }
                },
                "F", prism::Icon::Frame),
            prism::MenuItem::Sep(),
            prism::MenuItem::Item(
                "Reset Preview Camera", [this] {
                    if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                        s->previewCameraResetPending = true;
                    }
                },
                "", prism::Icon::Camera),
            prism::MenuItem::Check(
                "Show Fixed Bounds", session != nullptr && session->showBounds, [this] {
                    if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                        s->showBounds = !s->showBounds;
                    }
                }),
        };
    }});
    desc.menus.push_back({"Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Spawn from Blueprint: vfx.spawn(\"assets/VFX_Effect.uvfx\", pos)", {}).disabled(),
            prism::MenuItem::Item("Right-click the Particle Graph canvas to add modules", {}).disabled(),
            prism::MenuItem::Item("Press Space to Play/Pause preview; scrub the timeline below the viewport", {}).disabled(),
        };
    }});
    return BuildEditorHeader(std::move(desc));
}

void EditorVfxEditor::SetGraphFocus(const bool on) {
    m_graphFocus = on;
    for (const std::shared_ptr<prism::Widget>& panel : {m_leftColumn, m_rightColumn, m_compilePanel}) {
        if (panel) {
            panel->setVisible(!on);
        }
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
}

prism::Ref<prism::Widget> EditorVfxEditor::BuildToolbar() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(5.0F, 1.0F, 5.0F, 1.0F));

    auto save = ToolButton(prism::Icon::Save, "Save Particle System (.uvfx) (Ctrl+S)", "Save");
    save->onClick = [this] { static_cast<void>(m_editor.SaveVfxEditor()); };
    row->add(save, CentreSlot());

    auto undo = ToolButton(prism::Icon::Undo, "Undo (Ctrl+Z)");
    undo->onClick = [this] { static_cast<void>(m_editor.UndoVfxEdit()); };
    auto redo = ToolButton(prism::Icon::Redo, "Redo (Ctrl+Y)");
    redo->onClick = [this] { static_cast<void>(m_editor.RedoVfxEdit()); };
    row->add(undo, CentreSlot());
    row->add(redo, CentreSlot());
    row->add(HSeparator(), CentreSlot());

    auto compile = ToolButton(prism::Icon::Compile, "Compile all emitters and refresh preview (F7)", "Compile");
    compile->onClick = [this] {
        static_cast<void>(m_editor.CompileVfxEditor());
        m_shownCompileCount = static_cast<std::size_t>(-1);
    };
    row->add(compile, CentreSlot());
    row->add(HSeparator(), CentreSlot());

    m_seedButton = ToolButton(prism::Icon::Lock, "Deterministic fixed seed on restart", "Fixed Seed");
    m_seedButton->setCheckable(true);
    m_seedButton->onClick = [this] {
        if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
            s->fixedSeed = !s->fixedSeed;
            m_editor.RestartVfxPreview();
        }
    };
    row->add(m_seedButton, CentreSlot());

    m_boundsButton = ToolButton(prism::Icon::Cube, "Show system fixed bounds box in 3D preview", "Bounds");
    m_boundsButton->setCheckable(true);
    m_boundsButton->onClick = [this] {
        if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
            s->showBounds = !s->showBounds;
        }
    };
    row->add(m_boundsButton, CentreSlot());
    auto focus = ToolButton(prism::Icon::LayoutFull, "Focus: the graph fills the window (side panels and compile results hidden)", "Focus");
    focus->setCheckable(true);
    focus->onToggle = [this](const bool on) { SetGraphFocus(on); };
    row->add(focus, CentreSlot());
    if (prism::Ref<prism::Widget> controls = MakeTouchControlsButton()) {   // touch screens: Undo, Ctrl, ...
        row->add(controls, CentreSlot());
    }

    m_status =prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, prism::Margin(10.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});

    return MakeSectionHeader(std::move(row));
}

prism::Ref<prism::Widget> EditorVfxEditor::BuildLeftColumn() {
    // Top pane: 3D Preview screen -> Transport controls directly underneath -> AAA Timeline directly underneath.
    auto previewHeaderRow = prism::hbox(6.0F);
    previewHeaderRow->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto previewTitle = prism::make<prism::Label>("3D Preview", prism::FontRole::Medium);
    previewHeaderRow->add(previewTitle, CentreSlot());
    previewHeaderRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_previewStateBadge = prism::make<prism::Label>("STOPPED", prism::FontRole::Small);
    m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::textDim));
    m_previewStatePill = prism::make<prism::Border>();
    m_previewStatePill->setBackground(prism::ThemeColor(&prism::Theme::control));
    m_previewStatePill->setBorder(prism::ThemeColor(&prism::Theme::borderStrong), prism::Margin(1.0F));
    m_previewStatePill->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
    m_previewStatePill->setRadius(3.0F);
    m_previewStatePill->setChild(m_previewStateBadge);
    previewHeaderRow->add(m_previewStatePill, CentreSlot());

    auto resetCamBtn = prism::make<prism::Button>("", prism::Icon::Camera, prism::ButtonStyle::Flat);
    resetCamBtn->setTooltip("Reset preview camera to system bounds");
    resetCamBtn->onClick = [this] {
        if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
            s->previewCameraResetPending = true;
        }
    };
    previewHeaderRow->add(resetCamBtn, CentreSlot());

    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};

    m_statsOverlayLabel = prism::make<prism::Label>("", prism::FontRole::Mono);
    m_statsOverlayLabel->setColor(prism::ThemeColor(&prism::Theme::textBright));
    auto statsCard = prism::make<prism::Border>();
    statsCard->setBackground(prism::theme().popup.withAlpha(205));
    statsCard->setBorder(prism::ThemeColor(&prism::Theme::borderStrong), prism::Margin(1.0F));
    statsCard->setPadding(prism::Margin(8.0F, 5.0F, 8.0F, 5.0F));
    statsCard->setRadius(3.0F);
    statsCard->setChild(m_statsOverlayLabel);

    auto statsRow = prism::hbox();
    statsRow->setPadding(prism::Margin(8.0F, 8.0F, 8.0F, 8.0F));
    statsRow->add(statsCard, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Top});
    statsRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto statsContainer = prism::vbox();
    statsContainer->add(statsRow);
    statsContainer->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    auto viewportOverlay = prism::make<prism::Overlay>();
    viewportOverlay->add(m_viewport);
    viewportOverlay->add(statsContainer);

    // Transport bar directly beneath the 3D Preview viewport.
    auto transportRow = prism::hbox(4.0F);
    transportRow->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));

    m_playButton = prism::make<prism::Button>("Play", prism::Icon::Play, prism::ButtonStyle::Success);
    m_playButton->setTooltip("Play or pause preview simulation (Space)");
    m_playButton->onClick = [this] {
        if (const Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
            if (s->playing) {
                m_editor.PauseVfxPreview();
            } else {
                m_editor.PlayVfxPreview();
            }
        }
    };
    transportRow->add(m_playButton, CentreSlot());

    m_stopButton = ToolButton(prism::Icon::Stop, "Stop preview and reset playhead to 0.00s");
    m_stopButton->onClick = [this] { m_editor.StopVfxPreview(); };
    transportRow->add(m_stopButton, CentreSlot());

    auto restartBtn = ToolButton(prism::Icon::Refresh, "Restart simulation from t = 0");
    restartBtn->onClick = [this] { m_editor.RestartVfxPreview(); };
    transportRow->add(restartBtn, CentreSlot());

    m_loopButton = ToolButton(prism::Icon::NodeLoop, "Toggle loop playback");
    m_loopButton->setCheckable(true);
    m_loopButton->onClick = [this] {
        if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
            s->loop = !s->loop;
        }
    };
    transportRow->add(m_loopButton, CentreSlot());

    m_speedCombo = prism::make<prism::ComboBox>(
        std::vector<std::string>{"0.1x", "0.25x", "0.5x", "1.0x", "2.0x", "4.0x"}, 3);
    m_speedCombo->setTooltip("Preview simulation speed");
    m_speedCombo->onChange = [this](const int idx) {
        static constexpr std::array<double, 6> kSpeeds{0.1, 0.25, 0.5, 1.0, 2.0, 4.0};
        if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr();
            s != nullptr && idx >= 0 && static_cast<std::size_t>(idx) < kSpeeds.size()) {
            s->speed = kSpeeds[static_cast<std::size_t>(idx)];
        }
    };
    auto speedBox = prism::make<prism::SizeBox>();
    speedBox->setWidthOverride(64.0F);
    speedBox->setContent(m_speedCombo);
    transportRow->add(speedBox, CentreSlot());

    transportRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_timecodeLabel = prism::make<prism::Label>("0.00 / 0.00s", prism::FontRole::Mono);
    m_timecodeLabel->setColor(prism::ThemeColor(&prism::Theme::text));
    transportRow->add(m_timecodeLabel, CentreSlot());

    auto transportBar = prism::make<prism::Border>();
    transportBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    transportBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 1.0F));
    transportBar->setChild(transportRow);

    // Upgraded studio timeline directly underneath the transport bar.
    m_timeline = prism::make<VfxTimelineWidget>(m_editor);

    auto previewPane = prism::vbox(0.0F);
    previewPane->add(MakeSectionHeader(std::move(previewHeaderRow)));
    previewPane->add(viewportOverlay, prism::Slot{1.0F});
    previewPane->add(transportBar);
    previewPane->add(m_timeline);

    // Bottom pane: Emitters list + green "+ Add" button.
    auto emittersHeader = prism::hbox(4.0F);
    emittersHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto emittersTitle = prism::make<prism::Label>("Emitters", prism::FontRole::Medium);
    emittersHeader->add(emittersTitle, CentreSlot());
    emittersHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_addEmitterButton = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Success);
    m_addEmitterButton->onClick = [this] {
        ShowAddEmitterMenu(m_addEmitterButton ? m_addEmitterButton->rect() : prism::Rect{});
    };
    emittersHeader->add(m_addEmitterButton, CentreSlot());

    m_emittersScroll = prism::make<prism::ScrollBox>();
    auto emittersBox = prism::vbox(0.0F);
    emittersBox->add(MakeSectionHeader(std::move(emittersHeader)));
    emittersBox->add(m_emittersScroll, prism::Slot{1.0F});

    auto leftSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    leftSplit->add(Panel(previewPane), prism::Splitter::Pane{470.0F, 280.0F, 1e9F, true});
    leftSplit->add(Panel(emittersBox), prism::Splitter::Pane{310.0F, 160.0F, 1e9F, false});
    EditorLayoutStore::Instance().BindSplitter("vfx.left", *leftSplit);
    return leftSplit;
}

prism::Ref<prism::Widget> EditorVfxEditor::BuildCentreColumn() {
    // Top pane: Full Particle Graph canvas (no Split/Preview toggle).
    auto graphHeader = prism::hbox(6.0F);
    graphHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto graphTitle = prism::make<prism::Label>("Particle Graph", prism::FontRole::Medium);
    graphHeader->add(graphTitle, CentreSlot());
    auto graphHint = prism::make<prism::Label>(
        "Right-click to add modules  |  Drag wire to connect  |  Del to remove",
        prism::FontRole::Small);
    graphHint->setColor(prism::ThemeColor(&prism::Theme::textDim));
    graphHeader->add(graphHint, prism::Slot{1.0F, prism::Margin(8.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Left, prism::VAlign::Center});
    auto frameAllBtn = prism::make<prism::Button>("Frame", prism::Icon::Frame, prism::ButtonStyle::Flat);
    frameAllBtn->setTooltip("Frame all nodes in the graph (Home)");
    frameAllBtn->onClick = [this] {
        if (m_graph) {
            m_graph->frameAll();
        }
    };
    graphHeader->add(frameAllBtn, CentreSlot());

    m_graph = prism::make<prism::BlueprintGraph>();
    prism::BlueprintGraph::Features graphFeatures = m_graph->features();
    graphFeatures.toolbar = false;
    graphFeatures.crumbs = false;
    // The VFX Editor session owns Undo/Redo, Save, and Compile; disabling PRISM's internal
    // history and host shortcuts prevents Ctrl+Z from restoring demo nodes over the emitter.
    graphFeatures.history = false;
    graphFeatures.shortcuts = false;
    m_graph->setFeatures(graphFeatures);
    m_graph->onSaveRequested = [this] { static_cast<void>(m_editor.SaveVfxEditor()); };
    m_graph->onCompileRequested = [this] {
        static_cast<void>(m_editor.CompileVfxEditor());
        m_shownCompileCount = static_cast<std::size_t>(-1);
    };
    // The palette, the right-click menu and wire-drag search all offer the VFX node library (modules by
    // stage, plus Math / Value nodes) and create nodes through the VFX spawner; connections follow the
    // VFX stage rules, and the four stage output nodes are structural (never deleted or copied).
    m_graph->setNodeLibrary(BuildVfxGraphNodeLibrary(m_shownSystem, true));
    m_graph->onSpawn = [this](const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) {
        return m_graph ? SpawnVfxGraphNode(*m_graph, spec, world) : -1;
    };
    m_graph->canConnectHook = [this](const int fromNode, const int fromPin, const int toNode, const int toPin,
                                     std::string* const why) {
        return m_graph == nullptr || CanConnectVfxPins(*m_graph, fromNode, fromPin, toNode, toPin, why);
    };
    m_graph->nodeLocked = [](const int nodeId) { return IsVfxStageNodeId(nodeId); };
    m_graph->onSelectionChanged = [this](const std::vector<int>& selectedIds) {
        if (m_syncingGraph || !m_graph) {
            return;
        }
        std::uint32_t picked = 0U;
        for (const int id : selectedIds) {
            if (!IsVfxStageNodeId(id) && id > 0) {
                picked = static_cast<std::uint32_t>(id);
                break;
            }
        }
        if (picked != m_selectedModuleId) {
            m_selectedModuleId = picked;
            m_shownModuleId = picked;
            RebuildInspector();
        }
    };
    m_graph->onChanged = [this] {
        if (m_syncingGraph) {
            return;
        }
        SyncGraphToAsset();
    };

    auto graphBox = prism::vbox(0.0F);
    graphBox->add(MakeSectionHeader(std::move(graphHeader)));
    graphBox->add(m_graph, prism::Slot{1.0F});

    // Bottom pane: Compile Results header + diagnostics table.
    auto compileHeader = prism::hbox(6.0F);
    compileHeader->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    auto compileTitle = prism::make<prism::Label>("Compile Results", prism::FontRole::Medium);
    compileHeader->add(compileTitle, CentreSlot());
    compileHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_compileSummaryLabel = prism::make<prism::Label>("Ready", prism::FontRole::Small);
    m_compileSummaryLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    compileHeader->add(m_compileSummaryLabel, CentreSlot());

    m_compileTable = prism::make<prism::TableView>();
    m_compileTable->addColumn(prism::TableColumn{"sev", "Severity", 80.0F});
    m_compileTable->addColumn(prism::TableColumn{"emitter", "Emitter", 120.0F});
    m_compileTable->addColumn(prism::TableColumn{"module", "Module ID", 80.0F});
    m_compileTable->addColumn(prism::TableColumn{"msg", "Message", 420.0F, 120.0F, true, true});
    m_compileTable->cellText = [this](const int row, const int col) -> std::string {
        const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
        if (session == nullptr || row < 0 || static_cast<std::size_t>(row) >= session->compileMessages.size()) {
            return {};
        }
        const VFX::ParticleCompileMessage& msg = session->compileMessages[static_cast<std::size_t>(row)];
        switch (col) {
            case 0:
                return msg.severity == VFX::ParticleCompileMessage::Severity::Error ? "ERROR" : "Warning";
            case 1:
                if (msg.emitterIndex < session->system.emitters.size()) {
                    return session->system.emitters[msg.emitterIndex].name;
                }
                return "#" + std::to_string(msg.emitterIndex);
            case 2:
                return msg.moduleId != 0U ? std::to_string(msg.moduleId) : std::string("Stage");
            default:
                return msg.text;
        }
    };
    m_compileTable->onSelect = [this](const std::vector<int>& rows) {
        Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
        if (session == nullptr || rows.empty()) {
            return;
        }
        const int r = rows.front();
        if (r < 0 || static_cast<std::size_t>(r) >= session->compileMessages.size()) {
            return;
        }
        const VFX::ParticleCompileMessage& msg = session->compileMessages[static_cast<std::size_t>(r)];
        if (msg.emitterIndex < session->system.emitters.size() && session->selectedEmitter != msg.emitterIndex) {
            session->selectedEmitter = msg.emitterIndex;
            m_shownEmitterIndex = msg.emitterIndex;
            RebuildGraphFromAsset();
            if (m_graph) {
                m_graph->frameAll();
            }
            RebuildEmittersList();
        }
        if (msg.moduleId != 0U) {
            SelectModuleId(msg.moduleId);
            if (m_graph) {
                m_graph->frameSelection();
            }
        } else if (m_graph) {
            m_graph->select(kVfxStageNodeRender);
            m_graph->frameSelection();
        }
    };

    auto compileBox = prism::vbox(0.0F);
    compileBox->add(MakeSectionHeader(std::move(compileHeader)));
    compileBox->add(m_compileTable, prism::Slot{1.0F});

    auto centreSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centreSplit->add(Panel(graphBox), prism::Splitter::Pane{590.0F, 260.0F, 1e9F, true});
    m_compilePanel = Panel(compileBox);
    centreSplit->add(m_compilePanel, prism::Splitter::Pane{165.0F, 110.0F, 360.0F, false});
    EditorLayoutStore::Instance().BindSplitter("vfx.centre", *centreSplit);
    return centreSplit;
}

prism::Ref<prism::Widget> EditorVfxEditor::BuildRightColumn() {
    // Top pane: Inspector. Bottom pane: User Parameters (+ Add).
    auto inspectorHeader = prism::hbox(4.0F);
    inspectorHeader->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    auto inspectorTitle = prism::make<prism::Label>("Inspector", prism::FontRole::Medium);
    inspectorHeader->add(inspectorTitle, CentreSlot());

    m_inspectorScroll = prism::make<prism::ScrollBox>();
    auto inspectorBox = prism::vbox(0.0F);
    inspectorBox->add(MakeSectionHeader(std::move(inspectorHeader)));
    inspectorBox->add(m_inspectorScroll, prism::Slot{1.0F});

    auto paramsHeader = prism::hbox(4.0F);
    paramsHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto paramsTitle = prism::make<prism::Label>("User Parameters", prism::FontRole::Medium);
    paramsHeader->add(paramsTitle, CentreSlot());
    paramsHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto addParamBtn = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    addParamBtn->onClick = [this] {
        m_editor.EditVfxSystem([](Asset::ParticleSystemAsset& sys) {
            if (sys.parameters.size() >= Asset::kMaximumParticleUserParameters) {
                return;
            }
            Asset::ParticleUserParameter param{};
            param.name = MakeUniqueParameterName(sys, "Param");
            param.type = Asset::ParticleUserParameterType::Float;
            param.value = Math::Vector3{1.0F, 0.0F, 0.0F};
            param.alpha = 1.0F;
            sys.parameters.push_back(std::move(param));
        });
    };
    paramsHeader->add(addParamBtn, CentreSlot());

    m_parametersScroll = prism::make<prism::ScrollBox>();
    auto paramsBox = prism::vbox(0.0F);
    paramsBox->add(MakeSectionHeader(std::move(paramsHeader)));
    paramsBox->add(m_parametersScroll, prism::Slot{1.0F});

    auto rightSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    rightSplit->add(Panel(inspectorBox), prism::Splitter::Pane{460.0F, 200.0F, 1e9F, true});
    rightSplit->add(Panel(paramsBox), prism::Splitter::Pane{280.0F, 150.0F, 1e9F, false});
    EditorLayoutStore::Instance().BindSplitter("vfx.right", *rightSplit);
    return rightSplit;
}

void EditorVfxEditor::ShowAddEmitterMenu(const prism::Rect anchor) {
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<VFX::ParticleTemplateKind, const char*>, 5> kTemplates{{
        {VFX::ParticleTemplateKind::Empty, "Empty Emitter"},
        {VFX::ParticleTemplateKind::Fountain, "Fountain Preset"},
        {VFX::ParticleTemplateKind::Sparks, "Sparks Preset"},
        {VFX::ParticleTemplateKind::Fire, "Fire Preset"},
        {VFX::ParticleTemplateKind::Smoke, "Smoke Preset"},
    }};
    for (const auto& [kind, label] : kTemplates) {
        items.push_back(prism::MenuItem::Item(label, [this, kind] {
            m_editor.EditVfxSystem([this, kind](Asset::ParticleSystemAsset& sys) {
                if (sys.emitters.size() >= Asset::kMaximumParticleEmitters) {
                    return;
                }
                Asset::ParticleSystemAsset tpl = VFX::CreateParticleTemplate(kind);
                if (tpl.emitters.empty()) {
                    return;
                }
                Asset::ParticleEmitterAsset em = std::move(tpl.emitters.front());
                em.name = MakeUniqueEmitterName(sys, em.name.empty() ? "Emitter" : em.name);
                std::uint32_t nextId = NextFreeModuleIdInSystem(sys);
                for (auto* stageVec : {&em.emitterUpdate, &em.particleSpawn, &em.particleUpdate, &em.render, &em.detached}) {
                    for (Asset::ParticleModule& mod : *stageVec) {
                        mod.id = nextId++;
                    }
                }
                sys.emitters.push_back(std::move(em));
                if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                    s->selectedEmitter = sys.emitters.size() - 1U;
                }
                m_selectedModuleId = 0U;
            });
        }, "", prism::Icon::Light));
    }
    prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
}

void EditorVfxEditor::RebuildEmittersList() {
    if (!m_emittersScroll) {
        return;
    }
    Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    auto list = prism::vbox(4.0F);
    list->setPadding(prism::Margin(6.0F));

    for (std::size_t i = 0U; i < session->system.emitters.size(); ++i) {
        const Asset::ParticleEmitterAsset& emitter = session->system.emitters[i];
        const bool selected = (i == session->selectedEmitter);
        const bool solo = (session->soloEmitter.has_value() && *session->soloEmitter == i);

        auto card = prism::make<prism::Border>();
        card->setBackground(selected ? prism::ThemeColor(&prism::Theme::rowSelected)
                                     : prism::ThemeColor(&prism::Theme::panel));
        card->setBorder(selected ? prism::ThemeColor(&prism::Theme::highlight)
                                 : prism::ThemeColor(&prism::Theme::border),
                        prism::Margin(1.0F));
        card->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));

        auto col = prism::vbox(3.0F);
        auto topRow = prism::hbox(4.0F);

        auto enToggle = prism::make<prism::CheckBox>("", emitter.enabled);
        enToggle->setTooltip("Enable or disable this emitter");
        enToggle->onChange = [this, i](const bool v) {
            m_editor.EditVfxSystem([i, v](Asset::ParticleSystemAsset& sys) {
                if (i < sys.emitters.size()) {
                    sys.emitters[i].enabled = v;
                }
            });
        };
        topRow->add(enToggle, CentreSlot());

        auto selectBtn = prism::make<prism::Button>(
            emitter.name.empty() ? ("Emitter " + std::to_string(i + 1U)) : emitter.name,
            prism::Icon::Light,
            selected ? prism::ButtonStyle::Primary : prism::ButtonStyle::Flat);
        selectBtn->onClick = [this, i] {
            if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                if (s->selectedEmitter != i) {
                    s->selectedEmitter = i;
                    m_selectedModuleId = 0U;
                }
            }
        };
        topRow->add(selectBtn, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

        auto soloBtn = prism::make<prism::Button>("S", prism::Icon::None,
                                                  solo ? prism::ButtonStyle::Primary : prism::ButtonStyle::Toolbar);
        soloBtn->setTooltip("Solo this emitter in the preview");
        soloBtn->onClick = [this, i] {
            if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                if (s->soloEmitter.has_value() && *s->soloEmitter == i) {
                    s->soloEmitter.reset();
                } else {
                    s->soloEmitter = i;
                }
                static_cast<void>(m_editor.CompileVfxEditor());
            }
        };
        topRow->add(soloBtn, CentreSlot());
        col->add(topRow);

        auto actionsRow = prism::hbox(3.0F);
        auto nameEdit = prism::make<prism::TextInput>(emitter.name, "Emitter name");
        nameEdit->onCommit = [this, i](const std::string& text) {
            if (text.empty()) {
                return;
            }
            m_editor.EditVfxSystem([i, text](Asset::ParticleSystemAsset& sys) {
                if (i < sys.emitters.size()) {
                    sys.emitters[i].name = text;
                }
            });
        };
        actionsRow->add(nameEdit, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

        auto upBtn = prism::make<prism::Button>("", prism::Icon::ChevronUp, prism::ButtonStyle::Flat);
        upBtn->setTooltip("Move emitter up");
        upBtn->setEnabled(i > 0U);
        upBtn->onClick = [this, i] {
            m_editor.EditVfxSystem([this, i](Asset::ParticleSystemAsset& sys) {
                if (i > 0U && i < sys.emitters.size()) {
                    std::swap(sys.emitters[i - 1U], sys.emitters[i]);
                    if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                        s->selectedEmitter = i - 1U;
                    }
                }
            });
        };
        auto downBtn = prism::make<prism::Button>("", prism::Icon::ChevronDown, prism::ButtonStyle::Flat);
        downBtn->setTooltip("Move emitter down");
        downBtn->setEnabled(i + 1U < session->system.emitters.size());
        downBtn->onClick = [this, i] {
            m_editor.EditVfxSystem([this, i](Asset::ParticleSystemAsset& sys) {
                if (i + 1U < sys.emitters.size()) {
                    std::swap(sys.emitters[i], sys.emitters[i + 1U]);
                    if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                        s->selectedEmitter = i + 1U;
                    }
                }
            });
        };
        auto dupBtn = prism::make<prism::Button>("", prism::Icon::Duplicate, prism::ButtonStyle::Flat);
        dupBtn->setTooltip("Duplicate emitter");
        dupBtn->onClick = [this, i] {
            m_editor.EditVfxSystem([this, i](Asset::ParticleSystemAsset& sys) {
                if (i >= sys.emitters.size() || sys.emitters.size() >= Asset::kMaximumParticleEmitters) {
                    return;
                }
                Asset::ParticleEmitterAsset copy = sys.emitters[i];
                copy.name = MakeUniqueEmitterName(sys, copy.name + "_Copy");
                std::unordered_map<std::uint32_t, std::uint32_t> idRemap;
                std::uint32_t nextId = NextFreeModuleIdInSystem(sys);
                for (auto* stageVec : {&copy.emitterUpdate, &copy.particleSpawn, &copy.particleUpdate, &copy.render, &copy.detached}) {
                    for (Asset::ParticleModule& mod : *stageVec) {
                        const std::uint32_t newId = nextId++;
                        idRemap[mod.id] = newId;
                        mod.id = newId;
                    }
                }
                for (Asset::ParticleMathNode& mathNode : copy.mathNodes) {
                    const std::uint32_t newId = nextId++;
                    idRemap[mathNode.id] = newId;
                    mathNode.id = newId;
                }
                const auto remapId = [&idRemap](std::uint32_t& id) {
                    if (id != 0U) {
                        if (const auto it = idRemap.find(id); it != idRemap.end()) {
                            id = it->second;
                        }
                    }
                };
                for (auto* stageVec : {&copy.emitterUpdate, &copy.particleSpawn, &copy.particleUpdate, &copy.render, &copy.detached}) {
                    for (Asset::ParticleModule& mod : *stageVec) {
                        for (auto& [pin, in] : mod.floats) {
                            remapId(in.wireNode);
                        }
                        for (auto& [pin, in] : mod.vectors) {
                            remapId(in.wireNode);
                        }
                    }
                }
                for (Asset::ParticleMathNode& mathNode : copy.mathNodes) {
                    for (std::uint32_t& inputId : mathNode.inputs) {
                        remapId(inputId);
                    }
                }
                for (Asset::ParticleDetachedLink& link : copy.detachedLinks) {
                    remapId(link.fromModuleId);
                    remapId(link.toModuleId);
                }
                sys.emitters.insert(sys.emitters.begin() + static_cast<std::ptrdiff_t>(i + 1U), std::move(copy));
                if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                    s->selectedEmitter = i + 1U;
                }
                m_selectedModuleId = 0U;
            });
        };
        auto delBtn = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
        delBtn->setTooltip("Delete emitter");
        delBtn->setEnabled(session->system.emitters.size() > 1U);
        delBtn->onClick = [this, i] {
            m_editor.EditVfxSystem([this, i](Asset::ParticleSystemAsset& sys) {
                if (sys.emitters.size() <= 1U || i >= sys.emitters.size()) {
                    return;
                }
                sys.emitters.erase(sys.emitters.begin() + static_cast<std::ptrdiff_t>(i));
                if (Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                    if (s->selectedEmitter >= sys.emitters.size()) {
                        s->selectedEmitter = sys.emitters.size() - 1U;
                    }
                    if (s->soloEmitter.has_value() && *s->soloEmitter >= sys.emitters.size()) {
                        s->soloEmitter.reset();
                    }
                }
                m_selectedModuleId = 0U;
            });
        };

        actionsRow->add(upBtn, CentreSlot());
        actionsRow->add(downBtn, CentreSlot());
        actionsRow->add(dupBtn, CentreSlot());
        actionsRow->add(delBtn, CentreSlot());
        col->add(actionsRow);

        card->setChild(col);
        list->add(card);
    }

    m_emittersScroll->setContent(prism::make<FitWidth>(list));
}

void EditorVfxEditor::RebuildParametersList() {
    if (!m_parametersScroll) {
        return;
    }
    Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    auto list = prism::vbox(4.0F);
    list->setPadding(prism::Margin(6.0F));

    if (session->system.parameters.empty()) {
        auto emptyLabel = prism::make<prism::Label>(
            "No user parameters.\nClick + Add to expose a parameter to scripts and modules.",
            prism::FontRole::Small);
        emptyLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
        list->add(emptyLabel);
    }

    for (std::size_t i = 0U; i < session->system.parameters.size(); ++i) {
        const Asset::ParticleUserParameter& param = session->system.parameters[i];
        auto card = prism::make<prism::Border>();
        card->setBackground(prism::ThemeColor(&prism::Theme::panel));
        card->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
        card->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));

        auto col = prism::vbox(3.0F);
        auto topRow = prism::hbox(4.0F);

        auto nameField = prism::make<prism::TextInput>(param.name, "Name");
        nameField->onCommit = [this, i](const std::string& newName) {
            if (newName.empty()) {
                return;
            }
            m_editor.EditVfxSystem([i, newName](Asset::ParticleSystemAsset& sys) {
                if (i >= sys.parameters.size()) {
                    return;
                }
                const std::string oldName = sys.parameters[i].name;
                sys.parameters[i].name = newName;
                for (Asset::ParticleEmitterAsset& em : sys.emitters) {
                    for (auto* stageVec : {&em.emitterUpdate, &em.particleSpawn, &em.particleUpdate, &em.render, &em.detached}) {
                        for (Asset::ParticleModule& mod : *stageVec) {
                            for (auto& [pin, in] : mod.floats) {
                                if (in.kind == Asset::ParticleValueKind::UserParameter && in.parameter == oldName) {
                                    in.parameter = newName;
                                }
                            }
                            for (auto& [pin, in] : mod.vectors) {
                                if (in.kind == Asset::ParticleValueKind::UserParameter && in.parameter == oldName) {
                                    in.parameter = newName;
                                }
                            }
                            for (auto& [pin, in] : mod.colors) {
                                if (in.kind == Asset::ParticleValueKind::UserParameter && in.parameter == oldName) {
                                    in.parameter = newName;
                                }
                            }
                        }
                    }
                }
            });
        };
        topRow->add(nameField, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});

        auto typeCombo = prism::make<prism::ComboBox>(
            std::vector<std::string>{"Float", "Vector", "Color", "Bool", "Int"},
            static_cast<int>(param.type));
        typeCombo->onChange = [this, i](const int typeIdx) {
            m_editor.EditVfxSystem([i, typeIdx](Asset::ParticleSystemAsset& sys) {
                if (i < sys.parameters.size() && typeIdx >= 0 && typeIdx <= 4) {
                    sys.parameters[i].type = static_cast<Asset::ParticleUserParameterType>(typeIdx);
                }
            });
        };
        auto typeBox = prism::make<prism::SizeBox>();
        typeBox->setWidthOverride(78.0F);
        typeBox->setContent(typeCombo);
        topRow->add(typeBox, CentreSlot());

        auto delBtn = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
        delBtn->onClick = [this, i] {
            m_editor.EditVfxSystem([i](Asset::ParticleSystemAsset& sys) {
                if (i < sys.parameters.size()) {
                    sys.parameters.erase(sys.parameters.begin() + static_cast<std::ptrdiff_t>(i));
                }
            });
        };
        topRow->add(delBtn, CentreSlot());
        col->add(topRow);

        // Default value editor based on parameter type.
        switch (param.type) {
            case Asset::ParticleUserParameterType::Float: {
                auto nf = prism::make<prism::NumberField>(param.value.x, 0.05F, 3);
                nf->onChange = [this, i](const float v) {
                    m_editor.EditVfxSystem([i, v](Asset::ParticleSystemAsset& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = static_cast<float>(v);
                        }
                    });
                };
                col->add(nf);
                break;
            }
            case Asset::ParticleUserParameterType::Int: {
                auto nf = prism::make<prism::NumberField>(
                    std::round(param.value.x), 1.0F, 0);
                nf->onChange = [this, i](const float v) {
                    m_editor.EditVfxSystem([i, v](Asset::ParticleSystemAsset& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = static_cast<float>(std::llround(static_cast<double>(v)));
                        }
                    });
                };
                col->add(nf);
                break;
            }
            case Asset::ParticleUserParameterType::Bool: {
                auto cb = prism::make<prism::CheckBox>("Enabled by default", param.value.x >= 0.5F);
                cb->onChange = [this, i](const bool v) {
                    m_editor.EditVfxSystem([i, v](Asset::ParticleSystemAsset& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = v ? 1.0F : 0.0F;
                        }
                    });
                };
                col->add(cb);
                break;
            }
            case Asset::ParticleUserParameterType::Vector: {
                auto vf = prism::make<prism::Vec3Field>(0.05F, 3);
                vf->setValue(param.value.x, param.value.y, param.value.z);
                prism::Vec3Field* const rawVf = vf.get();
                vf->onChange = [this, i, rawVf](const int, const float) {
                    const float x = rawVf->get(0);
                    const float y = rawVf->get(1);
                    const float z = rawVf->get(2);
                    m_editor.EditVfxSystem([i, x, y, z](Asset::ParticleSystemAsset& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value =
                                Math::Vector3{x, y, z};
                        }
                    });
                };
                col->add(vf);
                break;
            }
            case Asset::ParticleUserParameterType::Color: {
                const auto toByte = [](const float c) -> std::uint8_t {
                    return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0F), 0L, 255L));
                };
                auto swatch = prism::make<prism::ColorField>(prism::Color(
                    toByte(param.value.x),
                    toByte(param.value.y),
                    toByte(param.value.z),
                    toByte(param.alpha)));
                swatch->onChange = [this, i](const prism::Color c) {
                    m_editor.EditVfxSystem([i, c](Asset::ParticleSystemAsset& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value = Math::Vector3{
                                static_cast<float>(c.r) / 255.0F,
                                static_cast<float>(c.g) / 255.0F,
                                static_cast<float>(c.b) / 255.0F};
                            sys.parameters[i].alpha = static_cast<float>(c.a) / 255.0F;
                        }
                    });
                };
                col->add(swatch);
                break;
            }
        }

        card->setChild(col);
        list->add(card);
    }

    m_parametersScroll->setContent(prism::make<FitWidth>(list));
}

void EditorVfxEditor::RebuildGraphFromAsset() {
    if (!m_graph) {
        return;
    }
    const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr || session->selectedEmitter >= session->system.emitters.size()) {
        return;
    }
    m_syncingGraph = true;
    m_graph->setNodeLibrary(BuildVfxGraphNodeLibrary(session->system, true));
    BuildEmitterGraph(session->system, session->selectedEmitter, *m_graph);
    // Sync BlueprintGraph::m_lastSnapshot against the freshly rebuilt graph so future drags/edits
    // always detect a diff and emit onChanged(). Must run BEFORE ApplyVfxCompileMessagesToGraph
    // because commit() clears BlueprintGraph::m_compiled.
    m_graph->commit("Sync");
    ApplyVfxCompileMessagesToGraph(*m_graph, session->compileMessages);
    if (m_selectedModuleId != 0U) {
        m_graph->select(static_cast<int>(m_selectedModuleId));
    }
    m_syncingGraph = false;
}

void EditorVfxEditor::SyncGraphToAsset() {
    if (m_syncingGraph || !m_graph) {
        return;
    }
    Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr || session->selectedEmitter >= session->system.emitters.size()) {
        return;
    }
    const std::size_t emitterIdx = session->selectedEmitter;
    Asset::ParticleEmitterAsset extracted{};
    if (!ExtractEmitterFromGraph(*m_graph, session->system, emitterIdx, extracted)) {
        return;
    }
    m_syncingGraph = true;
    m_editor.EditVfxSystem([emitterIdx, &extracted](Asset::ParticleSystemAsset& sys) {
        if (emitterIdx < sys.emitters.size()) {
            sys.emitters[emitterIdx] = extracted;
        }
    });
    if (const Editor::VfxEditSession* const updated = m_editor.VfxEditSessionPtr()) {
        m_shownSystem = updated->system;
        m_shownCompileCount = updated->compileMessages.size();
        ApplyVfxCompileMessagesToGraph(*m_graph, updated->compileMessages);
    }
    RebuildCompileTable();
    RebuildInspector();
    m_syncingGraph = false;
}

void EditorVfxEditor::RebuildInspector() {
    if (!m_inspectorScroll) {
        return;
    }
    const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    m_inspectorScroll->setContent(BuildVfxInspectorContent(
        m_editor, session->selectedEmitter, m_selectedModuleId, [this] {
            if (const Editor::VfxEditSession* const s = m_editor.VfxEditSessionPtr()) {
                m_shownSystem = s->system;
                m_shownCompileCount = s->compileMessages.size();
            }
            RebuildGraphFromAsset();
            RebuildCompileTable();
            RebuildEmittersList();
        }));
}

void EditorVfxEditor::RebuildCompileTable() {
    const Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    const std::size_t count = session != nullptr ? session->compileMessages.size() : 0U;
    if (m_compileTable) {
        m_compileTable->setRowCount(static_cast<int>(count));
        m_compileTable->invalidate();
    }
    if (m_compileSummaryLabel && session != nullptr) {
        std::size_t errorCount = 0U;
        std::size_t warnCount = 0U;
        for (const VFX::ParticleCompileMessage& msg : session->compileMessages) {
            if (msg.severity == VFX::ParticleCompileMessage::Severity::Error) {
                ++errorCount;
            } else {
                ++warnCount;
            }
        }
        std::string summary;
        prism::ThemeColor summaryColor(&prism::Theme::success);
        if (errorCount > 0U) {
            summary = std::to_string(errorCount) + (errorCount == 1U ? " error" : " errors");
            if (warnCount > 0U) {
                summary += ", " + std::to_string(warnCount) + (warnCount == 1U ? " warning" : " warnings");
            }
            summaryColor = prism::ThemeColor(&prism::Theme::error);
        } else if (warnCount > 0U) {
            summary = std::to_string(warnCount) + (warnCount == 1U ? " warning" : " warnings");
            summaryColor = prism::ThemeColor(&prism::Theme::warning);
        } else {
            summary = "Ready (0 issues)";
        }
        if (m_compileSummaryLabel->text() != summary) {
            m_compileSummaryLabel->setText(summary);
        }
        m_compileSummaryLabel->setColor(summaryColor);
    }
}

void EditorVfxEditor::Refresh() {
    Editor::VfxEditSession* const session = m_editor.VfxEditSessionPtr();
    if (session == nullptr) {
        return;
    }
    if (m_tab) {
        m_tab->setDirty(m_editor.IsVfxEditorDirty());
    }
    if (m_playButton && (!m_shownPlayingInit || m_shownPlaying != session->playing)) {
        m_shownPlaying = session->playing;
        m_shownPlayingInit = true;
        if (session->playing) {
            m_playButton->setText("Pause");
            m_playButton->setIcon(prism::Icon::Pause);
            m_playButton->setStyle(prism::ButtonStyle::Primary);
        } else {
            m_playButton->setText("Play");
            m_playButton->setIcon(prism::Icon::Play);
            m_playButton->setStyle(prism::ButtonStyle::Success);
        }
    }
    if (m_previewStateBadge && m_previewStatePill) {
        const std::uint8_t nextState = session->playing
                                           ? kPreviewStatePlaying
                                           : (session->playhead > 1.0e-4 ? kPreviewStatePaused : kPreviewStateStopped);
        if (nextState != m_shownPreviewState) {
            m_shownPreviewState = nextState;
            const prism::Theme& t = prism::theme();
            if (nextState == kPreviewStatePlaying) {
                m_previewStateBadge->setText("PLAYING");
                m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::success));
                m_previewStatePill->setBackground(t.panel.mix(t.success, 0.18F));
                m_previewStatePill->setBorder(t.success.withAlpha(165), prism::Margin(1.0F));
            } else if (nextState == kPreviewStatePaused) {
                m_previewStateBadge->setText("PAUSED");
                m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::warning));
                m_previewStatePill->setBackground(t.panel.mix(t.warning, 0.18F));
                m_previewStatePill->setBorder(t.warning.withAlpha(165), prism::Margin(1.0F));
            } else {
                m_previewStateBadge->setText("STOPPED");
                m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::textDim));
                m_previewStatePill->setBackground(prism::ThemeColor(&prism::Theme::control));
                m_previewStatePill->setBorder(prism::ThemeColor(&prism::Theme::borderStrong), prism::Margin(1.0F));
            }
        }
    }
    if (m_loopButton) {
        m_loopButton->setChecked(session->loop);
    }
    if (m_seedButton) {
        m_seedButton->setChecked(session->fixedSeed);
    }
    if (m_gridButton) {
        m_gridButton->setChecked(session->showGrid);
    }
    if (m_boundsButton) {
        m_boundsButton->setChecked(session->showBounds);
    }
    if (m_timecodeLabel) {
        const std::string tc = SecondsText(session->playhead) + " / " +
                               SecondsText(static_cast<double>(session->system.duration)) + "s";
        if (m_timecodeLabel->text() != tc) {
            m_timecodeLabel->setText(tc);
        }
    }
    if (m_statsOverlayLabel) {
        const std::string stats = FormatVfxPreviewStatsText(m_editor);
        if (m_statsOverlayLabel->text() != stats) {
            m_statsOverlayLabel->setText(stats);
        }
    }
    if (m_status) {
        std::size_t errorCount = 0U;
        for (const VFX::ParticleCompileMessage& msg : session->compileMessages) {
            if (msg.severity == VFX::ParticleCompileMessage::Severity::Error) {
                ++errorCount;
            }
        }
        std::string text;
        if (errorCount > 0U) {
            text = std::to_string(errorCount) + " compile error(s) (preview holding last good program)";
        } else if (!session->status.empty()) {
            text = session->status;
        } else {
            text = "Ready";
        }
        if (m_status->text() != text) {
            m_status->setText(text);
        }
    }
    if (m_timeline) {
        m_timeline->invalidate();
    }

    const bool emitterChanged = !m_shownValid || m_shownEmitterIndex != session->selectedEmitter;
    const bool assetChanged =
        !m_shownValid || !(m_shownSystem == session->system) ||
        emitterChanged ||
        m_shownSoloEmitter != session->soloEmitter;
    const bool selectionChanged = !m_shownValid || m_shownModuleId != m_selectedModuleId;
    if (assetChanged) {
        m_shownSystem = session->system;
        m_shownEmitterIndex = session->selectedEmitter;
        m_shownSoloEmitter = session->soloEmitter;
        m_shownModuleId = m_selectedModuleId;
        m_shownCompileCount = session->compileMessages.size();
        m_shownValid = true;
        RebuildEmittersList();
        RebuildParametersList();
        RebuildGraphFromAsset();
        if (emitterChanged && m_graph) {
            m_graph->frameAll();
        }
        RebuildInspector();
        RebuildCompileTable();
    } else {
        if (selectionChanged) {
            // Selection changes update only the Inspector; never rebuild the graph mid-drag.
            m_shownModuleId = m_selectedModuleId;
            RebuildInspector();
        }
        if (m_shownCompileCount != session->compileMessages.size()) {
            m_shownCompileCount = session->compileMessages.size();
            if (m_graph) {
                ApplyVfxCompileMessagesToGraph(*m_graph, session->compileMessages);
            }
            RebuildCompileTable();
        }
    }
}

void EditorVfxEditor::Render() {
    if (m_window == 0 || m_editor.VfxEditSessionPtr() == nullptr) {
        return;
    }
    std::vector<EditorHost::ViewImage> views;
    if (m_viewport && m_viewport->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInput input = m_host.CaptureViewInput(m_window, rect);
            const float dpiScale = std::max(1.0F, input.dpiScale);
            Math::Vector2 used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderVfxPreview(
                Math::Vector2{std::max(1.0F, std::floor(rect.w * dpiScale)), std::max(1.0F, std::floor(rect.h * dpiScale))},
                used, input);
            m_host.ApplyViewCursor(m_window, input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                const float invScale = dpiScale > 1.0F ? (1.0F / dpiScale) : 1.0F;
                EditorHost::ViewImage view{};
                view.rect = prism::Rect{std::floor(rect.x), std::floor(rect.y), used.x * invScale, used.y * invScale};
                view.texture = static_cast<std::uint32_t>(texture);
                view.usedWidth = used.x;
                view.usedHeight = used.y;
                view.textureWidth = used.x;
                view.textureHeight = used.y;
                views.push_back(view);
            }
        }
    }
    m_host.RenderWindow(m_window, views);
}

} // namespace UVE::Editor
