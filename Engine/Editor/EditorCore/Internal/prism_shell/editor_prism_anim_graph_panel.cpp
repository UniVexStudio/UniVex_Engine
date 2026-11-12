// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#include "editor_prism_anim_graph_panel.h"

#include "editor_prism_blend_space_view.h"
#include "editor_prism_chrome.h"
#include "editor_prism_state_machine_view.h"

#include "animation_graph_editing.h"
#include "animation_sequencer_component.h"
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

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;

constexpr std::array<Kind, 16> kAddableKindsUVE{Kind::Clip,       Kind::Blend2,      Kind::BlendSpace1D, Kind::BlendSpace2D,
                                                Kind::Select,     Kind::BlendByBool, Kind::Additive,     Kind::LayeredBlend,
                                                Kind::OneShot,    Kind::TimeScale,   Kind::TimeSeek,     Kind::StateMachine,
                                                Kind::Slot,       Kind::TwoBoneIK,   Kind::ModifyBone,   Kind::LookAt};

[[nodiscard]] prism::Slot CentreSlotUVE() {
    return prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center};
}

[[nodiscard]] std::string NameOfUVE(const Scene::IEntityManagerUVE& em, const Scene::EntityUVE entity, const char* fallback) {
    return em.HasComponentUVE<Scene::NameComponentUVE>(entity) ? em.GetComponentUVE<Scene::NameComponentUVE>(entity).name
                                                               : std::string{fallback};
}

[[nodiscard]] std::string UniqueParameterNameUVE(const Scene::AnimationGraphComponentUVE& graph, const std::string& base) {
    const auto taken = [&graph](const std::string& name) {
        return std::ranges::any_of(graph.parameters, [&name](const Scene::AnimationParameterUVE& p) { return p.name == name; });
    };
    if (!taken(base)) {
        return base;
    }
    for (int i = 2; i < 1000; ++i) {
        const std::string candidate = base + std::to_string(i);
        if (!taken(candidate)) {
            return candidate;
        }
    }
    return base + "_";
}

/// A section title in the side panel.
[[nodiscard]] prism::Ref<prism::Widget> SectionUVE(const std::string& title) {
    auto label = prism::make<prism::Label>(title, prism::FontRole::Semibold);
    label->setPadding(prism::Margin(8.0F, 10.0F, 8.0F, 4.0F));
    return label;
}

/// Holds its content to the width it is given: a long help line or name ends in "..." instead of
/// widening the whole Details column (a ScrollBox otherwise lays its content out at its widest).
class FitWidthUVE final : public prism::Widget {
public:
    explicit FitWidthUVE(prism::Ref<prism::Widget> content) : m_content(std::move(content)) { addChild(m_content); }
    prism::Vec2 measure(const prism::Vec2 avail) override {
        const prism::Vec2 inner = m_content->measure(avail);
        return m_desired = constrain({std::min(inner.x, std::max(1.0F, avail.x)), inner.y});
    }
    void arrange(const prism::Rect r) override {
        m_rect = r;
        m_content->arrange(r);
    }
    void paint(prism::Canvas& c) override { m_content->paint(c); }

private:
    prism::Ref<prism::Widget> m_content;
};

/// A small sparkline of a Float parameter's recent values.
class SparklineUVE final : public prism::Widget {
public:
    explicit SparklineUVE(const std::vector<float>* samples) : m_samples(samples) {}
    void SetSamplesUVE(const std::vector<float>* samples) { m_samples = samples; invalidate(); }
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
        c.polyline(points, t.accent, 1.2F);
    }

private:
    const std::vector<float>* m_samples = nullptr;
};

} // namespace

EditorPrismAnimGraphPanelUVE::EditorPrismAnimGraphPanelUVE(EditorUVE& editor) : m_editor(editor) {}
EditorPrismAnimGraphPanelUVE::~EditorPrismAnimGraphPanelUVE() = default;

const Scene::AnimationGraphComponentUVE* EditorPrismAnimGraphPanelUVE::GraphPtrUVE() const {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (m_view.tree == Scene::kInvalidEntityUVE || !em.IsAliveUVE(m_view.tree) ||
        !em.HasComponentUVE<Scene::AnimationGraphComponentUVE>(m_view.tree)) {
        return nullptr;
    }
    return &em.GetComponentUVE<Scene::AnimationGraphComponentUVE>(m_view.tree);
}

Scene::AnimationGraphComponentUVE* EditorPrismAnimGraphPanelUVE::LiveGraphUVE() {
    Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (m_view.tree == Scene::kInvalidEntityUVE || !em.IsAliveUVE(m_view.tree) ||
        !em.HasComponentUVE<Scene::AnimationGraphComponentUVE>(m_view.tree)) {
        return nullptr;
    }
    return &em.GetComponentUVE<Scene::AnimationGraphComponentUVE>(m_view.tree);
}

