// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/editor/animation_graph_editing_uve.h"

#include <algorithm>
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
    return kind == Kind::BlendSpace1D || kind == Kind::StateMachine;
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
    }
    return "?";
}

const char* AnimationGraphKindHelpUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "What the target shows.";
        case Kind::Clip: return "Plays a clip.";
        case Kind::Blend2: return "Mixes A and B by a weight: 0 is A, 1 is B.";
        case Kind::BlendSpace1D:
            return "Places its inputs along a line and mixes the two either side of a value - walk, jog, run by speed.";
        case Kind::Additive: return "Lays the Layer's motion on top of the Base, scaled by a weight.";
        case Kind::OneShot: return "Plays Shot once over Base when a trigger fires, fading in and out.";
        case Kind::TimeScale: return "Runs its input faster or slower.";
        case Kind::StateMachine: return "Its inputs are states. Transitions move between them and crossfade.";
    }
    return "";
}

std::string AnimationGraphSlotLabelUVE(const Kind kind, const std::size_t slot) {
    switch (kind) {
        case Kind::Blend2: return slot == 0U ? "A" : "B";
        case Kind::Additive: return slot == 0U ? "Base" : "Layer";
        case Kind::OneShot: return slot == 0U ? "Base" : "Shot";
        case Kind::StateMachine: return "State " + std::to_string(slot + 1U);
        case Kind::BlendSpace1D: return "Point " + std::to_string(slot + 1U);
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
            added.inputs = {0U, 0U};
            added.points = {0.0F, 1.0F};
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
        node->inputs.size() >= Scene::kMaximumAnimationNodeInputsUVE) {
        return false;
    }
    node->inputs.push_back(0U);
    if (node->kind == Kind::BlendSpace1D) {
        node->points.push_back(node->points.empty() ? 0.0F : node->points.back() + 1.0F);
    }
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
    if (node->kind == Kind::BlendSpace1D && slot < node->points.size()) {
        node->points.erase(node->points.begin() + static_cast<std::ptrdiff_t>(slot));
    }
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
    }
    return true;
}

} // namespace UVE::Editor
