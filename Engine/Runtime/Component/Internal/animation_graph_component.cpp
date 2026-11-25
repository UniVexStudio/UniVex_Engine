// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_graph_component.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphNodeKind;

[[nodiscard]] bool IsNameValid(const std::string& name) noexcept {
    return name.size() <= kMaximumAnimationNameBytes && name.find('\0') == std::string::npos;
}

[[nodiscard]] bool IsFiniteNonNegative(const float value) noexcept {
    return std::isfinite(value) && value >= 0.0F;
}

/// Inputs a kind needs: exactly, or at least when `atLeast`.
struct InputRule final {
    std::size_t count = 0U;
    bool atLeast = false;
};

[[nodiscard]] InputRule InputRuleFor(const Kind kind) noexcept {
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
        case Kind::Select:
        case Kind::StateMachine:
            return {1U, true};
        case Kind::BlendSpace1D:
        case Kind::BlendSpace2D:
            return {0U, false};
        case Kind::LayeredBlend:
            return {2U, false};
        case Kind::TimeSeek:
        case Kind::Slot:
        case Kind::TwoBoneIK:
        case Kind::ModifyBone:
        case Kind::LookAt:
            return {1U, false};
        case Kind::BlendByBool:
            return {2U, false};
    }
    return {0U, false};
}

[[nodiscard]] const char* KindName(const Kind kind) noexcept {
    switch (kind) {
        case Kind::Output: return "Output";
        case Kind::Clip: return "Clip";
        case Kind::Blend2: return "Blend2";
        case Kind::BlendSpace1D: return "BlendSpace1D";
        case Kind::Additive: return "Additive";
        case Kind::OneShot: return "OneShot";
        case Kind::TimeScale: return "TimeScale";
        case Kind::StateMachine: return "StateMachine";
        case Kind::BlendSpace2D: return "BlendSpace2D";
        case Kind::Select: return "Select";
        case Kind::LayeredBlend: return "LayeredBlend";
        case Kind::TimeSeek: return "TimeSeek";
        case Kind::Slot: return "Slot";
        case Kind::TwoBoneIK: return "TwoBoneIK";
        case Kind::ModifyBone: return "ModifyBone";
        case Kind::LookAt: return "LookAt";
        case Kind::BlendByBool: return "BlendByBool";
    }
    return "?";
}

} // namespace

