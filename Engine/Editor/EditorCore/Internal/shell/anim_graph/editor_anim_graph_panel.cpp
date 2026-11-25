// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_anim_graph_panel.h"
#include "editor_fit_width.h"

#include "editor_blend_space_view.h"
#include "editor_chrome.h"
#include "editor_state_machine_view.h"
#include "editor_touch_controls.h"

#include "animation_graph_editing.h"
#include "animation_sequencer_component.h"
#include "montage_sequencer_component.h"
#include "name_component.h"
#include "skeleton_3d.h"

#include "prism/Application.h"
#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/Menu.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <unordered_map>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;

constexpr std::array<Kind, 16> kAddableKinds{Kind::Clip,       Kind::Blend2,      Kind::BlendSpace1D, Kind::BlendSpace2D,
                                                Kind::Select,     Kind::BlendByBool, Kind::Additive,     Kind::LayeredBlend,
                                                Kind::OneShot,    Kind::TimeScale,   Kind::TimeSeek,     Kind::StateMachine,
                                                Kind::Slot,       Kind::TwoBoneIK,   Kind::ModifyBone,   Kind::LookAt};

[[nodiscard]] prism::Slot CentreSlot() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string NameOf(const Scene::IEntityManager& em, const Scene::Entity entity, const char* fallback) {
    return em.HasComponent<Scene::NameComponent>(entity) ? em.GetComponent<Scene::NameComponent>(entity).name
                                                               : std::string{fallback};
}


/// A small sparkline of a Float parameter's recent values.
class Sparkline final : public prism::Widget {
public:
    explicit Sparkline(const std::vector<float>* samples) : m_samples(samples) {}
    void SetSamples(const std::vector<float>* samples) { m_samples = samples; invalidate(); }
    prism::Vec2 measure(prism::Vec2) override { return m_desired = constrain({36.0F, 18.0F}); }
    void paint(prism::Canvas& c) override {
        const prism::Theme& t = prism::theme();
        c.fillRoundRect(m_rect, 2.0F, t.field);
        if (m_samples == nullptr || m_samples->size() < 2U) {
            return;
        }
        const auto [lo, hi] = std::minmax_element(m_samples->begin(), m_samples->end());
        const float span = *hi - *lo > 1.0e-5F ? *hi - *lo : 1.0F;
        std::vector<prism::Vec2> points;
        const std::size_t n = m_samples->size();
        for (std::size_t i = 0U; i < n; ++i) {
            points.push_back(prism::Vec2{m_rect.x + 2.0F + (m_rect.w - 4.0F) * static_cast<float>(i) / static_cast<float>(n - 1U),
                                         m_rect.bottom() - 3.0F - ((*m_samples)[i] - *lo) / span * (m_rect.h - 6.0F)});
        }
        c.polyline(points, t.highlight, 1.2F);
    }

private:
    const std::vector<float>* m_samples = nullptr;
};

} // namespace

EditorAnimGraphPanel::EditorAnimGraphPanel(Editor& editor) : m_editor(editor) {}
EditorAnimGraphPanel::~EditorAnimGraphPanel() = default;

const Scene::AnimationGraphComponent* EditorAnimGraphPanel::GraphPtr() const {
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (m_view.tree == Scene::kInvalidEntity || !em.IsAlive(m_view.tree) ||
        !em.HasComponent<Scene::AnimationGraphComponent>(m_view.tree)) {
        return nullptr;
    }
    return &em.GetComponent<Scene::AnimationGraphComponent>(m_view.tree);
}

Scene::AnimationGraphComponent* EditorAnimGraphPanel::LiveGraph() {
    Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (m_view.tree == Scene::kInvalidEntity || !em.IsAlive(m_view.tree) ||
        !em.HasComponent<Scene::AnimationGraphComponent>(m_view.tree)) {
        return nullptr;
    }
    return &em.GetComponent<Scene::AnimationGraphComponent>(m_view.tree);
}

namespace {


} // namespace

// ---------------------------------------------------------------------------------------- build

prism::Ref<prism::Widget> EditorAnimGraphPanel::Build() {
    m_graph = prism::make<prism::BlueprintGraph>();
    prism::BlueprintGraph::Features features;
    features.toolbar = false;
    features.crumbs = false;
    features.minimap = false;
    features.watermark = false;
    features.shortcuts = false;
    features.history = false;   // the editor's undo: every change is a component edit
    features.singleLinkOutputs = true;   // a pose feeds one input; a node added off a wire goes into it
    features.minimap = true;             // the overview, bottom right
    features.comments = true;            // C: a comment box around the selection
    m_graph->setFeatures(features);
    m_graph->clear();
    m_graph->setNodeLibrary(NodeLibrary());
    m_graph->onSpawn = [this](const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) { return Spawn(spec, world); };
    m_graph->onCommit = [this](const std::string&) { ApplyGraphEdit(); };
    m_graph->onSelectionChanged = [this](const std::vector<int>& ids) {
        if (m_rebuilding) {
            return;
        }
        m_selected.assign(ids.begin(), ids.end());
        m_sideDirty = true;
    };
    m_graph->onNodeDoubleClicked = [this](const int id) {
        const Graph* graph = GraphPtr();
        if (graph == nullptr) {
            return;
        }
        const auto node = std::ranges::find(graph->nodes, static_cast<std::uint32_t>(id), &Node::id);
        if (node != graph->nodes.end() &&
            (node->kind == Kind::StateMachine || node->kind == Kind::BlendSpace1D || node->kind == Kind::BlendSpace2D)) {
            OpenFocus(node->id);
        }
    };
    m_graph->onNodeRenamed = [this](const int id, const std::string& to) { RenameNode(static_cast<std::uint32_t>(id), to); };
    m_graph->onNodeAction = [this](const std::string& action, const std::vector<int>& ids) { return NodeAction(action, ids); };
    m_graph->extendNodeMenu = [this](const int id, std::vector<prism::MenuItem>& items) {
        const Graph* graph = GraphPtr();
        if (graph == nullptr) {
            return;
        }
        const auto node = std::ranges::find(graph->nodes, static_cast<std::uint32_t>(id), &Node::id);
        if (node == graph->nodes.end()) {
            return;
        }
        const std::uint32_t nodeId = node->id;
        if (node->kind == Kind::StateMachine) {
            items.push_back(prism::MenuItem::Item("Open States", [this, nodeId] { OpenFocus(nodeId); }, "", prism::Icon::StateMachine));
        } else if (node->kind == Kind::BlendSpace1D || node->kind == Kind::BlendSpace2D) {
            items.push_back(prism::MenuItem::Item("Open Blend Space", [this, nodeId] { OpenFocus(nodeId); }, "", prism::Icon::NodeBlendSpace));
        }
    };
    m_graph->canConnectHook = [this](const int fromNode, int, const int toNode, const int toPin, std::string* why) {
        const Graph* graph = GraphPtr();
        if (graph == nullptr) {
            return false;
        }
        const bool ok = CanConnectAnimationGraphNodes(graph->nodes, static_cast<std::uint32_t>(toNode), static_cast<std::size_t>(toPin),
                                                         static_cast<std::uint32_t>(fromNode));
        if (!ok && why != nullptr) {
            *why = "That wire would make a loop or does not fit this input";
        }
        return ok;
    };
    // The side panel lives in the Entity Editor's Details column (Side), not beside the graph.
    m_side = prism::make<prism::ScrollBox>();
    auto sideBox = prism::make<prism::SizeBox>();
    auto sideFrame = prism::make<prism::Border>();
    sideFrame->setBackground(prism::ThemeColor(&prism::Theme::panel));
    sideFrame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F, 0.0F, 0.0F, 0.0F));
    sideFrame->setChild(m_side);
    sideBox->setContent(sideFrame);
    m_sidePanel = sideBox;

    // The tree, or the State Machine / Blend Space opened from it.
    m_stateView = prism::make<StateMachineView>(*this);
    m_spaceView = prism::make<BlendSpaceView>(*this);
    m_canvas = prism::make<prism::StackSwitcher>();
    m_canvas->addPage(m_graph);
    m_canvas->addPage(m_stateView);
    m_canvas->addPage(m_spaceView);
    m_canvas->setActiveIndex(0);
    // The breadcrumb while a state, a State Machine or a Blend Space is open: Tree > Locomotion >
    // Idle; each one a button back to it.
    auto crumbRow = prism::hbox(2.0F);
    crumbRow->setPadding(prism::Margin(6.0F, 2.0F, 6.0F, 2.0F));
    m_crumbRow = crumbRow;
    auto crumbs = prism::make<prism::Border>();
    crumbs->setBackground(prism::ThemeColor(&prism::Theme::window));
    crumbs->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    crumbs->setChild(crumbRow);
    crumbs->setVisible(false);
    m_crumbs = crumbs;
    auto left = prism::vbox();
    left->add(m_crumbs);
    left->add(m_canvas, prism::Slot{1.0F});

    auto body = prism::hbox();
    body->add(left, prism::Slot{1.0F});
    auto page = prism::vbox();
    page->add(BuildToolbar());
    page->add(body, prism::Slot{1.0F});
    m_shownFocus = ~0U;
    m_shown.reset();
    m_shownTree = Scene::kInvalidEntity;
    m_sideDirty = true;
    return page;
}

