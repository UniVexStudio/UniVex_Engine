// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/animation_graph_editing_uve.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace UVE::Editor {
namespace {

using Kind = Scene::AnimationGraphNodeKindUVE;
using Scene::AnimationGraphNodeUVE;

[[nodiscard]] AnimationGraphNodeUVE* FindNodeUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t id) {
    const auto it = std::ranges::find(nodes, id, &AnimationGraphNodeUVE::id);
    return it == nodes.end() || id == 0U ? nullptr : &*it;
}

[[nodiscard]] const AnimationGraphNodeUVE* FindNodeUVE(const std::vector<AnimationGraphNodeUVE>& nodes,
                                                       const std::uint32_t id) {
    const auto it = std::ranges::find(nodes, id, &AnimationGraphNodeUVE::id);
    return it == nodes.end() || id == 0U ? nullptr : &*it;
}

/// True when `wanted` is `from` or feeds it, directly or through other nodes.
[[nodiscard]] bool IsUpstreamUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t from,
                                 const std::uint32_t wanted) {
    std::vector<std::uint32_t> pending{from};
    std::vector<std::uint32_t> seen;
    while (!pending.empty()) {
        const std::uint32_t id = pending.back();
        pending.pop_back();
        if (id == wanted) {
            return true;
        }
        if (std::ranges::find(seen, id) != seen.end()) {
            continue;
        }
        seen.push_back(id);
        if (const AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, id)) {
            for (const std::uint32_t input : node->inputs) {
                if (input != 0U) {
                    pending.push_back(input);
                }
            }
        }
    }
    return false;
}

[[nodiscard]] bool HasVariableSlotsUVE(const Kind kind) noexcept {
    return kind == Kind::Select || kind == Kind::StateMachine;
}

} // namespace

const char* AnimationGraphKindLabelUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "Output";
        case Kind::Clip: return "Clip";
        case Kind::Blend2: return "Blend";
        case Kind::BlendSpace1D: return "Blend Space";
        case Kind::Additive: return "Additive";
        case Kind::OneShot: return "One Shot";
        case Kind::TimeScale: return "Time Scale";
        case Kind::StateMachine: return "State Machine";
        case Kind::BlendSpace2D: return "Blend Space 2D";
        case Kind::Select: return "Select";
        case Kind::LayeredBlend: return "Layered Blend";
        case Kind::TimeSeek: return "Time Seek";
    }
    return "?";
}

const char* AnimationGraphKindHelpUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "What the target shows.";
        case Kind::Clip: return "Plays a clip.";
        case Kind::Blend2: return "Mixes A and B by a weight: 0 is A, 1 is B.";
        case Kind::BlendSpace1D:
            return "Places animations along a line and mixes the two either side of a value - walk, jog, run by speed.";
        case Kind::Additive: return "Lays the Layer's motion on top of the Base, scaled by a weight.";
        case Kind::OneShot: return "Plays Shot once over Base when a trigger fires, fading in and out.";
        case Kind::TimeScale: return "Runs its input faster or slower.";
        case Kind::StateMachine: return "Its inputs are states. Transitions move between them and crossfade.";
        case Kind::BlendSpace2D:
            return "Places animations on a plane and blends inside the triangle a two-value position is in - strafe by X and Z.";
        case Kind::Select:
            return "Plays the input a Bool or number picks, fading when the pick changes - stance by weapon.";
        case Kind::LayeredBlend:
            return "Lays the Layer over the Base on chosen bone branches only - shoot with the upper body while running.";
        case Kind::TimeSeek: return "Jumps its input to a time when its trigger fires, then plays on.";
    }
    return "";
}