std::string DescribeAnimationGraphProblem(const AnimationGraphComponent& component) {
    const std::vector<AnimationGraphNode>& nodes = component.nodes;
    if (nodes.size() > kMaximumAnimationGraphNodes) {
        return "too many nodes";
    }
    if (component.comments.size() > 512U) {
        return "too many comments";
    }
    for (const AnimationGraphComment& comment : component.comments) {
        if (comment.title.size() > 256U || !std::isfinite(comment.position.x) || !std::isfinite(comment.position.y) ||
            !std::isfinite(comment.size.x) || !std::isfinite(comment.size.y) || comment.size.x < 0.0F || comment.size.y < 0.0F) {
            return "a comment has an invalid title or box";
        }
    }
    if (component.parameters.size() > kMaximumAnimationParameters) {
        return "too many parameters";
    }

    std::unordered_map<std::string, AnimationParameterType> parameterTypes;
    for (const AnimationParameter& parameter : component.parameters) {
        if (parameter.name.empty() || !IsNameValid(parameter.name)) {
            return "a parameter has no name, or its name is too long";
        }
        if (parameter.type > AnimationParameterType::Vector3 || !std::isfinite(parameter.value) ||
            !std::isfinite(parameter.vector.x) || !std::isfinite(parameter.vector.y) || !std::isfinite(parameter.vector.z) ||
            parameter.source > AnimationParameterSource::CharacterGrounded) {
            return "parameter \"" + parameter.name + "\" has an invalid type or value";
        }
        if (!parameterTypes.emplace(parameter.name, parameter.type).second) {
            return "two parameters are named \"" + parameter.name + "\"";
        }
    }

    std::unordered_map<std::uint32_t, std::size_t> indexById;
    std::size_t outputs = 0U;
    for (std::size_t index = 0U; index < nodes.size(); ++index) {
        const AnimationGraphNode& node = nodes[index];
        if (node.id == 0U || !indexById.emplace(node.id, index).second) {
            return "node ids must be unique and non-zero";
        }
        if (node.kind > Kind::BlendByBool || !IsNameValid(node.name)) {
            return "a node has an invalid kind or name";
        }
        outputs += node.kind == Kind::Output ? 1U : 0U;
    }
    if (outputs != 1U) {
        return "a graph needs exactly one Output node";
    }

    std::unordered_set<std::uint32_t> used;
    for (const AnimationGraphNode& node : nodes) {
        const std::string label = node.name.empty() ? std::string{KindName(node.kind)} : node.name;
        const InputRule rule = InputRuleFor(node.kind);
        if (node.inputs.size() > kMaximumAnimationObjectInputs ||
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
            !std::isfinite(node.value) || !IsFiniteNonNegative(node.fadeSeconds) ||
            !IsFiniteNonNegative(node.smoothingSeconds) || node.blendMode > AnimationBlendMode::NearestInStep ||
            !IsNameValid(node.parameter) || !std::isfinite(node.valueY) || !IsNameValid(node.parameterY)) {
            return label + ": invalid value";
        }
        const auto finite3 = [](const Math::Vector3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
        if (!finite3(node.target) || !finite3(node.pole) || !finite3(node.rotationDegrees) || !finite3(node.boneScale) ||
            node.targetSource > AnimationGraphTargetSource::Bone || node.modifyMode > AnimationBoneModifyMode::Replace ||
            node.space > AnimationBoneSpace::Component || !std::isfinite(node.clampDegrees) || node.clampDegrees < 0.0F ||
            !IsNameValid(node.targetBone) || !IsNameValid(node.alphaParameter) || !IsFiniteNonNegative(node.fadeBackSeconds) ||
            !IsFiniteNonNegative(node.startSeconds) || !std::isfinite(node.fadeOutSeconds) ||
            !IsFiniteNonNegative(node.autoRestartDelaySeconds)) {
            return label + ": invalid value";
        }
        if (node.kind == Kind::TwoBoneIK && node.bones.size() > 3U) {
            return label + ": a Two Bone IK names three bones: root, middle, tip";
        }
        if (!std::isfinite(node.areaMin.x) || !std::isfinite(node.areaMin.y) || !std::isfinite(node.areaMax.x) ||
            !std::isfinite(node.areaMax.y) || node.areaMax.x <= node.areaMin.x || node.areaMax.y <= node.areaMin.y) {
            return label + ": its area's maximum must be above its minimum";
        }
        const bool blendSpace = node.kind == Kind::BlendSpace1D || node.kind == Kind::BlendSpace2D;
        if (!blendSpace && !node.blendPoints.empty()) {
            return label + ": only a Blend Space has blend points";
        }
        if (node.blendPoints.size() > kMaximumAnimationObjectInputs) {
            return label + ": too many points";
        }
        for (std::size_t point = 0U; point < node.blendPoints.size(); ++point) {
            const AnimationBlendPoint& at = node.blendPoints[point];
            if (!std::isfinite(at.position.x) || !std::isfinite(at.position.y) || !std::isfinite(at.speed)) {
                return label + ": a point is not a number";
            }
            if (node.kind == Kind::BlendSpace1D && point > 0U && at.position.x <= node.blendPoints[point - 1U].position.x) {
                return label + ": points must rise from left to right";
            }
            for (std::size_t other = 0U; other < point; ++other) {
                if (node.blendPoints[other].position == at.position) {
                    return label + ": two points are in the same place";
                }
            }
        }
        if (node.bones.size() > kMaximumAnimationLayerBones ||
            std::ranges::any_of(node.bones, [](const std::string& bone) { return bone.empty() || !IsNameValid(bone); })) {
            return label + ": a layer bone has no name, or too long a one";
        }
        if (node.kind == Kind::StateMachine) {
            if (node.entryState >= node.inputs.size() || node.transitions.size() > kMaximumAnimationTransitions) {
                return label + ": invalid entry state";
            }
            for (const AnimationGraphTransition& transition : node.transitions) {
                const bool fromValid =
                    transition.fromState == kAnyAnimationState || transition.fromState < node.inputs.size();
                const bool exitValid = std::isfinite(transition.exitPhase) && transition.exitPhase <= 1.0F;
                if (!fromValid || transition.toState >= node.inputs.size() || !exitValid ||
                    !IsFiniteNonNegative(transition.fadeSeconds) ||
                    transition.start > AnimationTransitionStart::Continue ||
                    transition.curve > AnimationTransitionCurve::EaseInOut ||
                    transition.conditions.size() > kMaximumAnimationTransitionConditions) {
                    return label + ": a transition is incomplete";
                }
                for (const AnimationTransitionCondition& test : transition.conditions) {
                    if (test.condition > AnimationCondition::Triggered || !std::isfinite(test.threshold) ||
                        !IsNameValid(test.parameter)) {
                        return label + ": a transition's condition is incomplete";
                    }
                }
            }
            if (node.statePositions.size() > node.inputs.size() ||
                std::ranges::any_of(node.statePositions, [](const Math::Vector2& at) {
                    return !std::isfinite(at.x) || !std::isfinite(at.y);
                }) ||
                !std::isfinite(node.entryPosition.x) || !std::isfinite(node.entryPosition.y) ||
                !std::isfinite(node.anyPosition.x) || !std::isfinite(node.anyPosition.y)) {
                return label + ": a state's place in its view is invalid";
            }
            if (node.stateNames.size() > node.inputs.size() ||
                std::ranges::any_of(node.stateNames, [](const std::string& name) { return !IsNameValid(name); })) {
                return label + ": a state's name is invalid";
            }
        } else if (!node.transitions.empty() || !node.statePositions.empty() || !node.stateNames.empty()) {
            return label + ": only a StateMachine has transitions";
        }
        if (node.gridX < 1U || node.gridX > 64U || node.gridY < 1U || node.gridY > 64U || !IsNameValid(node.axisNameX) ||
            !IsNameValid(node.axisNameY)) {
            return label + ": its grid or axis names are invalid";
        }
    }

    // Every node has at most one parent, so a cycle is a walk up the parents that comes back.
    std::unordered_map<std::uint32_t, std::uint32_t> parentOf;
    for (const AnimationGraphNode& node : nodes) {
        for (const std::uint32_t input : node.inputs) {
            if (input != 0U) {
                parentOf[input] = node.id;
            }
        }
    }
    for (const AnimationGraphNode& node : nodes) {
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

bool IsAnimationGraphComponentValid(const AnimationGraphComponent& component) noexcept {
    try {
        return DescribeAnimationGraphProblem(component).empty();
    } catch (...) {
        return false;
    }
}

std::uint32_t NextAnimationGraphNodeId(const std::vector<AnimationGraphNode>& nodes) noexcept {
    std::uint32_t highest = 0U;
    for (const AnimationGraphNode& node : nodes) {
        highest = std::max(highest, node.id);
    }
    return highest + 1U;
}

std::size_t MigrateBlendSpaceInputs(std::vector<AnimationGraphNode>& nodes,
                                       const std::vector<std::vector<Math::Vector2>>& legacyPositions) {
    std::size_t kept = 0U;
    std::vector<std::uint32_t> folded;
    for (std::size_t index = 0U; index < nodes.size() && index < legacyPositions.size(); ++index) {
        AnimationGraphNode& space = nodes[index];
        if ((space.kind != Kind::BlendSpace1D && space.kind != Kind::BlendSpace2D) || space.inputs.empty()) {
            continue;
        }
        const std::vector<Math::Vector2>& positions = legacyPositions[index];
        for (std::size_t slot = 0U; slot < space.inputs.size(); ++slot) {
            AnimationBlendPoint point;
            point.position = slot < positions.size() ? positions[slot]
                                                     : Math::Vector2{static_cast<float>(slot), 0.0F};
            const std::uint32_t input = space.inputs[slot];
            const auto fed = std::find_if(nodes.begin(), nodes.end(),
                                          [input](const AnimationGraphNode& node) { return input != 0U && node.id == input; });
            if (fed != nodes.end() && fed->kind == Kind::Clip) {
                point.clip = fed->clip;
                point.speed = fed->speed;
                point.loop = fed->loop;
                folded.push_back(input);
            } else if (fed != nodes.end()) {
                ++kept; // left in the graph, unconnected
            }
            space.blendPoints.push_back(point);
        }
        space.inputs.clear();
    }
    std::erase_if(nodes, [&folded](const AnimationGraphNode& node) {
        return std::find(folded.begin(), folded.end(), node.id) != folded.end();
    });
    return kept;
}

} // namespace UVE::Scene
