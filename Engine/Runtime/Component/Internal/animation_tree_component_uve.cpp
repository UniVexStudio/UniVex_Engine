// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/component/animation_tree_component_uve.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphObjectKindUVE;

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
        case Kind::Select:
        case Kind::StateMachine:
            return {1U, true};
        case Kind::BlendSpace1D:
        case Kind::BlendSpace2D:
            return {0U, false};
        case Kind::LayeredBlend:
            return {2U, false};
        case Kind::TimeSeek:
            return {1U, false};
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
        case Kind::BlendSpace2D: return "BlendSpace2D";
        case Kind::Select: return "Select";
        case Kind::LayeredBlend: return "LayeredBlend";
        case Kind::TimeSeek: return "TimeSeek";
    }
    return "?";
}

} // namespace

std::string DescribeAnimationGraphProblemUVE(const AnimationTreeComponentUVE& component) {
    const std::vector<AnimationGraphObjectUVE>& objects = component.objects;
    if (objects.size() > kMaximumAnimationGraphObjectsUVE) {
        return "too many objects";
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
    for (std::size_t index = 0U; index < objects.size(); ++index) {
        const AnimationGraphObjectUVE& object = objects[index];
        if (object.id == 0U || !indexById.emplace(object.id, index).second) {
            return "object ids must be unique and non-zero";
        }
        if (object.kind > Kind::TimeSeek || !IsNameValidUVE(object.name)) {
            return "an object has an invalid kind or name";
        }
        outputs += object.kind == Kind::Output ? 1U : 0U;
    }
    if (outputs != 1U) {
        return "a graph needs exactly one Output object";
    }

    std::unordered_set<std::uint32_t> used;
    for (const AnimationGraphObjectUVE& object : objects) {
        const std::string label = object.name.empty() ? std::string{KindNameUVE(object.kind)} : object.name;
        const InputRuleUVE rule = InputRuleForUVE(object.kind);
        if (object.inputs.size() > kMaximumAnimationObjectInputsUVE ||
            (rule.atLeast ? object.inputs.size() < rule.count : object.inputs.size() != rule.count)) {
            return label + ": wrong number of inputs";
        }
        for (const std::uint32_t input : object.inputs) {
            if (input == 0U) {
                continue; // an empty slot: a graph being built, which evaluates to no pose there
            }
            const auto found = indexById.find(input);
            if (found == indexById.end()) {
                return label + ": an input is not connected to an object";
            }
            if (objects[found->second].kind == Kind::Output) {
                return label + ": the Output object cannot feed another object";
            }
            if (!used.insert(input).second) {
                return label + ": an object feeds more than one input";
            }
        }
        if (!std::isfinite(object.position.x) || !std::isfinite(object.position.y) || !std::isfinite(object.speed) ||
            !std::isfinite(object.value) || !IsFiniteNonNegativeUVE(object.fadeSeconds) ||
            !IsFiniteNonNegativeUVE(object.smoothingSeconds) || object.blendMode > AnimationBlendModeUVE::NearestInStep ||
            !IsNameValidUVE(object.parameter) || !std::isfinite(object.valueY) || !IsNameValidUVE(object.parameterY)) {
            return label + ": invalid value";
        }
        if (!std::isfinite(object.areaMin.x) || !std::isfinite(object.areaMin.y) || !std::isfinite(object.areaMax.x) ||
            !std::isfinite(object.areaMax.y) || object.areaMax.x <= object.areaMin.x || object.areaMax.y <= object.areaMin.y) {
            return label + ": its area's maximum must be above its minimum";
        }
        const bool blendSpace = object.kind == Kind::BlendSpace1D || object.kind == Kind::BlendSpace2D;
        if (!blendSpace && !object.blendPoints.empty()) {
            return label + ": only a Blend Space has blend points";
        }
        if (object.blendPoints.size() > kMaximumAnimationObjectInputsUVE) {
            return label + ": too many points";
        }
        for (std::size_t point = 0U; point < object.blendPoints.size(); ++point) {
            const AnimationBlendPointUVE& at = object.blendPoints[point];
            if (!std::isfinite(at.position.x) || !std::isfinite(at.position.y) || !std::isfinite(at.speed)) {
                return label + ": a point is not a number";
            }
            if (object.kind == Kind::BlendSpace1D && point > 0U && at.position.x <= object.blendPoints[point - 1U].position.x) {
                return label + ": points must rise from left to right";
            }
            for (std::size_t other = 0U; other < point; ++other) {
                if (object.blendPoints[other].position == at.position) {
                    return label + ": two points are in the same place";
                }
            }
        }
        if (object.bones.size() > kMaximumAnimationLayerBonesUVE ||
            std::ranges::any_of(object.bones, [](const std::string& bone) { return bone.empty() || !IsNameValidUVE(bone); })) {
            return label + ": a layer bone has no name, or too long a one";
        }
        if (object.kind == Kind::StateMachine) {
            if (object.entryState >= object.inputs.size() || object.transitions.size() > kMaximumAnimationTransitionsUVE) {
                return label + ": invalid entry state";
            }
            for (const AnimationTransitionUVE& transition : object.transitions) {
                const bool fromValid =
                    transition.fromState == kAnyAnimationStateUVE || transition.fromState < object.inputs.size();
                const bool exitValid = std::isfinite(transition.exitPhase) && transition.exitPhase <= 1.0F;
                if (!fromValid || transition.toState >= object.inputs.size() || !exitValid ||
                    !IsFiniteNonNegativeUVE(transition.fadeSeconds) ||
                    transition.start > AnimationTransitionStartUVE::Continue ||
                    transition.curve > AnimationTransitionCurveUVE::EaseInOut ||
                    transition.conditions.size() > kMaximumAnimationTransitionConditionsUVE) {
                    return label + ": a transition is incomplete";
                }
                for (const AnimationTransitionConditionUVE& test : transition.conditions) {
                    if (test.condition > AnimationConditionUVE::Triggered || !std::isfinite(test.threshold) ||
                        !IsNameValidUVE(test.parameter)) {
                        return label + ": a transition's condition is incomplete";
                    }
                }
            }
            if (object.statePositions.size() > object.inputs.size() ||
                std::ranges::any_of(object.statePositions, [](const Math::Vector2UVE& at) {
                    return !std::isfinite(at.x) || !std::isfinite(at.y);
                }) ||
                !std::isfinite(object.entryPosition.x) || !std::isfinite(object.entryPosition.y) ||
                !std::isfinite(object.anyPosition.x) || !std::isfinite(object.anyPosition.y)) {
                return label + ": a state's place in its view is invalid";
            }
        } else if (!object.transitions.empty() || !object.statePositions.empty()) {
            return label + ": only a StateMachine has transitions";
        }
    }

    // Every object has at most one parent, so a cycle is a walk up the parents that comes back.
    std::unordered_map<std::uint32_t, std::uint32_t> parentOf;
    for (const AnimationGraphObjectUVE& object : objects) {
        for (const std::uint32_t input : object.inputs) {
            if (input != 0U) {
                parentOf[input] = object.id;
            }
        }
    }
    for (const AnimationGraphObjectUVE& object : objects) {
        std::uint32_t current = object.id;
        for (std::size_t steps = 0U; steps <= objects.size(); ++steps) {
            const auto parent = parentOf.find(current);
            if (parent == parentOf.end()) {
                break;
            }
            current = parent->second;
            if (current == object.id) {
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

std::uint32_t NextAnimationGraphObjectIdUVE(const std::vector<AnimationGraphObjectUVE>& objects) noexcept {
    std::uint32_t highest = 0U;
    for (const AnimationGraphObjectUVE& object : objects) {
        highest = std::max(highest, object.id);
    }
    return highest + 1U;
}

std::size_t MigrateBlendSpaceInputsUVE(std::vector<AnimationGraphObjectUVE>& objects,
                                       const std::vector<std::vector<Math::Vector2UVE>>& legacyPositions) {
    std::size_t kept = 0U;
    std::vector<std::uint32_t> folded;
    for (std::size_t index = 0U; index < objects.size() && index < legacyPositions.size(); ++index) {
        AnimationGraphObjectUVE& space = objects[index];
        if ((space.kind != Kind::BlendSpace1D && space.kind != Kind::BlendSpace2D) || space.inputs.empty()) {
            continue;
        }
        const std::vector<Math::Vector2UVE>& positions = legacyPositions[index];
        for (std::size_t slot = 0U; slot < space.inputs.size(); ++slot) {
            AnimationBlendPointUVE point;
            point.position = slot < positions.size() ? positions[slot]
                                                     : Math::Vector2UVE{static_cast<float>(slot), 0.0F};
            const std::uint32_t input = space.inputs[slot];
            const auto fed = std::find_if(objects.begin(), objects.end(),
                                          [input](const AnimationGraphObjectUVE& object) { return input != 0U && object.id == input; });
            if (fed != objects.end() && fed->kind == Kind::Clip) {
                point.clip = fed->clip;
                point.speed = fed->speed;
                point.loop = fed->loop;
                folded.push_back(input);
            } else if (fed != objects.end()) {
                ++kept; // left in the graph, unconnected
            }
            space.blendPoints.push_back(point);
        }
        space.inputs.clear();
    }
    std::erase_if(objects, [&folded](const AnimationGraphObjectUVE& object) {
        return std::find(folded.begin(), folded.end(), object.id) != folded.end();
    });
    return kept;
}

} // namespace UVE::Scene
