// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include "editor.h"

#include "animation_graph_component.h"
#include "animation_graph_editing.h"

#include "prism/BlueprintGraph.h"
#include "prism/Widget.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace prism {
class Box;
class Button;
class ComboBox;
class Label;
class LogView;
class NumberField;
class PropertyGrid;
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
/// is one undo step through EditAnimationGraph, and the blueprint view is rebuilt from the
/// component whenever the component changes (undo included).
class StateMachineView;
class BlendSpaceView;

class EditorAnimGraphPanel final {
    friend class StateMachineView;
    friend class BlendSpaceView;

public:
    explicit EditorAnimGraphPanel(Editor& editor);
    ~EditorAnimGraphPanel();
    EditorAnimGraphPanel(const EditorAnimGraphPanel&) = delete;
    EditorAnimGraphPanel& operator=(const EditorAnimGraphPanel&) = delete;

    [[nodiscard]] prism::Ref<prism::Widget> Build();
    /// Runs the preview while the page is `shown`, and rebuilds the graph and the side panel when
    /// the component changed. Inside the Entity Editor's window.
    void Refresh(double deltaSeconds, bool shown);
    /// Opens a State Machine or Blend Space node in its own view (the breadcrumb leads back), or
    /// goes back to the tree.
    void OpenFocus(std::uint32_t node);
    void CloseFocus();
    /// Opens the graph of a State Machine's state (what it plays).
    void OpenStateGraph(std::uint32_t machine, std::uint32_t slot);
    /// The graph's settings (sequencer, parameters, the picked node / state / transition / point,
    /// the preview log), shown in the Details column while the graph page is up.
    [[nodiscard]] prism::Ref<prism::Widget> Side() const { return m_sidePanel; }

private:
    using Node = Scene::AnimationGraphNode;
    using Graph = Scene::AnimationGraphComponent;

    [[nodiscard]] prism::Ref<prism::Widget> BuildToolbar();
    [[nodiscard]] const Graph* GraphPtr() const;
    [[nodiscard]] Graph* LiveGraph();

    // ---- the blueprint view of the component ----------------------------------------------------
    void RebuildGraph(const Graph& graph);
    [[nodiscard]] prism::BlueprintNode MakeNode(const Node& node) const;
    [[nodiscard]] std::vector<prism::BlueprintNodeSpec> NodeLibrary() const;
    /// The graph's own edit (move, wire, unwire, delete) written to the component as one step.
    void ApplyGraphEdit();
    int Spawn(const prism::BlueprintNodeSpec& spec, prism::Vec2 world);

    // ---- settings inside the nodes (editor_anim_graph_bodies.cpp) --------------------------
    [[nodiscard]] static float BodyHeight(const Node& node);
    [[nodiscard]] static std::string Summary(const Node& node);
    [[nodiscard]] prism::Ref<prism::Widget> BuildBody(const Node& node);
    [[nodiscard]] std::vector<std::string> ParameterNames(std::initializer_list<Scene::AnimationParameterType> types) const;
    [[nodiscard]] std::vector<Asset::AssetGuid> ClipChoices() const;
    /// The MontageSequencers whose montages reach this graph: the linked one, else every one in the
    /// entity; and the names of their montages that play on `slot`.
    [[nodiscard]] std::vector<Scene::Entity> MontageSequencers() const;
    [[nodiscard]] std::vector<std::string> MontagesOnSlot(const std::string& slot) const;
    void ShowMontageSequencerMenu(prism::Vec2 at);

    // ---- edits ----------------------------------------------------------------------------------
    /// One undo step on the component; the view is rebuilt on the next refresh.
    bool Edit(const std::function<void(Graph&)>& change);
    bool EditNode(std::uint32_t id, const std::function<void(Node&)>& change);
    /// A slider or number drag: changes the component live (the preview follows), and the release
    /// records the whole gesture as one undo step.
    void LiveNode(std::uint32_t id, const std::function<void(Node&)>& change);
    /// `axis` 0..2 sets that component of a Vector3 parameter; -1 its value.
    void LiveParameter(std::size_t index, float value, int axis = -1);
    void CommitLive();

