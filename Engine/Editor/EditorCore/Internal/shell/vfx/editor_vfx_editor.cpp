// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_vfx_editor.h"

#include <cstddef>
#include <cstdint>

#include "editor.h"
#include "editor_chrome.h"
#include "editor_fit_width.h"
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
#include "prism/Tabs.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace UVE::Editor {
namespace {

constexpr int kVfxWindowWidthUVE = 1360;
constexpr int kVfxWindowHeightUVE = 840;

[[nodiscard]] prism::Slot CentreSlotUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string SecondsTextUVE(const double t) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.2f", t);
    return buf;
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
        m_graph.reset();
        m_timeline.reset();
        m_compileTable.reset();
        m_emittersScroll.reset();
        m_parametersScroll.reset();
        m_inspectorScroll.reset();
        m_tab.reset();
        m_shownValid = false;
    }
    if (session == nullptr) {
        return;
    }
    if (m_window == 0) {
        m_selectedModuleId = 0U;
        m_closeAsked = false;
        m_shownValid = false;
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
    window->add(BuildToolbarUVE());

    auto columns = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    columns->add(BuildLeftColumnUVE(), prism::Splitter::Pane{270.0F, 210.0F, 480.0F, false});
    columns->add(BuildCentreColumnUVE(), prism::Splitter::Pane{730.0F, 380.0F, 1e9F, true});
    columns->add(BuildRightColumnUVE(), prism::Splitter::Pane{360.0F, 260.0F, 640.0F, false});
    window->add(columns, prism::Slot{1.0F});

    auto frame = prism::make<prism::Border>();
    frame->setBackground(prism::ThemeColor(&prism::Theme::window));
    frame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F));
    frame->setChild(window);
    ApplyCenterModeUVE(m_centerMode);
    return frame;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildTitleBarUVE() {
    auto row = prism::hbox();
    auto menus = prism::make<prism::MenuBar>();
    menus->setShowLogo(false);
    menus->setItemPadding(12.0F);
    menus->addMenu("File", [this] {
        std::vector<prism::MenuItem> items;
        items.push_back(prism::MenuItem::Item(
            "Save", [this] { static_cast<void>(m_editor.SaveVfxEditorUVE()); }, "Ctrl+S", prism::Icon::Save));
        items.push_back(prism::MenuItem::Item(
            "Compile", [this] { static_cast<void>(m_editor.CompileVfxEditorUVE()); }, "F7", prism::Icon::Check));
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
    });
    menus->addMenu("Edit", [this] {
        const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
        prism::MenuItem undo = prism::MenuItem::Item(
            "Undo", [this] { static_cast<void>(m_editor.UndoVfxEditUVE()); }, "Ctrl+Z", prism::Icon::Undo);
        undo.enabled = session != nullptr && !session->undo.empty();
        prism::MenuItem redo = prism::MenuItem::Item(
            "Redo", [this] { static_cast<void>(m_editor.RedoVfxEditUVE()); }, "Ctrl+Y", prism::Icon::Redo);
        redo.enabled = session != nullptr && !session->redo.empty();
        return std::vector<prism::MenuItem>{std::move(undo), std::move(redo)};
    });
    menus->addMenu("Window", [this] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Check("Split Preview + Graph", m_centerMode == CenterModeUVE::Split,
                                   [this] { ApplyCenterModeUVE(CenterModeUVE::Split); }),
            prism::MenuItem::Check("Preview Only", m_centerMode == CenterModeUVE::PreviewOnly,
                                   [this] { ApplyCenterModeUVE(CenterModeUVE::PreviewOnly); }),
            prism::MenuItem::Check("Graph Only", m_centerMode == CenterModeUVE::GraphOnly,
                                   [this] { ApplyCenterModeUVE(CenterModeUVE::GraphOnly); }),
        };
    });
    menus->addMenu("Help", [] {
        return std::vector<prism::MenuItem>{
            prism::MenuItem::Item("Spawn from UVScript: vfx.spawn(\"assets/VFX_Effect.uvfx\", pos)", {}).disabled(),
            prism::MenuItem::Item("Right-click the Particle Graph canvas to add modules", {}).disabled(),
        };
    });
    row->add(menus, CentreSlotUVE());
    row->add(prism::make<DragStripUVE>(), prism::Slot{1.0F});
    AddWindowButtonsUVE(*row, [this] { RequestCloseUVE(); });

    auto tabRow = prism::hbox();
    m_tab = prism::make<DocumentTabUVE>();
    m_tab->setFallbackIcon(prism::Icon::Light);
    m_tab->setName(m_openPath.empty() && m_editor.VfxEditSessionPtrUVE() != nullptr
                       ? m_editor.VfxEditSessionPtrUVE()->path.stem().string()
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

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildToolbarUVE() {
    auto row = prism::hbox(4.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));

    auto save = ToolButtonUVE(prism::Icon::Save, "Save Particle System (.uvfx)", "Save");
    save->onClick = [this] { static_cast<void>(m_editor.SaveVfxEditorUVE()); };
    row->add(save, CentreSlotUVE());

    auto undo = ToolButtonUVE(prism::Icon::Undo, "Undo");
    undo->onClick = [this] { static_cast<void>(m_editor.UndoVfxEditUVE()); };
    auto redo = ToolButtonUVE(prism::Icon::Redo, "Redo");
    redo->onClick = [this] { static_cast<void>(m_editor.RedoVfxEditUVE()); };
    row->add(undo, CentreSlotUVE());
    row->add(redo, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());

    auto compile = ToolButtonUVE(prism::Icon::Check, "Compile all emitters and update live preview", "Compile");
    compile->onClick = [this] {
        static_cast<void>(m_editor.CompileVfxEditorUVE());
        m_shownCompileCount = static_cast<std::size_t>(-1);
    };
    row->add(compile, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());

    m_playButton = ToolButtonUVE(prism::Icon::Play, "Play preview simulation");
    m_playButton->setCheckable(true);
    m_playButton->onClick = [this] { m_editor.PlayVfxPreviewUVE(); };
    m_pauseButton = ToolButtonUVE(prism::Icon::Pause, "Pause preview simulation");
    m_pauseButton->setCheckable(true);
    m_pauseButton->onClick = [this] { m_editor.PauseVfxPreviewUVE(); };
    auto restart = ToolButtonUVE(prism::Icon::Refresh, "Restart simulation from t = 0", "Restart");
    restart->onClick = [this] { m_editor.RestartVfxPreviewUVE(); };

    row->add(m_playButton, CentreSlotUVE());
    row->add(m_pauseButton, CentreSlotUVE());
    row->add(restart, CentreSlotUVE());

    m_speedCombo = prism::make<prism::ComboBox>(
        std::vector<std::string>{"0.1x", "0.25x", "0.5x", "1.0x", "2.0x", "4.0x"}, 3);
    m_speedCombo->onChange = [this](const int idx) {
        static constexpr std::array<double, 6> kSpeeds{0.1, 0.25, 0.5, 1.0, 2.0, 4.0};
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE();
            s != nullptr && idx >= 0 && static_cast<std::size_t>(idx) < kSpeeds.size()) {
            s->speed = kSpeeds[static_cast<std::size_t>(idx)];
        }
    };
    auto speedBox = prism::make<prism::SizeBox>();
    speedBox->setWidthOverride(74.0F);
    speedBox->setContent(m_speedCombo);
    row->add(speedBox, CentreSlotUVE());

    m_loopButton = ToolButtonUVE(prism::Icon::NodeLoop, "Loop preview playback", "Loop");
    m_loopButton->setCheckable(true);
    m_loopButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->loop = !s->loop;
        }
    };
    row->add(m_loopButton, CentreSlotUVE());

    m_seedButton = ToolButtonUVE(prism::Icon::Lock, "Deterministic fixed seed on restart", "Fixed Seed");
    m_seedButton->setCheckable(true);
    m_seedButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->fixedSeed = !s->fixedSeed;
            m_editor.RestartVfxPreviewUVE();
        }
    };
    row->add(m_seedButton, CentreSlotUVE());

    m_gridButton = ToolButtonUVE(prism::Icon::Grid, "Show floor grid in preview", "Grid");
    m_gridButton->setCheckable(true);
    m_gridButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->showGrid = !s->showGrid;
        }
    };
    row->add(m_gridButton, CentreSlotUVE());

    m_boundsButton = ToolButtonUVE(prism::Icon::Cube, "Show system fixed bounds box in preview", "Bounds");
    m_boundsButton->setCheckable(true);
    m_boundsButton->onClick = [this] {
        if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
            s->showBounds = !s->showBounds;
        }
    };
    row->add(m_boundsButton, CentreSlotUVE());

    row->add(HSeparatorUVE(), CentreSlotUVE());

    m_centerModeSeg = prism::make<prism::Segmented>();
    m_centerModeSeg->add("Split").add("Preview").add("Graph");
    m_centerModeSeg->setSelected(2);
    m_centerModeSeg->onSelect = [this](const int idx) {
        ApplyCenterModeUVE(idx == 1 ? CenterModeUVE::PreviewOnly
                           : idx == 2 ? CenterModeUVE::GraphOnly
                                      : CenterModeUVE::Split);
    };
    row->add(m_centerModeSeg, CentreSlotUVE());

    m_status = prism::make<prism::Label>("", prism::FontRole::Small);
    m_status->setColor(prism::ThemeColor(&prism::Theme::textDim));
    row->add(m_status, prism::Slot{1.0F, prism::Margin(10.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});

    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildLeftColumnUVE() {
    // Top half: Emitters list. Bottom half: User Parameters list.
    auto emittersHeader = prism::hbox(4.0F);
    emittersHeader->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));
    auto emittersTitle = prism::make<prism::Label>("Emitters", prism::FontRole::Medium);
    emittersHeader->add(emittersTitle, CentreSlotUVE());
    emittersHeader->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
    m_addEmitterButton = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Success);
    m_addEmitterButton->onClick = [this] {
        ShowAddEmitterMenuUVE(m_addEmitterButton ? m_addEmitterButton->rect() : prism::Rect{});
    };
    emittersHeader->add(m_addEmitterButton, CentreSlotUVE());

    auto emittersHeaderBar = prism::make<prism::Border>();
    emittersHeaderBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    emittersHeaderBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    emittersHeaderBar->setChild(emittersHeader);

    m_emittersScroll = prism::make<prism::ScrollBox>();
    auto emittersBox = prism::vbox(0.0F);
    emittersBox->add(emittersHeaderBar);
    emittersBox->add(m_emittersScroll, prism::Slot{1.0F});

    auto paramsHeader = prism::hbox(4.0F);
    paramsHeader->setPadding(prism::Margin(6.0F, 4.0F, 6.0F, 4.0F));
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

    auto paramsHeaderBar = prism::make<prism::Border>();
    paramsHeaderBar->setBackground(prism::ThemeColor(&prism::Theme::header));
    paramsHeaderBar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    paramsHeaderBar->setChild(paramsHeader);

    m_parametersScroll = prism::make<prism::ScrollBox>();
    auto paramsBox = prism::vbox(0.0F);
    paramsBox->add(paramsHeaderBar);
    paramsBox->add(m_parametersScroll, prism::Slot{1.0F});

    auto leftSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    leftSplit->add(PanelUVE(emittersBox), prism::Splitter::Pane{420.0F, 150.0F, 1e9F, true});
    leftSplit->add(PanelUVE(paramsBox), prism::Splitter::Pane{320.0F, 130.0F, 1e9F, false});
    return leftSplit;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildCentreColumnUVE() {
    m_viewport = prism::make<prism::EngineView>();
    m_viewport->mode = prism::EngineView::Mode::Hole;
    m_viewport->onInput = [](const prism::EngineInput&) {};

    m_statsOverlayLabel = prism::make<prism::Label>("", prism::FontRole::Mono);
    m_statsOverlayLabel->setColor(prism::Color(225, 232, 245));
    auto statsCard = prism::make<prism::Border>();
    statsCard->setBackground(prism::Color(16, 18, 22, 196));
    statsCard->setBorder(prism::Color(58, 66, 78, 190), prism::Margin(1.0F));
    statsCard->setPadding(prism::Margin(8.0F, 5.0F, 8.0F, 5.0F));
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
    m_previewPane = PanelUVE(viewportOverlay);

    m_graph = prism::make<prism::BlueprintGraph>();
    prism::BlueprintGraph::Features graphFeatures = m_graph->features();
    graphFeatures.toolbar = false;
    graphFeatures.crumbs = false;
    m_graph->setFeatures(graphFeatures);
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
            RebuildInspectorUVE();
        }
    };
    m_graph->onChanged = [this] {
        if (m_syncingGraph) {
            return;
        }
        SyncGraphToAssetUVE();
    };
    m_graphPane = PanelUVE(m_graph);

    m_centerTopSplitter = prism::make<prism::Splitter>(prism::Orientation::Horizontal);
    m_centerTopSplitter->add(m_previewPane, prism::Splitter::Pane{340.0F, 180.0F, 1e9F, false});
    m_centerTopSplitter->add(m_graphPane, prism::Splitter::Pane{520.0F, 240.0F, 1e9F, true});

    m_timeline = prism::make<VfxTimelineWidgetUVE>(m_editor);

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
            RebuildGraphFromAssetUVE();
        }
        if (msg.moduleId != 0U) {
            SelectModuleIdUVE(msg.moduleId);
        } else if (m_graph) {
            m_graph->select(kVfxStageNodeRenderUVE);
        }
    };

    auto bottomBox = prism::vbox(0.0F);
    bottomBox->add(m_timeline);
    bottomBox->add(m_compileTable, prism::Slot{1.0F});

    auto centreSplit = prism::make<prism::Splitter>(prism::Orientation::Vertical);
    centreSplit->add(m_centerTopSplitter, prism::Splitter::Pane{560.0F, 240.0F, 1e9F, true});
    centreSplit->add(PanelUVE(bottomBox), prism::Splitter::Pane{185.0F, 110.0F, 420.0F, false});
    return centreSplit;
}

