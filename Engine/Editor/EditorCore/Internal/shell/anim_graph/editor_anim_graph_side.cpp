// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

// The Animation Graph side panel while a State Machine or a Blend Space is open: everything about
// the picked transition (its conditions, exit phase, start, fade and curve, interruptible, on,
// priority), the picked state (its name, what it plays, Start Here), the Blend Space's tool,
// snapping and area, and the picked point (its animation, speed, loop and place).

#include "editor_anim_graph_panel.h"

#include "editor_chrome.h"

#include "animation_graph_editing.h"

#include "prism/Containers.h"
#include "prism/Controls.h"
#include "prism/Layouts.h"
#include "prism/TextInput.h"
#include "prism/Theme.h"
#include "prism/Views.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKind;
using Condition = Scene::AnimationCondition;

[[nodiscard]] bool NeedsParameter(const Condition condition) noexcept {
    return condition != Condition::Always && condition != Condition::AtEnd;
}

} // namespace

void EditorAnimGraphPanel::BuildStateMachineSide(prism::PropertyGrid& grid, const Graph& graph, const Node& machine) {
    int cat = -1;
    const Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const std::uint32_t id = machine.id;
    const auto stateName = [&graph, &machine](const std::uint32_t slot) -> std::string {
        if (slot == Scene::kAnyAnimationState) {
            return "Any State";
        }
        return AnimationStateName(graph.nodes, machine, slot);
    };

    // ---- a transition ----------------------------------------------------------------------------
    if (state.pickedTransition >= 0 && static_cast<std::size_t>(state.pickedTransition) < machine.transitions.size()) {
        const auto index = static_cast<std::size_t>(state.pickedTransition);
        const Scene::AnimationGraphTransition& t = machine.transitions[index];
        cat = grid.addCategory(stateName(t.fromState) + "  ->  " + stateName(t.toState));
        const auto editTransition = [this, id, index](const std::function<void(Scene::AnimationGraphTransition&)>& change) {
            EditNode(id, [&change, index](Node& n) {
                if (index < n.transitions.size()) {
                    change(n.transitions[index]);
                }
            });
        };
        auto flags = prism::hbox(10.0F);
        auto enabled = prism::make<prism::CheckBox>("On", t.enabled);
        enabled->onChange = [editTransition](const bool v) { editTransition([v](Scene::AnimationGraphTransition& x) { x.enabled = v; }); };
        auto interruptible = prism::make<prism::CheckBox>("Interruptible", t.interruptible);
        interruptible->setTooltip("Another transition may cut in while this one fades");
        interruptible->onChange = [editTransition](const bool v) {
            editTransition([v](Scene::AnimationGraphTransition& x) { x.interruptible = v; });
        };
        flags->add(enabled);
        flags->add(interruptible);
        grid.addRow(cat, "", flags);

        // Its conditions: all must hold (an empty list waits for nothing).
        cat = grid.addCategory("Conditions");
        static const std::vector<std::string> kConditions{"Always", "At End", "Parameter >", "Parameter <", "Parameter true", "Parameter false", "Triggered"};
        for (std::size_t ci = 0U; ci < t.conditions.size(); ++ci) {
            const Scene::AnimationTransitionCondition& condition = t.conditions[ci];
            auto row = prism::hbox(4.0F);
            row->setPadding(prism::Margin(10.0F, 0.0F, 12.0F, 0.0F));
            auto kind = prism::make<prism::ComboBox>(kConditions, static_cast<int>(condition.condition));
            kind->onChange = [editTransition, ci](const int i) {
                editTransition([ci, i](Scene::AnimationGraphTransition& x) {
                    if (ci < x.conditions.size()) {
                        x.conditions[ci].condition = static_cast<Condition>(std::clamp(i, 0, 6));
                    }
                });
            };
            auto kindBox = prism::make<prism::SizeBox>();
            kindBox->setWidthOverride(112.0F);
            kindBox->setContent(kind);
            row->add(kindBox, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            if (NeedsParameter(condition.condition)) {
                const bool numeric = condition.condition == Condition::ParameterGreater || condition.condition == Condition::ParameterLess;
                const bool trigger = condition.condition == Condition::Triggered;
                std::vector<std::string> names =
                    numeric ? ParameterNames({Scene::AnimationParameterType::Float})
                            : trigger ? ParameterNames({Scene::AnimationParameterType::Trigger})
                                      : ParameterNames({Scene::AnimationParameterType::Bool, Scene::AnimationParameterType::Trigger});
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
                    editTransition([ci, &picked](Scene::AnimationGraphTransition& x) {
                        if (ci < x.conditions.size()) {
                            x.conditions[ci].parameter = picked;
                        }
                    });
                };
                row->add(parameter, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
                if (numeric) {
                    auto threshold = prism::make<prism::NumberField>(condition.threshold, 0.01F, 2);
                    threshold->onCommit = [editTransition, ci](const float v) {
                        editTransition([ci, v](Scene::AnimationGraphTransition& x) {
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
            auto remove = ToolButton(prism::Icon::Close, "Remove the condition");
            remove->onClick = [editTransition, ci] {
                editTransition([ci](Scene::AnimationGraphTransition& x) {
                    if (ci < x.conditions.size()) {
                        x.conditions.erase(x.conditions.begin() + static_cast<std::ptrdiff_t>(ci));
                    }
                });
            };
            row->add(remove, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
            grid.addRow(cat, "If", row);
        }
        auto addCondition = prism::make<prism::Button>("Condition", prism::Icon::Plus, prism::ButtonStyle::Flat);
        addCondition->onClick = [editTransition] {
            editTransition([](Scene::AnimationGraphTransition& x) {
                if (x.conditions.size() < Scene::kMaximumAnimationTransitionConditions) {
                    x.conditions.push_back(Scene::AnimationTransitionCondition{});
                }
            });
        };
        grid.addRow(cat, "", addCondition);

        // How it hands over.
        cat = grid.addCategory("Blend");
        auto fade = prism::make<prism::NumberField>(t.fadeSeconds, 0.01F, 2);
        fade->setRange(0.0F, 10.0F);
        fade->setSuffix(" s");
        fade->onChange = [this, id, index](const float v) {
            LiveNode(id, [index, v](Node& n) {
                if (index < n.transitions.size()) {
                    n.transitions[index].fadeSeconds = std::max(0.0F, v);
                }
            });
        };
        fade->onCommit = [this](float) { CommitLive(); };
        grid.addRow(cat, "Fade", fade);
        auto curve = prism::make<prism::ComboBox>(std::vector<std::string>{"Linear", "Ease In", "Ease Out", "Ease In Out"}, static_cast<int>(t.curve));
        curve->onChange = [editTransition](const int i) {
            editTransition([i](Scene::AnimationGraphTransition& x) { x.curve = static_cast<Scene::AnimationTransitionCurve>(std::clamp(i, 0, 3)); });
        };
        grid.addRow(cat, "Curve", curve);
        auto start = prism::make<prism::ComboBox>(std::vector<std::string>{"Restart", "In step", "Continue"}, static_cast<int>(t.start));
        start->setTooltip("Restart: the new state from its start. In step: at the phase the old one reached. Continue: where it was.");
        start->onChange = [editTransition](const int i) {
            editTransition([i](Scene::AnimationGraphTransition& x) { x.start = static_cast<Scene::AnimationTransitionStart>(std::clamp(i, 0, 2)); });
        };
        grid.addRow(cat, "Start", start);
        auto exitRow = prism::hbox(6.0F);
        auto exitOn = prism::make<prism::CheckBox>("At phase", t.exitPhase >= 0.0F);
        exitOn->setTooltip("Wait until the current state reaches this phase (0..1) before leaving");
        exitOn->onChange = [editTransition](const bool v) {
            editTransition([v](Scene::AnimationGraphTransition& x) { x.exitPhase = v ? std::max(0.0F, x.exitPhase) : -1.0F; });
        };
        auto exitPhase = prism::make<prism::NumberField>(std::max(0.0F, t.exitPhase), 0.01F, 2);
        exitPhase->setRange(0.0F, 1.0F);
        exitPhase->setEnabled(t.exitPhase >= 0.0F);
        exitPhase->onCommit = [editTransition](const float v) {
            editTransition([v](Scene::AnimationGraphTransition& x) { x.exitPhase = std::clamp(v, 0.0F, 1.0F); });
        };
        exitRow->add(exitOn, prism::Slot{0.0F, {}, prism::HAlign::Left, prism::VAlign::Center});
        exitRow->add(exitPhase, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        grid.addRow(cat, "Exit", exitRow);
        // Priority: earlier transitions win when several could fire.
        auto order = prism::hbox(4.0F);
        auto up = prism::make<prism::Button>("Earlier", prism::Icon::ChevronUp, prism::ButtonStyle::Flat);
        up->setEnabled(index > 0U);
        up->onClick = [this, id, index] {
            if (Edit([id, index](Graph& g) { static_cast<void>(MoveAnimationTransition(g.nodes, id, index, index - 1U)); })) {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(index) - 1;
            }
        };
        auto down = prism::make<prism::Button>("Later", prism::Icon::ChevronDown, prism::ButtonStyle::Flat);
        down->setEnabled(index + 1U < machine.transitions.size());
        down->onClick = [this, id, index] {
            if (Edit([id, index](Graph& g) { static_cast<void>(MoveAnimationTransition(g.nodes, id, index, index + 1U)); })) {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(index) + 1;
            }
        };
        order->add(up, prism::Slot{1.0F});
        order->add(down, prism::Slot{1.0F});
        grid.addRow(cat, "Priority", order);
        auto remove = prism::make<prism::Button>("Remove Transition", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->onClick = [this, id, index] {
            Edit([id, index](Graph& g) { static_cast<void>(RemoveAnimationTransition(g.nodes, id, index)); });
            m_editor.m_animGraph.pickedTransition = -1;
        };
        grid.addRow(cat, "", remove);
        return;
    }

    // ---- a state ---------------------------------------------------------------------------------
    if (state.pickedState >= 0 && static_cast<std::size_t>(state.pickedState) < machine.inputs.size()) {
        const auto slot = static_cast<std::uint32_t>(state.pickedState);
        cat = grid.addCategory("State");
        const std::uint32_t child = machine.inputs[slot];
        const auto node = std::ranges::find(graph.nodes, child, &Node::id);
        auto name = prism::make<prism::TextInput>(stateName(slot), "Name");
        name->onCommit = [this, id, slot](const std::string& to) {
            if (to.empty()) {
                return;
            }
            EditNode(id, [slot, &to](Node& n) {
                if (slot < n.inputs.size()) {
                    if (n.stateNames.size() < n.inputs.size()) {
                        n.stateNames.resize(n.inputs.size());
                    }
                    n.stateNames[slot] = to;
                }
            });
        };
        grid.addRow(cat, "Name", name, "F2 in the State Machine renames it in place");
        auto open = prism::make<prism::Button>("Open State Graph", prism::Icon::StateGraph, prism::ButtonStyle::Primary);
        open->onClick = [this, id, slot] { OpenStateGraph(id, slot); };
        grid.addRow(cat, "", open, "What the state plays: its own graph, ending in its Output Pose (double-click the state)");
        if (node == graph.nodes.end()) {
            auto empty = prism::make<prism::Label>("Plays nothing yet: open its graph and wire a pose to Output Pose");
            empty->setColor(prism::ThemeColor(&prism::Theme::textDim));
            grid.addRow(cat, "Plays", empty);
        }
        if (node != graph.nodes.end()) {
            if (node->kind != Kind::Clip) {
                auto plays = prism::make<prism::Label>(std::string{AnimationGraphKindLabel(node->kind)} + (node->name.empty() ? "" : "  (" + node->name + ")"));
                grid.addRow(cat, "Plays", plays);
            }
            if (node->kind == Kind::Clip) {
                std::vector<Asset::AssetGuid> choices{Asset::AssetGuid{}};
                std::vector<std::string> names{"None"};
                int selected = 0;
                for (const Asset::AssetGuid guid : ClipChoices()) {
                    choices.push_back(guid);
                    names.push_back(m_editor.AnimationClipName(guid));
                    if (guid == node->clip) {
                        selected = static_cast<int>(choices.size()) - 1;
                    }
                }
                auto clip = prism::make<prism::ComboBox>(names, selected);
                clip->onChange = [this, child, choices](const int i) {
                    if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) {
                        const Asset::AssetGuid picked = choices[static_cast<std::size_t>(i)];
                        EditNode(child, [picked](Node& n) { n.clip = picked; });
                    }
                };
                grid.addRow(cat, "Plays", clip);
                auto loop = prism::make<prism::CheckBox>("Loop", node->loop);
                loop->onChange = [this, child](const bool v) { EditNode(child, [v](Node& n) { n.loop = v; }); };
                grid.addRow(cat, "", loop);
            } else {
            }
        }
        auto startHere = prism::make<prism::Button>(machine.entryState == slot ? "Starts here" : "Start Here", prism::Icon::NodeEntry,
                                                    prism::ButtonStyle::Default);
        startHere->setEnabled(machine.entryState != slot);
        startHere->onClick = [this, id, slot] { EditNode(id, [slot](Node& n) { n.entryState = slot; }); };
        grid.addRow(cat, "", startHere);
        cat = grid.addCategory("Transitions");
        for (std::size_t i = 0U; i < machine.transitions.size(); ++i) {
            const Scene::AnimationGraphTransition& t = machine.transitions[i];
            if (t.toState == slot && t.fromState != slot && t.fromState != Scene::kAnyAnimationState) {
                auto in = prism::make<prism::Button>(stateName(t.fromState), prism::Icon::NodeTransition, prism::ButtonStyle::Flat);
                in->setTooltip(DescribeAnimationTransition(t));
                in->onClick = [this, i] {
                    m_editor.m_animGraph.pickedTransition = static_cast<int>(i);
                    m_editor.m_animGraph.pickedState = -1;
                };
                grid.addRow(cat, "From", in);
            }
            if (t.fromState != slot && t.fromState != Scene::kAnyAnimationState) {
                continue;
            }
            auto link = prism::make<prism::Button>(stateName(t.toState), prism::Icon::NodeTransition, prism::ButtonStyle::Flat);
            link->setTooltip(DescribeAnimationTransition(t));
            link->onClick = [this, i] {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(i);
                m_editor.m_animGraph.pickedState = -1;
            };
            grid.addRow(cat, t.fromState == slot ? "To" : "Any to", link);
        }
        // A new transition to another state, picked from a list.
        std::vector<std::string> others{"Add transition to..."};
        std::vector<std::uint32_t> otherSlots{0U};
        for (std::uint32_t other = 0U; other < machine.inputs.size(); ++other) {
            if (other != slot) {
                others.push_back(stateName(other));
                otherSlots.push_back(other);
            }
        }
        auto addTo = prism::make<prism::ComboBox>(others, 0);
        addTo->onChange = [this, id, slot, otherSlots](const int i) {
            if (i <= 0 || static_cast<std::size_t>(i) >= otherSlots.size()) {
                return;
            }
            std::optional<std::size_t> added;
            const std::uint32_t to = otherSlots[static_cast<std::size_t>(i)];
            Edit([&](Graph& g) { added = AddAnimationTransition(g.nodes, id, slot, to); });
            if (added.has_value()) {
                m_editor.m_animGraph.pickedTransition = static_cast<int>(*added);
                m_editor.m_animGraph.pickedState = -1;
            }
        };
        grid.addRow(cat, "", addTo, "Or drag from this state's edge to another state");
        auto remove = prism::make<prism::Button>("Delete State", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->setEnabled(machine.inputs.size() > 1U);
        remove->onClick = [this, id, slot] {
            Edit([id, slot](Graph& g) { static_cast<void>(RemoveAnimationGraphInputSlot(g.nodes, id, slot)); });
            m_editor.m_animGraph.pickedState = -1;
        };
        grid.addRow(cat, "", remove, "Its graph goes with it");
        return;
    }

    // ---- the machine itself ----------------------------------------------------------------------
    cat = grid.addCategory("State Machine");
    auto machineName = prism::make<prism::TextInput>(machine.name, "Name");
    machineName->onCommit = [this, id](const std::string& to) { RenameNode(id, to); };
    grid.addRow(cat, "Name", machineName);
    std::vector<std::string> names;
    for (std::uint32_t slot = 0U; slot < machine.inputs.size(); ++slot) {
        names.push_back(stateName(slot));
    }
    auto entry = prism::make<prism::ComboBox>(names, static_cast<int>(std::min<std::size_t>(machine.entryState, names.empty() ? 0U : names.size() - 1U)));
    entry->onChange = [this, id](const int i) {
        if (i >= 0) {
            EditNode(id, [i](Node& n) { n.entryState = static_cast<std::uint32_t>(i); });
        }
    };
    grid.addRow(cat, "Entry State", entry, "Where the machine starts");
    auto counts = prism::make<prism::Label>(std::to_string(machine.inputs.size()) + (machine.inputs.size() == 1U ? " state, " : " states, ") +
                                            std::to_string(machine.transitions.size()) + (machine.transitions.size() == 1U ? " transition" : " transitions"));
    counts->setColor(prism::ThemeColor(&prism::Theme::textDim));
    grid.addRow(cat, "", counts);
    cat = grid.addCategory("States");
    for (std::uint32_t slot = 0U; slot < machine.inputs.size(); ++slot) {
        auto row = prism::hbox(4.0F);
        auto pick = prism::make<prism::Button>(stateName(slot), slot == machine.entryState ? prism::Icon::NodeEntry : prism::Icon::StateGraph,
                                               prism::ButtonStyle::Flat);
        pick->setTooltip("Pick it");
        pick->onClick = [this, slot] {
            m_editor.m_animGraph.pickedState = static_cast<int>(slot);
            m_editor.m_animGraph.pickedTransition = -1;
            m_sideDirty = true;
        };
        row->add(pick, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        auto openGraph = prism::make<prism::Button>("", prism::Icon::ChevronRight, prism::ButtonStyle::Flat);
        openGraph->setTooltip("Open its graph");
        openGraph->onClick = [this, id, slot] { OpenStateGraph(id, slot); };
        row->add(openGraph, prism::Slot{0.0F, {}, prism::HAlign::Right, prism::VAlign::Center});
        grid.addRow(cat, "", row);
    }
    auto addState = prism::make<prism::Button>("Add State", prism::Icon::Plus, prism::ButtonStyle::Default);
    addState->onClick = [this, id] {
        std::optional<std::size_t> added;
        Edit([&](Graph& g) {
            const auto m = std::ranges::find(g.nodes, id, &Node::id);
            const Math::Vector2 at = m != g.nodes.end() ? AnimationStatePosition(*m, m->inputs.size()) : Math::Vector2{};
            added = AddAnimationState(g.nodes, id, at);
        });
        if (added.has_value()) {
            m_editor.m_animGraph.pickedState = static_cast<int>(*added);
            m_editor.m_animGraph.pickedTransition = -1;
        }
    };
    grid.addRow(cat, "", addState, "Or double-click empty space in the State Machine");
    static_cast<void>(cat);
}

void EditorAnimGraphPanel::BuildBlendSpaceSide(prism::PropertyGrid& grid, const Graph& graph, const Node& space) {
    static_cast<void>(graph);
    int cat = -1;
    Editor::AnimationGraphViewState& state = m_editor.m_animGraph;
    const std::uint32_t id = space.id;
    const bool two = space.kind == Kind::BlendSpace2D;
    cat = grid.addCategory(space.name.empty() ? std::string{AnimationGraphKindLabel(space.kind)} : space.name);
    state.spaceTool = 0;   // points are added by dragging animations in, or from the menus
    const auto pairRow = [](const prism::Ref<prism::Widget>& a, const prism::Ref<prism::Widget>& b) {
        auto row = prism::hbox(4.0F);
        row->add(a, prism::Slot{1.0F});
        row->add(b, prism::Slot{1.0F});
        return row;
    };
    std::vector<std::string> floats{"(its own value)"};
    std::vector<std::string> vectors{"(its own value)"};
    for (const Scene::AnimationParameter& p : graph.parameters) {
        if (p.type == Scene::AnimationParameterType::Float) {
            floats.push_back(p.name);
        } else if (p.type == Scene::AnimationParameterType::Vector3) {
            vectors.push_back(p.name);
        }
    }
    // A 2D space may be moved by one Vector3 (x and y at once) instead of two Floats.
    const bool byVector = two && std::ranges::find(vectors, space.parameter) != vectors.end() && !space.parameter.empty();
    // ---- Axis Settings: each axis's name, range, grid and the parameter that moves along it.
    for (const bool y : two ? std::vector<bool>{false, true} : std::vector<bool>{false}) {
        cat = grid.addCategory(two ? (y ? "Vertical Axis" : "Horizontal Axis") : "Axis");
        auto name = prism::make<prism::TextInput>(y ? space.axisNameY : space.axisNameX, y ? "Y" : "X");
        name->onCommit = [this, id, y](const std::string& to) { EditNode(id, [y, &to](Node& n) { (y ? n.axisNameY : n.axisNameX) = to; }); };
        grid.addRow(cat, "Name", name, "Shown along the axis");
        const auto areaField = [this, id, y](const float value, const bool high) {
            auto field = prism::make<prism::NumberField>(value, 0.05F, 2);
            field->setScrubbable(false);
            field->onCommit = [this, id, y, high](const float v) {
                EditNode(id, [y, high, v](Node& n) {
                    float& target = y ? (high ? n.areaMax.y : n.areaMin.y) : (high ? n.areaMax.x : n.areaMin.x);
                    target = v;
                });
            };
            return field;
        };
        grid.addRow(cat, "Minimum / Maximum", pairRow(areaField(y ? space.areaMin.y : space.areaMin.x, false), areaField(y ? space.areaMax.y : space.areaMax.x, true)),
                    "The range the editor shows and the grid divides");
        auto divisions = prism::make<prism::NumberField>(static_cast<float>(y ? space.gridY : space.gridX), 1.0F, 0);
        divisions->setRange(1.0F, 64.0F);
        divisions->setScrubbable(false);
        divisions->onCommit = [this, id, y](const float v) {
            const auto count = static_cast<std::uint32_t>(std::clamp(std::lround(v), 1L, 64L));
            EditNode(id, [y, count](Node& n) { (y ? n.gridY : n.gridX) = count; });
        };
        grid.addRow(cat, "Grid Divisions", divisions, "Lines across the axis; points snap to them");
        if (byVector && y) {
            auto note = prism::make<prism::Label>("From " + space.parameter + " (its y)");
            note->setColor(prism::ThemeColor(&prism::Theme::textDim));
            grid.addRow(cat, "Parameter", note);
            continue;
        }
        std::vector<std::string> choices = floats;
        if (two && !y) {
            for (std::size_t k = 1U; k < vectors.size(); ++k) {
                choices.push_back(vectors[k]);
            }
        }
        const std::string& bound = y ? space.parameterY : space.parameter;
        int selected = 0;
        for (std::size_t k = 1U; k < choices.size(); ++k) {
            if (choices[k] == bound) {
                selected = static_cast<int>(k);
            }
        }
        auto parameter = prism::make<prism::ComboBox>(choices, selected);
        parameter->onChange = [this, id, y, choices](const int k) {
            const std::string to = k > 0 && static_cast<std::size_t>(k) < choices.size() ? choices[static_cast<std::size_t>(k)] : std::string{};
            EditNode(id, [y, &to](Node& n) { (y ? n.parameterY : n.parameter) = to; });
        };
        grid.addRow(cat, "Parameter", parameter,
                    two && !y ? "A Float for this axis, or a Vector3 for both (x, y)" : "The Float the gameplay moves along this axis");
    }
    cat = grid.addCategory("Blending");
    auto snapGrid = prism::make<prism::CheckBox>("Snap to Grid", space.snapToGrid);
    snapGrid->onChange = [this, id](const bool v) { EditNode(id, [v](Node& n) { n.snapToGrid = v; }); };
    grid.addRow(cat, "", snapGrid, "Points land on the grid's lines (Shift while dragging: free)");
    auto mode = prism::make<prism::ComboBox>(std::vector<std::string>{"Blend", "Nearest", "Nearest, in step"}, static_cast<int>(space.blendMode));
    mode->onChange = [this, id](const int k) {
        EditNode(id, [k](Node& n) { n.blendMode = static_cast<Scene::AnimationBlendMode>(std::clamp(k, 0, 2)); });
    };
    grid.addRow(cat, "Mode", mode, "Blend mixes the nearest points; Nearest plays one (in step: from the same phase)");
    auto sync = prism::make<prism::CheckBox>("Keep in step", space.sync);
    sync->onChange = [this, id](const bool v) { EditNode(id, [v](Node& n) { n.sync = v; }); };
    grid.addRow(cat, "Sync", sync, "The heaviest animation leads; the others follow its phase (feet together)");
    auto smoothing = prism::make<prism::NumberField>(space.smoothingSeconds, 0.01F, 2);
    smoothing->setRange(0.0F, 5.0F);
    smoothing->setScrubbable(false);
    smoothing->setSuffix(" s");
    smoothing->setTooltip("How long the position takes to close half the gap to its parameters (0: at once)");
    smoothing->onCommit = [this, id](const float v) { EditNode(id, [v](Node& n) { n.smoothingSeconds = std::clamp(v, 0.0F, 5.0F); }); };
    grid.addRow(cat, "Smoothing", smoothing);
    auto count = prism::make<prism::Label>(std::to_string(space.blendPoints.size()) + (space.blendPoints.size() == 1U ? " animation" : " animations"));
    count->setColor(prism::ThemeColor(&prism::Theme::textDim));
    grid.addRow(cat, "", count);

    // The picked point.
    if (state.pickedState >= 0 && static_cast<std::size_t>(state.pickedState) < space.blendPoints.size()) {
        const auto slot = static_cast<std::size_t>(state.pickedState);
        const Scene::AnimationBlendPoint& point = space.blendPoints[slot];
        cat = grid.addCategory("Point " + std::to_string(slot + 1U));
        const auto editPoint = [this, id, slot](const std::function<void(Scene::AnimationBlendPoint&)>& change) {
            EditNode(id, [&change, slot](Node& n) {
                if (slot < n.blendPoints.size()) {
                    change(n.blendPoints[slot]);
                }
            });
        };
        std::vector<Asset::AssetGuid> choices{Asset::AssetGuid{}};
        std::vector<std::string> names{"None"};
        int selected = 0;
        for (const Asset::AssetGuid guid : ClipChoices()) {
            choices.push_back(guid);
            names.push_back(m_editor.AnimationClipName(guid));
            if (guid == point.clip) {
                selected = static_cast<int>(choices.size()) - 1;
            }
        }
        auto clip = prism::make<prism::ComboBox>(names, selected);
        clip->onChange = [editPoint, choices](const int i) {
            if (i >= 0 && static_cast<std::size_t>(i) < choices.size()) {
                const Asset::AssetGuid picked = choices[static_cast<std::size_t>(i)];
                editPoint([picked](Scene::AnimationBlendPoint& p) { p.clip = picked; });
            }
        };
        grid.addRow(cat, "Animation", clip);
        auto speedRow = prism::hbox(6.0F);
        auto speed = prism::make<prism::NumberField>(point.speed, 0.01F, 2);
        speed->setSuffix("x");
        speed->setScrubbable(false);
        speed->onCommit = [editPoint](const float v) { editPoint([v](Scene::AnimationBlendPoint& p) { p.speed = v; }); };
        auto loop = prism::make<prism::CheckBox>("Loop", point.loop);
        loop->onChange = [editPoint](const bool v) { editPoint([v](Scene::AnimationBlendPoint& p) { p.loop = v; }); };
        speedRow->add(speed, prism::Slot{1.0F, {}, prism::HAlign::Fill, prism::VAlign::Center});
        grid.addRow(cat, "Rate", speedRow);
        grid.addRow(cat, "", loop);
        const auto placeField = [this, id, slot, point](const bool y) {
            auto field = prism::make<prism::NumberField>(y ? point.position.y : point.position.x, 0.01F, 2);
            field->setScrubbable(false);
            field->onCommit = [this, id, slot, point, y](const float v) {
                const Math::Vector2 to = y ? Math::Vector2{point.position.x, v} : Math::Vector2{v, point.position.y};
                Edit([id, slot, to](Graph& g) { static_cast<void>(MoveBlendSpacePoint(g.nodes, id, slot, to)); });
            };
            return field;
        };
        const prism::Ref<prism::Widget> place = two ? prism::Ref<prism::Widget>{pairRow(placeField(false), placeField(true))}
                                                    : prism::Ref<prism::Widget>{placeField(false)};
        grid.addRow(cat, two ? "X / Y" : "X", place);
        auto remove = prism::make<prism::Button>("Delete Point", prism::Icon::Trash, prism::ButtonStyle::Default);
        remove->onClick = [this, id, slot] {
            Edit([id, slot](Graph& g) { static_cast<void>(RemoveBlendSpacePoint(g.nodes, id, slot)); });
            m_editor.m_animGraph.pickedState = -1;
        };
        grid.addRow(cat, "", remove);
    }
    return;
}

} // namespace UVE::Editor