// ---------------------------------------------------------------------------------------- build

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildUVE() {
    m_graph = prism::make<prism::BlueprintGraph>();
    prism::BlueprintGraph::Features features;
    features.toolbar = false;
    features.crumbs = false;
    features.minimap = false;
    features.watermark = false;
    features.shortcuts = false;
    features.history = false;   // the editor's undo: every change is a component edit
    m_graph->setFeatures(features);
    m_graph->clear();
    m_graph->setNodeLibrary(NodeLibraryUVE());
    m_graph->onSpawn = [this](const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) { return SpawnUVE(spec, world); };
    m_graph->onCommit = [this](const std::string&) { ApplyGraphEditUVE(); };
    m_graph->onSelectionChanged = [this](const std::vector<int>& ids) {
        if (m_rebuilding) {
            return;
        }
        m_selected.assign(ids.begin(), ids.end());
        m_sideDirty = true;
    };
    m_graph->onNodeDoubleClicked = [this](const int id) {
        const GraphUVE* graph = GraphPtrUVE();
        if (graph == nullptr) {
            return;
        }
        const auto node = std::ranges::find(graph->nodes, static_cast<std::uint32_t>(id), &NodeUVE::id);
        if (node != graph->nodes.end() &&
            (node->kind == Kind::StateMachine || node->kind == Kind::BlendSpace1D || node->kind == Kind::BlendSpace2D)) {
            OpenFocusUVE(node->id);
        }
    };
    m_graph->canConnectHook = [this](const int fromNode, int, const int toNode, const int toPin, std::string* why) {
        const GraphUVE* graph = GraphPtrUVE();
        if (graph == nullptr) {
            return false;
        }
        const bool ok = CanConnectAnimationGraphNodesUVE(graph->nodes, static_cast<std::uint32_t>(toNode), static_cast<std::size_t>(toPin),
                                                         static_cast<std::uint32_t>(fromNode));
        if (!ok && why != nullptr) {
            *why = "That wire would make a loop or does not fit this input";
        }
        return ok;
    };
    // The side panel lives in the Entity Editor's Details column (SideUVE), not beside the graph.
    m_side = prism::make<prism::ScrollBox>();
    auto sideBox = prism::make<prism::SizeBox>();
    auto sideFrame = prism::make<prism::Border>();
    sideFrame->setBackground(prism::ThemeColor(&prism::Theme::panel));
    sideFrame->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(1.0F, 0.0F, 0.0F, 0.0F));
    sideFrame->setChild(m_side);
    sideBox->setContent(sideFrame);
    m_sidePanel = sideBox;

    // The tree, or the State Machine / Blend Space opened from it.
    m_stateView = prism::make<StateMachineViewUVE>(*this);
    m_spaceView = prism::make<BlendSpaceViewUVE>(*this);
    m_canvas = prism::make<prism::StackSwitcher>();
    m_canvas->addPage(m_graph);
    m_canvas->addPage(m_stateView);
    m_canvas->addPage(m_spaceView);
    m_canvas->setActiveIndex(0);
    // The breadcrumb while one is open: Tree > its name.
    auto crumbRow = prism::hbox(4.0F);
    crumbRow->setPadding(prism::Margin(6.0F, 2.0F, 6.0F, 2.0F));
    auto back = prism::make<prism::Button>("Tree", prism::Icon::AnimGraph, prism::ButtonStyle::Flat);
    back->setTooltip("Back to the tree (Esc)");
    back->onClick = [this] { CloseFocusUVE(); };
    crumbRow->add(back, CentreSlotUVE());
    auto chevron = prism::make<prism::Label>(">", prism::FontRole::Semibold);
    chevron->setColor(prism::ThemeColor(&prism::Theme::textDim));
    crumbRow->add(chevron, CentreSlotUVE());
    m_crumbLabel = prism::make<prism::Label>("", prism::FontRole::Semibold);
    crumbRow->add(m_crumbLabel, CentreSlotUVE());
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
    page->add(BuildToolbarUVE());
    page->add(body, prism::Slot{1.0F});
    m_shownFocus = ~0U;
    m_shown.reset();
    m_shownTree = Scene::kInvalidEntityUVE;
    m_sideDirty = true;
    return page;
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildToolbarUVE() {
    auto row = prism::hbox(3.0F);
    row->setPadding(prism::Margin(6.0F, 3.0F, 6.0F, 3.0F));
    m_graphButton = ToolButtonUVE(prism::Icon::AnimGraph, "The AnimationGraph shown (selecting one in Components picks it too)", "Graph");
    m_graphButton->onClick = [this] { ShowGraphMenuUVE(); };
    row->add(m_graphButton, CentreSlotUVE());
    auto add = ToolButtonUVE(prism::Icon::Plus, "Add a node (or right-click the graph, or drag a wire off a pin)", "Add Node");
    add->onClick = [this] {
        if (m_graph) {
            const prism::Rect r = m_graph->rect();
            ShowAddMenuUVE(prism::Vec2{r.x + 12.0F, r.y + 8.0F});
        }
    };
    row->add(add, CentreSlotUVE());
    auto fit = ToolButtonUVE(prism::Icon::Frame, "Frame the whole graph (F)");
    fit->onClick = [this] {
        if (m_graph) {
            m_graph->frameAll();
        }
    };
    row->add(fit, CentreSlotUVE());
    row->add(HSeparatorUVE(), CentreSlotUVE());
    m_previewButton = ToolButtonUVE(prism::Icon::Eye, "Run the graph on the entity's skeleton (runtime pose only, never saved)", "Preview");
    m_previewButton->setCheckable(true);
    m_previewButton->onClick = [this] {
        m_editor.m_animGraph.previewing = !m_editor.m_animGraph.previewing;
        if (!m_editor.m_animGraph.previewing) {
            m_editor.StopAnimationGraphPreviewUVE();
        }
    };
    row->add(m_previewButton, CentreSlotUVE());
    m_playButton = ToolButtonUVE(prism::Icon::Pause, "Pause / play the preview");
    m_playButton->onClick = [this] { m_editor.m_animGraph.previewPaused = !m_editor.m_animGraph.previewPaused; };
    row->add(m_playButton, CentreSlotUVE());
    auto step = ToolButtonUVE(prism::Icon::StepForward, "One frame (while paused)");
    step->onClick = [this] {
        m_editor.m_animGraph.previewPaused = true;
        m_editor.m_animGraph.previewStepOnce = true;
    };
    row->add(step, CentreSlotUVE());
    auto rate = prism::make<prism::ComboBox>(std::vector<std::string>{"0.25x", "0.5x", "1x", "2x"}, 2);
    rate->onChange = [this](const int i) {
        static constexpr std::array<float, 4> kRates{0.25F, 0.5F, 1.0F, 2.0F};
        if (i >= 0 && i < 4) {
            m_editor.m_animGraph.previewRate = kRates[static_cast<std::size_t>(i)];
        }
    };
    auto rateBox = prism::make<prism::SizeBox>();
    rateBox->setWidthOverride(70.0F);
    rateBox->setContent(rate);
    row->add(rateBox, CentreSlotUVE());
    m_problem = prism::make<prism::Label>("", prism::FontRole::Small);
    m_problem->setColor(prism::ThemeColor(&prism::Theme::warning));
    row->add(m_problem, prism::Slot{1.0F, prism::Margin(12.0F, 0.0F, 0.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    auto bar = prism::make<prism::Border>();
    bar->setBackground(prism::ThemeColor(&prism::Theme::header));
    bar->setBorder(prism::ThemeColor(&prism::Theme::border), prism::Margin(0.0F, 0.0F, 0.0F, 1.0F));
    bar->setChild(row);
    return bar;
}

// ------------------------------------------------------------------------------- blueprint view

prism::BlueprintNode EditorPrismAnimGraphPanelUVE::MakeNodeUVE(const NodeUVE& node) const {
    prism::BlueprintNode n;
    n.id = static_cast<int>(node.id);
    const std::string label = AnimationGraphKindLabelUVE(node.kind);
    n.title = node.name.empty() ? label : node.name;
    n.subtitle = node.name.empty() || node.name == label ? std::string{} : label;
    n.category = prism::BlueprintNodeCategory::Animation;
    n.pos = prism::Vec2{node.position.x, node.position.y};
    n.tooltip = AnimationGraphKindHelpUVE(node.kind);
    switch (node.kind) {
        case Kind::Output: n.icon = prism::Icon::NodeOutput; n.category = prism::BlueprintNodeCategory::Event; break;
        case Kind::Clip: n.icon = prism::Icon::NodeClip; break;
        case Kind::Blend2: n.icon = prism::Icon::NodeBlend; break;
        case Kind::BlendSpace1D:
        case Kind::BlendSpace2D: n.icon = prism::Icon::NodeBlendSpace; break;
        case Kind::Additive: n.icon = prism::Icon::NodeMixer; break;
        case Kind::OneShot: n.icon = prism::Icon::NodeFlag; break;
        case Kind::TimeScale: n.icon = prism::Icon::NodeTimer; break;
        case Kind::TimeSeek: n.icon = prism::Icon::NodeClock; break;
        case Kind::StateMachine: n.icon = prism::Icon::StateGraph; n.category = prism::BlueprintNodeCategory::State; break;
        case Kind::Select: n.icon = prism::Icon::NodeSelector; break;
        case Kind::LayeredBlend: n.icon = prism::Icon::Layers; break;
        case Kind::Slot: n.icon = prism::Icon::NodeSlot; break;
        case Kind::TwoBoneIK: n.icon = prism::Icon::NodeBone; break;
        case Kind::ModifyBone: n.icon = prism::Icon::NodeScale; break;
        case Kind::LookAt: n.icon = prism::Icon::Eye; break;
        case Kind::BlendByBool: n.icon = prism::Icon::NodeToggle; break;
    }
    for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
        prism::BlueprintPin pin;
        pin.name = AnimationGraphSlotLabelUVE(node.kind, slot);
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
    n.bodyH = BodyHeightUVE(node);
    n.bodyW = n.bodyH > 0.0F ? 232.0F : 0.0F;
    n.bodySummary = SummaryUVE(node);
    return n;
}

std::vector<prism::BlueprintNodeSpec> EditorPrismAnimGraphPanelUVE::NodeLibraryUVE() const {
    std::vector<prism::BlueprintNodeSpec> library;
    for (const Kind kind : kAddableKindsUVE) {
        prism::BlueprintNodeSpec spec;
        spec.category = kind == Kind::StateMachine                                                ? "State Machines"
                        : kind == Kind::Clip || kind == Kind::Slot                                   ? "Sources"
                        : kind == Kind::TwoBoneIK || kind == Kind::ModifyBone || kind == Kind::LookAt ? "Bones"
                                                                                                      : "Blends";
        spec.name = AnimationGraphKindLabelUVE(kind);
        spec.keywords = AnimationGraphKindHelpUVE(kind);
        spec.make = [this, kind] {
            NodeUVE node;
            node.kind = kind;
            node.name = AnimationGraphKindLabelUVE(kind);
            return MakeNodeUVE(node);
        };
        library.push_back(std::move(spec));
    }
    return library;
}

void EditorPrismAnimGraphPanelUVE::RebuildGraphUVE(const GraphUVE& graph) {
    if (!m_graph) {
        return;
    }
    m_rebuilding = true;
    const bool first = m_shownTree != m_view.tree;
    m_graph->clear();
    for (const NodeUVE& node : graph.nodes) {
        m_graph->addNode(MakeNodeUVE(node));
    }
    for (const NodeUVE& node : graph.nodes) {
        for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
            if (node.inputs[slot] != 0U) {
                m_graph->connectPins(static_cast<int>(node.inputs[slot]), 0, static_cast<int>(node.id), static_cast<int>(slot),
                                     prism::BlueprintPinType::Pose);
            }
        }
    }
    for (const NodeUVE& node : graph.nodes) {
        if (BodyHeightUVE(node) > 0.0F) {
            m_graph->setNodeBody(static_cast<int>(node.id), BuildBodyUVE(node));
        }
    }
    std::erase_if(m_selected, [&graph](const std::uint32_t id) { return std::ranges::none_of(graph.nodes, [id](const NodeUVE& n) { return n.id == id; }); });
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
}

void EditorPrismAnimGraphPanelUVE::ApplyGraphEditUVE() {
    if (m_rebuilding || !m_graph) {
        return;
    }
    const GraphUVE* graph = GraphPtrUVE();
    if (graph == nullptr) {
        return;
    }
    const prism::BlueprintGraph& view = *m_graph;
    static_cast<void>(EditUVE([&view](GraphUVE& g) {
        std::vector<std::uint32_t> gone;
        for (const NodeUVE& node : g.nodes) {
            if (view.findNode(static_cast<int>(node.id)) == nullptr) {
                gone.push_back(node.id);
            }
        }
        if (!gone.empty()) {
            static_cast<void>(DeleteAnimationGraphNodesUVE(g.nodes, gone));
        }
        for (NodeUVE& node : g.nodes) {
            if (const prism::BlueprintNode* shown = view.findNode(static_cast<int>(node.id))) {
                node.position = Math::Vector2UVE{shown->pos.x, shown->pos.y};
            }
            std::fill(node.inputs.begin(), node.inputs.end(), 0U);
        }
        for (const prism::BlueprintWire& wire : view.wires()) {
            for (NodeUVE& node : g.nodes) {
                if (static_cast<int>(node.id) == wire.toNodeId && wire.toPinIndex >= 0 &&
                    static_cast<std::size_t>(wire.toPinIndex) < node.inputs.size()) {
                    node.inputs[static_cast<std::size_t>(wire.toPinIndex)] = static_cast<std::uint32_t>(wire.fromNodeId);
                }
            }
        }
    }));
    // Whatever the component kept (a refused change is dropped), the view shows it next refresh.
    m_shown.reset();
}

int EditorPrismAnimGraphPanelUVE::SpawnUVE(const prism::BlueprintNodeSpec& spec, const prism::Vec2 world) {
    std::optional<Kind> kind;
    for (const Kind candidate : kAddableKindsUVE) {
        if (spec.name == AnimationGraphKindLabelUVE(candidate)) {
            kind = candidate;
        }
    }
    if (!kind.has_value() || !m_graph) {
        return -1;
    }
    std::uint32_t added = 0U;
    if (!EditUVE([&](GraphUVE& g) { added = AddAnimationGraphNodeUVE(g.nodes, *kind, Math::Vector2UVE{world.x, world.y}); }) || added == 0U) {
        return -1;
    }
    const GraphUVE* graph = GraphPtrUVE();
    if (graph == nullptr) {
        return -1;
    }
    const auto node = std::ranges::find(graph->nodes, added, &NodeUVE::id);
    if (node == graph->nodes.end()) {
        return -1;
    }
    m_rebuilding = true;
    m_graph->addNode(MakeNodeUVE(*node));
    if (BodyHeightUVE(*node) > 0.0F) {
        m_graph->setNodeBody(static_cast<int>(added), BuildBodyUVE(*node));
    }
    m_rebuilding = false;
    m_selected = {added};
    m_sideDirty = true;
    return static_cast<int>(added);
}

void EditorPrismAnimGraphPanelUVE::ShowAddMenuUVE(const prism::Vec2 screen) {
    std::vector<prism::MenuItem> items;
    const prism::Rect r = m_graph ? m_graph->rect() : prism::Rect{};
    const prism::Vec2 world = m_graph ? m_graph->screenToWorld(prism::Vec2{r.x + r.w * 0.4F, r.y + r.h * 0.4F}) : prism::Vec2{};
    for (const prism::BlueprintNodeSpec& spec : NodeLibraryUVE()) {
        prism::BlueprintNode look = spec.make();
        items.push_back(prism::MenuItem::Item(spec.name, [this, spec, world] {
            if (SpawnUVE(spec, world) > 0 && m_graph) {
                m_graph->clearSelection();
            }
        }, "", look.icon));
    }
    prism::showContextMenu(std::move(items), screen);
}

// ---------------------------------------------------------------------------------------- edits

bool EditorPrismAnimGraphPanelUVE::EditUVE(const std::function<void(GraphUVE&)>& change) {
    if (m_view.tree == Scene::kInvalidEntityUVE) {
        return false;
    }
    m_sideDirty = true;
    return m_editor.EditAnimationGraphUVE(m_view.tree, change);
}

bool EditorPrismAnimGraphPanelUVE::EditNodeUVE(const std::uint32_t id, const std::function<void(NodeUVE&)>& change) {
    return EditUVE([&](GraphUVE& g) {
        if (auto node = std::ranges::find(g.nodes, id, &NodeUVE::id); node != g.nodes.end()) {
            change(*node);
        }
    });
}

void EditorPrismAnimGraphPanelUVE::LiveNodeUVE(const std::uint32_t id, const std::function<void(NodeUVE&)>& change) {
    GraphUVE* graph = LiveGraphUVE();
    if (graph == nullptr) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_liveBefore = *graph;
    }
    if (auto node = std::ranges::find(graph->nodes, id, &NodeUVE::id); node != graph->nodes.end()) {
        change(*node);
    }
}

void EditorPrismAnimGraphPanelUVE::LiveParameterUVE(const std::size_t index, const float value) {
    GraphUVE* graph = LiveGraphUVE();
    if (graph == nullptr || index >= graph->parameters.size()) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_liveBefore = *graph;
    }
    graph->parameters[index].value = value;
}