prism::Ref<prism::Widget> EditorVfxEditorUVE::BuildRightColumnUVE() {
    m_inspectorScroll = prism::make<prism::ScrollBox>();
    auto tabs = prism::make<prism::TabView>();
    tabs->addTab("Inspector", prism::Icon::Properties, PanelUVE(m_inspectorScroll));
    return tabs;
}

void EditorVfxEditorUVE::ApplyCenterModeUVE(const CenterModeUVE mode) {
    m_centerMode = mode;
    if (m_previewPane) {
        m_previewPane->setVisible(mode != CenterModeUVE::GraphOnly);
    }
    if (m_graphPane) {
        m_graphPane->setVisible(mode != CenterModeUVE::PreviewOnly);
    }
    if (m_centerModeSeg) {
        m_centerModeSeg->setSelected(mode == CenterModeUVE::PreviewOnly ? 1
                                     : mode == CenterModeUVE::GraphOnly ? 2
                                                                        : 0);
    }
    if (prism::Application::exists()) {
        prism::Application::get().invalidateLayout();
    }
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
        card->setBackground(selected ? prism::Color(44, 62, 88) : prism::Color(30, 32, 37));
        card->setBorder(selected ? prism::theme().accent : prism::theme().border, prism::Margin(1.0F));
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
                    m_shownValid = false;
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
                m_shownValid = false;
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
                std::uint32_t nextId = NextFreeModuleIdInSystemUVE(sys);
                for (auto* stageVec : {&copy.emitterUpdate, &copy.particleSpawn, &copy.particleUpdate, &copy.render, &copy.detached}) {
                    for (Asset::ParticleModuleUVE& mod : *stageVec) {
                        mod.id = nextId++;
                    }
                }
                sys.emitters.insert(sys.emitters.begin() + static_cast<std::ptrdiff_t>(i + 1U), std::move(copy));
                if (EditorUVE::VfxEditSessionUVE* const s = m_editor.VfxEditSessionPtrUVE()) {
                    s->selectedEmitter = i + 1U;
                }
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
        card->setBackground(prism::Color(30, 32, 37));
        card->setBorder(prism::theme().border, prism::Margin(1.0F));
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
    BuildEmitterGraphUVE(session->system, session->selectedEmitter, *m_graph);
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
            }
            RebuildGraphFromAssetUVE();
            RebuildCompileTableUVE();
            RebuildEmittersListUVE();
        }));
}