prism::Ref<prism::Widget> EditorAnimGraphPanel::BuildToolbar() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    m_graphButton = ToolButton(prism::Icon::AnimGraph, "The AnimationGraph shown (selecting one in Components picks it too)", "Graph");
    m_graphButton->onClick = [this] { ShowGraphMenu(); };
    row->add(m_graphButton, CentreSlot());
    auto fit = ToolButton(prism::Icon::Frame, "Frame the whole graph (F)");
    fit->onClick = [this] {
        if (m_graph) {
            m_graph->frameAll();
        }
    };
    row->add(fit, CentreSlot());
    row->add(HSeparator(), CentreSlot());
    m_previewButton = ToolButton(prism::Icon::PreviewPose, "Run the graph on the entity's skeleton (runtime pose only, never saved)", "Preview");
    m_previewButton->setCheckable(true);
    m_previewButton->onClick = [this] {
        m_editor.m_animGraph.previewing = !m_editor.m_animGraph.previewing;
        if (!m_editor.m_animGraph.previewing) {
            m_editor.StopAnimationGraphPreview();
        }
    };
    row->add(m_previewButton, CentreSlot());
    m_playButton = ToolButton(prism::Icon::Pause, "Pause / play the preview");
    m_playButton->onClick = [this] { m_editor.m_animGraph.previewPaused = !m_editor.m_animGraph.previewPaused; };
    row->add(m_playButton, CentreSlot());
    auto step = ToolButton(prism::Icon::StepForward, "One frame (while paused)");
    step->onClick = [this] {
        m_editor.m_animGraph.previewPaused = true;
        m_editor.m_animGraph.previewStepOnce = true;
    };
    row->add(step, CentreSlot());
    if (prism::Ref<prism::Widget> controls = MakeTouchControlsButton()) {   // touch screens: Undo, Ctrl, ...
        row->add(controls, CentreSlot());
    }
    auto rate =prism::make<prism::ComboBox>(std::vector<std::string>{"0.25x", "0.5x", "1x", "2x"}, 2);
    rate->onChange = [this](const int i) {
        static constexpr std::array<float, 4> kRates{0.25F, 0.5F, 1.0F, 2.0F};
        if (i >= 0 && i < 4) {
            m_editor.m_animGraph.previewRate = kRates[static_cast<std::size_t>(i)];
        }
    };
    auto rateBox = prism::make<prism::SizeBox>();
    rateBox->setWidthOverride(70.0F);
    rateBox->setContent(rate);
    row->add(rateBox, CentreSlot());
    // The graph's problem, as a warning badge; what is wrong is its tooltip.
    m_problem = prism::make<prism::Button>("", prism::Icon::Warning, prism::ButtonStyle::Toolbar);
    m_problem->setIconColor(prism::ThemeColor(&prism::Theme::warning));
    m_problem->setVisible(false);
    row->add(m_problem, prism::Slot{1.0F, prism::Margin(12.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

// ------------------------------------------------------------------------------- blueprint view

prism::BlueprintNode EditorAnimGraphPanel::MakeNode(const Node& node) const {
    prism::BlueprintNode n;
    n.id = static_cast<int>(node.id);
    const std::string label = AnimationGraphKindLabel(node.kind);
    n.title = node.name.empty() ? label : node.name;
    n.subtitle = node.name.empty() || node.name == label ? std::string{} : label;
    n.category = prism::BlueprintNodeCategory::Animation;
    n.pos = prism::Vec2{node.position.x, node.position.y};
    n.tooltip = AnimationGraphKindHelp(node.kind);
    switch (node.kind) {
        case Kind::Output: n.icon = prism::Icon::NodeOutput; n.category = prism::BlueprintNodeCategory::Event; break;
        case Kind::Clip: n.icon = prism::Icon::AnimClipNode; break;
        case Kind::Blend2: n.icon = prism::Icon::NodeBlend; break;
        case Kind::BlendSpace1D:
        case Kind::BlendSpace2D: n.icon = prism::Icon::NodeBlendSpace; break;
        case Kind::Additive: n.icon = prism::Icon::AdditivePose; break;
        case Kind::OneShot: n.icon = prism::Icon::OneShot; break;
        case Kind::TimeScale: n.icon = prism::Icon::TimeScale; break;
        case Kind::TimeSeek: n.icon = prism::Icon::TimeSeek; break;
        case Kind::StateMachine: n.icon = prism::Icon::StateMachine; n.category = prism::BlueprintNodeCategory::State; break;
        case Kind::Select: n.icon = prism::Icon::AnimSelect; break;
        case Kind::LayeredBlend: n.icon = prism::Icon::LayeredBlend; break;
        case Kind::Slot: n.icon = prism::Icon::NodeSlot; break;
        case Kind::TwoBoneIK: n.icon = prism::Icon::TwoBoneIK; break;
        case Kind::ModifyBone: n.icon = prism::Icon::ModifyBone; break;
        case Kind::LookAt: n.icon = prism::Icon::LookAt; break;
        case Kind::BlendByBool: n.icon = prism::Icon::BlendByBool; break;
    }
    // A State Machine's inputs are its states: they are opened, not wired, so it shows none.
    for (std::size_t slot = 0U; slot < node.inputs.size() && node.kind != Kind::StateMachine; ++slot) {
        prism::BlueprintPin pin;
        pin.name = AnimationGraphSlotLabel(node.kind, slot);
        pin.type = prism::BlueprintPinType::Pose;
        pin.kind = prism::BlueprintPinKind::Input;
        n.inputs.push_back(std::move(pin));
    }
    if (node.kind != Kind::Output) {
        prism::BlueprintPin out;
        out.name = "Pose";
        out.type = prism::BlueprintPinType::Pose;
        out.kind = prism::BlueprintPinKind::Output;
        n.outputs.push_back(std::move(out));
    }
    n.bodyH = BodyHeight(node);
    n.bodyW = n.bodyH > 0.0F ? 232.0F : 0.0F;
    n.bodySummary = Summary(node);
    return n;
}

std::vector<prism::BlueprintNodeSpec> EditorAnimGraphPanel::NodeLibrary() const {
    std::vector<prism::BlueprintNodeSpec> library;
    for (const Kind kind : kAddableKinds) {
        prism::BlueprintNodeSpec spec;
        spec.category = kind == Kind::StateMachine                                                ? "State Machines"
                        : kind == Kind::Clip || kind == Kind::Slot                                   ? "Sources"
                        : kind == Kind::TwoBoneIK || kind == Kind::ModifyBone || kind == Kind::LookAt ? "Bones"
                                                                                                      : "Blends";
        spec.name = AnimationGraphKindLabel(kind);
        spec.keywords = AnimationGraphKindHelp(kind);
        spec.make = [this, kind] {
            // Made as the graph would make it, so the palette sees its real input pins (a wire
            // dragged off an output offers the nodes that take a pose).
            std::vector<Node> scratch;
            const std::uint32_t id = AddAnimationGraphNode(scratch, kind, Math::Vector2{});
            const auto made = std::ranges::find(scratch, id, &Node::id);
            Node node = made != scratch.end() ? *made : Node{};
            node.kind = kind;
            node.name = AnimationGraphKindLabel(kind);
            return MakeNode(node);
        };
        library.push_back(std::move(spec));
    }
    return library;
}

void EditorAnimGraphPanel::RebuildGraph(const Graph& graph) {
    if (!m_graph) {
        return;
    }
    m_rebuilding = true;
    const AnimationGraphScope scope = Scope();
    const bool first = m_shownTree != m_view.tree || !(m_shownScope == scope);
    const std::vector<std::uint32_t> shown = AnimationGraphScopeNodes(graph.nodes, scope);
    const auto inScope = [&shown](const std::uint32_t id) { return std::ranges::find(shown, id) != shown.end(); };
    m_graph->clear();
    for (const Node& node : graph.nodes) {
        if (inScope(node.id)) {
            m_graph->addNode(MakeNode(node));
        }
    }
    // A state's graph ends in its result: the pose the state plays.
    const Node* machine = nullptr;
    if (scope.machine != 0U) {
        const auto found = std::ranges::find(graph.nodes, scope.machine, &Node::id);
        machine = found != graph.nodes.end() ? &*found : nullptr;
    }
    if (machine != nullptr) {
        prism::BlueprintNode result;
        result.id = kStateResult;
        result.title = "Output Pose";
        result.subtitle = AnimationStateName(graph.nodes, *machine, scope.state);
        result.category = prism::BlueprintNodeCategory::Event;
        result.icon = prism::Icon::NodeOutput;
        result.pos = prism::Vec2{0.0F, 0.0F};
        result.tooltip = "What this state plays: the pose wired here";
        prism::BlueprintPin pin;
        pin.name = "Result";
        pin.type = prism::BlueprintPinType::Pose;
        pin.kind = prism::BlueprintPinKind::Input;
        result.inputs.push_back(std::move(pin));
        m_graph->addNode(std::move(result));
    }
    // Each comment keeps the same id across rebuilds (its place among this graph's comments, far
    // above any node id), so a rename or a drag that outlives a rebuild still finds its box.
    int commentOrdinal = 0;
    for (const Scene::AnimationGraphComment& comment : graph.comments) {
        if (!(AnimationGraphScope{comment.owner, comment.ownerState} == scope)) {
            continue;
        }
        prism::BlueprintComment box;
        box.id = 0x40000000 + commentOrdinal++;
        box.title = comment.title;
        box.bounds = prism::Rect{comment.position.x, comment.position.y, comment.size.x, comment.size.y};
        box.color = prism::Color(static_cast<std::uint8_t>(comment.color >> 24U), static_cast<std::uint8_t>(comment.color >> 16U),
                                 static_cast<std::uint8_t>(comment.color >> 8U), static_cast<std::uint8_t>(comment.color));
        m_graph->addComment(box);
    }
    for (const Node& node : graph.nodes) {
        if (!inScope(node.id) || node.kind == Kind::StateMachine) {
            continue;
        }
        for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
            if (node.inputs[slot] != 0U && inScope(node.inputs[slot])) {
                m_graph->connectPins(static_cast<int>(node.inputs[slot]), 0, static_cast<int>(node.id), static_cast<int>(slot),
                                     prism::BlueprintPinType::Pose);
            }
        }
    }
    if (machine != nullptr && scope.state < machine->inputs.size() && machine->inputs[scope.state] != 0U &&
        inScope(machine->inputs[scope.state])) {
        m_graph->connectPins(static_cast<int>(machine->inputs[scope.state]), 0, kStateResult, 0, prism::BlueprintPinType::Pose);
    }
    for (const Node& node : graph.nodes) {
        if (inScope(node.id) && BodyHeight(node) > 0.0F) {
            m_graph->setNodeBody(static_cast<int>(node.id), BuildBody(node));
        }
    }
    std::erase_if(m_selected, [&graph](const std::uint32_t id) { return std::ranges::none_of(graph.nodes, [id](const Node& n) { return n.id == id; }); });
    for (const std::uint32_t id : m_selected) {
        m_graph->select(static_cast<int>(id), true);
    }
    // The graph's own "last state", so its next commit reports only what the user did.
    m_graph->commit("Sync");
    m_rebuilding = false;
    if (first) {
        m_graph->frameAll();
    }
    m_shown = graph;
    m_shownTree = m_view.tree;
    m_shownScope = scope;
}

void EditorAnimGraphPanel::ApplyGraphEdit() {
    if (m_rebuilding || !m_graph) {
        return;
    }
    const Graph* graph = GraphPtr();
    if (graph == nullptr) {
        return;
    }
    const prism::BlueprintGraph& view = *m_graph;
    const AnimationGraphScope scope = m_shownScope;
    static_cast<void>(Edit([&view, scope](Graph& g) {
        // What the view shows is this scope's nodes: the ones missing were deleted there.
        const std::vector<std::uint32_t> shown = AnimationGraphScopeNodes(g.nodes, scope);
        std::vector<std::uint32_t> gone;
        for (const std::uint32_t id : shown) {
            if (view.findNode(static_cast<int>(id)) == nullptr) {
                gone.push_back(id);
            }
        }
        if (!gone.empty()) {
            static_cast<void>(DeleteAnimationGraphNodes(g.nodes, gone));
        }
        // Everything shown here stays here, wired or not (a wire taken off keeps it in this graph).
        for (Node& node : g.nodes) {
            const prism::BlueprintNode* drawn = view.findNode(static_cast<int>(node.id));
            if (drawn == nullptr || static_cast<int>(node.id) == kStateResult) {
                continue;
            }
            node.position = Math::Vector2{drawn->pos.x, drawn->pos.y};
            node.owner = scope.machine;
            node.ownerState = scope.state;
            if (node.kind != Kind::StateMachine) {
                std::fill(node.inputs.begin(), node.inputs.end(), 0U);
            }
        }
        Node* machine = nullptr;
        if (scope.machine != 0U) {
            const auto found = std::ranges::find(g.nodes, scope.machine, &Node::id);
            machine = found != g.nodes.end() && scope.state < found->inputs.size() ? &*found : nullptr;
            if (machine != nullptr) {
                machine->inputs[scope.state] = 0U;
            }
        }
        // This graph's comment boxes as the view has them (made, moved, resized, renamed or deleted).
        std::erase_if(g.comments, [scope](const Scene::AnimationGraphComment& c) { return AnimationGraphScope{c.owner, c.ownerState} == scope; });
        for (const prism::BlueprintComment& box : view.comments()) {
            Scene::AnimationGraphComment comment;
            comment.title = box.title;
            comment.position = Math::Vector2{box.bounds.x, box.bounds.y};
            comment.size = Math::Vector2{box.bounds.w, box.bounds.h};
            comment.color = (static_cast<std::uint32_t>(box.color.r) << 24U) | (static_cast<std::uint32_t>(box.color.g) << 16U) |
                            (static_cast<std::uint32_t>(box.color.b) << 8U) | static_cast<std::uint32_t>(box.color.a);
            comment.owner = scope.machine;
            comment.ownerState = scope.state;
            g.comments.push_back(std::move(comment));
        }
        for (const prism::BlueprintWire& wire : view.wires()) {
            if (wire.toNodeId == kStateResult) {
                if (machine != nullptr) {
                    machine->inputs[scope.state] = static_cast<std::uint32_t>(wire.fromNodeId);
                }
                continue;
            }
            for (Node& node : g.nodes) {
                if (static_cast<int>(node.id) == wire.toNodeId && node.kind != Kind::StateMachine && wire.toPinIndex >= 0 &&
                    static_cast<std::size_t>(wire.toPinIndex) < node.inputs.size()) {
                    node.inputs[static_cast<std::size_t>(wire.toPinIndex)] = static_cast<std::uint32_t>(wire.fromNodeId);
                }
            }
        }
    }));
    // Whatever the component kept (a refused change is dropped), the view shows it next refresh.
    m_shown.reset();
}

int EditorAnimGraphPanel::Spawn(const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) {
    std::optional<Kind> kind;
    for (const Kind candidate : kAddableKinds) {
        if (spec.name == AnimationGraphKindLabel(candidate)) {
            kind = candidate;
        }
    }
    if (!kind.has_value() || !m_graph) {
        return -1;
    }
    std::uint32_t added = 0U;
    const AnimationGraphScope scope = m_shownScope;
    if (!Edit([&](Graph& g) {
            added = AddAnimationGraphNode(g.nodes, *kind, Math::Vector2{world.x, world.y});
            if (added != 0U) {
                BindNewNodeParameters(g, added);
                // Made in a state's graph: it stays there.
                if (const auto made = std::ranges::find(g.nodes, added, &Node::id); made != g.nodes.end()) {
                    made->owner = scope.machine;
                    made->ownerState = scope.state;
                }
                // A State Machine starts with a first state, playing a Clip in its own graph.
                if (*kind == Kind::StateMachine) {
                    static_cast<void>(AddAnimationState(g.nodes, added, Math::Vector2{0.0F, 0.0F}));
                }
            }
        }) || added == 0U) {
        return -1;
    }
    const Graph* graph = GraphPtr();
    if (graph == nullptr) {
        return -1;
    }
    const auto node = std::ranges::find(graph->nodes, added, &Node::id);
    if (node == graph->nodes.end()) {
        return -1;
    }
    m_rebuilding = true;
    m_graph->addNode(MakeNode(*node));
    if (BodyHeight(*node) > 0.0F) {
        m_graph->setNodeBody(static_cast<int>(added), BuildBody(*node));
    }
    m_rebuilding = false;
    m_selected = {added};
    m_sideDirty = true;
    return static_cast<int>(added);
}

// ---------------------------------------------------------------------------------------- edits

bool EditorAnimGraphPanel::Edit(const std::function<void(Graph&)>& change) {
    if (m_view.tree == Scene::kInvalidEntity) {
        return false;
    }
    m_sideDirty = true;
    return m_editor.EditAnimationGraph(m_view.tree, change);
}

bool EditorAnimGraphPanel::EditNode(const std::uint32_t id, const std::function<void(Node&)>& change) {
    return Edit([&](Graph& g) {
        if (auto node = std::ranges::find(g.nodes, id, &Node::id); node != g.nodes.end()) {
            change(*node);
        }
    });
}

void EditorAnimGraphPanel::LiveNode(const std::uint32_t id, const std::function<void(Node&)>& change) {
    Graph* graph = LiveGraph();
    if (graph == nullptr) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_liveBefore = *graph;
    }
    if (auto node = std::ranges::find(graph->nodes, id, &Node::id); node != graph->nodes.end()) {
        change(*node);
    }
}

void EditorAnimGraphPanel::LiveParameter(const std::size_t index, const float value, const int axis) {
    Graph* graph = LiveGraph();
    if (graph == nullptr || index >= graph->parameters.size()) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_liveBefore = *graph;
    }
    Scene::AnimationParameter& parameter = graph->parameters[index];
    if (axis == 0) {
        parameter.vector.x = value;
    } else if (axis == 1) {
        parameter.vector.y = value;
    } else if (axis == 2) {
        parameter.vector.z = value;
    } else {
        parameter.value = value;
    }
}

