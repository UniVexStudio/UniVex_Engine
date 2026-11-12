// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "animation_graph_component.h"

#include "prism/BlueprintGraph.h"
#include "prism/Widget.h"

#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism {
class Button;
class ComboBox;
class Label;
class LogView;
class NumberField;
class ScrollBox;
class StackSwitcher;
} // namespace prism

namespace UVE::Editor {

/// The Entity Editor dock's Animation Graph page: the entity's AnimationGraph (a pose tree, Godot style)
/// as PRISM blueprint nodes, each node's settings inside it (Godot style, in the blueprint look):
///
///   [Graph v] Add Node  Fit | Preview  > || >| 1x | ! problem                                  |
///   [ (Clip)----\                                                      ][ Graph               ]
///   [ |Walk v   |  (Blend2)-----\                                       ][  Sequencer, problem ]
///   [ |Loop x1.0| -|A  weight===|-\      (Output)                       ][ Parameters          ]
///   [ (Clip)----/  |B  by Speed | ----> |Pose                           ][  Speed  Float  0.40 ]
///   [ |Run v    |                                                       ][ Selected node       ]
///                                                                        [ Preview + log       ]
///
/// The graph is the engine's: every change made in it (move, wire, unwire, delete, add, a setting)
/// is one undo step through EditAnimationGraphUVE, and the blueprint view is rebuilt from the
/// component whenever the component changes (undo included).
class StateMachineViewUVE;
class BlendSpaceViewUVE;

class EditorPrismAnimGraphPanelUVE final {
    friend class StateMachineViewUVE;
    friend class BlendSpaceViewUVE;

public:
    explicit EditorPrismAnimGraphPanelUVE(EditorUVE& editor);
    ~EditorPrismAnimGraphPanelUVE();
    EditorPrismAnimGraphPanelUVE(const EditorPrismAnimGraphPanelUVE&) = delete;
    EditorPrismAnimGraphPanelUVE& operator=(const EditorPrismAnimGraphPanelUVE&) = delete;

    [[nodiscard]] prism::Ref<prism::Widget> BuildUVE();
    /// Runs the preview while the page is `shown`, and rebuilds the graph and the side panel when
    /// the component changed. Inside the Entity Editor's window.
    void RefreshUVE(double deltaSeconds, bool shown);
    /// Opens a State Machine or Blend Space node in its own view (the breadcrumb leads back), or
    /// goes back to the tree.
    void OpenFocusUVE(std::uint32_t node);
    void CloseFocusUVE();
    /// The graph's settings (sequencer, parameters, the picked node / state / transition / point,
    /// the preview log), shown in the Details column while the graph page is up.
    [[nodiscard]] prism::Ref<prism::Widget> SideUVE() const { return m_sidePanel; }

private:
    using NodeUVE = Scene::AnimationGraphNodeUVE;
    using GraphUVE = Scene::AnimationGraphComponentUVE;

    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbarUVE();
    [[nodiscard]] const GraphUVE* GraphPtrUVE() const;
    [[nodiscard]] GraphUVE* LiveGraphUVE();

    // ---- the blueprint view of the component ----------------------------------------------------
    void RebuildGraphUVE(const GraphUVE& graph);
    [[nodiscard]] prism::BlueprintNode MakeNodeUVE(const NodeUVE& node) const;
    [[nodiscard]] std::vector<prism::BlueprintNodeSpec> NodeLibraryUVE() const;
    /// The graph's own edit (move, wire, unwire, delete) written to the component as one step.
    void ApplyGraphEditUVE();
    int SpawnUVE(const prism::BlueprintNodeSpec& spec, prism::Vec2 world);
    void ShowAddMenuUVE(prism::Vec2 screen);

    // ---- settings inside the nodes (editor_prism_anim_graph_bodies.cpp) --------------------------
    [[nodiscard]] static float BodyHeightUVE(const NodeUVE& node);
    [[nodiscard]] static std::string SummaryUVE(const NodeUVE& node);
    [[nodiscard]] prism::Ref<prism::Widget> BuildBodyUVE(const NodeUVE& node);
    [[nodiscard]] std::vector<std::string> ParameterNamesUVE(std::initializer_list<Scene::AnimationParameterTypeUVE> types) const;
    [[nodiscard]] std::vector<Asset::AssetGuidUVE> ClipChoicesUVE() const;
    /// The MontagePlayers whose montages reach this graph: the linked one, else every one in the
    /// entity; and the names of their montages that play on `slot`.
    [[nodiscard]] std::vector<Scene::EntityUVE> MontagePlayersUVE() const;
    [[nodiscard]] std::vector<std::string> MontagesOnSlotUVE(const std::string& slot) const;
    void ShowMontagePlayerMenuUVE(prism::Vec2 at);