std::string AnimationGraphSlotLabelUVE(const Kind kind, const std::size_t slot) {
    switch (kind) {
        case Kind::Blend2: return slot == 0U ? "A" : "B";
        case Kind::Additive: return slot == 0U ? "Base" : "Layer";
        case Kind::OneShot: return slot == 0U ? "Base" : "Shot";
        case Kind::StateMachine: return "State " + std::to_string(slot + 1U);
        case Kind::BlendSpace1D:
        case Kind::BlendSpace2D: return "Point " + std::to_string(slot + 1U);
        case Kind::Select: return "Option " + std::to_string(slot);
        case Kind::LayeredBlend: return slot == 0U ? "Base" : "Layer";
        default: return "Input";
    }
}

std::uint32_t AddAnimationGraphNodeUVE(std::vector<AnimationGraphNodeUVE>& nodes, const Kind kind,
                                       const Math::Vector2UVE position) {
    if (kind == Kind::Output || nodes.size() >= Scene::kMaximumAnimationGraphNodesUVE) {
        return 0U;
    }
    AnimationGraphNodeUVE added;
    added.id = Scene::NextAnimationGraphNodeIdUVE(nodes);
    added.kind = kind;
    added.name = AnimationGraphKindLabelUVE(kind);
    added.position = position;
    switch (kind) {
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::OneShot:
            added.inputs = {0U, 0U};
            break;
        case Kind::TimeScale:
        case Kind::StateMachine:
            added.inputs = {0U};
            break;
        case Kind::BlendSpace1D:
            // Empty: its animations are added in its editor, each at a point on the line.
            added.value = 0.0F;
            added.areaMin = Math::Vector2UVE{-0.25F, -1.0F};
            added.areaMax = Math::Vector2UVE{1.25F, 1.0F};
            break;
        case Kind::BlendSpace2D:
            added.value = 0.0F;
            added.areaMin = Math::Vector2UVE{-1.25F, -1.25F};
            added.areaMax = Math::Vector2UVE{1.25F, 1.25F};
            break;
        case Kind::Select:
            added.inputs = {0U, 0U};
            added.value = 0.0F;
            break;
        case Kind::LayeredBlend:
            added.inputs = {0U, 0U};
            added.value = 1.0F;
            break;
        case Kind::TimeSeek:
            added.inputs = {0U};
            added.value = 0.0F;
            break;
        default:
            break;
    }
    nodes.push_back(std::move(added));
    return nodes.back().id;
}

bool CanConnectAnimationGraphNodesUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t target,
                                      const std::size_t slot, const std::uint32_t source) {
    const AnimationGraphNodeUVE* const to = FindNodeUVE(nodes, target);
    const AnimationGraphNodeUVE* const from = FindNodeUVE(nodes, source);
    if (to == nullptr || from == nullptr || slot >= to->inputs.size() || from->kind == Kind::Output ||
        source == target) {
        return false;
    }
    // A cycle: the target already feeds the source.
    return !IsUpstreamUVE(nodes, source, target);
}

bool ConnectAnimationGraphNodesUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t target,
                                   const std::size_t slot, const std::uint32_t source) {
    if (!CanConnectAnimationGraphNodesUVE(nodes, target, slot, source)) {
        return false;
    }
    for (AnimationGraphNodeUVE& node : nodes) {
        std::ranges::replace(node.inputs, source, 0U);
    }
    FindNodeUVE(nodes, target)->inputs[slot] = source;
    return true;
}

bool DisconnectAnimationGraphInputUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t target,
                                      const std::size_t slot) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, target);
    if (node == nullptr || slot >= node->inputs.size() || node->inputs[slot] == 0U) {
        return false;
    }
    node->inputs[slot] = 0U;
    return true;
}

std::size_t DeleteAnimationGraphNodesUVE(std::vector<AnimationGraphNodeUVE>& nodes,
                                         const std::vector<std::uint32_t>& ids) {
    const auto doomed = [&ids](const AnimationGraphNodeUVE& node) {
        return node.kind != Kind::Output && std::ranges::find(ids, node.id) != ids.end();
    };
    std::vector<std::uint32_t> removed;
    for (const AnimationGraphNodeUVE& node : nodes) {
        if (doomed(node)) {
            removed.push_back(node.id);
        }
    }
    std::erase_if(nodes, doomed);
    for (AnimationGraphNodeUVE& node : nodes) {
        for (std::uint32_t& input : node.inputs) {
            if (std::ranges::find(removed, input) != removed.end()) {
                input = 0U;
            }
        }
    }
    return removed.size();
}

