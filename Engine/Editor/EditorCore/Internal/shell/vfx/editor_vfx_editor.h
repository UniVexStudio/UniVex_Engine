// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor_icons.h"

#include "particle_system_asset.h"
#include "vfx_program.h"

#include "prism/Widget.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace prism {
class BlueprintGraph;
class Border;
class Button;
class ComboBox;
class EngineView;
class Label;
class ScrollBox;
class TableView;
} // namespace prism

namespace UVE::Editor {

class Editor;
class EditorHost;
class DocumentTab;
class VfxTimelineWidget;

/// The VFX / Particle Graph Editor window (`.uvfx`), opened in a secondary PRISM window:
///
///   [#1 logo] [title bar: File Edit Window Help | drag strip | - [] x]
///   [       ] [#2 the particle system's document tab                 ]
///   [ Save | Undo Redo | Compile | FixedSeed Grid Bounds | status    ]
///   [ Left: 3D Preview + Transport + Timeline / Emitters | Centre: Particle Graph / Compile Results | Right: Inspector / User Parameters ]
class EditorVfxEditor final {
public:
    EditorVfxEditor(Editor& editor, EditorHost& host);
    ~EditorVfxEditor();
    EditorVfxEditor(const EditorVfxEditor&) = delete;
    EditorVfxEditor& operator=(const EditorVfxEditor&) = delete;

    void Sync();
    void Render();
    [[nodiscard]] int WindowId() const noexcept { return m_window; }
    bool SaveScreenshot(const std::filesystem::path& path);

    [[nodiscard]] std::uint32_t SelectedModuleId() const noexcept { return m_selectedModuleId; }
    void SelectModuleId(std::uint32_t moduleId);

    [[nodiscard]] prism::BlueprintGraph* GraphWidget() noexcept { return m_graph.get(); }

private:
    [[nodiscard]] prism::Ref<prism::Widget> Build();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbar();
    [[nodiscard]] prism::Ref<prism::Widget> BuildLeftColumn();
    [[nodiscard]] prism::Ref<prism::Widget> BuildCentreColumn();
    [[nodiscard]] prism::Ref<prism::Widget> BuildRightColumn();

    void RequestClose();
    void Refresh();
    void RebuildEmittersList();
    void RebuildParametersList();
    void RebuildGraphFromAsset();
    void SyncGraphToAsset();
    void RebuildInspector();
    void RebuildCompileTable();
    void ShowAddEmitterMenu(prism::Rect anchor);

    Editor& m_editor;
    EditorHost& m_host;
    EditorIcons m_icons;
    int m_window = 0;
    std::filesystem::path m_openPath;
    bool m_closeAsked = false;
    std::chrono::steady_clock::time_point m_lastTick{};

    std::shared_ptr<DocumentTab> m_tab;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_stopButton;
    std::shared_ptr<prism::Button> m_loopButton;
    std::shared_ptr<prism::Button> m_seedButton;
    std::shared_ptr<prism::Button> m_gridButton;
    std::shared_ptr<prism::Button> m_boundsButton;
    // Focus: the graph alone (phones), with the side columns and the compile results hidden.
    std::shared_ptr<prism::Widget> m_leftColumn;
    std::shared_ptr<prism::Widget> m_rightColumn;
    std::shared_ptr<prism::Widget> m_compilePanel;
    bool m_graphFocus = false;
    void SetGraphFocus(bool on);
    std::shared_ptr<prism::ComboBox> m_speedCombo;
    std::shared_ptr<prism::Border> m_previewStatePill;
    std::shared_ptr<prism::Label> m_previewStateBadge;
    std::shared_ptr<prism::Label> m_timecodeLabel;
    std::shared_ptr<prism::Label> m_compileSummaryLabel;
    std::shared_ptr<prism::Label> m_status;

    std::shared_ptr<prism::ScrollBox> m_emittersScroll;
    std::shared_ptr<prism::Button> m_addEmitterButton;
    std::shared_ptr<prism::ScrollBox> m_parametersScroll;

    std::shared_ptr<prism::EngineView> m_viewport;
    std::shared_ptr<prism::Label> m_statsOverlayLabel;
    std::shared_ptr<prism::BlueprintGraph> m_graph;
    std::shared_ptr<VfxTimelineWidget> m_timeline;
    std::shared_ptr<prism::TableView> m_compileTable;
    std::shared_ptr<prism::ScrollBox> m_inspectorScroll;

    std::uint32_t m_selectedModuleId = 0U;
    bool m_syncingGraph = false;

    Asset::ParticleSystemAsset m_shownSystem;
    std::size_t m_shownEmitterIndex = 0U;
    std::optional<std::size_t> m_shownSoloEmitter;
    std::uint32_t m_shownModuleId = 0U;
    std::size_t m_shownCompileCount = static_cast<std::size_t>(-1);
    std::uint8_t m_shownPreviewState = 0xFFU;
    bool m_shownPlaying = false;
    bool m_shownPlayingInit = false;
    bool m_shownValid = false;
};

} // namespace UVE::Editor