    // ---- the side panel ---------------------------------------------------------------------------
    void RebuildSide(const Graph& graph);
    void BuildParameters(prism::PropertyGrid& grid, const Graph& graph);
    void BuildSelected(prism::PropertyGrid& grid, const Graph& graph);
    void BuildPreview(prism::PropertyGrid& grid);
    /// The side panel while a State Machine is open: the picked state or transition's settings;
    /// while a Blend Space is open: its tool, snapping, area and the picked point
    /// (editor_anim_graph_side.cpp).
    void BuildStateMachineSide(prism::PropertyGrid& grid, const Graph& graph, const Node& machine);
    void BuildBlendSpaceSide(prism::PropertyGrid& grid, const Graph& graph, const Node& space);
    [[nodiscard]] const Node* FocusNode() const;
    /// The graph the tree page shows (checked: a scope whose machine went is the top).
    [[nodiscard]] AnimationGraphScope Scope() const;
    /// The id of a state graph's result node, drawn there and never stored.
    static constexpr int kStateResult = 0x7FFFFF00;
    void SyncFocus();
    void RefreshLiveValues(const Graph& graph);
    void ShowGraphMenu();
    /// Renames a node; its parameter still named after it follows.
    void RenameNode(std::uint32_t id, const std::string& to);
    /// The graph's node menu and keys: Delete, Cut, Copy, Paste and Duplicate as component edits.
    bool NodeAction(const std::string& action, const std::vector<int>& ids);
    void ShowSequencerMenu(prism::Vec2 at);

    Editor& m_editor;
    Editor::AnimationGraphPanelView m_view;
    std::shared_ptr<prism::BlueprintGraph> m_graph;
    std::shared_ptr<prism::StackSwitcher> m_canvas;
    std::shared_ptr<StateMachineView> m_stateView;
    std::shared_ptr<BlendSpaceView> m_spaceView;
    std::shared_ptr<prism::Widget> m_crumbs;
    std::shared_ptr<prism::Box> m_crumbRow;
    std::string m_crumbSignature = "-";
    AnimationGraphScope m_shownScope;
    std::uint32_t m_shownFocus = 0U;
    int m_shownPickedState = -1;
    int m_shownPickedTransition = -1;
    std::shared_ptr<prism::ScrollBox> m_side;
    std::shared_ptr<prism::Widget> m_sidePanel;
    std::shared_ptr<prism::Button> m_graphButton;
    std::shared_ptr<prism::Button> m_previewButton;
    std::shared_ptr<prism::Button> m_playButton;
    std::shared_ptr<prism::Button> m_problem;
    std::shared_ptr<prism::Label> m_empty;

    /// What the view last showed, so it is rebuilt only when the component (or the tree) changes.
    Scene::Entity m_shownTree = Scene::kInvalidEntity;
    std::optional<Graph> m_shown;
    std::vector<std::uint32_t> m_selected;
    std::vector<std::uint32_t> m_shownSelection;
    bool m_rebuilding = false;
    bool m_sideDirty = true;
    /// A live gesture's starting point (see LiveNode).
    std::optional<Graph> m_liveBefore;
    /// Widgets the refresh keeps current while the preview runs.
    struct ParameterRow final {
        std::string name;
        std::shared_ptr<prism::NumberField> number;
        std::shared_ptr<prism::Widget> sparkline;
    };
    std::vector<ParameterRow> m_parameterRows;
    std::shared_ptr<prism::LogView> m_log;
    /// Montage slots read from their files, by GUID (empty: unreadable).
    mutable std::unordered_map<std::uint64_t, std::string> m_montageSlots;
    std::vector<Node> m_clipboard;
    mutable std::vector<Asset::AssetGuid> m_projectClips;
    mutable std::chrono::steady_clock::time_point m_projectClipsAt{};
    mutable bool m_projectClipsValid = false;
    std::size_t m_shownLogLines = static_cast<std::size_t>(-1);
};

} // namespace UVE::Editor
