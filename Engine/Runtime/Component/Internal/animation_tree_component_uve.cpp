// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_tree_component_uve.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphNodeKindUVE;

[[nodiscard]] bool IsNameValidUVE(const std::string& name) noexcept {
    return name.size() <= kMaximumAnimationNameBytesUVE && name.find('\0') == std::string::npos;
}

[[nodiscard]] bool IsFiniteNonNegativeUVE(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

/// Inputs a kind needs: exactly, or at least when `atLeast`.
struct InputRuleUVE final {
    std::size_t count = 0U;
    bool atLeast = false;
};

[[nodiscard]] InputRuleUVE InputRuleForUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output:
        case Kind::TimeScale:
            return {1U, false};
        case Kind::Clip:
            return {0U, false};
        case Kind::Blend2:
        case Kind::Additive:
        case Kind::OneShot:
            return {2U, false};
        case Kind::BlendSpace1D:
        case Kind::StateMachine:
            return {1U, true};
    }
    return {0U, false};
}

[[nodiscard]] const char* KindNameUVE(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "Output";
        case Kind::Clip: return "Clip";
        case Kind::Blend2: return "Blend2";
        case Kind::BlendSpace1D: return "BlendSpace1D";
        case Kind::Additive: return "Additive";
        case Kind::OneShot: return "OneShot";
        case Kind::TimeScale: return "TimeScale";
        case Kind::StateMachine: return "StateMachine";
    }
    return "?";
}

} // namespace

std::string DescribeAnimationGraphProblemUVE(const AnimationTreeComponentUVE& component) {
    const std::vector<AnimationGraphNodeUVE>& nodes = component.nodes;
    if (nodes.size() > kMaximumAnimationGraphNodesUVE) {
        return "too many nodes";
    }
    if (component.parameters.size() > kMaximumAnimationParametersUVE) {
        return "too many parameters";
    }

    std::unordered_map<std::string, AnimationParameterTypeUVE> parameterTypes;
    for (const AnimationParameterUVE& parameter : component.parameters) {
        if (parameter.name.empty() || !IsNameValidUVE(parameter.name)) {
            return "a parameter has no name, or its name is too long";
        }
        if (parameter.type > AnimationParameterTypeUVE::Trigger || !std::isfinite(parameter.value)) {
            return "parameter \"" + parameter.name + "\" has an invalid type or value";
        }
        if (!parameterTypes.emplace(parameter.name, parameter.type).second) {
            return "two parameters are named \"" + parameter.name + "\"";
        }
    }

    std::unordered_map<std::uint32_t, std::size_t> indexById;
    std::size_t outputs = 0U;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const AnimationGraphNodeUVE& node = nodes[index];
        if (node.id == 0U || !indexById.emplace(node.id, index).second) {
            return "node ids must be unique and non-zero";
        }
        if (node.kind > Kind::StateMachine || !IsNameValidUVE(node.name)) {
            return "a node has an invalid kind or name";
        }
        outputs += node.kind == Kind::Output ? 1U : 0U;
    }
    if (outputs != 1U) {
        return "a graph needs exactly one Output node";
    }

    std::unordered_set<std::uint32_t> used;
    for (const AnimationGraphNodeUVE& node : nodes) {
        const std::string label = node.name.empty() ? std::string{KindNameUVE(node.kind)} : node.name;
        const InputRuleUVE rule = InputRuleForUVE(node.kind);
        if (node.inputs.size() > kMaximumAnimationNodeInputsUVE ||
            (rule.atLeast ? node.inputs.size() < rule.count : node.inputs.size() != rule.count)) {
            return label + ": wrong number of inputs";
        }
        for (const std::uint32_t input : node.inputs) {
            if (input == 0U) {
                continue; // an empty slot: a graph being built, which evaluates to no pose there
            }
            const auto found = indexById.find(input);
            if (found == indexById.end()) {
                return label + ": an input is not connected to a node";
            }
            if (nodes[found->second].kind == Kind::Output) {
                return label + ": the Output node cannot feed another node";
            }
            if (!used.insert(input).second) {
                return label + ": a node feeds more than one input";
            }
        }
        if (!std::isfinite(node.position.x) || !std::isfinite(node.position.y) || !std::isfinite(node.speed) ||
            !std::isfinite(node.value) || !IsFiniteNonNegativeUVE(node.fadeSeconds) ||
            !IsNameValidUVE(node.parameter)) {
            return label + ": invalid value";
        }
        if (node.kind == Kind::BlendSpace1D) {
            if (node.points.size() != node.inputs.size()) {
                return label + ": needs one point per input";
            }
            for (std::size_t point = 0U; point < node.points.size(); ++point) {
                if (!std::isfinite(node.points[point]) ||
                    (point > 0U && node.points[point] <= node.points[point - 1U])) {
                    return label + ": points must rise from left to right";
                }
            }
        }
        if (node.kind == Kind::StateMachine) {
            if (node.entryState >= node.inputs.size() || node.transitions.size() > kMaximumAnimationTransitionsUVE) {
                return label + ": invalid entry state";
            }
            for (const AnimationTransitionUVE& transition : node.transitions) {
                const bool fromValid =
                    transition.fromState == kAnyAnimationStateUVE || transition.fromState < node.inputs.size();
                if (!fromValid || transition.toState >= node.inputs.size() ||
                    transition.condition > AnimationConditionUVE::Triggered ||
                    !IsFiniteNonNegativeUVE(transition.fadeSeconds) || !std::isfinite(transition.threshold) ||
                    !IsNameValidUVE(transition.parameter)) {
                    return label + ": a transition is incomplete";
                }
            }
        } else if (!node.transitions.empty()) {
            return label + ": only a StateMachine has transitions";
        }
    }

    // Every node has at most one parent, so a cycle is a walk up the parents that comes back.
    std::unordered_map<std::uint32_t, std::uint32_t> parentOf;
    for (const AnimationGraphNodeUVE& node : nodes) {
        for (const std::uint32_t input : node.inputs) {
            if (input != 0U) {
                parentOf[input] = node.id;
            }
        }
    }
    for (const AnimationGraphNodeUVE& node : nodes) {
        std::uint32_t current = node.id;
        for (std::size_t steps = 0U; steps <= nodes.size(); ++steps) {
            const auto parent = parentOf.find(current);
            if (parent == parentOf.end()) {
                break;
            }
            current = parent->second;
            if (current == node.id) {
                return "the graph has a loop";
            }
        }
    }
    return {};
}

bool IsAnimationTreeComponentValidUVE(const AnimationTreeComponentUVE& component) noexcept {
    try {
        return DescribeAnimationGraphProblemUVE(component).empty();
    } catch (...) {
        return false;
    }
}

std::uint32_t NextAnimationGraphNodeIdUVE(const std::vector<AnimationGraphNodeUVE>& nodes) noexcept {
    std::uint32_t highest = 0U;
    for (const AnimationGraphNodeUVE& node : nodes) {
        highest = std::max(highest, node.id);
    }
    return highest + 1U;
}

} // namespace UVE::Scene