std::vector<std::uint32_t> DuplicateAnimationGraphNodesUVE(std::vector<AnimationGraphNodeUVE>& nodes,
                                                           const std::vector<std::uint32_t>& ids,
                                                           const Math::Vector2UVE offset) {
    std::unordered_map<std::uint32_t, std::uint32_t> copyOf;
    std::vector<AnimationGraphNodeUVE> copies;
    std::uint32_t nextId = Scene::NextAnimationGraphNodeIdUVE(nodes);
    for (const std::uint32_t id : ids) {
        const AnimationGraphNodeUVE* const original = FindNodeUVE(std::as_const(nodes), id);
        if (original == nullptr || original->kind == Kind::Output || copyOf.contains(id) ||
            nodes.size() + copies.size() >= Scene::kMaximumAnimationGraphNodesUVE) {
            continue;
        }
        AnimationGraphNodeUVE copy = *original;
        copy.id = nextId++;
        copy.position = copy.position + offset;
        copyOf.emplace(id, copy.id);
        copies.push_back(std::move(copy));
    }
    std::vector<std::uint32_t> created;
    for (AnimationGraphNodeUVE& copy : copies) {
        for (std::uint32_t& input : copy.inputs) {
            const auto it = copyOf.find(input);
            input = it == copyOf.end() ? 0U : it->second;
        }
        created.push_back(copy.id);
        nodes.push_back(std::move(copy));
    }
    return created;
}

bool AddAnimationGraphInputSlotUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t target) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, target);
    if (node == nullptr || !HasVariableSlotsUVE(node->kind) ||
        node->inputs.size() >= Scene::kMaximumAnimationObjectInputsUVE) {
        return false;
    }
    node->inputs.push_back(0U);
    return true;
}

bool RemoveAnimationGraphInputSlotUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t target,
                                      const std::size_t slot) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, target);
    if (node == nullptr || !HasVariableSlotsUVE(node->kind) || slot >= node->inputs.size() ||
        node->inputs.size() <= 1U) {
        return false;
    }
    node->inputs.erase(node->inputs.begin() + static_cast<std::ptrdiff_t>(slot));
    if (node->kind == Kind::StateMachine) {
        const auto state = static_cast<std::uint32_t>(slot);
        std::erase_if(node->transitions, [state](const Scene::AnimationTransitionUVE& transition) {
            return transition.fromState == state || transition.toState == state;
        });
        for (Scene::AnimationTransitionUVE& transition : node->transitions) {
            if (transition.fromState != Scene::kAnyAnimationStateUVE && transition.fromState > state) {
                --transition.fromState;
            }
            if (transition.toState > state) {
                --transition.toState;
            }
        }
        node->entryState = std::min(node->entryState, static_cast<std::uint32_t>(node->inputs.size() - 1U));
        if (slot < node->statePositions.size()) {
            node->statePositions.erase(node->statePositions.begin() + static_cast<std::ptrdiff_t>(slot));
        }
    }
    return true;
}

Math::Vector2UVE AnimationStatePositionUVE(const AnimationGraphNodeUVE& machine, const std::size_t slot) {
    if (slot < machine.statePositions.size()) {
        return machine.statePositions[slot];
    }
    // Not placed yet: a grid three wide, right of Entry.
    constexpr float kColumnUVE = 200.0F;
    constexpr float kRowUVE = 110.0F;
    return Math::Vector2UVE{static_cast<float>(slot % 3U) * kColumnUVE, static_cast<float>(slot / 3U) * kRowUVE};
}