void EditorPrismAnimGraphPanelUVE::CommitLiveUVE() {
    GraphUVE* graph = LiveGraphUVE();
    if (graph == nullptr || !m_liveBefore.has_value()) {
        m_liveBefore.reset();
        return;
    }
    // Back to where the gesture started, then the gesture's result as one undo step.
    const GraphUVE after = *graph;
    graph->parameters = m_liveBefore->parameters;
    graph->nodes = m_liveBefore->nodes;
    m_liveBefore.reset();
    static_cast<void>(EditUVE([&after](GraphUVE& g) {
        g.parameters = after.parameters;
        g.nodes = after.nodes;
    }));
}

// -------------------------------------------------------------------------------------- refresh

void EditorPrismAnimGraphPanelUVE::RefreshUVE(const double deltaSeconds, const bool shown) {
    if (!shown || !m_graph) {
        return;
    }
    if (!m_liveBefore.has_value()) {
        m_editor.StepAnimationGraphPreviewUVE(deltaSeconds);
    }
    m_view = m_editor.SyncAnimationGraphViewUVE();
    const GraphUVE* graph = GraphPtrUVE();
    if (graph == nullptr) {
        return;
    }
    SyncFocusUVE();
    if (!m_liveBefore.has_value()) {
        if (m_shownTree != m_view.tree || !m_shown.has_value() || !m_shown->HasSameSettingsUVE(*graph)) {
            RebuildGraphUVE(*graph);
            m_sideDirty = true;
        }
        const EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
        if (m_sideDirty || m_selected != m_shownSelection || state.pickedState != m_shownPickedState ||
            state.pickedTransition != m_shownPickedTransition) {
            RebuildSideUVE(*graph);
        }
    }
    RefreshLiveValuesUVE(*graph);
}

