// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_editor.h"

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

constexpr int kVfxWindowWidthUVE = 1440;
constexpr int kVfxWindowHeightUVE = 860;

constexpr std::uint8_t kPreviewStateStoppedUVE = 0U;
constexpr std::uint8_t kPreviewStatePausedUVE = 1U;
constexpr std::uint8_t kPreviewStatePlayingUVE = 2U;

/// Root window border that intercepts VFX Editor shortcuts (Save, Compile, Undo/Redo, Play/Pause)
/// during PRISM's tunnel phase while respecting focused text inputs and modal popups.
class VfxWindowFrameUVE final : public prism::Border {
public:
    std::function<bool(const prism::KeyEvent&)> onShortcut;

    prism::Reply onPreviewKeyDown(const prism::KeyEvent& e) override {
        if (onShortcut && onShortcut(e)) {
            return prism::Reply::handled();
        }
        return prism::Border::onPreviewKeyDown(e);
    }
};

[[nodiscard]] prism::Slot CentreSlotUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string SecondsTextUVE(const double t) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", t);
    return buf;
}

[[nodiscard]] prism::Ref<prism::Border> MakeSectionHeaderUVE(prism::Ref<prism::Box> row) {
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(std::move(row));
    return bar;
}

[[nodiscard]] std::string MakeUniqueEmitterNameUVE(const Asset::ParticleSystemAssetUVE& sys,
                                                   const std::string& baseName) {
    const auto exists = [&sys](const std::string& candidate) {
        return std::any_of(sys.emitters.begin(), sys.emitters.end(),
                           [&candidate](const Asset::ParticleEmitterAssetUVE& e) { return e.name == candidate; });
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

[[nodiscard]] std::string MakeUniqueParameterNameUVE(const Asset::ParticleSystemAssetUVE& sys,
                                                     const std::string& baseName) {
    const auto exists = [&sys](const std::string& candidate) {
        return std::any_of(sys.parameters.begin(), sys.parameters.end(),
                           [&candidate](const Asset::ParticleUserParameterUVE& p) { return p.name == candidate; });
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

[[nodiscard]] std::uint32_t NextFreeModuleIdInSystemUVE(const Asset::ParticleSystemAssetUVE& sys) noexcept {
    std::uint32_t maxId = 1U;
    for (const Asset::ParticleEmitterAssetUVE& emitter : sys.emitters) {
        for (const auto* stageVec : {&emitter.emitterUpdate, &emitter.particleSpawn, &emitter.particleUpdate, &emitter.render, &emitter.detached}) {
            for (const Asset::ParticleModuleUVE& mod : *stageVec) {
                maxId = std::max(maxId, mod.id);
            }
        }
        for (const Asset::ParticleMathNodeUVE& mathNode : emitter.mathNodes) {
            maxId = std::max(maxId, mathNode.id);
        }
    }
    return maxId + 1U;
}

} // namespace

EditorVfxEditorUVE::EditorVfxEditorUVE(EditorUVE& editor, EditorHostUVE& host)
    : m_editor(editor), m_host(host) {}

EditorVfxEditorUVE::~EditorVfxEditorUVE() {
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.CloseWindowUVE(m_window);
    }
}

bool EditorVfxEditorUVE::SaveScreenshotUVE(const std::filesystem::path& path) {
    if (m_window == 0 || !m_host.IsReadyUVE()) {
        return false;
    }
    return m_host.SaveWindowScreenshotUVE(m_window, path);
}

void EditorVfxEditorUVE::SelectModuleIdUVE(const std::uint32_t moduleId) {
    m_selectedModuleId = moduleId;
    m_shownModuleId = moduleId;
    if (m_graph && moduleId != 0U) {
        m_graph->select(static_cast<int>(moduleId));
    }
    if (m_window != 0 && m_host.IsReadyUVE()) {
        m_host.AppUVE().runInWindow(m_window, [this] { RebuildInspectorUVE(); });
    }
}

void EditorVfxEditorUVE::SyncUVE() {
    if (!m_host.IsReadyUVE()) {
        return;
    }
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    const std::filesystem::path path = session != nullptr ? session->path : std::filesystem::path{};
    if (m_window != 0 && (session == nullptr || path != m_openPath)) {
        m_host.CloseWindowUVE(m_window);
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
        m_window = m_host.OpenWindowUVE(
            BuildUVE(),
            "Particle System Editor - " + path.filename().string(),
            kVfxWindowWidthUVE,
            kVfxWindowHeightUVE);
        if (m_window == 0) {
            m_editor.CloseVfxEditorUVE();
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
    const double dt = m_lastTick.time_since_epoch().count() == 0
                          ? (1.0 / 60.0)
                          : std::chrono::duration<double>(now - m_lastTick).count();
    m_lastTick = now;
    m_editor.TickVfxEditorUVE(std::min(dt, 0.1));
    m_host.AppUVE().runInWindow(m_window, [this] { RefreshUVE(); });
}

void EditorVfxEditorUVE::RequestCloseUVE() {
    if (!m_editor.IsVfxEditorDirtyUVE()) {
        m_editor.CloseVfxEditorUVE();
        return;
    }
    if (m_closeAsked) {
        return;
    }
    m_closeAsked = true;
    const std::string name = m_openPath.filename().string();
    m_host.AppUVE().runInWindow(m_window, [this, name] {
        auto text = prism::make<prism::Label>("Save the changes to " + name + " before closing?");
        prism::showDialog(
            "Close Particle System",
            text,
            {prism::DialogButton{"Save", true, [this] {
                 m_closeAsked = false;
                 if (m_editor.SaveVfxEditorUVE()) {
                     m_editor.CloseVfxEditorUVE();
                 }
             }},
             prism::DialogButton{"Don't Save", false, [this] {
                 m_closeAsked = false;
                 m_editor.CloseVfxEditorUVE();
             }},
             prism::DialogButton{"Cancel", false, [this] { m_closeAsked = false; }}},
            420.0F);
    });
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildUVE() {
    auto window = prism::vbox();
    window->add(BuildTitleBarUVE());

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    m_leftColumn = BuildLeftColumnUVE();
    m_rightColumn = BuildRightColumnUVE();
    columns->add(m_leftColumn, prism::Splitter::Pane{395.0F, 330.0F, 640.0F, false});
    columns->add(BuildCentreColumnUVE(), prism::Splitter::Pane{695.0F, 420.0F, 1e9F, true});
    columns->add(m_rightColumn, prism::Splitter::Pane{350.0F, 260.0F, 600.0F, false});
    window->add(columns, prism::Slot{1.0F});

    auto frame = prism::make<VfxWindowFrameUVE>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->onShortcut = [this](const prism::KeyEvent& e) -> bool {
        const bool ctrl = (e.mods & prism::ModCtrl) != 0U;
        const bool shift = (e.mods & prism::ModShift) != 0U;
        const bool alt = (e.mods & prism::ModAlt) != 0U;
        if (ctrl && !alt && !shift && e.key == prism::Key::S) {
            static_cast<void>(m_editor.SaveVfxEditorUVE());
            return true;
        }
        if (!ctrl && !alt && !shift && e.key == prism::Key::F7) {
            static_cast<void>(m_editor.CompileVfxEditorUVE());
            m_shownCompileCount = static_cast<std::size_t>(-1);
            return true;
        }
        const bool modalOrTyping = prism::Application::exists() && prism::Application::get().wantsKeyboard();
        if (modalOrTyping) {
            return false;
        }
        if (ctrl && !alt && e.key == prism::Key::Z) {
            static_cast<void>(shift ? m_editor.RedoVfxEditUVE() : m_editor.UndoVfxEditUVE());
            return true;
        }
        if (ctrl && !alt && !shift && e.key == prism::Key::Y) {
            static_cast<void>(m_editor.RedoVfxEditUVE());
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
                if (const EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                    if (s->playing) {
                        m_editor.PauseVfxPreviewUVE();
                    } else {
                        m_editor.PlayVfxPreviewUVE();
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

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildTitleBarUVE() {
    m_tab = prism::make<DocumentTabUVE>();
    m_tab->setFallbackIcon(prism::Icon::VfxGraph);
    m_tab->setName(m_openPath.empty() && m_editor.VfxEditSessionPtrUVE() != nullptr
                       ? m_editor.VfxEditSessionPtrUVE()->path.stem().string()
                       : m_openPath.stem().string());

    EditorHeaderDescUVE desc;
    desc.documentTab = m_tab;
    desc.toolbarRow = BuildToolbarUVE();
    desc.onClose = [this] { RequestCloseUVE(); };
    desc.menus.push_back({"File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item(
            "Save", [this] { static_cast<void>(m_editor.SaveVfxEditorUVE()); }, "Ctrl+S", prism::Icon::Save));
        items.push_back(prism::MenuItem::Item(
            "Compile",
            [this] {
                static_cast<void>(m_editor.CompileVfxEditorUVE());
                m_shownCompileCount = static_cast<std::size_t>(-1);
            },
            "F7", prism::Icon::Check));
        items.push_back(prism::MenuItem::Item("Show in Asset Table", [this] {
            const Asset::ProjectFileSnapshotUVE now = m_editor.m_services->GetProjectFileIndexUVE().GetSnapshotUVE();
            std::error_code error;
            const std::filesystem::path relative = std::filesystem::relative(m_openPath, now.contentRoot, error);
            if (!error) {
                m_editor.m_assetTableDirectory = relative.parent_path();
            }
        }, "", prism::Icon::Folder));
        items.push_back(prism::MenuItem::Sep());
        items.push_back(prism::MenuItem::Item("Close", [this] { RequestCloseUVE(); }, "", prism::Icon::Close));
        return items;
    }});
    desc.menus.push_back({"Edit", [this] {
        const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
        prism::MenuItem undo = prism::MenuItem::Item(
            "Undo", [this] { static_cast<void>(m_editor.UndoVfxEditUVE()); }, "Ctrl+Z", prism::Icon::Undo);
        undo.enabled = session != nullptr && !session->undo.empty();
        prism::MenuItem redo = prism::MenuItem::Item(
            "Redo", [this] { static_cast<void>(m_editor.RedoVfxEditUVE()); }, "Ctrl+Y", prism::Icon::Redo);
        redo.enabled = session != nullptr && !session->redo.empty();
        return std::vector<prism::MenuItem>{std::move(undo), std::move(redo)};
    }});
    desc.menus.push_back({"Window", [this] {
        const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
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
                    if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                        s->previewCameraResetPending = true;
                    }
                },
                "", prism::Icon::Camera),
            prism::MenuItem::Check(
                "Show Fixed Bounds", session != nullptr && session->showBounds, [this] {
                    if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
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
    return BuildEditorHeaderUVE(std::move(desc));
}

void EditorVfxEditorUVE::SetGraphFocusUVE(const bool on) {
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

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildToolbarUVE() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(5.0F, 1.0F, 5.0F, 1.0F));

    auto save = ToolButtonUVE(prism::Icon::Save, "Save Particle System (.uvfx) (Ctrl+S)", "Save");
    save->onClick = [this] { static_cast<void>(m_editor.SaveVfxEditorUVE()); };
    row->add(save, CentreSlotUVE());

    auto undo = ToolButtonUVE(prism::Icon::Undo, "Undo (Ctrl+Z)");
    undo->onClick = [this] { static_cast<void>(m_editor.UndoVfxEditUVE()); };
    auto redo = ToolButtonUVE(prism::Icon::Redo, "Redo (Ctrl+Y)");
    redo->onClick = [this] { static_cast<void>(m_editor.RedoVfxEditUVE()); };
    row->add(undo, CentreSlotUVE());
    row->add(redo, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());

    auto compile = ToolButtonUVE(prism::Icon::Compile, "Compile all emitters and refresh preview (F7)", "Compile");
    compile->onClick = [this] {
        static_cast<void>(m_editor.CompileVfxEditorUVE());
        m_shownCompileCount = static_cast<std::size_t>(-1);
    };
    row->add(compile, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());

    m_seedButton = ToolButtonUVE(prism::Icon::Lock, "Deterministic fixed seed on restart", "Fixed Seed");
    m_seedButton->setCheckable(true);
    m_seedButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->fixedSeed = !s->fixedSeed;
            m_editor.RestartVfxPreviewUVE();
        }
    };
    row->add(m_seedButton, CentreSlotUVE());

    m_boundsButton = ToolButtonUVE(prism::Icon::Cube, "Show system fixed bounds box in 3D preview", "Bounds");
    m_boundsButton->setCheckable(true);
    m_boundsButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->showBounds = !s->showBounds;
        }
    };
    row->add(m_boundsButton, CentreSlotUVE());
    auto focus = ToolButtonUVE(prism::Icon::LayoutFull, "Focus: the graph fills the window (side panels and compile results hidden)", "Focus");
    focus->setCheckable(true);
    focus->onToggle = [this](const bool on) { SetGraphFocusUVE(on); };
    row->add(focus, CentreSlotUVE());
    if (prism::Ref<prism::Widget> controls = MakeTouchControlsButtonUVE()) {   // touch screens: Undo, Ctrl, ...
        row->add(controls, CentreSlotUVE());
    }

    m_status =prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, prism::Margin(10.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});

    return MakeSectionHeaderUVE(std::move(row));
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildLeftColumnUVE() {
    // Top pane: 3D Preview screen -> Transport controls directly underneath -> AAA Timeline directly underneath.
    auto previewHeaderRow = prism::hbox(6.0F);
    previewHeaderRow->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto previewTitle = prism::make<prism::Label>("3D Preview", prism::FontRole::Medium);
    previewHeaderRow->add(previewTitle, CentreSlotUVE());
    previewHeaderRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_previewStateBadge = prism::make<prism::Label>("STOPPED", prism::FontRole::Small);
    m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::textDim));
    m_previewStatePill = prism::make<prism::Border>();
    m_previewStatePill->setBackground(prism::ThemeColor(&prism::Theme::control));
    m_previewStatePill->setBorder(prism::ThemeColor(&prism::Theme::borderStrong), prism::Margin(1.0F));
    m_previewStatePill->setPadding(prism::Margin(6.0F, 1.0F, 6.0F, 1.0F));
    m_previewStatePill->setRadius(3.0F);
    m_previewStatePill->setChild(m_previewStateBadge);
    previewHeaderRow->add(m_previewStatePill, CentreSlotUVE());

    auto resetCamBtn = prism::make<prism::Button>("", prism::Icon::Camera, prism::ButtonStyle::Flat);
    resetCamBtn->setTooltip("Reset preview camera to system bounds");
    resetCamBtn->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->previewCameraResetPending = true;
        }
    };
    previewHeaderRow->add(resetCamBtn, CentreSlotUVE());

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
        if (const EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            if (s->playing) {
                m_editor.PauseVfxPreviewUVE();
            } else {
                m_editor.PlayVfxPreviewUVE();
            }
        }
    };
    transportRow->add(m_playButton, CentreSlotUVE());

    m_stopButton = ToolButtonUVE(prism::Icon::Stop, "Stop preview and reset playhead to 0.00s");
    m_stopButton->onClick = [this] { m_editor.StopVfxPreviewUVE(); };
    transportRow->add(m_stopButton, CentreSlotUVE());

    auto restartBtn = ToolButtonUVE(prism::Icon::Refresh, "Restart simulation from t = 0");
    restartBtn->onClick = [this] { m_editor.RestartVfxPreviewUVE(); };
    transportRow->add(restartBtn, CentreSlotUVE());

    m_loopButton = ToolButtonUVE(prism::Icon::NodeLoop, "Toggle loop playback");
    m_loopButton->setCheckable(true);
    m_loopButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->loop = !s->loop;
        }
    };
    transportRow->add(m_loopButton, CentreSlotUVE());

    m_speedCombo = prism::make<prism::ComboBox>(
        std::vector<std::string>{"0.1x", "0.25x", "0.5x", "1.0x", "2.0x", "4.0x"}, 3);
    m_speedCombo->setTooltip("Preview simulation speed");
    m_speedCombo->onChange = [this](const int idx) {
        static constexpr std::array<double, 6> kSpeeds{0.1, 0.25, 0.5, 1.0, 2.0, 4.0};
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE();
            s != nullptr && idx >= 0 && static_cast<std::size_t>(idx) < kSpeeds.size()) {
            s->speed = kSpeeds[static_cast<std::size_t>(idx)];
        }
    };
    auto speedBox = prism::make<prism::SizeBox>();
    speedBox->setWidthOverride(64.0F);
    speedBox->setContent(m_speedCombo);
    transportRow->add(speedBox, CentreSlotUVE());

    transportRow->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});

    m_timecodeLabel = prism::make<prism::Label>("0.00 / 0.00s", prism::FontRole::Mono);
    m_timecodeLabel->setColor(prism::ThemeColor(&prism::Theme::text));
    transportRow->add(m_timecodeLabel, CentreSlotUVE());

    auto transportBar = prism::make<prism::Border>();
    transportBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    transportBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 1.0F, 0.0F, 1.0F));
    transportBar->setChild(transportRow);

    // Upgraded studio timeline directly underneath the transport bar.
    m_timeline = prism::make<VfxTimelineWidgetUVE>(m_editor);

    auto previewPane = prism::vbox(0.0F);
    previewPane->add(MakeSectionHeaderUVE(std::move(previewHeaderRow)));
    previewPane->add(viewportOverlay, prism::Slot{1.0F});
    previewPane->add(transportBar);
    previewPane->add(m_timeline);

    // Bottom pane: Emitters list + green "+ Add" button.
    auto emittersHeader = prism::hbox(4.0F);
    emittersHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto emittersTitle = prism::make<prism::Label>("Emitters", prism::FontRole::Medium);
    emittersHeader->add(emittersTitle, CentreSlotUVE());
    emittersHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_addEmitterButton = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Success);
    m_addEmitterButton->onClick = [this] {
        ShowAddEmitterMenuUVE(m_addEmitterButton ? m_addEmitterButton->rect() : prism::Rect{});
    };
    emittersHeader->add(m_addEmitterButton, CentreSlotUVE());

    m_emittersScroll = prism::make<prism::ScrollBox>();
    auto emittersBox = prism::vbox(0.0F);
    emittersBox->add(MakeSectionHeaderUVE(std::move(emittersHeader)));
    emittersBox->add(m_emittersScroll, prism::Slot{1.0F});

    auto leftSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    leftSplit->add(PanelUVE(previewPane), prism::Splitter::Pane{470.0F, 280.0F, 1e9F, true});
    leftSplit->add(PanelUVE(emittersBox), prism::Splitter::Pane{310.0F, 160.0F, 1e9F, false});
    return leftSplit;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildCentreColumnUVE() {
    // Top pane: Full Particle Graph canvas (no Split/Preview toggle).
    auto graphHeader = prism::hbox(6.0F);
    graphHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto graphTitle = prism::make<prism::Label>("Particle Graph", prism::FontRole::Medium);
    graphHeader->add(graphTitle, CentreSlotUVE());
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
    graphHeader->add(frameAllBtn, CentreSlotUVE());

    m_graph = prism::make<prism::BlueprintGraph>();
    prism::BlueprintGraph::Features graphFeatures = m_graph->features();
    graphFeatures.toolbar = false;
    graphFeatures.crumbs = false;
    // The VFX Editor session owns Undo/Redo, Save, and Compile; disabling PRISM's internal
    // history and host shortcuts prevents Ctrl+Z from restoring demo nodes over the emitter.
    graphFeatures.history = false;
    graphFeatures.shortcuts = false;
    m_graph->setFeatures(graphFeatures);
    m_graph->onSaveRequested = [this] { static_cast<void>(m_editor.SaveVfxEditorUVE()); };
    m_graph->onCompileRequested = [this] {
        static_cast<void>(m_editor.CompileVfxEditorUVE());
        m_shownCompileCount = static_cast<std::size_t>(-1);
    };
    // The palette, the right-click menu and wire-drag search all offer the VFX node library (modules by
    // stage, plus Math / Value nodes) and create nodes through the VFX spawner; connections follow the
    // VFX stage rules, and the four stage output nodes are structural (never deleted or copied).
    m_graph->setNodeLibrary(BuildVfxGraphNodeLibraryUVE(m_shownSystem, true));
    m_graph->onSpawn = [this](const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) {
        return m_graph ? SpawnVfxGraphNodeUVE(*m_graph, spec, world) : -1;
    };
    m_graph->canConnectHook = [this](const int fromNode, const int fromPin, const int toNode, const int toPin,
                                     std::string* const why) {
        return m_graph == nullptr || CanConnectVfxPinsUVE(*m_graph, fromNode, fromPin, toNode, toPin, why);
    };
    m_graph->nodeLocked = [](const int nodeId) { return IsVfxStageNodeIdUVE(nodeId); };
    m_graph->onSelectionChanged = [this](const std::vector<int>& selectedIds) {
        if (m_syncingGraph || !m_graph) {
            return;
        }
        std::uint32_t picked = 0U;
        for (const int id : selectedIds) {
            if (!IsVfxStageNodeIdUVE(id) && id > 0) {
                picked = static_cast<std::uint32_t>(id);
                break;
            }
        }
        if (picked != m_selectedModuleId) {
            m_selectedModuleId = picked;
            m_shownModuleId = picked;
            RebuildInspectorUVE();
        }
    };
    m_graph->onChanged = [this] {
        if (m_syncingGraph) {
            return;
        }
        SyncGraphToAssetUVE();
    };

    auto graphBox = prism::vbox(0.0F);
    graphBox->add(MakeSectionHeaderUVE(std::move(graphHeader)));
    graphBox->add(m_graph, prism::Slot{1.0F});

    // Bottom pane: Compile Results header + diagnostics table.
    auto compileHeader = prism::hbox(6.0F);
    compileHeader->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    auto compileTitle = prism::make<prism::Label>("Compile Results", prism::FontRole::Medium);
    compileHeader->add(compileTitle, CentreSlotUVE());
    compileHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_compileSummaryLabel = prism::make<prism::Label>("Ready", prism::FontRole::Small);
    m_compileSummaryLabel->setColor(prism::ThemeColor(&prism::Theme::textDim));
    compileHeader->add(m_compileSummaryLabel, CentreSlotUVE());

    m_compileTable = prism::make<prism::TableView>();
    m_compileTable->addColumn(prism::TableColumn{"sev", "Severity", 80.0F});
    m_compileTable->addColumn(prism::TableColumn{"emitter", "Emitter", 120.0F});
    m_compileTable->addColumn(prism::TableColumn{"module", "Module ID", 80.0F});
    m_compileTable->addColumn(prism::TableColumn{"msg", "Message", 420.0F, 120.0F, true, true});
    m_compileTable->cellText = [this](const int row, const int col) -> std::string {
        const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
        if (session == nullptr || row < 0 || static_cast<std::size_t>(row) >= session->compileMessages.size()) {
            return {};
        }
        const VFX::ParticleCompileMessageUVE& msg = session->compileMessages[static_cast<std::size_t>(row)];
        switch (col) {
            case 0:
                return msg.severity == VFX::ParticleCompileMessageUVE::Severity::Error ? "ERROR" : "Warning";
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
        EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
        if (session == nullptr || rows.empty()) {
            return;
        }
        const int r = rows.front();
        if (r < 0 || static_cast<std::size_t>(r) >= session->compileMessages.size()) {
            return;
        }
        const VFX::ParticleCompileMessageUVE& msg = session->compileMessages[static_cast<std::size_t>(r)];
        if (msg.emitterIndex < session->system.emitters.size() && session->selectedEmitter != msg.emitterIndex) {
            session->selectedEmitter = msg.emitterIndex;
            m_shownEmitterIndex = msg.emitterIndex;
            RebuildGraphFromAssetUVE();
            if (m_graph) {
                m_graph->frameAll();
            }
            RebuildEmittersListUVE();
        }
        if (msg.moduleId != 0U) {
            SelectModuleIdUVE(msg.moduleId);
            if (m_graph) {
                m_graph->frameSelection();
            }
        } else if (m_graph) {
            m_graph->select(kVfxStageNodeRenderUVE);
            m_graph->frameSelection();
        }
    };

    auto compileBox = prism::vbox(0.0F);
    compileBox->add(MakeSectionHeaderUVE(std::move(compileHeader)));
    compileBox->add(m_compileTable, prism::Slot{1.0F});

    auto centreSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centreSplit->add(PanelUVE(graphBox), prism::Splitter::Pane{590.0F, 260.0F, 1e9F, true});
    m_compilePanel = PanelUVE(compileBox);
    centreSplit->add(m_compilePanel, prism::Splitter::Pane{165.0F, 110.0F, 360.0F, false});
    return centreSplit;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildRightColumnUVE() {
    // Top pane: Inspector. Bottom pane: User Parameters (+ Add).
    auto inspectorHeader = prism::hbox(4.0F);
    inspectorHeader->setPadding(prism::Margin(8.0F, 4.0F, 8.0F, 4.0F));
    auto inspectorTitle = prism::make<prism::Label>("Inspector", prism::FontRole::Medium);
    inspectorHeader->add(inspectorTitle, CentreSlotUVE());

    m_inspectorScroll = prism::make<prism::ScrollBox>();
    auto inspectorBox = prism::vbox(0.0F);
    inspectorBox->add(MakeSectionHeaderUVE(std::move(inspectorHeader)));
    inspectorBox->add(m_inspectorScroll, prism::Slot{1.0F});

    auto paramsHeader = prism::hbox(4.0F);
    paramsHeader->setPadding(prism::Margin(8.0F, 4.0F, 6.0F, 4.0F));
    auto paramsTitle = prism::make<prism::Label>("User Parameters", prism::FontRole::Medium);
    paramsHeader->add(paramsTitle, CentreSlotUVE());
    paramsHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    auto addParamBtn = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    addParamBtn->onClick = [this] {
        m_editor.EditVfxSystemUVE([](Asset::ParticleSystemAssetUVE& sys) {
            if (sys.parameters.size() >= Asset::kMaximumParticleUserParametersUVE) {
                return;
            }
            Asset::ParticleUserParameterUVE param{};
            param.name = MakeUniqueParameterNameUVE(sys, "Param");
            param.type = Asset::ParticleUserParameterTypeUVE::Float;
            param.value = Math::Vector3UVE{1.0F, 0.0F, 0.0F};
            param.alpha = 1.0F;
            sys.parameters.push_back(std::move(param));
        });
    };
    paramsHeader->add(addParamBtn, CentreSlotUVE());

    m_parametersScroll = prism::make<prism::ScrollBox>();
    auto paramsBox = prism::vbox(0.0F);
    paramsBox->add(MakeSectionHeaderUVE(std::move(paramsHeader)));
    paramsBox->add(m_parametersScroll, prism::Slot{1.0F});

    auto rightSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    rightSplit->add(PanelUVE(inspectorBox), prism::Splitter::Pane{460.0F, 200.0F, 1e9F, true});
    rightSplit->add(PanelUVE(paramsBox), prism::Splitter::Pane{280.0F, 150.0F, 1e9F, false});
    return rightSplit;
}

void EditorVfxEditorUVE::ShowAddEmitterMenuUVE(const prism::Rect anchor) {
    std::vector<prism::MenuItem> items;
    static constexpr std::array<std::pair<VFX::ParticleTemplateKindUVE, const char*>, 5> kTemplates{{
        {VFX::ParticleTemplateKindUVE::Empty, "Empty Emitter"},
        {VFX::ParticleTemplateKindUVE::Fountain, "Fountain Preset"},
        {VFX::ParticleTemplateKindUVE::Sparks, "Sparks Preset"},
        {VFX::ParticleTemplateKindUVE::Fire, "Fire Preset"},
        {VFX::ParticleTemplateKindUVE::Smoke, "Smoke Preset"},
    }};
    for (const auto& [kind, label] : kTemplates) {
        items.push_back(prism::MenuItem::Item(label, [this, kind] {
            m_editor.EditVfxSystemUVE([this, kind](Asset::ParticleSystemAssetUVE& sys) {
                if (sys.emitters.size() >= Asset::kMaximumParticleEmittersUVE) {
                    return;
                }
                Asset::ParticleSystemAssetUVE tpl = VFX::CreateParticleTemplateUVE(kind);
                if (tpl.emitters.empty()) {
                    return;
                }
                Asset::ParticleEmitterAssetUVE em = std::move(tpl.emitters.front());
                em.name = MakeUniqueEmitterNameUVE(sys, em.name.empty() ? "Emitter" : em.name);
                std::uint32_t nextId = NextFreeModuleIdInSystemUVE(sys);
                for (auto* stageVec : {&em.emitterUpdate, &em.particleSpawn, &em.particleUpdate, &em.render, &em.detached}) {
                    for (Asset::ParticleModuleUVE& mod : *stageVec) {
                        mod.id = nextId++;
                    }
                }
                sys.emitters.push_back(std::move(em));
                if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                    s->selectedEmitter = sys.emitters.size() - 1U;
                }
                m_selectedModuleId = 0U;
            });
        }, "", prism::Icon::Light));
    }
    prism::showMenu(std::move(items), anchor, prism::PopupPlacement::Below);
}

void EditorVfxEditorUVE::RebuildEmittersListUVE() {
    if (!m_emittersScroll) {
        return;
    }
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    auto list = prism::vbox(4.0F);
    list->setPadding(prism::Margin(6.0F));

    for (std::size_t i = 0U; i < session->system.emitters.size(); ++i) {
        const Asset::ParticleEmitterAssetUVE& emitter = session->system.emitters[i];
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
            m_editor.EditVfxSystemUVE([i, v](Asset::ParticleSystemAssetUVE& sys) {
                if (i < sys.emitters.size()) {
                    sys.emitters[i].enabled = v;
                }
            });
        };
        topRow->add(enToggle, CentreSlotUVE());

        auto selectBtn = prism::make<prism::Button>(
            emitter.name.empty() ? ("Emitter " + std::to_string(i + 1U)) : emitter.name,
            prism::Icon::Light,
            selected ? prism::ButtonStyle::Primary : prism::ButtonStyle::Flat);
        selectBtn->onClick = [this, i] {
            if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
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
            if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                if (s->soloEmitter.has_value() && *s->soloEmitter == i) {
                    s->soloEmitter.reset();
                } else {
                    s->soloEmitter = i;
                }
                static_cast<void>(m_editor.CompileVfxEditorUVE());
            }
        };
        topRow->add(soloBtn, CentreSlotUVE());
        col->add(topRow);

        auto actionsRow = prism::hbox(3.0F);
        auto nameEdit = prism::make<prism::TextInput>(emitter.name, "Emitter name");
        nameEdit->onCommit = [this, i](const std::string& text) {
            if (text.empty()) {
                return;
            }
            m_editor.EditVfxSystemUVE([i, text](Asset::ParticleSystemAssetUVE& sys) {
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
            m_editor.EditVfxSystemUVE([this, i](Asset::ParticleSystemAssetUVE& sys) {
                if (i > 0U && i < sys.emitters.size()) {
                    std::swap(sys.emitters[i - 1U], sys.emitters[i]);
                    if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                        s->selectedEmitter = i - 1U;
                    }
                }
            });
        };
        auto downBtn = prism::make<prism::Button>("", prism::Icon::ChevronDown, prism::ButtonStyle::Flat);
        downBtn->setTooltip("Move emitter down");
        downBtn->setEnabled(i + 1U < session->system.emitters.size());
        downBtn->onClick = [this, i] {
            m_editor.EditVfxSystemUVE([this, i](Asset::ParticleSystemAssetUVE& sys) {
                if (i + 1U < sys.emitters.size()) {
                    std::swap(sys.emitters[i], sys.emitters[i + 1U]);
                    if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                        s->selectedEmitter = i + 1U;
                    }
                }
            });
        };
        auto dupBtn = prism::make<prism::Button>("", prism::Icon::Duplicate, prism::ButtonStyle::Flat);
        dupBtn->setTooltip("Duplicate emitter");
        dupBtn->onClick = [this, i] {
            m_editor.EditVfxSystemUVE([this, i](Asset::ParticleSystemAssetUVE& sys) {
                if (i >= sys.emitters.size() || sys.emitters.size() >= Asset::kMaximumParticleEmittersUVE) {
                    return;
                }
                Asset::ParticleEmitterAssetUVE copy = sys.emitters[i];
                copy.name = MakeUniqueEmitterNameUVE(sys, copy.name + "_Copy");
                std::unordered_map<std::uint32_t, std::uint32_t> idRemap;
                std::uint32_t nextId = NextFreeModuleIdInSystemUVE(sys);
                for (auto* stageVec : {&copy.emitterUpdate, &copy.particleSpawn, &copy.particleUpdate, &copy.render, &copy.detached}) {
                    for (Asset::ParticleModuleUVE& mod : *stageVec) {
                        const std::uint32_t newId = nextId++;
                        idRemap[mod.id] = newId;
                        mod.id = newId;
                    }
                }
                for (Asset::ParticleMathNodeUVE& mathNode : copy.mathNodes) {
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
                    for (Asset::ParticleModuleUVE& mod : *stageVec) {
                        for (auto& [pin, in] : mod.floats) {
                            remapId(in.wireNode);
                        }
                        for (auto& [pin, in] : mod.vectors) {
                            remapId(in.wireNode);
                        }
                    }
                }
                for (Asset::ParticleMathNodeUVE& mathNode : copy.mathNodes) {
                    for (std::uint32_t& inputId : mathNode.inputs) {
                        remapId(inputId);
                    }
                }
                for (Asset::ParticleDetachedLinkUVE& link : copy.detachedLinks) {
                    remapId(link.fromModuleId);
                    remapId(link.toModuleId);
                }
                sys.emitters.insert(sys.emitters.begin() + static_cast<std::ptrdiff_t>(i + 1U), std::move(copy));
                if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                    s->selectedEmitter = i + 1U;
                }
                m_selectedModuleId = 0U;
            });
        };
        auto delBtn = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
        delBtn->setTooltip("Delete emitter");
        delBtn->setEnabled(session->system.emitters.size() > 1U);
        delBtn->onClick = [this, i] {
            m_editor.EditVfxSystemUVE([this, i](Asset::ParticleSystemAssetUVE& sys) {
                if (sys.emitters.size() <= 1U || i >= sys.emitters.size()) {
                    return;
                }
                sys.emitters.erase(sys.emitters.begin() + static_cast<std::ptrdiff_t>(i));
                if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
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

        actionsRow->add(upBtn, CentreSlotUVE());
        actionsRow->add(downBtn, CentreSlotUVE());
        actionsRow->add(dupBtn, CentreSlotUVE());
        actionsRow->add(delBtn, CentreSlotUVE());
        col->add(actionsRow);

        card->setChild(col);
        list->add(card);
    }

    m_emittersScroll->setContent(prism::make<FitWidthUVE>(list));
}

void EditorVfxEditorUVE::RebuildParametersListUVE() {
    if (!m_parametersScroll) {
        return;
    }
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
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
        const Asset::ParticleUserParameterUVE& param = session->system.parameters[i];
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
            m_editor.EditVfxSystemUVE([i, newName](Asset::ParticleSystemAssetUVE& sys) {
                if (i >= sys.parameters.size()) {
                    return;
                }
                const std::string oldName = sys.parameters[i].name;
                sys.parameters[i].name = newName;
                for (Asset::ParticleEmitterAssetUVE& em : sys.emitters) {
                    for (auto* stageVec : {&em.emitterUpdate, &em.particleSpawn, &em.particleUpdate, &em.render, &em.detached}) {
                        for (Asset::ParticleModuleUVE& mod : *stageVec) {
                            for (auto& [pin, in] : mod.floats) {
                                if (in.kind == Asset::ParticleValueKindUVE::UserParameter && in.parameter == oldName) {
                                    in.parameter = newName;
                                }
                            }
                            for (auto& [pin, in] : mod.vectors) {
                                if (in.kind == Asset::ParticleValueKindUVE::UserParameter && in.parameter == oldName) {
                                    in.parameter = newName;
                                }
                            }
                            for (auto& [pin, in] : mod.colors) {
                                if (in.kind == Asset::ParticleValueKindUVE::UserParameter && in.parameter == oldName) {
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
            m_editor.EditVfxSystemUVE([i, typeIdx](Asset::ParticleSystemAssetUVE& sys) {
                if (i < sys.parameters.size() && typeIdx >= 0 && typeIdx <= 4) {
                    sys.parameters[i].type = static_cast<Asset::ParticleUserParameterTypeUVE>(typeIdx);
                }
            });
        };
        auto typeBox = prism::make<prism::SizeBox>();
        typeBox->setWidthOverride(78.0F);
        typeBox->setContent(typeCombo);
        topRow->add(typeBox, CentreSlotUVE());

        auto delBtn = prism::make<prism::Button>("", prism::Icon::Trash, prism::ButtonStyle::Flat);
        delBtn->onClick = [this, i] {
            m_editor.EditVfxSystemUVE([i](Asset::ParticleSystemAssetUVE& sys) {
                if (i < sys.parameters.size()) {
                    sys.parameters.erase(sys.parameters.begin() + static_cast<std::ptrdiff_t>(i));
                }
            });
        };
        topRow->add(delBtn, CentreSlotUVE());
        col->add(topRow);

        // Default value editor based on parameter type.
        switch (param.type) {
            case Asset::ParticleUserParameterTypeUVE::Float: {
                auto nf = prism::make<prism::NumberField>(param.value.x, 0.05F, 3);
                nf->onChange = [this, i](const float v) {
                    m_editor.EditVfxSystemUVE([i, v](Asset::ParticleSystemAssetUVE& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = static_cast<float>(v);
                        }
                    });
                };
                col->add(nf);
                break;
            }
            case Asset::ParticleUserParameterTypeUVE::Int: {
                auto nf = prism::make<prism::NumberField>(
                    std::round(param.value.x), 1.0F, 0);
                nf->onChange = [this, i](const float v) {
                    m_editor.EditVfxSystemUVE([i, v](Asset::ParticleSystemAssetUVE& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = static_cast<float>(std::llround(static_cast<double>(v)));
                        }
                    });
                };
                col->add(nf);
                break;
            }
            case Asset::ParticleUserParameterTypeUVE::Bool: {
                auto cb = prism::make<prism::CheckBox>("Enabled by default", param.value.x >= 0.5F);
                cb->onChange = [this, i](const bool v) {
                    m_editor.EditVfxSystemUVE([i, v](Asset::ParticleSystemAssetUVE& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value.x = v ? 1.0F : 0.0F;
                        }
                    });
                };
                col->add(cb);
                break;
            }
            case Asset::ParticleUserParameterTypeUVE::Vector: {
                auto vf = prism::make<prism::Vec3Field>(0.05F, 3);
                vf->setValue(param.value.x, param.value.y, param.value.z);
                prism::Vec3Field* const rawVf = vf.get();
                vf->onChange = [this, i, rawVf](const int, const float) {
                    const float x = rawVf->get(0);
                    const float y = rawVf->get(1);
                    const float z = rawVf->get(2);
                    m_editor.EditVfxSystemUVE([i, x, y, z](Asset::ParticleSystemAssetUVE& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value =
                                Math::Vector3UVE{x, y, z};
                        }
                    });
                };
                col->add(vf);
                break;
            }
            case Asset::ParticleUserParameterTypeUVE::Color: {
                const auto toByte = [](const float c) -> std::uint8_t {
                    return static_cast<std::uint8_t>(std::clamp(std::lround(c * 255.0F), 0L, 255L));
                };
                auto swatch = prism::make<prism::ColorField>(prism::Color(
                    toByte(param.value.x),
                    toByte(param.value.y),
                    toByte(param.value.z),
                    toByte(param.alpha)));
                swatch->onChange = [this, i](const prism::Color c) {
                    m_editor.EditVfxSystemUVE([i, c](Asset::ParticleSystemAssetUVE& sys) {
                        if (i < sys.parameters.size()) {
                            sys.parameters[i].value = Math::Vector3UVE{
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

    m_parametersScroll->setContent(prism::make<FitWidthUVE>(list));
}

void EditorVfxEditorUVE::RebuildGraphFromAssetUVE() {
    if (!m_graph) {
        return;
    }
    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr || session->selectedEmitter >= session->system.emitters.size()) {
        return;
    }
    m_syncingGraph = true;
    m_graph->setNodeLibrary(BuildVfxGraphNodeLibraryUVE(session->system, true));
    BuildEmitterGraphUVE(session->system, session->selectedEmitter, *m_graph);
    // Sync BlueprintGraph::m_lastSnapshot against the freshly rebuilt graph so future drags/edits
    // always detect a diff and emit onChanged(). Must run BEFORE ApplyVfxCompileMessagesToGraphUVE
    // because commit() clears BlueprintGraph::m_compiled.
    m_graph->commit("Sync");
    ApplyVfxCompileMessagesToGraphUVE(*m_graph, session->compileMessages);
    if (m_selectedModuleId != 0U) {
        m_graph->select(static_cast<int>(m_selectedModuleId));
    }
    m_syncingGraph = false;
}

void EditorVfxEditorUVE::SyncGraphToAssetUVE() {
    if (m_syncingGraph || !m_graph) {
        return;
    }
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr || session->selectedEmitter >= session->system.emitters.size()) {
        return;
    }
    const std::size_t emitterIdx = session->selectedEmitter;
    Asset::ParticleEmitterAssetUVE extracted{};
    if (!ExtractEmitterFromGraphUVE(*m_graph, session->system, emitterIdx, extracted)) {
        return;
    }
    m_syncingGraph = true;
    m_editor.EditVfxSystemUVE([emitterIdx, &extracted](Asset::ParticleSystemAssetUVE& sys) {
        if (emitterIdx < sys.emitters.size()) {
            sys.emitters[emitterIdx] = extracted;
        }
    });
    if (const EditorUVE::VfxEditSessionUVE* const updated = m_editor.VfxEditSessionPtrUVE()) {
        m_shownSystem = updated->system;
        m_shownCompileCount = updated->compileMessages.size();
        ApplyVfxCompileMessagesToGraphUVE(*m_graph, updated->compileMessages);
    }
    RebuildCompileTableUVE();
    RebuildInspectorUVE();
    m_syncingGraph = false;
}

void EditorVfxEditorUVE::RebuildInspectorUVE() {
    if (!m_inspectorScroll) {
        return;
    }
    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    m_inspectorScroll->setContent(BuildVfxInspectorContentUVE(
        m_editor, session->selectedEmitter, m_selectedModuleId, [this] {
            if (const EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                m_shownSystem = s->system;
                m_shownCompileCount = s->compileMessages.size();
            }
            RebuildGraphFromAssetUVE();
            RebuildCompileTableUVE();
            RebuildEmittersListUVE();
        }));
}

void EditorVfxEditorUVE::RebuildCompileTableUVE() {
    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    const std::size_t count = session != nullptr ? session->compileMessages.size() : 0U;
    if (m_compileTable) {
        m_compileTable->setRowCount(static_cast<int>(count));
        m_compileTable->invalidate();
    }
    if (m_compileSummaryLabel && session != nullptr) {
        std::size_t errorCount = 0U;
        std::size_t warnCount = 0U;
        for (const VFX::ParticleCompileMessageUVE& msg : session->compileMessages) {
            if (msg.severity == VFX::ParticleCompileMessageUVE::Severity::Error) {
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

void EditorVfxEditorUVE::RefreshUVE() {
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    if (m_tab) {
        m_tab->setDirty(m_editor.IsVfxEditorDirtyUVE());
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
                                           ? kPreviewStatePlayingUVE
                                           : (session->playhead > 1.0e-4 ? kPreviewStatePausedUVE : kPreviewStateStoppedUVE);
        if (nextState != m_shownPreviewState) {
            m_shownPreviewState = nextState;
            const prism::Theme& t = prism::theme();
            if (nextState == kPreviewStatePlayingUVE) {
                m_previewStateBadge->setText("PLAYING");
                m_previewStateBadge->setColor(prism::ThemeColor(&prism::Theme::success));
                m_previewStatePill->setBackground(t.panel.mix(t.success, 0.18F));
                m_previewStatePill->setBorder(t.success.withAlpha(165), prism::Margin(1.0F));
            } else if (nextState == kPreviewStatePausedUVE) {
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
        const std::string tc = SecondsTextUVE(session->playhead) + " / " +
                               SecondsTextUVE(static_cast<double>(session->system.duration)) + "s";
        if (m_timecodeLabel->text() != tc) {
            m_timecodeLabel->setText(tc);
        }
    }
    if (m_statsOverlayLabel) {
        const std::string stats = FormatVfxPreviewStatsTextUVE(m_editor);
        if (m_statsOverlayLabel->text() != stats) {
            m_statsOverlayLabel->setText(stats);
        }
    }
    if (m_status) {
        std::size_t errorCount = 0U;
        for (const VFX::ParticleCompileMessageUVE& msg : session->compileMessages) {
            if (msg.severity == VFX::ParticleCompileMessageUVE::Severity::Error) {
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
        RebuildEmittersListUVE();
        RebuildParametersListUVE();
        RebuildGraphFromAssetUVE();
        if (emitterChanged && m_graph) {
            m_graph->frameAll();
        }
        RebuildInspectorUVE();
        RebuildCompileTableUVE();
    } else {
        if (selectionChanged) {
            // Selection changes update only the Inspector; never rebuild the graph mid-drag.
            m_shownModuleId = m_selectedModuleId;
            RebuildInspectorUVE();
        }
        if (m_shownCompileCount != session->compileMessages.size()) {
            m_shownCompileCount = session->compileMessages.size();
            if (m_graph) {
                ApplyVfxCompileMessagesToGraphUVE(*m_graph, session->compileMessages);
            }
            RebuildCompileTableUVE();
        }
    }
}

void EditorVfxEditorUVE::RenderUVE() {
    if (m_window == 0 || m_editor.VfxEditSessionPtrUVE() == nullptr) {
        return;
    }
    std::vector<EditorHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInputUVE input = m_host.CaptureViewInputUVE(m_window, rect);
            const float dpiScale = std::max(1.0F, input.dpiScale);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderVfxPreviewUVE(
                Math::Vector2UVE{std::max(1.0F, std::floor(rect.w * dpiScale)), std::max(1.0F, std::floor(rect.h * dpiScale))},
                used, input);
            m_host.ApplyViewCursorUVE(m_window, input);
            if (texture != 0U && used.x > 0.0F && used.y > 0.0F) {
                const float invScale = dpiScale > 1.0F ? (1.0F / dpiScale) : 1.0F;
                EditorHostUVE::ViewImageUVE view{};
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
    m_host.RenderWindowUVE(m_window, views);
}

} // namespace UVE::Editor