void EditorAnimGraphPanel::CommitLive() {
    Graph* graph = LiveGraph();
    if (graph == nullptr || !m_liveBefore.has_value()) {
        m_liveBefore.reset();
        return;
    }
    // Back to where the gesture started, then the gesture's result as one undo step.
    const Graph after = *graph;
    graph->parameters = m_liveBefore->parameters;
    graph->nodes = m_liveBefore->nodes;
    m_liveBefore.reset();
    static_cast<void>(Edit([&after](Graph& g) {
        g.parameters = after.parameters;
        g.nodes = after.nodes;
    }));
}

// -------------------------------------------------------------------------------------- refresh

void EditorAnimGraphPanel::Refresh(const double deltaSeconds, const bool shown) {
    if (!shown || !m_graph) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_editor.StepAnimationGraphPreview(deltaSeconds);
    }
    m_view = m_editor.SyncAnimationGraphView();
    const Graph* graph = GraphPtr();
    if (graph == nullptr) {
        return;
    }
    SyncFocus();
    if (!m_liveBefore.has_value()) {
        if (m_shownTree != m_view.tree || !m_shown.has_value() || !m_shown->HasSameSettings(*graph) || !(m_shownScope == Scope())) {
            RebuildGraph(*graph);
            m_sideDirty = true;
        }
        const Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
        if (m_sideDirty || m_selected != m_shownSelection || state.pickedState != m_shownPickedState ||
            state.pickedTransition != m_shownPickedTransition) {
            RebuildSide(*graph);
        }
    }
    RefreshLiveValues(*graph);
}

