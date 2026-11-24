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

class EditorUVE;
class EditorHostUVE;
class DocumentTabUVE;
class VfxTimelineWidgetUVE;

/// The VFX / Particle Graph Editor window (`.uvfx`), opened in a secondary PRISM window:
///
///   [#1 logo] [title bar: File Edit Window Help | drag strip | - [] x]
///   [       ] [#2 the particle system's document tab                 ]
///   [ Save | Undo Redo | Compile | FixedSeed Grid Bounds | status    ]
///   [ Left: 3D Preview + Transport + Timeline / Emitters | Centre: Particle Graph / Compile Results | Right: Inspector / User Parameters ]
class EditorVfxEditorUVE final {
public:
    EditorVfxEditorUVE(EditorUVE& editor, EditorHostUVE& host);
    ~EditorVfxEditorUVE();
    EditorVfxEditorUVE(const EditorVfxEditorUVE&) = delete;
    EditorVfxEditorUVE& operator=(const EditorVfxEditorUVE&) = delete;

    void SyncUVE();
    void RenderUVE();
    [[nodiscard]] int WindowIdUVE() const noexcept { return m_window; }
    bool SaveScreenshotUVE(const std::filesystem::path& path);

    [[nodiscard]] std::uint32_t SelectedModuleIdUVE() const noexcept { return m_selectedModuleId; }
    void SelectModuleIdUVE(std::uint32_t moduleId);

    [[nodiscard]] prism::BlueprintGraph* GraphWidgetUVE() noexcept { return m_graph.get(); }

private:
    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildTitleBarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildLeftColumnUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildCentreColumnUVE();
    [[nodiscard]] prism::Ref<prism::Widget> BuildRightColumnUVE();

    void RequestCloseUVE();
    void RefreshUVE();
    void RebuildEmittersListUVE();
    void RebuildParametersListUVE();
    void RebuildGraphFromAssetUVE();
    void SyncGraphToAssetUVE();
    void RebuildInspectorUVE();
    void RebuildCompileTableUVE();
    void ShowAddEmitterMenuUVE(prism::Rect anchor);

    EditorUVE& m_editor;
    EditorHostUVE& m_host;
    EditorIconsUVE m_icons;
    int m_window = 0;
    std::filesystem::path m_openPath;
    bool m_closeAsked = false;
    std::chrono::steady_clock::time_point m_lastTick{};

    std::shared_ptr<DocumentTabUVE> m_tab;
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
    void SetGraphFocusUVE(bool on);
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
    std::shared_ptr<VfxTimelineWidgetUVE> m_timeline;
    std::shared_ptr<prism::TableView> m_compileTable;
    std::shared_ptr<prism::ScrollBox> m_inspectorScroll;

    std::uint32_t m_selectedModuleId = 0U;
    bool m_syncingGraph = false;

    Asset::ParticleSystemAssetUVE m_shownSystem;
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