bool SetAnimationStatePositionUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t machine,
                                  const std::size_t slot, const Math::Vector2UVE position) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, machine);
    if (node == nullptr || node->kind != Kind::StateMachine || slot >= node->inputs.size() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return false;
    }
    // The states before it keep where they are drawn now.
    while (node->statePositions.size() <= slot) {
        node->statePositions.push_back(AnimationStatePositionUVE(*node, node->statePositions.size()));
    }
    node->statePositions[slot] = position;
    return true;
}

std::optional<std::size_t> AddAnimationStateUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t machine,
                                                const Math::Vector2UVE position) {
    AnimationGraphNodeUVE* node = FindNodeUVE(nodes, machine);
    if (node == nullptr || node->kind != Kind::StateMachine || node->inputs.size() >= Scene::kMaximumAnimationObjectInputsUVE ||
        nodes.size() >= Scene::kMaximumAnimationGraphNodesUVE) {
        return std::nullopt;
    }
    // A state plays something: it starts as a Clip, set beside the machine in the tree, whose
    // animation is picked next.
    const Math::Vector2UVE beside{node->position.x - 240.0F, node->position.y + static_cast<float>(node->inputs.size()) * 70.0F};
    const std::uint32_t clip = AddAnimationGraphNodeUVE(nodes, Kind::Clip, beside);
    if (clip == 0U) {
        return std::nullopt;
    }
    node = FindNodeUVE(nodes, machine); // the add may have moved the nodes
    std::size_t slot = node->inputs.size();
    if (slot == 1U && node->inputs[0] == 0U) {
        slot = 0U; // a new machine's empty first state takes it
    } else {
        node->inputs.push_back(0U);
    }
    node->inputs[slot] = clip;
    // Named for the state it plays, so the view and the tree read the same.
    FindNodeUVE(nodes, clip)->name = "State " + std::to_string(slot + 1U);
    static_cast<void>(SetAnimationStatePositionUVE(nodes, machine, slot, position));
    return slot;
}

std::optional<std::size_t> AddAnimationTransitionUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t machine,
                                                     const std::uint32_t from, const std::uint32_t to) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, machine);
    if (node == nullptr || node->kind != Kind::StateMachine || to >= node->inputs.size() || from == to ||
        (from != Scene::kAnyAnimationStateUVE && from >= node->inputs.size()) ||
        node->transitions.size() >= Scene::kMaximumAnimationTransitionsUVE) {
        return std::nullopt;
    }
    Scene::AnimationTransitionUVE transition;
    transition.fromState = from;
    transition.toState = to;
    node->transitions.push_back(transition);
    return node->transitions.size() - 1U;
}

bool RemoveAnimationTransitionUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t machine,
                                  const std::size_t index) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, machine);
    if (node == nullptr || node->kind != Kind::StateMachine || index >= node->transitions.size()) {
        return false;
    }
    node->transitions.erase(node->transitions.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool MoveAnimationTransitionUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t machine,
                                const std::size_t index, const std::size_t newIndex) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, machine);
    if (node == nullptr || node->kind != Kind::StateMachine || index >= node->transitions.size() ||
        newIndex >= node->transitions.size() || index == newIndex) {
        return false;
    }
    const Scene::AnimationTransitionUVE moved = node->transitions[index];
    node->transitions.erase(node->transitions.begin() + static_cast<std::ptrdiff_t>(index));
    node->transitions.insert(node->transitions.begin() + static_cast<std::ptrdiff_t>(newIndex), moved);
    return true;
}