void EditorVfxEditorUVE::RebuildCompileTableUVE() {
    if (!m_compileTable) {
        return;
    }
    const EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    const std::size_t count = session != nullptr ? session->compileMessages.size() : 0U;
    m_compileTable->setRowCount(static_cast<int>(count));
    m_compileTable->invalidate();
}

void EditorVfxEditorUVE::RefreshUVE() {
    EditorUVE::VfxEditSessionUVE* const session = m_editor.VfxEditSessionPtrUVE();
    if (session == nullptr) {
        return;
    }
    if (m_tab) {
        m_tab->setDirty(m_editor.IsVfxEditorDirtyUVE());
    }
    if (m_playButton) {
        m_playButton->setChecked(session->playing);
    }
    if (m_pauseButton) {
        m_pauseButton->setChecked(!session->playing);
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
    if (m_statsOverlayLabel) {
        m_statsOverlayLabel->setText(FormatVfxPreviewStatsTextUVE(m_editor));
    }
    if (m_status) {
        std::size_t errorCount = 0U;
        for (const VFX::ParticleCompileMessageUVE& msg : session->compileMessages) {
            if (msg.severity == VFX::ParticleCompileMessageUVE::Severity::Error) {
                ++errorCount;
            }
        }
        std::string text = SecondsTextUVE(session->playhead) + " / " +
                           SecondsTextUVE(static_cast<double>(session->system.duration)) + " s";
        if (errorCount > 0U) {
            text += "   |  " + std::to_string(errorCount) + " compile error(s) (preview holding last good program)";
        }
        if (!session->status.empty()) {
            text += "   |  " + session->status;
        }
        m_status->setText(text);
    }
    if (m_timeline) {
        m_timeline->invalidate();
    }

    const bool assetOrSelectionChanged =
        !m_shownValid || !(m_shownSystem == session->system) ||
        m_shownEmitterIndex != session->selectedEmitter ||
        m_shownSoloEmitter != session->soloEmitter ||
        m_shownModuleId != m_selectedModuleId;
    if (assetOrSelectionChanged) {
        m_shownSystem = session->system;
        m_shownEmitterIndex = session->selectedEmitter;
        m_shownSoloEmitter = session->soloEmitter;
        m_shownModuleId = m_selectedModuleId;
        m_shownValid = true;
        RebuildEmittersListUVE();
        RebuildParametersListUVE();
        RebuildGraphFromAssetUVE();
        RebuildInspectorUVE();
        RebuildCompileTableUVE();
    } else if (m_shownCompileCount != session->compileMessages.size()) {
        m_shownCompileCount = session->compileMessages.size();
        if (m_graph) {
            ApplyVfxCompileMessagesToGraphUVE(*m_graph, session->compileMessages);
        }
        RebuildCompileTableUVE();
    }
}

void EditorVfxEditorUVE::RenderUVE() {
    if (m_window == 0 || m_editor.VfxEditSessionPtrUVE() == nullptr) {
        return;
    }
    std::vector<EditorHostUVE::ViewImageUVE> views;
    if (m_viewport && m_viewport->visible() && m_previewPane && m_previewPane->visible()) {
        const prism::Rect rect = m_viewport->rect();
        if (rect.w >= 1.0F && rect.h >= 1.0F) {
            const EditorViewportInputUVE input = m_host.CaptureViewInputUVE(m_window, rect);
            Math::Vector2UVE used{0.0F, 0.0F};
            const std::uint64_t texture = m_editor.RenderVfxPreviewUVE(
                Math::Vector2UVE{std::floor(rect.w), std::floor(rect.h)}, used, input);
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