void EditorAnimGraphPanel::RefreshLiveValues(const Graph& graph) {
    const Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    if (m_graphButton) {
        m_graphButton->setVisible(m_view.graphs.size() > 1U);
        const std::string name = NameOf(em, m_view.tree, "AnimationGraph");
        if (m_graphButton->text() != name) {
            m_graphButton->setText(name);
        }
    }
    if (m_previewButton && m_previewButton->checked() != state.previewing) {
        m_previewButton->setChecked(state.previewing);
    }
    if (m_playButton) {
        m_playButton->setIcon(state.previewPaused ? prism::Icon::Play : prism::Icon::Pause);
    }
    if (m_problem) {
        std::string problem = Scene::DescribeAnimationGraphProblem(graph);
        if (problem.empty() && !state.status.empty()) {
            problem = state.status;
        }
        if (problem.empty() && m_editor.ResolveActiveSequencerForGraph(m_view.tree) == Scene::kInvalidEntity) {
            problem = "No AnimationSequencer: add one to the entity (the graph plays its animations)";
        }
        if (problem.empty() && m_view.skeleton == Scene::kInvalidEntity) {
            problem = "No skeleton to preview on: add a Skeleton3D to the entity";
        }
        if (m_problem->tooltip() != problem) {
            m_problem->setTooltip(problem);
            m_problem->setVisible(!problem.empty());
        }
    }
    // Parameter values the preview moves.
    for (const ParameterRow& row : m_parameterRows) {
        const auto parameter = std::ranges::find(graph.parameters, row.name, &Scene::AnimationParameter::name);
        if (parameter == graph.parameters.end()) {
            continue;
        }
        if (row.number && !row.number->focused() && std::abs(row.number->value() - parameter->value) > 1.0e-5F && !m_liveBefore.has_value()) {
            row.number->setValue(parameter->value);
        }
        if (row.sparkline) {
            const auto history = state.parameterHistory.find(row.name);
            static_cast<Sparkline&>(*row.sparkline).SetSamples(history != state.parameterHistory.end() ? &history->second : nullptr);
        }
    }
    if (m_log && state.previewLog.size() != m_shownLogLines) {
        m_log->clear();
        for (const Editor::AnimationGraphViewState::PreviewLogLine& line : state.previewLog) {
            char at[24];
            std::snprintf(at, sizeof(at), "%6.2fs  ", line.at);
            m_log->add(line.stateChange ? prism::LogLevel::Success : prism::LogLevel::Info, std::string{at} + line.text);
        }
        m_shownLogLines = state.previewLog.size();
    }
    if (m_canvas) {
        m_canvas->invalidate();
    }
}

