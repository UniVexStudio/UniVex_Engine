// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Graph side panel while a State Machine or a Blend Space is open: everything about
// the picked transition (its conditions, exit phase, start, fade and curve, interruptible, on,
// priority), the picked state (its name, what it plays, Start Here), the Blend Space's tool,
// snapping and area, and the picked point (its animation, speed, loop and place).

#include "editor_prism_anim_graph_panel.h"

#include "editor_prism_chrome.h"

#include "animation_graph_editing.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"

#include <algorithm>
#include <string>
#include <vector>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Condition = Scene::AnimationConditionUVE;

[[nodiscard]] prism::Ref<prism::Widget> TitleUVE(const std::string& title) {
    auto label = prism::make<prism::Label>(title, prism::FontRole::Semibold);
    label->setPadding(prism::Margin(8.0F, 10.0F, 8.0F, 4.0F));
    return label;
}

[[nodiscard]] prism::Ref<prism::Widget> HintUVE(const std::string& text) {
    auto label = prism::make<prism::Label>(text, prism::FontRole::Small);
    label->setColor(prism::ThemeColor(&prism::Theme::textDim));
    label->setPadding(prism::Margin(10.0F, 0.0F, 10.0F, 2.0F));
    label->setTooltip(text);
    return label;
}

[[nodiscard]] prism::Ref<prism::Widget> LabelledUVE(const std::string& label, const prism::Ref<prism::Widget>& control) {
    auto row = prism::hbox(6.0F);
    row->setPadding(prism::Margin(10.0F, 0.0F, 12.0F, 0.0F));
    auto text = prism::make<prism::Label>(label, prism::FontRole::Small);
    text->setColor(prism::ThemeColor(&prism::Theme::textDim));
    auto textBox = prism::make<prism::SizeBox>();
    textBox->setWidthOverride(78.0F);
    textBox->setContent(text);
    row->add(textBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    row->add(control, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    return row;
}

[[nodiscard]] bool NeedsParameterUVE(const Condition condition) noexcept {
    return condition != Condition::Always && condition != Condition::AtEnd;
}

} // namespace

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildStateMachineSideUVE(const GraphUVE& graph, const NodeUVE& machine) {
    auto box = prism::vbox(4.0F);
    const EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    const std::uint32_t id = machine.id;
    const auto stateName = [&graph, &machine](const std::uint32_t slot) -> std::string {
        if (slot == Scene::kAnyAnimationStateUVE) {
            return "Any State";
        }
        if (slot < machine.inputs.size() && machine.inputs[slot] != 0U) {
            const auto child = std::ranges::find(graph.nodes, machine.inputs[slot], &NodeUVE::id);
            if (child != graph.nodes.end() && !child->name.empty()) {
                return child->name;
            }
        }
        return AnimationGraphSlotLabelUVE(Kind::StateMachine, slot);
    };

    // ---- a transition ----------------------------------------------------------------------------
    if (state.pickedTransition >= 0 && static_cast<std::size_t>(state.pickedTransition) < machine.transitions.size()) {
        const auto index = static_cast<std::size_t>(state.pickedTransition);
        const Scene::AnimationGraphTransitionUVE& t = machine.transitions[index];
        box->add(TitleUVE(stateName(t.fromState) + "  ->  " + stateName(t.toState)));
        box->add(HintUVE(DescribeAnimationTransitionUVE(t)));
        const auto editTransition = [this, id, index](const std::function<void(Scene::AnimationGraphTransitionUVE&)>& change) {
            EditNodeUVE(id, [&change, index](NodeUVE& n) {
                if (index < n.transitions.size()) {
                    change(n.transitions[index]);
                }
            });
        };
        auto flags = prism::hbox(10.0F);
        auto enabled = prism::make<prism::CheckBox>("On", t.enabled);
        enabled->onChange = [editTransition](const bool v) { editTransition([v](Scene::AnimationGraphTransitionUVE& x) { x.enabled = v; }); };
        auto interruptible = prism::make<prism::CheckBox>("Interruptible", t.interruptible);
        interruptible->setTooltip("Another transition may cut in while this one fades");
        interruptible->onChange = [editTransition](const bool v) {
            editTransition([v](Scene::AnimationGraphTransitionUVE& x) { x.interruptible = v; });
        };
        flags->add(enabled);
        flags->add(interruptible);
        box->add(LabelledUVE("", flags));

        // Its conditions: all must hold (an empty list waits for nothing).
        box->add(TitleUVE("Conditions  (all must hold)"));
        static const std::vector<std::string> kConditions{"Always", "At End", "Parameter >", "Parameter <", "Parameter true", "Parameter false", "Triggered"};
        for (std::size_t ci = 0U; ci < t.conditions.size(); ++ci) {
            const Scene::AnimationTransitionConditionUVE& condition = t.conditions[ci];
            auto row = prism::hbox(4.0F);
            row->setPadding(prism::Margin(10.0F, 0.0F, 12.0F, 0.0F));
            auto kind = prism::make<prism::ComboBox>(kConditions, static_cast<int>(condition.condition));
            kind->onChange = [editTransition, ci](const int i) {
                editTransition([ci, i](Scene::AnimationGraphTransitionUVE& x) {
                    if (ci < x.conditions.size()) {
                        x.conditions[ci].condition = static_cast<Condition>(std::clamp(i, 0, 6));
                    }
                });
            };
            auto kindBox = prism::make<prism::SizeBox>();
            kindBox->setWidthOverride(112.0F);
            kindBox->setContent(kind);
            row->add(kindBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            if (NeedsParameterUVE(condition.condition)) {
                const bool numeric = condition.condition == Condition::ParameterGreater || condition.condition == Condition::ParameterLess;
                const bool trigger = condition.condition == Condition::Triggered;
                std::vector<std::string> names =
                    numeric ? ParameterNamesUVE({Scene::AnimationParameterTypeUVE::Float})
                            : trigger ? ParameterNamesUVE({Scene::AnimationParameterTypeUVE::Trigger})
                                      : ParameterNamesUVE({Scene::AnimationParameterTypeUVE::Bool, Scene::AnimationParameterTypeUVE::Trigger});
                int selected = 0;
                std::vector<std::string> items{"(pick)"};
                for (std::size_t i = 0U; i < names.size(); ++i) {
                    items.push_back(names[i]);
                    if (names[i] == condition.parameter) {
                        selected = static_cast<int>(i) + 1;
                    }
                }
                auto parameter = prism::make<prism::ComboBox>(items, selected);
                parameter->onChange = [editTransition, ci, names](const int i) {
                    const std::string picked = i <= 0 ? std::string{} : names[static_cast<std::size_t>(i - 1)];
                    editTransition([ci, &picked](Scene::AnimationGraphTransitionUVE& x) {
                        if (ci < x.conditions.size()) {
                            x.conditions[ci].parameter = picked;
                        }
                    });
                };
                row->add(parameter, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
                if (numeric) {
                    auto threshold = prism::make<prism::NumberField>(condition.threshold, 0.01F, 2);
                    threshold->onCommit = [editTransition, ci](const float v) {
                        editTransition([ci, v](Scene::AnimationGraphTransitionUVE& x) {
                            if (ci < x.conditions.size()) {
                                x.conditions[ci].threshold = v;
                            }
                        });
                    };
                    auto thresholdBox = prism::make<prism::SizeBox>();
                    thresholdBox->setWidthOverride(52.0F);
                    thresholdBox->setContent(threshold);
                    row->add(thresholdBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
                }
            } else {
                row->add(prism::make<prism::Spacer>(), prism::Slot{1.0F});
            }
            auto remove = ToolButtonUVE(prism::Icon::Close, "Remove the condition");
            remove->onClick = [editTransition, ci] {
                editTransition([ci](Scene::AnimationGraphTransitionUVE& x) {
                    if (ci < x.conditions.size()) {
                        x.conditions.erase(x.conditions.begin() + static_cast<std::ptrdiff_t>(ci));
                    }
                });
            };
            row->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            box->add(row);
        }
        auto addCondition = prism::make<prism::Button>("Condition", prism::Icon::Plus, prism::ButtonStyle::Flat);
        addCondition->onClick = [editTransition] {
            editTransition([](Scene::AnimationGraphTransitionUVE& x) {
                if (x.conditions.size() < Scene::kMaximumAnimationTransitionConditionsUVE) {
                    x.conditions.push_back(Scene::AnimationTransitionConditionUVE{});
                }
            });
        };
        box->add(LabelledUVE("", addCondition));

        // How it hands over.
        box->add(TitleUVE("Blend"));
        auto fade = prism::make<prism::NumberField>(t.fadeSeconds, 0.01F, 2);
        fade->setRange(0.0F, 10.0F);
        fade->setSuffix(" s");
        fade->onChange = [this, id, index](const float v) {
            LiveNodeUVE(id, [index, v](NodeUVE& n) {
                if (index < n.transitions.size()) {
                    n.transitions[index].fadeSeconds = std::max(0.0F, v);
                }
            });
        };
        fade->onCommit = [this](float) { CommitLiveUVE(); };
        box->add(LabelledUVE("Fade", fade));
        auto curve = prism::make<prism::ComboBox>(std::vector<std::string>{"Linear", "Ease In", "Ease Out", "Ease In Out"}, static_cast<int>(t.curve));
        curve->onChange = [editTransition](const int i) {
            editTransition([i](Scene::AnimationGraphTransitionUVE& x) { x.curve = static_cast<Scene::AnimationTransitionCurveUVE>(std::clamp(i, 0, 3)); });
        };
        box->add(LabelledUVE("Curve", curve));
        auto start = prism::make<prism::ComboBox>(std::vector<std::string>{"Restart", "In step", "Continue"}, static_cast<int>(t.start));
        start->setTooltip("Restart: the new state from its start. In step: at the phase the old one reached. Continue: where it was.");
        start->onChange = [editTransition](const int i) {
            editTransition([i](Scene::AnimationGraphTransitionUVE& x) { x.start = static_cast<Scene::AnimationTransitionStartUVE>(std::clamp(i, 0, 2)); });
        };
        box->add(LabelledUVE("Start", start));
        auto exitRow = prism::hbox(6.0F);
        auto exitOn = prism::make<prism::CheckBox>("At phase", t.exitPhase >= 0.0F);
        exitOn->setTooltip("Wait until the current state reaches this phase (0..1) before leaving");
        exitOn->onChange = [editTransition](const bool v) {
            editTransition([v](Scene::AnimationGraphTransitionUVE& x) { x.exitPhase = v ? std::max(0.0F, x.exitPhase) : -1.0F; });
        };
        auto exitPhase = prism::make<prism::NumberField>(std::max(0.0F, t.exitPhase), 0.01F, 2);
        exitPhase->setRange(0.0F, 1.0F);
        exitPhase->setEnabled(t.exitPhase >= 0.0F);
        exitPhase->onCommit = [editTransition](const float v) {
            editTransition([v](Scene::AnimationGraphTransitionUVE& x) { x.exitPhase = std::clamp(v, 0.0F, 1.0F); });
        };
        exitRow->add(exitOn, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        exitRow->add(exitPhase, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        box->add(LabelledUVE("Exit", exitRow));
        // Priority: earlier transitions win when several could fire.
        auto order = prism::hbox(4.0F);
        auto up = prism::make<prism::Button>("Earlier", prism::Icon::ChevronUp, prism::ButtonStyle::Flat);
        up->setEnabled(index > 0U);
        up->onClick = [this, id, index] {
            if (EditUVE([id, index](GraphUVE& g) { static_cast<void>(MoveAnimationTransitionUVE(g.nodes, id, index, index - 1U)); })) {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(index) - 1;
            }
        };
        auto down = prism::make<prism::Button>("Later", prism::Icon::ChevronDown, prism::ButtonStyle::Flat);
        down->setEnabled(index + 1U < machine.transitions.size());
        down->onClick = [this, id, index] {
            if (EditUVE([id, index](GraphUVE& g) { static_cast<void>(MoveAnimationTransitionUVE(g.nodes, id, index, index + 1U)); })) {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(index) + 1;
            }
        };
        order->add(up, prism::Slot{1.0F});
        order->add(down, prism::Slot{1.0F});
        box->add(LabelledUVE("Priority " + std::to_string(index + 1U), order));
        auto remove = prism::make<prism::Button>("Remove Transition", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->onClick = [this, id, index] {
            EditUVE([id, index](GraphUVE& g) { static_cast<void>(RemoveAnimationTransitionUVE(g.nodes, id, index)); });
            m_editor.m_animGraph.pickedTransition = -1;
        };
        box->add(LabelledUVE("", remove));
        return box;
    }

    // ---- a state ---------------------------------------------------------------------------------
    if (state.pickedState >= 0 && static_cast<std::size_t>(state.pickedState) < machine.inputs.size()) {
        const auto slot = static_cast<std::uint32_t>(state.pickedState);
        box->add(TitleUVE("State  -  " + stateName(slot)));
        const std::uint32_t child = machine.inputs[slot];
        const auto node = std::ranges::find(graph.nodes, child, &NodeUVE::id);
        if (node != graph.nodes.end()) {
            auto name = prism::make<prism::TextInput>(node->name, "Name");
            name->onCommit = [this, child](const std::string& to) { EditNodeUVE(child, [&to](NodeUVE& n) { n.name = to; }); };
            box->add(LabelledUVE("Name", name));
            if (node->kind == Kind::Clip) {
                std::vector<Asset::AssetGuidUVE> choices{Asset::AssetGuidUVE{}};
                std::vector<std::string> names{"None"};
                int selected = 0;
                for (const Asset::AssetGuidUVE guid : ClipChoicesUVE()) {
                    choices.push_back(guid);
                    names.push_back(m_editor.AnimationClipNameUVE(guid));
                    if (guid == node->clip) {
                        selected = static_cast<int>(choices.size()) - 1;
                    }
                }
                auto clip = prism::make<prism::ComboBox>(names, selected);
                clip->onChange = [this, child, choices](const int i) {
                    if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) {
                        const Asset::AssetGuidUVE picked = choices[static_cast<std::size_t>(i)];
                        EditNodeUVE(child, [picked](NodeUVE& n) { n.clip = picked; });
                    }
                };
                box->add(LabelledUVE("Plays", clip));
                auto loop = prism::make<prism::CheckBox>("Loop", node->loop);
                loop->onChange = [this, child](const bool v) { EditNodeUVE(child, [v](NodeUVE& n) { n.loop = v; }); };
                box->add(LabelledUVE("", loop));
            } else {
                box->add(HintUVE(std::string{"Plays a "} + AnimationGraphKindLabelUVE(node->kind) + ": double-click the state to open it"));
            }
        }
        auto startHere = prism::make<prism::Button>(machine.entryState == slot ? "Starts here" : "Start Here", prism::Icon::NodeEntry,
                                                    prism::ButtonStyle::Default);
        startHere->setEnabled(machine.entryState != slot);
        startHere->onClick = [this, id, slot] { EditNodeUVE(id, [slot](NodeUVE& n) { n.entryState = slot; }); };
        box->add(LabelledUVE("", startHere));
        box->add(TitleUVE("Leaves to"));
        bool any = false;
        for (std::size_t i = 0U; i < machine.transitions.size(); ++i) {
            const Scene::AnimationGraphTransitionUVE& t = machine.transitions[i];
            if (t.fromState != slot && t.fromState != Scene::kAnyAnimationStateUVE) {
                continue;
            }
            any = true;
            auto link = prism::make<prism::Button>((t.fromState == slot ? "" : "(any) ") + std::string{"-> "} + stateName(t.toState) + "   " +
                                                       std::to_string(t.conditions.size()) + " conditions",
                                                   prism::Icon::NodeTransition, prism::ButtonStyle::Flat);
            link->onClick = [this, i] {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(i);
                m_editor.m_animGraph.pickedState = -1;
            };
            box->add(LabelledUVE("", link));
        }
        if (!any) {
            box->add(HintUVE("No transitions out yet: Shift + drag from this state to another"));
        }
        auto remove = prism::make<prism::Button>("Remove State", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->onClick = [this, id, slot] {
            EditUVE([id, slot](GraphUVE& g) { static_cast<void>(RemoveAnimationGraphInputSlotUVE(g.nodes, id, slot)); });
            m_editor.m_animGraph.pickedState = -1;
        };
        box->add(LabelledUVE("", remove));
        return box;
    }

    box->add(TitleUVE(machine.name.empty() ? std::string{"State Machine"} : machine.name));
    box->add(HintUVE(std::to_string(machine.inputs.size()) + " states, " + std::to_string(machine.transitions.size()) + " transitions"));
    box->add(HintUVE("Pick a state or an arrow to edit it."));
    box->add(HintUVE("Double-click empty space: a new state."));
    box->add(HintUVE("Shift + drag between states: a transition."));
    return box;
}

prism::Ref<prism::Widget> EditorPrismAnimGraphPanelUVE::BuildBlendSpaceSideUVE(const GraphUVE& graph, const NodeUVE& space) {
    static_cast<void>(graph);
    auto box = prism::vbox(4.0F);
    EditorUVE::AnimationGraphViewStateUVE& state = m_editor.m_animGraph;
    const std::uint32_t id = space.id;
    const bool two = space.kind == Kind::BlendSpace2D;
    box->add(TitleUVE(space.name.empty() ? std::string{AnimationGraphKindLabelUVE(space.kind)} : space.name));
    auto tool = prism::make<prism::Segmented>();
    tool->add("Select", prism::Icon::Select, "Move points, or the cross on empty space");
    tool->add("Add", prism::Icon::Plus, "Click to put a point");
    tool->add("Remove", prism::Icon::Trash, "Click a point to take it away");
    tool->setSelected(std::clamp(state.spaceTool, 0, 2));
    tool->onSelect = [this](const int i) { m_editor.m_animGraph.spaceTool = i; };
    auto toolRow = prism::hbox();
    toolRow->setPadding(prism::Margin(10.0F, 0.0F, 12.0F, 2.0F));
    toolRow->add(tool, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    box->add(toolRow);
    auto snapRow = prism::hbox(6.0F);
    auto snap = prism::make<prism::CheckBox>("Snap", state.snap);
    snap->onChange = [this](const bool v) { m_editor.m_animGraph.snap = v; };
    auto step = prism::make<prism::NumberField>(state.snapStep, 0.01F, 2);
    step->setRange(0.01F, 100.0F);
    step->onCommit = [this](const float v) { m_editor.m_animGraph.snapStep = std::max(0.01F, v); };
    snapRow->add(snap, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
    snapRow->add(step, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
    box->add(LabelledUVE("Grid", snapRow));

    // The area the axes span.
    const auto areaField = [this, id](const float value, const int which) {
        auto field = prism::make<prism::NumberField>(value, 0.05F, 2);
        field->onCommit = [this, id, which](const float v) {
            EditNodeUVE(id, [which, v](NodeUVE& n) {
                switch (which) {
                    case 0: n.areaMin.x = v; break;
                    case 1: n.areaMax.x = v; break;
                    case 2: n.areaMin.y = v; break;
                    default: n.areaMax.y = v; break;
                }
            });
        };
        return field;
    };
    const auto pairRow = [](const prism::Ref<prism::Widget>& a, const prism::Ref<prism::Widget>& b) {
        auto row = prism::hbox(4.0F);
        row->add(a, prism::Slot{1.0F});
        row->add(b, prism::Slot{1.0F});
        return row;
    };
    box->add(LabelledUVE("X from / to", pairRow(areaField(space.areaMin.x, 0), areaField(space.areaMax.x, 1))));
    if (two) {
        box->add(LabelledUVE("Y from / to", pairRow(areaField(space.areaMin.y, 2), areaField(space.areaMax.y, 3))));
    }
    auto smoothing = prism::make<prism::NumberField>(space.smoothingSeconds, 0.01F, 2);
    smoothing->setRange(0.0F, 5.0F);
    smoothing->setSuffix(" s");
    smoothing->setTooltip("How long the position takes to close half the gap to its parameters (0: at once)");
    smoothing->onCommit = [this, id](const float v) { EditNodeUVE(id, [v](NodeUVE& n) { n.smoothingSeconds = std::clamp(v, 0.0F, 5.0F); }); };
    box->add(LabelledUVE("Smoothing", smoothing));

    // The picked point.
    if (state.pickedState >= 0 && static_cast<std::size_t>(state.pickedState) < space.blendPoints.size()) {
        const auto slot = static_cast<std::size_t>(state.pickedState);
        const Scene::AnimationBlendPointUVE& point = space.blendPoints[slot];
        box->add(TitleUVE("Point " + std::to_string(slot + 1U)));
        const auto editPoint = [this, id, slot](const std::function<void(Scene::AnimationBlendPointUVE&)>& change) {
            EditNodeUVE(id, [&change, slot](NodeUVE& n) {
                if (slot < n.blendPoints.size()) {
                    change(n.blendPoints[slot]);
                }
            });
        };
        std::vector<Asset::AssetGuidUVE> choices{Asset::AssetGuidUVE{}};
        std::vector<std::string> names{"None"};
        int selected = 0;
        for (const Asset::AssetGuidUVE guid : ClipChoicesUVE()) {
            choices.push_back(guid);
            names.push_back(m_editor.AnimationClipNameUVE(guid));
            if (guid == point.clip) {
                selected = static_cast<int>(choices.size()) - 1;
            }
        }
        auto clip = prism::make<prism::ComboBox>(names, selected);
        clip->onChange = [editPoint, choices](const int i) {
            if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) {
                const Asset::AssetGuidUVE picked = choices[static_cast<std::size_t>(i)];
                editPoint([picked](Scene::AnimationBlendPointUVE& p) { p.clip = picked; });
            }
        };
        box->add(LabelledUVE("Animation", clip));
        auto speedRow = prism::hbox(6.0F);
        auto speed = prism::make<prism::NumberField>(point.speed, 0.01F, 2);
        speed->setSuffix("x");
        speed->onCommit = [editPoint](const float v) { editPoint([v](Scene::AnimationBlendPointUVE& p) { p.speed = v; }); };
        auto loop = prism::make<prism::CheckBox>("Loop", point.loop);
        loop->onChange = [editPoint](const bool v) { editPoint([v](Scene::AnimationBlendPointUVE& p) { p.loop = v; }); };
        speedRow->add(speed, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        box->add(LabelledUVE("Speed", speedRow));
        box->add(LabelledUVE("", loop));
        const auto placeField = [this, id, slot, point](const bool y) {
            auto field = prism::make<prism::NumberField>(y ? point.position.y : point.position.x, 0.01F, 2);
            field->onCommit = [this, id, slot, point, y](const float v) {
                const Math::Vector2UVE to = y ? Math::Vector2UVE{point.position.x, v} : Math::Vector2UVE{v, point.position.y};
                EditUVE([id, slot, to](GraphUVE& g) { static_cast<void>(MoveBlendSpacePointUVE(g.nodes, id, slot, to)); });
            };
            return field;
        };
        const prism::Ref<prism::Widget> place = two ? prism::Ref<prism::Widget>{pairRow(placeField(false), placeField(true))}
                                                    : prism::Ref<prism::Widget>{placeField(false)};
        box->add(LabelledUVE(two ? "X / Y" : "X", place));
        auto remove = prism::make<prism::Button>("Remove Point", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->onClick = [this, id, slot] {
            EditUVE([id, slot](GraphUVE& g) { static_cast<void>(RemoveBlendSpacePointUVE(g.nodes, id, slot)); });
            m_editor.m_animGraph.pickedState = -1;
        };
        box->add(LabelledUVE("", remove));
    } else {
        box->add(HintUVE(std::to_string(space.blendPoints.size()) + " points. Pick one to set its animation."));
    }
    return box;
}

} // namespace UVE::Editor