std::string DescribeAnimationTransitionUVE(const Scene::AnimationTransitionUVE& transition) {
    using Condition = Scene::AnimationConditionUVE;
    std::string text;
    const auto name = [](const std::string& parameter) { return parameter.empty() ? std::string{"?"} : parameter; };
    const auto number = [](const float value) {
        char buffer[32];
        std::snprintf(buffer, sizeof(buffer), "%g", static_cast<double>(value));
        return std::string{buffer};
    };
    for (const Scene::AnimationTransitionConditionUVE& test : transition.conditions) {
        std::string part;
        switch (test.condition) {
            case Condition::Always: continue;
            case Condition::AtEnd: part = "state finished"; break;
            case Condition::ParameterGreater: part = name(test.parameter) + " > " + number(test.threshold); break;
            case Condition::ParameterLess: part = name(test.parameter) + " < " + number(test.threshold); break;
            case Condition::ParameterTrue: part = name(test.parameter); break;
            case Condition::ParameterFalse: part = "not " + name(test.parameter); break;
            case Condition::Triggered: part = name(test.parameter) + " fired"; break;
        }
        text += text.empty() ? part : " and " + part;
    }
    if (transition.exitPhase >= 0.0F) {
        const std::string wait = "after " + number(std::round(transition.exitPhase * 100.0F)) + "%";
        text += text.empty() ? wait : ", " + wait;
    }
    if (text.empty()) {
        text = "always";
    }
    if (!transition.enabled) {
        text += " (off)";
    }
    return text;
}

std::optional<std::size_t> AddBlendSpacePointUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t space,
                                                 const Math::Vector2UVE position, const Asset::AssetGuidUVE clip) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, space);
    if (node == nullptr || (node->kind != Kind::BlendSpace1D && node->kind != Kind::BlendSpace2D) ||
        node->blendPoints.size() >= Scene::kMaximumAnimationObjectInputsUVE || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return std::nullopt;
    }
    Scene::AnimationBlendPointUVE point;
    point.position = node->kind == Kind::BlendSpace1D ? Math::Vector2UVE{position.x, 0.0F} : position;
    point.clip = clip;
    if (std::ranges::find(node->blendPoints, point.position, &Scene::AnimationBlendPointUVE::position) !=
        node->blendPoints.end()) {
        return std::nullopt;
    }
    // A line keeps its points rising, so the new one goes where its x belongs.
    std::size_t slot = node->blendPoints.size();
    if (node->kind == Kind::BlendSpace1D) {
        slot = static_cast<std::size_t>(std::ranges::find_if(node->blendPoints, [&point](const Scene::AnimationBlendPointUVE& other) {
                                            return other.position.x > point.position.x;
                                        }) -
                                        node->blendPoints.begin());
    }
    node->blendPoints.insert(node->blendPoints.begin() + static_cast<std::ptrdiff_t>(slot), point);
    return slot;
}

bool RemoveBlendSpacePointUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t space, const std::size_t slot) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, space);
    if (node == nullptr || (node->kind != Kind::BlendSpace1D && node->kind != Kind::BlendSpace2D) ||
        slot >= node->blendPoints.size()) {
        return false;
    }
    node->blendPoints.erase(node->blendPoints.begin() + static_cast<std::ptrdiff_t>(slot));
    return true;
}

bool MoveBlendSpacePointUVE(std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t space, const std::size_t slot,
                            const Math::Vector2UVE position) {
    AnimationGraphNodeUVE* const node = FindNodeUVE(nodes, space);
    if (node == nullptr || (node->kind != Kind::BlendSpace1D && node->kind != Kind::BlendSpace2D) ||
        slot >= node->blendPoints.size() || !std::isfinite(position.x) || !std::isfinite(position.y)) {
        return false;
    }
    std::vector<Scene::AnimationBlendPointUVE>& points = node->blendPoints;
    if (node->kind == Kind::BlendSpace1D) {
        // Between its neighbours, so the line stays in order.
        if ((slot > 0U && position.x <= points[slot - 1U].position.x) ||
            (slot + 1U < points.size() && position.x >= points[slot + 1U].position.x)) {
            return false;
        }
        points[slot].position = Math::Vector2UVE{position.x, 0.0F};
        return true;
    }
    for (std::size_t other = 0U; other < points.size(); ++other) {
        if (other != slot && points[other].position == position) {
            return false;
        }
    }
    points[slot].position = position;
    return true;
}

} // namespace UVE::Editor