const Scene::AnimationGraphNode* EditorAnimGraphPanel::FocusNode() const {
    const Graph* graph = GraphPtr();
    const std::uint32_t focus = m_editor.m_animGraph.focus;
    if (graph == nullptr || focus == 0U) {
        return nullptr;
    }
    const auto node = std::ranges::find(graph->nodes, focus, &Node::id);
    return node != graph->nodes.end() ? &*node : nullptr;
}

void EditorAnimGraphPanel::OpenFocus(const std::uint32_t node) {
    Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    state.focus = node;
    state.pickedState = -1;
    state.pickedTransition = -1;
    state.stateFramed = false;
    m_sideDirty = true;
    SyncFocus();
}

void EditorAnimGraphPanel::OpenStateGraph(const std::uint32_t machine, const std::uint32_t slot) {
    Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    state.focus = 0U;
    state.scopeMachine = machine;
    state.scopeState = slot;
    state.pickedState = -1;
    state.pickedTransition = -1;
    m_selected.clear();
    m_sideDirty = true;
    SyncFocus();
}

AnimationGraphScope EditorAnimGraphPanel::Scope() const {
    const Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const Graph* graph = GraphPtr();
    if (graph == nullptr || state.scopeMachine == 0U) {
        return {};
    }
    const auto machine = std::ranges::find(graph->nodes, state.scopeMachine, &Node::id);
    if (machine == graph->nodes.end() || machine->kind != Kind::StateMachine || state.scopeState >= machine->inputs.size()) {
        return {};
    }
    return AnimationGraphScope{state.scopeMachine, state.scopeState};
}

void EditorAnimGraphPanel::CloseFocus() {
    // One step up: a State Machine or Blend Space goes back to the graph it sits in; a state's
    // graph goes back to its State Machine.
    Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const Graph* graph = GraphPtr();
    if (state.focus != 0U) {
        const AnimationGraphScope where = graph != nullptr ? AnimationGraphScopeOf(graph->nodes, state.focus) : AnimationGraphScope{};
        m_selected = {state.focus};
        state.focus = 0U;
        state.scopeMachine = where.machine;
        state.scopeState = where.state;
    } else if (const AnimationGraphScope scope = Scope(); scope.machine != 0U) {
        state.focus = scope.machine;
        state.stateFramed = false;
        state.pickedState = static_cast<int>(scope.state);
    } else {
        state.scopeMachine = 0U;
        state.scopeState = 0U;
    }
    state.pickedTransition = -1;
    m_sideDirty = true;
    SyncFocus();
}

void EditorAnimGraphPanel::SyncFocus() {
    Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const Node* focus = FocusNode();
    if (state.focus != 0U && (focus == nullptr || (focus->kind != Kind::StateMachine && focus->kind != Kind::BlendSpace1D &&
                                                   focus->kind != Kind::BlendSpace2D))) {
        state.focus = 0U;   // its node went (deleted, undone): back to the tree
        focus = nullptr;
    }
    if (state.focus == 0U && state.scopeMachine != 0U && Scope().machine == 0U) {
        state.scopeMachine = 0U;   // its State Machine or state went
        state.scopeState = 0U;
    }
    const int page = focus == nullptr ? 0 : focus->kind == Kind::StateMachine ? 1 : 2;
    if (m_canvas && m_canvas->activeIndex() != page) {
        m_canvas->setActiveIndex(page);
        if (prism::Application::exists()) {
            prism::Application::get().setFocus(page == 1 ? static_cast<prism::Widget*>(m_stateView.get())
                                               : page == 2 ? static_cast<prism::Widget*>(m_spaceView.get())
                                                           : static_cast<prism::Widget*>(m_graph.get()));
        }
    }
    // The path: Tree > each State Machine and state down to here > what is open.
    struct Crumb final {
        std::string text;
        prism::Icon icon = prism::Icon::None;
        std::function<void()> go;
    };
    std::vector<Crumb> path;
    const Graph* graph = GraphPtr();
    AnimationGraphScope scope = Scope();
    if (graph != nullptr && focus != nullptr) {
        const std::uint32_t id = focus->id;
        path.push_back(Crumb{(focus->name.empty() ? std::string{AnimationGraphKindLabel(focus->kind)} : focus->name),
                                focus->kind == Kind::StateMachine ? prism::Icon::StateMachine : prism::Icon::NodeBlendSpace,
                                [this, id] { OpenFocus(id); }});
        scope = AnimationGraphScopeOf(graph->nodes, focus->id);
    }
    for (std::size_t guard = 0U; graph != nullptr && scope.machine != 0U && guard < 32U; ++guard) {
        const auto machine = std::ranges::find(graph->nodes, scope.machine, &Node::id);
        if (machine == graph->nodes.end()) {
            break;
        }
        const std::uint32_t machineId = machine->id;
        const std::uint32_t slot = scope.state;
        path.push_back(Crumb{AnimationStateName(graph->nodes, *machine, scope.state), prism::Icon::StateGraph,
                                [this, machineId, slot] { OpenStateGraph(machineId, slot); }});
        path.push_back(Crumb{machine->name.empty() ? std::string{"State Machine"} : machine->name, prism::Icon::StateMachine,
                                [this, machineId] { OpenFocus(machineId); }});
        scope = AnimationGraphScopeOf(graph->nodes, machineId);
    }
    std::ranges::reverse(path);
    std::string signature;
    for (const Crumb& crumb : path) {
        signature += crumb.text + "\x1f";
    }
    if (m_crumbs) {
        m_crumbs->setVisible(!path.empty());
    }
    if (m_crumbRow && signature != m_crumbSignature) {
        m_crumbSignature = signature;
        m_crumbRow->clearChildren();
        auto up = prism::make<prism::Button>("", prism::Icon::ChevronLeft, prism::ButtonStyle::Flat);
        up->setTooltip("Up one level (Esc in a State Machine or Blend Space)");
        up->onClick = [this] { CloseFocus(); };
        m_crumbRow->add(up, CentreSlot());
        auto tree = prism::make<prism::Button>("Tree", prism::Icon::AnimTree, prism::ButtonStyle::Flat);
        tree->setTooltip("Back to the top of the graph");
        tree->onClick = [this] {
            Editor::AnimationGraphViewState& s = m_editor.m_animGraph;
            s.focus = 0U;
            s.scopeMachine = 0U;
            s.scopeState = 0U;
            m_sideDirty = true;
            SyncFocus();
        };
        m_crumbRow->add(tree, CentreSlot());
        for (std::size_t i = 0U; i < path.size(); ++i) {
            auto chevron = prism::make<prism::Label>(">", prism::FontRole::Semibold);
            chevron->setColor(prism::ThemeColor(&prism::Theme::textDim));
            m_crumbRow->add(chevron, CentreSlot());
            auto step = prism::make<prism::Button>(path[i].text, path[i].icon, i + 1U == path.size() ? prism::ButtonStyle::Default : prism::ButtonStyle::Flat);
            step->onClick = path[i].go;
            m_crumbRow->add(step, CentreSlot());
        }
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
    }
    const std::uint32_t where = state.focus != 0U ? state.focus : (state.scopeMachine << 8U) ^ (state.scopeState + 1U) ^ 0x80000000U;
    if (m_shownFocus != where) {
        m_shownFocus = where;
        m_sideDirty = true;
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
    }
}