    // ---- edits ----------------------------------------------------------------------------------
    /// One undo step on the component; the view is rebuilt on the next refresh.
    bool EditUVE(const std::function<void(GraphUVE&)>& change);
    bool EditNodeUVE(std::uint32_t id, const std::function<void(NodeUVE&)>& change);
    /// A slider or number drag: changes the component live (the preview follows), and the release
    /// records the whole gesture as one undo step.
    void LiveNodeUVE(std::uint32_t id, const std::function<void(NodeUVE&)>& change);
    void LiveParameterUVE(std::size_t index, float value);
    void CommitLiveUVE();

    // ---- the side panel ---------------------------------------------------------------------------
    void RebuildSideUVE(const GraphUVE& graph);
    [[nodiscard]] prism::Ref<prism::Widget> BuildParametersUVE(const GraphUVE& graph);
    [[nodiscard]] prism::Ref<prism::Widget> BuildSelectedUVE(const GraphUVE& graph);
    [[nodiscard]] prism::Ref<prism::Widget> BuildPreviewUVE();
    /// The side panel while a State Machine is open: the picked state or transition's settings;
    /// while a Blend Space is open: its tool, snapping, area and the picked point
    /// (editor_prism_anim_graph_side.cpp).
    [[nodiscard]] prism::Ref<prism::Widget> BuildStateMachineSideUVE(const GraphUVE& graph, const NodeUVE& machine);
    [[nodiscard]] prism::Ref<prism::Widget> BuildBlendSpaceSideUVE(const GraphUVE& graph, const NodeUVE& space);
    [[nodiscard]] const NodeUVE* FocusNodeUVE() const;
    void SyncFocusUVE();
    void RefreshLiveValuesUVE(const GraphUVE& graph);
    void ShowGraphMenuUVE();
    void ShowSequencerMenuUVE(prism::Vec2 at);

    EditorUVE& m_editor;
    EditorUVE::AnimationGraphPanelViewUVE m_view;
    std::shared_ptr<prism::BlueprintGraph> m_graph;
    std::shared_ptr<prism::StackSwitcher> m_canvas;
    std::shared_ptr<StateMachineViewUVE> m_stateView;
    std::shared_ptr<BlendSpaceViewUVE> m_spaceView;
    std::shared_ptr<prism::Widget> m_crumbs;
    std::shared_ptr<prism::Label> m_crumbLabel;
    std::uint32_t m_shownFocus = 0U;
    int m_shownPickedState = -1;
    int m_shownPickedTransition = -1;
    std::shared_ptr<prism::ScrollBox> m_side;
    std::shared_ptr<prism::Widget> m_sidePanel;
    std::shared_ptr<prism::Button> m_graphButton;
    std::shared_ptr<prism::Button> m_previewButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Label> m_problem;
    std::shared_ptr<prism::Label> m_empty;

    /// What the view last showed, so it is rebuilt only when the component (or the tree) changes.
    Scene::EntityUVE m_shownTree = Scene::kInvalidEntityUVE;
    std::optional<GraphUVE> m_shown;
    std::vector<std::uint32_t> m_selected;
    std::vector<std::uint32_t> m_shownSelection;
    bool m_rebuilding = false;
    bool m_sideDirty = true;
    /// A live gesture's starting point (see LiveNodeUVE).
    std::optional<GraphUVE> m_liveBefore;
    /// Widgets the refresh keeps current while the preview runs.
    struct ParameterRowUVE final {
        std::string name;
        std::shared_ptr<prism::NumberField> number;
        std::shared_ptr<prism::Widget> sparkline;
    };
    std::vector<ParameterRowUVE> m_parameterRows;
    std::shared_ptr<prism::LogView> m_log;
    /// Montage slots read from their files, by GUID (empty: unreadable).
    mutable std::unordered_map<std::uint64_t, std::string> m_montageSlots;
    std::size_t m_shownLogLines = static_cast<std::size_t>(-1);
};

} // namespace UVE::Editor