void EditorPrismAnimGraphPanelUVE::RefreshLiveValuesUVE(const GraphUVE& graph) {
    const EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    if (m_graphButton) {
        m_graphButton->setVisible(m_view.graphs.size() > 1U);
        const std::string name = NameOfUVE(em, m_view.tree, "AnimationGraph");
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
        std::string problem = Scene::DescribeAnimationGraphProblemUVE(graph);
        if (problem.empty() && !state.status.empty()) {
            problem = state.status;
        }
        if (problem.empty() && m_editor.ResolveActiveSequencerForGraphUVE(m_view.tree) == Scene::kInvalidEntityUVE) {
            problem = "No AnimationSequencer: add one to the entity (the graph plays its animations)";
        }
        if (problem.empty() && m_view.skeleton == Scene::kInvalidEntityUVE) {
            problem = "No skeleton to preview on: add a Skeleton3D to the entity";
        }
        if (m_problem->text() != problem) {
            m_problem->setText(problem);
        }
    }
    // Parameter values the preview moves.
    for (const ParameterRowUVE& row : m_parameterRows) {
        const auto parameter = std::ranges::find(graph.parameters, row.name, &Scene::AnimationParameterUVE::name);
        if (parameter == graph.parameters.end()) {
            continue;
        }
        if (row.number && !row.number->focused() && std::abs(row.number->value() - parameter->value) > 1.0e-5F && !m_liveBefore.has_value()) {
            row.number->setValue(parameter->value);
        }
        if (row.sparkline) {
            const auto history = state.parameterHistory.find(row.name);
            static_cast<SparklineUVE&>(*row.sparkline).SetSamplesUVE(history != state.parameterHistory.end() ? &history->second : nullptr);
        }
    }
    if (m_log && state.previewLog.size() != m_shownLogLines) {
        m_log->clear();
        for (const EditorUVE::AnimationGraphViewStateUVE::PreviewLogLineUVE& line : state.previewLog) {
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

const Scene::AnimationGraphNodeUVE* EditorPrismAnimGraphPanelUVE::FocusNodeUVE() const {
    const GraphUVE* graph = GraphPtrUVE();
    const std::uint32_t focus = m_editor.m_animGraph.focus;
    if (graph == nullptr || focus == 0U) {
        return nullptr;
    }
    const auto node = std::ranges::find(graph->nodes, focus, &NodeUVE::id);
    return node != graph->nodes.end() ? &*node : nullptr;
}

void EditorPrismAnimGraphPanelUVE::OpenFocusUVE(const std::uint32_t node) {
    EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    state.focus = node;
    state.pickedState = -1;
    state.pickedTransition = -1;
    state.stateFramed = false;
    m_sideDirty = true;
    SyncFocusUVE();
}

void EditorPrismAnimGraphPanelUVE::CloseFocusUVE() {
    EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    state.focus = 0U;
    state.pickedState = -1;
    state.pickedTransition = -1;
    m_sideDirty = true;
    SyncFocusUVE();
}

void EditorPrismAnimGraphPanelUVE::SyncFocusUVE() {
    EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    const NodeUVE* focus = FocusNodeUVE();
    if (state.focus != 0U && (focus == nullptr || (focus->kind != Kind::StateMachine && focus->kind != Kind::BlendSpace1D &&
                                                   focus->kind != Kind::BlendSpace2D))) {
        state.focus = 0U;   // its node went (deleted, undone): back to the tree
        focus = nullptr;
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
    if (m_crumbs) {
        m_crumbs->setVisible(focus != nullptr);
    }
    if (m_crumbLabel && focus != nullptr) {
        const std::string name = (focus->name.empty() ? std::string{AnimationGraphKindLabelUVE(focus->kind)} : focus->name) + "   (" +
                                 AnimationGraphKindLabelUVE(focus->kind) + ")";
        if (m_crumbLabel->text() != name) {
            m_crumbLabel->setText(name);
        }
    }
    if (m_shownFocus != state.focus) {
        m_shownFocus = state.focus;
        m_sideDirty = true;
        if (prism::Application::exists()) {
            prism::Application::get().invalidateLayout();
        }
    }
}

// ----------------------------------------------------------------------------------- side panel

void EditorPrismAnimGraphPanelUVE::RebuildSideUVE(const GraphUVE& graph) {
    if (!m_side) {
        return;
    }
    m_sideDirty = false;
    m_shownSelection = m_selected;
    m_parameterRows.clear();
    m_shownLogLines = static_cast<std::size_t>(-1);
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    auto page = prism::vbox(2.0F);
    page->setPadding(prism::Margin(0.0F, 0.0F, 0.0F, 10.0F));

    // The graph: which sequencer's clips it offers.
    page->add(SectionUVE("Graph"));
    {
        auto row = prism::hbox(6.0F);
        row->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 0.0F));
        auto label = prism::make<prism::Label>("Sequencer", prism::FontRole::Small);
        label->setColor(prism::ThemeColor(&prism::Theme::textDim));
        row->add(label, CentreSlotUVE());
        const Scene::EntityUVE sequencer = m_editor.ResolveActiveSequencerForGraphUVE(m_view.tree);
        const std::string name = sequencer != Scene::kInvalidEntityUVE ? NameOfUVE(em, sequencer, "AnimationSequencer") : std::string{"None"};
        auto pick = prism::make<prism::Button>(name, prism::Icon::ChevronDown, prism::ButtonStyle::Default);
        pick->setTooltip("The AnimationSequencer whose animations the Clip nodes pick from (" +
                         std::to_string(m_editor.GetSequencerClipsUVE(sequencer).size()) + " clips)");
        std::weak_ptr<prism::Button> weak = pick;
        pick->onClick = [this, weak] {
            if (auto b = weak.lock()) {
                ShowSequencerMenuUVE(prism::Vec2{b->rect().x, b->rect().bottom()});
            }
        };
        row->add(pick, prism::Slot{1.0F});
        page->add(row);
        auto skeleton = prism::make<prism::Label>(
            m_view.skeleton != Scene::kInvalidEntityUVE ? "Previews on " + NameOfUVE(em, m_view.skeleton, "Skeleton3D") : "No skeleton to preview on",
            prism::FontRole::Small);
        skeleton->setColor(prism::ThemeColor(&prism::Theme::textDim));
        skeleton->setPadding(prism::Margin(10.0F, 2.0F, 10.0F, 0.0F));
        page->add(skeleton);
    }
    m_shownPickedState = m_editor.m_animGraph.pickedState;
    m_shownPickedTransition = m_editor.m_animGraph.pickedTransition;
    if (const NodeUVE* focus = FocusNodeUVE(); focus != nullptr && focus->kind == Kind::StateMachine) {
        page->add(BuildStateMachineSideUVE(graph, *focus));
        page->add(BuildParametersUVE(graph));
    } else if (focus != nullptr) {
        page->add(BuildBlendSpaceSideUVE(graph, *focus));
        page->add(BuildParametersUVE(graph));
    } else {
        page->add(BuildParametersUVE(graph));
        page->add(BuildSelectedUVE(graph));
    }
    page->add(BuildPreviewUVE());
    m_side->setContent(prism::make<FitWidthUVE>(page));
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildParametersUVE(const GraphUVE& graph) {
    auto box = prism::vbox(3.0F);
    auto header = prism::hbox(4.0F);
    header->add(SectionUVE("Parameters  (" + std::to_string(graph.parameters.size()) + ")"), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    auto add = ToolButtonUVE(prism::Icon::Plus, "Add a parameter", "Add");
    std::weak_ptr<prism::Button> weakAdd = add;
    add->onClick = [this, weakAdd] {
        const auto addOf = [this](const Scene::AnimationParameterTypeUVE type, const char* base) {
            return [this, type, base] {
                EditUVE([type, base](GraphUVE& g) {
                    Scene::AnimationParameterUVE p;
                    p.name = UniqueParameterNameUVE(g, base);
                    p.type = type;
                    g.parameters.push_back(std::move(p));
                });
            };
        };
        std::vector<prism::MenuItem> items{prism::MenuItem::Item("Float", addOf(Scene::AnimationParameterTypeUVE::Float, "Speed"), "", prism::Icon::NodeSlider),
                                           prism::MenuItem::Item("Vector3", addOf(Scene::AnimationParameterTypeUVE::Vector3, "Target"), "", prism::Icon::Move),
                                           prism::MenuItem::Item("Bool", addOf(Scene::AnimationParameterTypeUVE::Bool, "IsMoving"), "", prism::Icon::NodeToggle),
                                           prism::MenuItem::Item("Trigger", addOf(Scene::AnimationParameterTypeUVE::Trigger, "Attack"), "", prism::Icon::NodeFlag)};
        if (auto b = weakAdd.lock()) {
            prism::showContextMenu(std::move(items), prism::Vec2{b->rect().x, b->rect().bottom()});
        }
    };
    header->add(add, prism::Slot{0.0F, prism::Margin(0.0F, 6.0F, 8.0F, 0.0F), prism::HAlign::Right, prism::VAlign::Center});
    box->add(header);
    if (graph.parameters.empty()) {
        auto none = prism::make<prism::Label>("None yet. Scripts set them: Speed, IsMoving...", prism::FontRole::Small);
        none->setColor(prism::ThemeColor(&prism::Theme::textDim));
        none->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 0.0F));
        box->add(none);
    }
    for (std::size_t index = 0U; index < graph.parameters.size(); ++index) {
        const Scene::AnimationParameterUVE& parameter = graph.parameters[index];
        auto row = prism::hbox(4.0F);
        row->setPadding(prism::Margin(8.0F, 0.0F, 14.0F, 0.0F));
        auto name = prism::make<prism::TextInput>(parameter.name, "Name");
        const std::string old = parameter.name;
        name->onCommit = [this, old](const std::string& to) {
            if (to.empty() || to == old) {
                return;
            }
            // References follow the rename: nodes and transitions read the parameter by name.
            EditUVE([&old, &to](GraphUVE& g) {
                for (Scene::AnimationParameterUVE& p : g.parameters) {
                    if (p.name == old) {
                        p.name = to;
                    }
                }
                for (NodeUVE& node : g.nodes) {
                    if (node.parameter == old) {
                        node.parameter = to;
                    }
                    if (node.parameterY == old) {
                        node.parameterY = to;
                    }
                    for (Scene::AnimationGraphTransitionUVE& t : node.transitions) {
                        for (Scene::AnimationTransitionConditionUVE& c : t.conditions) {
                            if (c.parameter == old) {
                                c.parameter = to;
                            }
                        }
                    }
                }
            });
        };
        auto nameBox = prism::make<prism::SizeBox>();
        nameBox->setWidthOverride(84.0F);
        nameBox->setContent(name);
        row->add(nameBox, CentreSlotUVE());
        auto type = prism::make<prism::ComboBox>(std::vector<std::string>{"Float", "Bool", "Trigger", "Vector3"}, static_cast<int>(parameter.type));
        type->onChange = [this, index](const int i) {
            EditUVE([index, i](GraphUVE& g) {
                if (index < g.parameters.size()) {
                    g.parameters[index].type = static_cast<Scene::AnimationParameterTypeUVE>(std::clamp(i, 0, 3));
                    g.parameters[index].value = 0.0F;
                }
            });
        };
        auto typeBox = prism::make<prism::SizeBox>();
        typeBox->setWidthOverride(64.0F);
        typeBox->setContent(type);
        row->add(typeBox, CentreSlotUVE());
        ParameterRowUVE live{parameter.name, nullptr, nullptr};
        switch (parameter.type) {
            case Scene::AnimationParameterTypeUVE::Float: {
                auto number = prism::make<prism::NumberField>(parameter.value, 0.01F, 2);
                number->onChange = [this, index](const float v) { LiveParameterUVE(index, v); };
                number->onCommit = [this](float) { CommitLiveUVE(); };
                row->add(number, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
                auto spark = prism::make<SparklineUVE>(nullptr);
                row->add(spark, CentreSlotUVE());
                live.number = number;
                live.sparkline = spark;
                break;
            }
            case Scene::AnimationParameterTypeUVE::Bool: {
                auto check = prism::make<prism::CheckBox>("", parameter.value > 0.5F);
                check->onChange = [this, index](const bool on) {
                    EditUVE([index, on](GraphUVE& g) {
                        if (index < g.parameters.size()) {
                            g.parameters[index].value = on ? 1.0F : 0.0F;
                        }
                    });
                };
                row->add(check, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                break;
            }
            case Scene::AnimationParameterTypeUVE::Vector3: {
                auto xyz = prism::hbox(2.0F);
                const float values[] = {parameter.vector.x, parameter.vector.y, parameter.vector.z};
                for (int axis = 0; axis < 3; ++axis) {
                    auto number = prism::make<prism::NumberField>(values[axis], 0.01F, 2);
                    number->onCommit = [this, index, axis](const float v) {
                        EditUVE([index, axis, v](GraphUVE& g) {
                            if (index < g.parameters.size()) {
                                Math::Vector3UVE& at = g.parameters[index].vector;
                                (axis == 0 ? at.x : axis == 1 ? at.y : at.z) = v;
                            }
                        });
                    };
                    xyz->add(number, prism::Slot{1.0F});
                }
                row->add(xyz, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
                break;
            }
            case Scene::AnimationParameterTypeUVE::Trigger: {
                auto fire = ToolButtonUVE(prism::Icon::NodeFlag, "Fire it now (the preview consumes it)", "Fire");
                fire->onClick = [this, index] {
                    if (GraphUVE* g = LiveGraphUVE(); g != nullptr && index < g->parameters.size()) {
                        g->parameters[index].value = 1.0F;   // runtime only: the step consumes it
                    }
                };
                row->add(fire, prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                break;
            }
        }
        auto remove = ToolButtonUVE(prism::Icon::Trash, "Remove the parameter");
        remove->onClick = [this, index] {
            EditUVE([index](GraphUVE& g) {
                if (index < g.parameters.size()) {
                    g.parameters.erase(g.parameters.begin() + static_cast<std::ptrdiff_t>(index));
                }
            });
        };
        row->add(remove, CentreSlotUVE());
        box->add(row);
        // Who writes it: a script, or the engine from the character (its speed, heading, air).
        if (parameter.type == Scene::AnimationParameterTypeUVE::Float || parameter.type == Scene::AnimationParameterTypeUVE::Bool) {
            auto sourceRow = prism::hbox(4.0F);
            sourceRow->setPadding(prism::Margin(8.0F, 0.0F, 14.0F, 4.0F));
            auto label = prism::make<prism::Label>("Set by", prism::FontRole::Small);
            label->setColor(prism::ThemeColor(&prism::Theme::textDim));
            auto labelBox = prism::make<prism::SizeBox>();
            labelBox->setWidthOverride(84.0F);
            labelBox->setContent(label);
            auto source = prism::make<prism::ComboBox>(
                std::vector<std::string>{"Script", "Character speed", "Character direction", "Character in air", "Character grounded"},
                static_cast<int>(parameter.source));
            source->setTooltip("From Character: the engine writes it every frame from the Character Controller the graph animates");
            source->onChange = [this, index](const int i) {
                EditUVE([index, i](GraphUVE& g) {
                    if (index < g.parameters.size()) {
                        g.parameters[index].source = static_cast<Scene::AnimationParameterSourceUVE>(std::clamp(i, 0, 4));
                    }
                });
            };
            sourceRow->add(labelBox, CentreSlotUVE());
            sourceRow->add(source, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
            box->add(sourceRow);
        }
        m_parameterRows.push_back(std::move(live));
    }
    return box;
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildSelectedUVE(const GraphUVE& graph) {
    auto box = prism::vbox(3.0F);
    if (m_selected.size() != 1U) {
        box->add(SectionUVE("Node"));
        auto hint = prism::make<prism::Label>(m_selected.empty() ? "Pick a node to name it and see what it does" : std::to_string(m_selected.size()) + " nodes picked",
                                              prism::FontRole::Small);
        hint->setColor(prism::ThemeColor(&prism::Theme::textDim));
        hint->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 0.0F));
        box->add(hint);
        return box;
    }
    const std::uint32_t id = m_selected.front();
    const auto found = std::ranges::find(graph.nodes, id, &NodeUVE::id);
    if (found == graph.nodes.end()) {
        return box;
    }
    const NodeUVE& node = *found;
    box->add(SectionUVE(std::string{"Node  -  "} + AnimationGraphKindLabelUVE(node.kind)));
    const auto labelled = [&box](const std::string& label, const prism::Ref<prism::Widget>& control) {
        auto row = prism::hbox(6.0F);
        row->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 0.0F));
        auto text = prism::make<prism::Label>(label, prism::FontRole::Small);
        text->setColor(prism::ThemeColor(&prism::Theme::textDim));
        auto textBox = prism::make<prism::SizeBox>();
        textBox->setWidthOverride(70.0F);
        textBox->setContent(text);
        row->add(textBox, CentreSlotUVE());
        row->add(control, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        box->add(row);
    };
    if (node.kind != Scene::AnimationGraphNodeKindUVE::Output) {
        auto name = prism::make<prism::TextInput>(node.name, "Name");
        name->onCommit = [this, id](const std::string& to) { EditNodeUVE(id, [&to](NodeUVE& n) { n.name = to; }); };
        labelled("Name", name);
    }
    auto help = prism::make<prism::Label>(AnimationGraphKindHelpUVE(node.kind), prism::FontRole::Small);
    help->setColor(prism::ThemeColor(&prism::Theme::textDim));
    help->setTooltip(AnimationGraphKindHelpUVE(node.kind));
    help->setPadding(prism::Margin(10.0F, 2.0F, 10.0F, 2.0F));
    box->add(help);

    // A Layered Blend's bones: the branches its second pose replaces, each with its children.
    if (node.kind == Scene::AnimationGraphNodeKindUVE::LayeredBlend) {
        for (std::size_t i = 0U; i < node.bones.size(); ++i) {
            auto remove = ToolButtonUVE(prism::Icon::Close, "Take this branch off");
            remove->onClick = [this, id, i] {
                EditNodeUVE(id, [i](NodeUVE& n) {
                    if (i < n.bones.size()) {
                        n.bones.erase(n.bones.begin() + static_cast<std::ptrdiff_t>(i));
                    }
                });
            };
            auto row = prism::hbox(4.0F);
            row->add(prism::make<prism::Label>(node.bones[i], prism::FontRole::Regular), prism::Slot{1.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            row->add(remove, CentreSlotUVE());
            labelled(i == 0U ? "Bones" : "", row);
        }
        std::vector<std::string> bones{"Add a bone..."};
        const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
        if (m_view.skeleton != Scene::kInvalidEntityUVE && em.HasComponentUVE<Scene::Skeleton3DComponentUVE>(m_view.skeleton)) {
            for (const Scene::SkeletonBoneUVE& bone : em.GetComponentUVE<Scene::Skeleton3DComponentUVE>(m_view.skeleton).bones) {
                if (std::ranges::find(node.bones, bone.name) == node.bones.end()) {
                    bones.push_back(bone.name);
                }
            }
        }
        auto pick = prism::make<prism::ComboBox>(bones, 0);
        pick->onChange = [this, id, bones](const int i) {
            if (i > 0 && static_cast<std::size_t>(i) < bones.size()) {
                const std::string bone = bones[static_cast<std::size_t>(i)];
                EditNodeUVE(id, [&bone](NodeUVE& n) {
                    if (n.bones.size() < Scene::kMaximumAnimationLayerBonesUVE) {
                        n.bones.push_back(bone);
                    }
                });
            }
        };
        labelled(node.bones.empty() ? "Bones" : "", pick);
    }
    if (node.kind != Scene::AnimationGraphNodeKindUVE::Output) {
        auto actions = prism::hbox(4.0F);
        auto duplicate = ToolButtonUVE(prism::Icon::Duplicate, "Duplicate the node", "Duplicate");
        duplicate->onClick = [this, id] {
            EditUVE([id](GraphUVE& g) {
                const std::vector<std::uint32_t> copies = DuplicateAnimationGraphNodesUVE(g.nodes, {id}, Math::Vector2UVE{40.0F, 40.0F});
                static_cast<void>(copies);
            });
        };
        auto remove = ToolButtonUVE(prism::Icon::Trash, "Delete the node (Delete)", "Delete");
        remove->onClick = [this, id] {
            m_selected.clear();
            EditUVE([id](GraphUVE& g) { static_cast<void>(DeleteAnimationGraphNodesUVE(g.nodes, {id})); });
        };
        actions->add(duplicate, CentreSlotUVE());
        actions->add(remove, CentreSlotUVE());
        labelled("", actions);
    }
    return box;
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildPreviewUVE() {
    auto box = prism::vbox(3.0F);
    box->add(SectionUVE("Preview log"));
    m_log = prism::make<prism::LogView>();
    auto logBox = prism::make<prism::SizeBox>();
    logBox->setHeightOverride(150.0F);
    logBox->setContent(m_log);
    auto frame = prism::hbox();
    frame->setPadding(prism::Margin(8.0F, 0.0F, 8.0F, 0.0F));
    frame->add(logBox, prism::Slot{1.0F});
    box->add(frame);
    return box;
}

// ---------------------------------------------------------------------------------------- menus

void EditorPrismAnimGraphPanelUVE::ShowGraphMenuUVE() {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    std::vector<prism::MenuItem> items;
    for (const Scene::EntityUVE graph : m_view.graphs) {
        items.push_back(prism::MenuItem::Check(NameOfUVE(em, graph, "AnimationGraph"), graph == m_view.tree,
                                               [this, graph] { m_editor.SelectEntityUVE(graph); }));
    }
    if (m_graphButton) {
        prism::showContextMenu(std::move(items), prism::Vec2{m_graphButton->rect().x, m_graphButton->rect().bottom()});
    }
}

void EditorPrismAnimGraphPanelUVE::ShowSequencerMenuUVE(const prism::Vec2 at) {
    const Scene::IEntityManagerUVE& em = m_editor.m_services->GetEntityManagerUVE();
    const GraphUVE* graph = GraphPtrUVE();
    const Scene::EntityUVE bound = graph != nullptr ? graph->sequencer : Scene::kInvalidEntityUVE;
    std::vector<prism::MenuItem> items;
    items.push_back(prism::MenuItem::Check("Automatic (the tree's own, its target's or its parent's)", bound == Scene::kInvalidEntityUVE, [this] {
        m_editor.m_animGraph.sequencer = Scene::kInvalidEntityUVE;
        EditUVE([](GraphUVE& g) { g.sequencer = Scene::kInvalidEntityUVE; });
    }));
    items.push_back(prism::MenuItem::Sep());
    for (const Scene::EntityUVE object : m_editor.CollectEntityEditorObjectsUVE()) {
        if (!em.HasComponentUVE<Scene::AnimationSequencerComponentUVE>(object)) {
            continue;
        }
        const std::string label =
            NameOfUVE(em, object, "AnimationSequencer") + "  (" + std::to_string(m_editor.GetSequencerClipsUVE(object).size()) + " clips)";
        items.push_back(prism::MenuItem::Check(label, bound == object, [this, object] {
            m_editor.m_animGraph.sequencer = object;
            EditUVE([object](GraphUVE& g) { g.sequencer = object; });
        }));
    }
    prism::showContextMenu(std::move(items), at);
}

} // namespace UVE::Editor