// ----------------------------------------------------------------------------------- side panel

void EditorAnimGraphPanel::RebuildSide(const Graph& graph) {
    if (!m_side) {
        return;
    }
    m_sideDirty = false;
    m_shownSelection = m_selected;
    m_parameterRows.clear();
    m_shownLogLines = static_cast<std::size_t>(-1);
    m_shownPickedState = m_editor.m_animGraph.pickedState;
    m_shownPickedTransition = m_editor.m_animGraph.pickedTransition;
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    // Built like the entity's Details: categories, a label column, one control per row.
    auto grid = prism::make<prism::PropertyGrid>();

    const int graphCategory = grid->addCategory("Graph");
    const auto picker = [](const std::string& text, std::function<void(prism::Vec2)> open) {
        auto button = prism::make<prism::Button>(text, prism::Icon::ChevronDown, prism::ButtonStyle::Default);
        std::weak_ptr<prism::Button> weak = button;
        button->onClick = [weak, open = std::move(open)] {
            if (auto b = weak.lock()) {
                open(prism::Vec2{b->rect().x, b->rect().bottom()});
            }
        };
        return button;
    };
    const Scene::Entity sequencer = m_editor.ResolveActiveSequencerForGraph(m_view.tree);
    grid->addRow(graphCategory, "Sequencer",
                 picker(sequencer != Scene::kInvalidEntity ? NameOf(em, sequencer, "AnimationSequencer") : std::string{"None"},
                        [this](const prism::Vec2 at) { ShowSequencerMenu(at); }),
                 "The AnimationSequencer whose animations the Clip nodes play");
    const Scene::Entity linkedPlayer = graph.montageSequencer;
    grid->addRow(graphCategory, "Montage Sequencer",
                 picker(linkedPlayer != Scene::kInvalidEntity && em.IsAlive(linkedPlayer) ? NameOf(em, linkedPlayer, "MontageSequencer")
                                                                                                 : std::string{"None"},
                        [this](const prism::Vec2 at) { ShowMontageSequencerMenu(at); }),
                 "Optional: the MontageSequencer whose motion montages play on the Slot nodes. None: any MontageSequencer on the same skeleton.");

    if (const Node* focus = FocusNode(); focus != nullptr && focus->kind == Kind::StateMachine) {
        BuildStateMachineSide(*grid, graph, *focus);
    } else if (focus != nullptr) {
        BuildBlendSpaceSide(*grid, graph, *focus);
    } else {
        BuildSelected(*grid, graph);
    }
    BuildParameters(*grid, graph);
    BuildPreview(*grid);
    m_side->setContent(prism::make<FitWidth>(grid));
}

