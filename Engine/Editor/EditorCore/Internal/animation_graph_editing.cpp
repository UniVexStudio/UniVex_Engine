// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_graph_editing.h"

#include <algorithm>
#include <cstdio>
#include <optional>
#include <cmath>
#include <unordered_map>
#include <utility>

#include <string>

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
        case Kind::Slot: return "Slot";
        case Kind::TwoBoneIK: return "Two Bone IK";
        case Kind::ModifyBone: return "Modify Bone";
        case Kind::LookAt: return "Look At";
        case Kind::BlendByBool: return "Blend by Bool";
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
        case Kind::Slot: return "Where an Animation Montage plays: the montage on this slot is laid over the input.";
        case Kind::TwoBoneIK:
            return "Bends a two-bone chain (upper arm, forearm, hand) so its tip reaches a target - a hand on a rail, a foot on a step.";
        case Kind::ModifyBone: return "Moves, turns and scales one bone, on top of the pose or instead of it.";
        case Kind::LookAt: return "Turns one bone to face a target, no further than its clamp - a head following the camera.";
        case Kind::BlendByBool: return "Plays True while a Bool is on and False while it is off, fading between them.";
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
        case Kind::BlendByBool: return slot == 0U ? "True" : "False";
        case Kind::TwoBoneIK:
        case Kind::ModifyBone:
        case Kind::LookAt: return "Pose";
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
        case Kind::Slot:
            added.inputs = {0U};
            added.parameter = "DefaultSlot";
            break;
        case Kind::TwoBoneIK:
            added.inputs = {0U};
            added.value = 1.0F;
            added.pole = Math::Vector3UVE{0.0F, 0.0F, 1.0F};
            break;
        case Kind::ModifyBone:
            added.inputs = {0U};
            added.value = 1.0F;
            break;
        case Kind::LookAt:
            added.inputs = {0U};
            added.value = 1.0F;
            added.clampDegrees = 75.0F;
            break;
        case Kind::BlendByBool:
            added.inputs = {0U, 0U};
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
                                         const std::vector<std::uint32_t>& given) {
    // A State Machine goes with everything in its states.
    std::vector<std::uint32_t> ids = given;
    for (const std::uint32_t id : given) {
        for (const std::uint32_t inside : AnimationGraphNodesInsideUVE(nodes, id)) {
            if (std::ranges::find(ids, inside) == ids.end()) {
                ids.push_back(inside);
            }
        }
    }
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
                                                           const std::vector<std::uint32_t>& given,
                                                           const Math::Vector2UVE offset) {
    // A State Machine is copied with its states' graphs (the copies keep their places in them).
    std::vector<std::uint32_t> ids = given;
    std::vector<std::uint32_t> inside;
    for (const std::uint32_t id : given) {
        for (const std::uint32_t member : AnimationGraphNodesInsideUVE(nodes, id)) {
            if (std::ranges::find(ids, member) == ids.end()) {
                ids.push_back(member);
                inside.push_back(member);
            }
        }
    }
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
        if (std::ranges::find(inside, id) == inside.end()) {
            copy.position = copy.position + offset;
        }
        copyOf.emplace(id, copy.id);
        copies.push_back(std::move(copy));
    }
    std::vector<std::uint32_t> created;
    for (AnimationGraphNodeUVE& copy : copies) {
        for (std::uint32_t& input : copy.inputs) {
            const auto it = copyOf.find(input);
            input = it == copyOf.end() ? 0U : it->second;
        }
        if (const auto owner = copyOf.find(copy.owner); owner != copyOf.end()) {
            copy.owner = owner->second;
        }
        nodes.push_back(std::move(copy));
    }
    for (const std::uint32_t id : given) {
        if (const auto copied = copyOf.find(id); copied != copyOf.end()) {
            created.push_back(copied->second);
        }
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
    // A state goes with its graph.
    std::vector<std::uint32_t> inside;
    if (node->kind == Kind::StateMachine) {
        for (const std::uint32_t id : AnimationGraphScopeNodesUVE(nodes, AnimationGraphScopeUVE{target, static_cast<std::uint32_t>(slot)})) {
            inside.push_back(id);
            for (const std::uint32_t deeper : AnimationGraphNodesInsideUVE(nodes, id)) {
                inside.push_back(deeper);
            }
        }
    }
    node->inputs.erase(node->inputs.begin() + static_cast<std::ptrdiff_t>(slot));
    if (node->kind == Kind::StateMachine) {
        const auto state = static_cast<std::uint32_t>(slot);
        if (slot < node->stateNames.size()) {
            node->stateNames.erase(node->stateNames.begin() + static_cast<std::ptrdiff_t>(slot));
        }
        std::erase_if(node->transitions, [state](const Scene::AnimationGraphTransitionUVE& transition) {
            return transition.fromState == state || transition.toState == state;
        });
        for (Scene::AnimationGraphTransitionUVE& transition : node->transitions) {
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
        for (AnimationGraphNodeUVE& other : nodes) {
            if (other.owner == target && other.ownerState > state) {
                --other.ownerState;
            }
        }
        if (!inside.empty()) {
            static_cast<void>(DeleteAnimationGraphNodesUVE(nodes, inside));
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
    // A state plays something: its graph starts as a Clip feeding the state's result, whose
    // animation is picked next.
    const std::uint32_t clip = AddAnimationGraphNodeUVE(nodes, Kind::Clip, Math::Vector2UVE{-320.0F, -40.0F});
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
    if (node->stateNames.size() < node->inputs.size()) {
        node->stateNames.resize(node->inputs.size());
    }
    // A name no other state has.
    std::uint32_t number = static_cast<std::uint32_t>(slot) + 1U;
    const auto taken = [&](const std::string& name) {
        for (std::size_t other = 0U; other < node->inputs.size(); ++other) {
            if (other != slot && AnimationStateNameUVE(nodes, *node, other) == name) {
                return true;
            }
        }
        return false;
    };
    while (taken("State " + std::to_string(number))) {
        ++number;
    }
    node->stateNames[slot] = "State " + std::to_string(number);
    AnimationGraphNodeUVE* made = FindNodeUVE(nodes, clip);
    made->owner = machine;
    made->ownerState = static_cast<std::uint32_t>(slot);
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
    Scene::AnimationGraphTransitionUVE transition;
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
    const Scene::AnimationGraphTransitionUVE moved = node->transitions[index];
    node->transitions.erase(node->transitions.begin() + static_cast<std::ptrdiff_t>(index));
    node->transitions.insert(node->transitions.begin() + static_cast<std::ptrdiff_t>(newIndex), moved);
    return true;
}

std::string DescribeAnimationTransitionUVE(const Scene::AnimationGraphTransitionUVE& transition) {
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

// ---------------------------------------------------------------------------- node parameters

std::string UniqueParameterNameUVE(const Scene::AnimationGraphComponentUVE& graph, const std::string& base) {
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

/// The parameter a node is driven by: which field holds its name, and its type. Every node but
/// Output and Slot has one (Godot's `parameters/<node>`): the gameplay (a Blueprint Class, a script)
/// sets the node by that name.
std::optional<AnimationNodeParameterUVE> NodeParameterOfUVE(const Kind kind) {
    using Type = Scene::AnimationParameterTypeUVE;
    using N = Scene::AnimationGraphNodeUVE;
    switch (kind) {
        case Kind::Clip:
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::LayeredBlend:
        case Kind::BlendSpace1D:
        case Kind::TimeScale:
        case Kind::Select: return AnimationNodeParameterUVE{&N::parameter, Type::Float};
        case Kind::BlendSpace2D: return AnimationNodeParameterUVE{&N::parameter, Type::Vector3};
        case Kind::OneShot:
        case Kind::TimeSeek:
        case Kind::StateMachine: return AnimationNodeParameterUVE{&N::parameter, Type::Trigger};
        case Kind::BlendByBool: return AnimationNodeParameterUVE{&N::parameter, Type::Bool};
        case Kind::TwoBoneIK:
        case Kind::LookAt:
        case Kind::ModifyBone: return AnimationNodeParameterUVE{&N::alphaParameter, Type::Float};
        case Kind::Output:
        case Kind::Slot: break;
    }
    return std::nullopt;
}

/// A node's name as a parameter name: its words run together ("Blend Space 2D" -> "BlendSpace2D").
std::string ParameterNameForNodeUVE(const std::string& nodeName) {
    std::string name;
    for (const char c : nodeName) {
        if (c != ' ') {
            name.push_back(c);
        }
    }
    return name.empty() ? std::string{"Param"} : name;
}

/// Renames a parameter and every use of it: the nodes it drives and the transition conditions that
/// read it (a Slot's name is not a parameter). False when `to` is empty or taken.
bool RenameGraphParameterUVE(Scene::AnimationGraphComponentUVE& graph, const std::string& from, const std::string& to) {
    if (to.empty() || to == from ||
        std::ranges::any_of(graph.parameters, [&to](const Scene::AnimationParameterUVE& p) { return p.name == to; })) {
        return false;
    }
    bool found = false;
    for (Scene::AnimationParameterUVE& p : graph.parameters) {
        if (p.name == from) {
            p.name = to;
            found = true;
        }
    }
    for (Scene::AnimationGraphNodeUVE& node : graph.nodes) {
        if (node.kind != Kind::Slot) {
            for (std::string* field : {&node.parameter, &node.parameterY, &node.alphaParameter}) {
                if (*field == from) {
                    *field = to;
                }
            }
        }
        for (Scene::AnimationGraphTransitionUVE& t : node.transitions) {
            for (Scene::AnimationTransitionConditionUVE& c : t.conditions) {
                if (c.parameter == from) {
                    c.parameter = to;
                }
            }
        }
    }
    return found;
}

/// Every node made gets its parameter at once, named after the node ("Walk" for a Blend Space 2D
/// called Walk), of the node's type, set to the node's current value, and bound to it.
void BindNewNodeParametersUVE(Scene::AnimationGraphComponentUVE& graph, const std::uint32_t id) {
    const auto node = std::ranges::find(graph.nodes, id, &Scene::AnimationGraphNodeUVE::id);
    if (node == graph.nodes.end()) {
        return;
    }
    const std::optional<AnimationNodeParameterUVE> spec = NodeParameterOfUVE(node->kind);
    if (!spec.has_value() || !((*node).*(spec->field)).empty()) {
        return;
    }
    Scene::AnimationParameterUVE parameter;
    parameter.name = UniqueParameterNameUVE(graph, ParameterNameForNodeUVE(node->name));
    parameter.type = spec->type;
    switch (node->kind) {
        case Kind::Clip:
        case Kind::TimeScale: parameter.value = node->speed; break;
        case Kind::BlendSpace2D: parameter.vector = Math::Vector3UVE{node->value, node->valueY, 0.0F}; break;
        case Kind::OneShot:
        case Kind::TimeSeek:
        case Kind::StateMachine:
        case Kind::BlendByBool: parameter.value = 0.0F; break;
        default: parameter.value = node->value; break;
    }
    (*node).*(spec->field) = parameter.name;
    if (node->kind == Kind::BlendSpace2D) {
        node->parameterY.clear();
    }
    graph.parameters.push_back(std::move(parameter));
}

AnimationGraphScopeUVE AnimationGraphScopeOfUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t id) {
    // Up the wires: the first State Machine met holds it, in the state that wire is.
    std::uint32_t at = id;
    for (std::size_t guard = 0U; guard <= nodes.size(); ++guard) {
        const AnimationGraphNodeUVE* parent = nullptr;
        std::size_t slot = 0U;
        for (const AnimationGraphNodeUVE& node : nodes) {
            const auto found = std::ranges::find(node.inputs, at);
            if (at != 0U && found != node.inputs.end()) {
                parent = &node;
                slot = static_cast<std::size_t>(found - node.inputs.begin());
                break;
            }
        }
        if (parent == nullptr) {
            const AnimationGraphNodeUVE* top = FindNodeUVE(nodes, at);
            if (top == nullptr || top->owner == 0U || top->owner == top->id) {
                return {};
            }
            const AnimationGraphNodeUVE* owner = FindNodeUVE(nodes, top->owner);
            if (owner == nullptr || owner->kind != Kind::StateMachine || top->ownerState >= owner->inputs.size()) {
                return {};
            }
            return AnimationGraphScopeUVE{owner->id, top->ownerState};
        }
        if (parent->kind == Kind::StateMachine) {
            return AnimationGraphScopeUVE{parent->id, static_cast<std::uint32_t>(slot)};
        }
        at = parent->id;
    }
    return {};
}

std::vector<std::uint32_t> AnimationGraphScopeNodesUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const AnimationGraphScopeUVE scope) {
    std::vector<std::uint32_t> shown;
    for (const AnimationGraphNodeUVE& node : nodes) {
        if (scope.machine != 0U && node.kind == Kind::Output) {
            continue;
        }
        if (AnimationGraphScopeOfUVE(nodes, node.id) == scope) {
            shown.push_back(node.id);
        }
    }
    return shown;
}

std::vector<std::uint32_t> AnimationGraphNodesInsideUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const std::uint32_t id) {
    std::vector<std::uint32_t> inside;
    std::vector<std::uint32_t> machines{id};
    while (!machines.empty()) {
        const std::uint32_t machine = machines.back();
        machines.pop_back();
        const AnimationGraphNodeUVE* node = FindNodeUVE(nodes, machine);
        if (node == nullptr || node->kind != Kind::StateMachine) {
            continue;
        }
        for (std::size_t state = 0U; state < node->inputs.size(); ++state) {
            for (const std::uint32_t member : AnimationGraphScopeNodesUVE(nodes, AnimationGraphScopeUVE{machine, static_cast<std::uint32_t>(state)})) {
                if (std::ranges::find(inside, member) == inside.end() && member != id) {
                    inside.push_back(member);
                    machines.push_back(member);
                }
            }
        }
    }
    return inside;
}

std::string AnimationStateNameUVE(const std::vector<AnimationGraphNodeUVE>& nodes, const AnimationGraphNodeUVE& machine, const std::size_t slot) {
    if (slot < machine.stateNames.size() && !machine.stateNames[slot].empty()) {
        return machine.stateNames[slot];
    }
    if (slot < machine.inputs.size() && machine.inputs[slot] != 0U) {
        if (const AnimationGraphNodeUVE* child = FindNodeUVE(nodes, machine.inputs[slot]); child != nullptr && !child->name.empty()) {
            return child->name;
        }
    }
    return "State " + std::to_string(slot + 1U);
}

} // namespace UVE::Editor