void EditorAnimGraphPanel::BuildParameters(prism::PropertyGrid& grid, const Graph& graph) {
    const int category = grid.addCategory("Parameters");
    static constexpr const char* kTypes[] = {"Float", "Bool", "Trigger", "Vector3"};
    for (std::size_t index = 0U; index < graph.parameters.size(); ++index) {
        const Scene::AnimationParameter& parameter = graph.parameters[index];
        auto row = prism::hbox(4.0F);
        auto name = prism::make<prism::TextInput>(parameter.name, "Name");
        const std::string old = parameter.name;
        // Which nodes it drives, so the list reads as the graph's controls.
        std::string drives;
        for (const Node& node : graph.nodes) {
            if (node.kind != Kind::Slot && (node.parameter == old || node.parameterY == old || node.alphaParameter == old)) {
                drives += (drives.empty() ? "" : ", ") + (node.name.empty() ? std::string(AnimationGraphKindLabel(node.kind)) : node.name);
            }
        }
        name->setTooltip(drives.empty() ? std::string{"Not used by a node"} : "Drives " + drives);
        name->onCommit = [this, old](const std::string& to) {
            if (to.empty() || to == old) {
                return;
            }
            // References follow the rename: nodes and transitions read the parameter by name.
            Edit([&old, &to](Graph& g) { static_cast<void>(RenameGraphParameter(g, old, to)); });
        };
        row->add(name, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        ParameterRow live{parameter.name, nullptr, nullptr};
        switch (parameter.type) {
            case Scene::AnimationParameterType::Float: {
                auto number = prism::make<prism::NumberField>(parameter.value, 0.01F, 2);
                number->onChange = [this, index](const float v) { LiveParameter(index, v); };
                number->onCommit = [this](float) { CommitLive(); };
                row->add(number, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
                live.number = number;
                break;
            }
            case Scene::AnimationParameterType::Bool: {
                auto check = prism::make<prism::CheckBox>("", parameter.value > 0.5F);
                check->onChange = [this, index](const bool on) {
                    Edit([index, on](Graph& g) {
                        if (index < g.parameters.size()) {
                            g.parameters[index].value = on ? 1.0F : 0.0F;
                        }
                    });
                };
                row->add(check, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                break;
            }
            case Scene::AnimationParameterType::Vector3: {
                const float values[] = {parameter.vector.x, parameter.vector.y, parameter.vector.z};
                for (int axis = 0; axis < 3; ++axis) {
                    auto number = prism::make<prism::NumberField>(values[axis], 0.01F, 2);
                    number->onCommit = [this, index, axis](const float v) {
                        Edit([index, axis, v](Graph& g) {
                            if (index < g.parameters.size()) {
                                Math::Vector3& at = g.parameters[index].vector;
                                (axis == 0 ? at.x : axis == 1 ? at.y : at.z) = v;
                            }
                        });
                    };
                    row->add(number, prism::Slot{0.6F});
                }
                break;
            }
            case Scene::AnimationParameterType::Trigger: {
                auto fire = ToolButton(prism::Icon::Fire, "Fire it now");
                fire->onClick = [this, index] {
                    if (Graph* g = LiveGraph(); g != nullptr && index < g->parameters.size()) {
                        g->parameters[index].value = 1.0F;   // runtime only: the step consumes it
                    }
                };
                row->add(fire, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                break;
            }
        }
        auto remove = ToolButton(prism::Icon::Trash, "Remove the parameter");
        remove->onClick = [this, index] {
            Edit([index](Graph& g) {
                if (index >= g.parameters.size()) {
                    return;
                }
                // Gone everywhere it was used: nodes fall back to their own value, and the
                // transition conditions that read it go with it.
                const std::string name = g.parameters[index].name;
                g.parameters.erase(g.parameters.begin() + static_cast<std::ptrdiff_t>(index));
                for (Node& node : g.nodes) {
                    for (std::string* bound : {&node.parameter, &node.parameterY, &node.alphaParameter}) {
                        if (*bound == name && node.kind != Kind::Slot) {
                            bound->clear();
                        }
                    }
                    for (Scene::AnimationGraphTransition& transition : node.transitions) {
                        std::erase_if(transition.conditions,
                                      [&name](const Scene::AnimationTransitionCondition& c) { return c.parameter == name; });
                    }
                }
            });
        };
        row->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        grid.addRow(category, kTypes[std::min<std::size_t>(static_cast<std::size_t>(parameter.type), 3U)], row);
        // Who writes it: a script, or the engine from the character.
        if (parameter.type == Scene::AnimationParameterType::Float || parameter.type == Scene::AnimationParameterType::Bool) {
            auto source = prism::make<prism::ComboBox>(
                std::vector<std::string>{"Script", "Character Speed", "Character Direction", "Character In Air", "Character Grounded"},
                static_cast<int>(parameter.source));
            source->onChange = [this, index](const int i) {
                Edit([index, i](Graph& g) {
                    if (index < g.parameters.size()) {
                        g.parameters[index].source = static_cast<Scene::AnimationParameterSource>(std::clamp(i, 0, 4));
                    }
                });
            };
            grid.addRow(category, "Set By", source, "Character: the engine writes it every frame from the Character Controller");
        }
        m_parameterRows.push_back(std::move(live));
    }
    // Add: one button, its type picked from the menu.
    auto add = prism::make<prism::Button>("Add", prism::Icon::Plus, prism::ButtonStyle::Default);
    std::weak_ptr<prism::Button> weakAdd = add;
    add->onClick = [this, weakAdd] {
        const auto addOf = [this](const Scene::AnimationParameterType type, const char* base) {
            return [this, type, base] {
                Edit([type, base](Graph& g) {
                    Scene::AnimationParameter p;
                    p.name = UniqueParameterName(g, base);
                    p.type = type;
                    g.parameters.push_back(std::move(p));
                });
            };
        };
        std::vector<prism::MenuItem> items{prism::MenuItem::Item("Float", addOf(Scene::AnimationParameterType::Float, "Speed"), "", prism::Icon::ParamFloat),
                                           prism::MenuItem::Item("Bool", addOf(Scene::AnimationParameterType::Bool, "IsMoving"), "", prism::Icon::ParamBool),
                                           prism::MenuItem::Item("Trigger", addOf(Scene::AnimationParameterType::Trigger, "Attack"), "", prism::Icon::ParamTrigger),
                                           prism::MenuItem::Item("Vector3", addOf(Scene::AnimationParameterType::Vector3, "Target"), "", prism::Icon::ParamVector)};
        if (auto b = weakAdd.lock()) {
            prism::showContextMenu(std::move(items), prism::Vec2{b->rect().x, b->rect().bottom()});
        }
    };
    grid.addRow(category, "", add);
}

void EditorAnimGraphPanel::BuildSelected(prism::PropertyGrid& grid, const Graph& graph) {
    if (m_selected.size() != 1U) {
        return;
    }
    const std::uint32_t id = m_selected.front();
    const auto found = std::ranges::find(graph.nodes, id, &Node::id);
    if (found == graph.nodes.end()) {
        return;
    }
    const Node& node = *found;
    const int category = grid.addCategory(AnimationGraphKindLabel(node.kind));
    if (node.kind == Scene::AnimationGraphNodeKind::Output) {
        return;
    }
    auto name = prism::make<prism::TextInput>(node.name, "Name");
    name->onCommit = [this, id](const std::string& to) { RenameNode(id, to); };
    grid.addRow(category, "Name", name, AnimationGraphKindHelp(node.kind));

    // Its parameter: the name the gameplay sets it by. Typing a name renames it everywhere (or,
    // with none bound yet, makes one).
    if (const std::optional<AnimationNodeParameter> spec = NodeParameterOf(node.kind); spec.has_value()) {
        const std::string bound = node.*(spec->field);
        static constexpr prism::Icon kIcons[] = {prism::Icon::ParamFloat, prism::Icon::ParamBool, prism::Icon::ParamTrigger,
                                                 prism::Icon::ParamVector};
        static constexpr const char* kTypes[] = {"Float", "Bool", "Trigger", "Vector3"};
        const auto typeIndex = std::min<std::size_t>(static_cast<std::size_t>(spec->type), 3U);
        auto icon = ToolButton(kIcons[typeIndex], kTypes[typeIndex]);
        icon->setEnabled(false);
        auto field = prism::make<prism::TextInput>(bound, "Parameter name");
        const Scene::AnimationParameterType type = spec->type;
        field->onCommit = [this, id, bound, type](const std::string& to) {
            if (to.empty() || to == bound) {
                return;
            }
            Edit([id, &bound, &to, type](Graph& g) {
                const bool exists = std::ranges::any_of(g.parameters, [&bound](const Scene::AnimationParameter& p) { return p.name == bound; });
                if (!bound.empty() && exists) {
                    static_cast<void>(RenameGraphParameter(g, bound, to));
                    return;
                }
                const auto n = std::ranges::find(g.nodes, id, &Node::id);
                const std::optional<AnimationNodeParameter> s = n != g.nodes.end() ? NodeParameterOf(n->kind) : std::nullopt;
                if (!s.has_value()) {
                    return;
                }
                if (std::ranges::none_of(g.parameters, [&to](const Scene::AnimationParameter& p) { return p.name == to; })) {
                    Scene::AnimationParameter made;
                    made.name = to;
                    made.type = type;
                    g.parameters.push_back(made);
                }
                (*n).*(s->field) = to;
            });
        };
        auto row = prism::hbox(4.0F);
        row->add(icon, CentreSlot());
        row->add(field, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        grid.addRow(category, "Parameter", row, std::string(kTypes[typeIndex]) + " the gameplay sets this node by");
    }

    // A Layered Blend's bones: the branches its second pose replaces, each with its children.
    if (node.kind == Scene::AnimationGraphNodeKind::LayeredBlend) {
        for (std::size_t i = 0U; i < node.bones.size(); ++i) {
            auto remove = ToolButton(prism::Icon::Close, "Take this branch off");
            remove->onClick = [this, id, i] {
                EditNode(id, [i](Node& n) {
                    if (i < n.bones.size()) {
                        n.bones.erase(n.bones.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                });
            };
            auto row = prism::hbox(4.0F);
            row->add(prism::make<prism::Label>(node.bones[i]), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            row->add(remove, CentreSlot());
            grid.addRow(category, i == 0U ? "Bones" : "", row);
        }
        std::vector<std::string> bones{"Add"};
        const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
        if (m_view.skeleton != Scene::kInvalidEntity && em.HasComponent<Scene::Skeleton3DComponent>(m_view.skeleton)) {
            for (const Scene::SkeletonBone& bone : em.GetComponent<Scene::Skeleton3DComponent>(m_view.skeleton).bones) {
                if (std::ranges::find(node.bones, bone.name) == node.bones.end()) {
                    bones.push_back(bone.name);
                }
            }
        }
        auto pick = prism::make<prism::ComboBox>(bones, 0);
        pick->onChange = [this, id, bones](const int i) {
            if (i > 0 && static_cast<std::size_t>(i) < bones.size()) {
                const std::string bone = bones[static_cast<std::size_t>(i)];
                EditNode(id, [&bone](Node& n) {
                    if (n.bones.size() < Scene::kMaximumAnimationLayerBones) {
                        n.bones.push_back(bone);
                    }
                });
            }
        };
        grid.addRow(category, node.bones.empty() ? "Bones" : "", pick);
    }
    auto actions = prism::hbox(4.0F);
    auto duplicate = prism::make<prism::Button>("Duplicate", prism::Icon::Duplicate, prism::ButtonStyle::Default);
    duplicate->onClick = [this, id] {
        Edit([id](Graph& g) { static_cast<void>(DuplicateAnimationGraphNodes(g.nodes, {id}, Math::Vector2{40.0F, 40.0F})); });
    };
    auto remove = prism::make<prism::Button>("Delete", prism::Icon::Trash, prism::ButtonStyle::Default);
    remove->onClick = [this, id] {
        m_selected.clear();
        Edit([id](Graph& g) { static_cast<void>(DeleteAnimationGraphNodes(g.nodes, {id})); });
    };
    actions->add(duplicate, prism::Slot{1.0F});
    actions->add(remove, prism::Slot{1.0F});
    grid.addRow(category, "", actions);
}

void EditorAnimGraphPanel::BuildPreview(prism::PropertyGrid& grid) {
    const int category = grid.addCategory("Preview Log", false);
    m_log = prism::make<prism::LogView>();
    auto logBox = prism::make<prism::SizeBox>();
    logBox->setHeightOverride(150.0F);
    logBox->setContent(m_log);
    grid.addRow(category, "", logBox);
}

// ---------------------------------------------------------------------------------------- menus

void EditorAnimGraphPanel::RenameNode(const std::uint32_t id, const std::string& to) {
    if (to.empty()) {
        return;
    }
    Edit([id, &to](Graph& g) {
        const auto n = std::ranges::find(g.nodes, id, &Node::id);
        if (n == g.nodes.end() || n->kind == Kind::Output) {
            return;
        }
        // Its parameter still named after it follows the new name ("Walk" -> "Run").
        if (const std::optional<AnimationNodeParameter> spec = NodeParameterOf(n->kind); spec.has_value()) {
            const std::string bound = (*n).*(spec->field);
            if (!bound.empty() && bound == ParameterNameForNode(n->name)) {
                const std::string wanted = UniqueParameterName(g, ParameterNameForNode(to));
                static_cast<void>(RenameGraphParameter(g, bound, wanted));
            }
        }
        const auto again = std::ranges::find(g.nodes, id, &Node::id);
        again->name = to;
    });
    m_sideDirty = true;
}

bool EditorAnimGraphPanel::NodeAction(const std::string& action, const std::vector<int>& ids) {
    const Graph* graph = GraphPtr();
    if (graph == nullptr) {
        return true;
    }
    std::vector<std::uint32_t> picked;
    for (const int id : ids) {
        const auto node = std::ranges::find(graph->nodes, static_cast<std::uint32_t>(id), &Node::id);
        if (node != graph->nodes.end() && node->kind != Kind::Output) {
            picked.push_back(node->id);
        }
    }
    if (action == "Copy" || action == "Cut") {
        m_clipboard.clear();
        for (const std::uint32_t id : picked) {
            m_clipboard.push_back(*std::ranges::find(graph->nodes, id, &Node::id));
        }
        if (action == "Cut" && !picked.empty()) {
            Edit([&picked](Graph& g) { static_cast<void>(DeleteAnimationGraphNodes(g.nodes, picked)); });
        }
        return true;
    }
    if (action == "Delete") {
        if (!picked.empty()) {
            Edit([&picked](Graph& g) { static_cast<void>(DeleteAnimationGraphNodes(g.nodes, picked)); });
        }
        m_selected.clear();
        m_sideDirty = true;
        return true;
    }
    if (action == "Duplicate") {
        std::vector<std::uint32_t> made;
        if (!picked.empty()) {
            Edit([&](Graph& g) { made = DuplicateAnimationGraphNodes(g.nodes, picked, Math::Vector2{40.0F, 40.0F}); });
        }
        m_selected = made;
        m_sideDirty = true;
        return true;
    }
    if (action == "Paste") {
        if (m_clipboard.empty()) {
            return true;
        }
        const prism::Vec2 mouse = prism::Application::exists() ? prism::Application::get().mousePos() : prism::Vec2{};
        const prism::Vec2 at = m_graph ? m_graph->screenToWorld(mouse) : prism::Vec2{};
        std::vector<std::uint32_t> made;
        const std::vector<Node> copies = m_clipboard;
        Edit([&](Graph& g) {
            std::uint32_t next = 1U;
            for (const Node& node : g.nodes) {
                next = std::max(next, node.id + 1U);
            }
            float left = 1e30F, top = 1e30F;
            for (const Node& node : copies) {
                left = std::min(left, node.position.x);
                top = std::min(top, node.position.y);
            }
            std::unordered_map<std::uint32_t, std::uint32_t> remap;
            for (const Node& node : copies) {
                remap.emplace(node.id, next++);
            }
            for (Node node : copies) {
                if (g.nodes.size() >= Scene::kMaximumAnimationGraphNodes) {
                    break;
                }
                node.id = remap[node.id];
                for (std::uint32_t& input : node.inputs) {
                    const auto found = remap.find(input);
                    input = found != remap.end() ? found->second : 0U;
                }
                node.position = Math::Vector2{at.x + node.position.x - left, at.y + node.position.y - top};
                node.owner = m_shownScope.machine;
                node.ownerState = m_shownScope.state;
                made.push_back(node.id);
                g.nodes.push_back(std::move(node));
            }
        });
        m_selected = made;
        m_sideDirty = true;
        return true;
    }
    return false;
}

void EditorAnimGraphPanel::ShowGraphMenu() {
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    std::vector<prism::MenuItem> items;
    for (const Scene::Entity graph : m_view.graphs) {
        items.push_back(prism::MenuItem::Check(NameOf(em, graph, "AnimationGraph"), graph == m_view.tree,
                                               [this, graph] { m_editor.SelectEntity(graph); }));
    }
    if (m_graphButton) {
        prism::showContextMenu(std::move(items), prism::Vec2{m_graphButton->rect().x, m_graphButton->rect().bottom()});
    }
}

void EditorAnimGraphPanel::ShowMontageSequencerMenu(const prism::Vec2 at) {
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const Graph* graph = GraphPtr();
    const Scene::Entity bound = graph != nullptr ? graph->montageSequencer : Scene::kInvalidEntity;
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Check("None", bound == Scene::kInvalidEntity, [this] {
        m_montageSlots.clear();
        Edit([](Graph& g) { g.montageSequencer = Scene::kInvalidEntity; });
    }));
    items.push_back(prism::MenuItem::Sep());
    for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
        if (!em.HasComponent<Scene::MontageSequencerComponent>(object)) {
            continue;
        }
        items.push_back(prism::MenuItem::Check(NameOf(em, object, "MontageSequencer"), bound == object, [this, object] {
            m_montageSlots.clear();
            Edit([object](Graph& g) { g.montageSequencer = object; });
        }));
    }
    prism::showContextMenu(std::move(items), at);
}

void EditorAnimGraphPanel::ShowSequencerMenu(const prism::Vec2 at) {
    const Scene::IEntityManager& em = m_editor.m_services->GetEntityManager();
    const Graph* graph = GraphPtr();
    const Scene::Entity bound = graph != nullptr ? graph->sequencer : Scene::kInvalidEntity;
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Check("Automatic", bound == Scene::kInvalidEntity, [this] {
        m_editor.m_animGraph.sequencer = Scene::kInvalidEntity;
        Edit([](Graph& g) { g.sequencer = Scene::kInvalidEntity; });
    }));
    items.push_back(prism::MenuItem::Sep());
    for (const Scene::Entity object : m_editor.CollectEntityEditorObjects()) {
        if (!em.HasComponent<Scene::AnimationSequencerComponent>(object)) {
            continue;
        }
        items.push_back(prism::MenuItem::Check(NameOf(em, object, "AnimationSequencer"), bound == object, [this, object] {
            m_editor.m_animGraph.sequencer = object;
            Edit([object](Graph& g) { g.sequencer = object; });
        }));
    }
    prism::showContextMenu(std::move(items), at);
}

} // namespace UVE::Editor
