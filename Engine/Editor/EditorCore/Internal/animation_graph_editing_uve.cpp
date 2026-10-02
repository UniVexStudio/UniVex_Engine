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

using Kind = Scene::AnimationGraphObjectKindUVE;
using Scene::AnimationGraphObjectUVE;

[[nodiscard]] AnimationGraphObjectUVE* FindObjectUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t id) {
    const auto it = std::ranges::find(objects, id, &AnimationGraphObjectUVE::id);
    return it == objects.end() || id == 0U ? nullptr : &*it;
}

[[nodiscard]] const AnimationGraphObjectUVE* FindObjectUVE(const std::vector<AnimationGraphObjectUVE>& objects,
                                                       const std::uint32_t id) {
    const auto it = std::ranges::find(objects, id, &AnimationGraphObjectUVE::id);
    return it == objects.end() || id == 0U ? nullptr : &*it;
}

/// True when `wanted` is `from` or feeds it, directly or through other objects.
[[nodiscard]] bool IsUpstreamUVE(const std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t from,
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
        if (const AnimationGraphObjectUVE* const object = FindObjectUVE(objects, id)) {
            for (const std::uint32_t input : object->inputs) {
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

std::uint32_t AddAnimationGraphObjectUVE(std::vector<AnimationGraphObjectUVE>& objects, const Kind kind,
                                       const Math::Vector2UVE position) {
    if (kind == Kind::Output || objects.size() >= Scene::kMaximumAnimationGraphObjectsUVE) {
        return 0U;
    }
    AnimationGraphObjectUVE added;
    added.id = Scene::NextAnimationGraphObjectIdUVE(objects);
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
    objects.push_back(std::move(added));
    return objects.back().id;
}

bool CanConnectAnimationGraphObjectsUVE(const std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t target,
                                      const std::size_t slot, const std::uint32_t source) {
    const AnimationGraphObjectUVE* const to = FindObjectUVE(objects, target);
    const AnimationGraphObjectUVE* const from = FindObjectUVE(objects, source);
    if (to == nullptr || from == nullptr || slot >= to->inputs.size() || from->kind == Kind::Output ||
        source == target) {
        return false;
    }
    // A cycle: the target already feeds the source.
    return !IsUpstreamUVE(objects, source, target);
}

bool ConnectAnimationGraphObjectsUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t target,
                                   const std::size_t slot, const std::uint32_t source) {
    if (!CanConnectAnimationGraphObjectsUVE(objects, target, slot, source)) {
        return false;
    }
    for (AnimationGraphObjectUVE& object : objects) {
        std::ranges::replace(object.inputs, source, 0U);
    }
    FindObjectUVE(objects, target)->inputs[slot] = source;
    return true;
}

bool DisconnectAnimationGraphInputUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t target,
                                      const std::size_t slot) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, target);
    if (object == nullptr || slot >= object->inputs.size() || object->inputs[slot] == 0U) {
        return false;
    }
    object->inputs[slot] = 0U;
    return true;
}

std::size_t DeleteAnimationGraphObjectsUVE(std::vector<AnimationGraphObjectUVE>& objects,
                                         const std::vector<std::uint32_t>& ids) {
    const auto doomed = [&ids](const AnimationGraphObjectUVE& object) {
        return object.kind != Kind::Output && std::ranges::find(ids, object.id) != ids.end();
    };
    std::vector<std::uint32_t> removed;
    for (const AnimationGraphObjectUVE& object : objects) {
        if (doomed(object)) {
            removed.push_back(object.id);
        }
    }
    std::erase_if(objects, doomed);
    for (AnimationGraphObjectUVE& object : objects) {
        for (std::uint32_t& input : object.inputs) {
            if (std::ranges::find(removed, input) != removed.end()) {
                input = 0U;
            }
        }
    }
    return removed.size();
}

std::vector<std::uint32_t> DuplicateAnimationGraphObjectsUVE(std::vector<AnimationGraphObjectUVE>& objects,
                                                           const std::vector<std::uint32_t>& ids,
                                                           const Math::Vector2UVE offset) {
    std::unordered_map<std::uint32_t, std::uint32_t> copyOf;
    std::vector<AnimationGraphObjectUVE> copies;
    std::uint32_t nextId = Scene::NextAnimationGraphObjectIdUVE(objects);
    for (const std::uint32_t id : ids) {
        const AnimationGraphObjectUVE* const original = FindObjectUVE(std::as_const(objects), id);
        if (original == nullptr || original->kind == Kind::Output || copyOf.contains(id) ||
            objects.size() + copies.size() >= Scene::kMaximumAnimationGraphObjectsUVE) {
            continue;
        }
        AnimationGraphObjectUVE copy = *original;
        copy.id = nextId++;
        copy.position = copy.position + offset;
        copyOf.emplace(id, copy.id);
        copies.push_back(std::move(copy));
    }
    std::vector<std::uint32_t> created;
    for (AnimationGraphObjectUVE& copy : copies) {
        for (std::uint32_t& input : copy.inputs) {
            const auto it = copyOf.find(input);
            input = it == copyOf.end() ? 0U : it->second;
        }
        created.push_back(copy.id);
        objects.push_back(std::move(copy));
    }
    return created;
}

bool AddAnimationGraphInputSlotUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t target) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, target);
    if (object == nullptr || !HasVariableSlotsUVE(object->kind) ||
        object->inputs.size() >= Scene::kMaximumAnimationObjectInputsUVE) {
        return false;
    }
    object->inputs.push_back(0U);
    return true;
}

bool RemoveAnimationGraphInputSlotUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t target,
                                      const std::size_t slot) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, target);
    if (object == nullptr || !HasVariableSlotsUVE(object->kind) || slot >= object->inputs.size() ||
        object->inputs.size() <= 1U) {
        return false;
    }
    object->inputs.erase(object->inputs.begin() + static_cast<std::ptrdiff_t>(slot));
    if (object->kind == Kind::StateMachine) {
        const auto state = static_cast<std::uint32_t>(slot);
        std::erase_if(object->transitions, [state](const Scene::AnimationTransitionUVE& transition) {
            return transition.fromState == state || transition.toState == state;
        });
        for (Scene::AnimationTransitionUVE& transition : object->transitions) {
            if (transition.fromState != Scene::kAnyAnimationStateUVE && transition.fromState > state) {
                --transition.fromState;
            }
            if (transition.toState > state) {
                --transition.toState;
            }
        }
        object->entryState = std::min(object->entryState, static_cast<std::uint32_t>(object->inputs.size() - 1U));
        if (slot < object->statePositions.size()) {
            object->statePositions.erase(object->statePositions.begin() + static_cast<std::ptrdiff_t>(slot));
        }
    }
    return true;
}

Math::Vector2UVE AnimationStatePositionUVE(const AnimationGraphObjectUVE& machine, const std::size_t slot) {
    if (slot < machine.statePositions.size()) {
        return machine.statePositions[slot];
    }
    // Not placed yet: a grid three wide, right of Entry.
    constexpr float kColumnUVE = 200.0F;
    constexpr float kRowUVE = 110.0F;
    return Math::Vector2UVE{static_cast<float>(slot % 3U) * kColumnUVE, static_cast<float>(slot / 3U) * kRowUVE};
}

bool SetAnimationStatePositionUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t machine,
                                  const std::size_t slot, const Math::Vector2UVE position) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, machine);
    if (object == nullptr || object->kind != Kind::StateMachine || slot >= object->inputs.size() || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return false;
    }
    // The states before it keep where they are drawn now.
    while (object->statePositions.size() <= slot) {
        object->statePositions.push_back(AnimationStatePositionUVE(*object, object->statePositions.size()));
    }
    object->statePositions[slot] = position;
    return true;
}

std::optional<std::size_t> AddAnimationStateUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t machine,
                                                const Math::Vector2UVE position) {
    AnimationGraphObjectUVE* object = FindObjectUVE(objects, machine);
    if (object == nullptr || object->kind != Kind::StateMachine || object->inputs.size() >= Scene::kMaximumAnimationObjectInputsUVE ||
        objects.size() >= Scene::kMaximumAnimationGraphObjectsUVE) {
        return std::nullopt;
    }
    // A state plays something: it starts as a Clip, set beside the machine in the tree, whose
    // animation is picked next.
    const Math::Vector2UVE beside{object->position.x - 240.0F, object->position.y + static_cast<float>(object->inputs.size()) * 70.0F};
    const std::uint32_t clip = AddAnimationGraphObjectUVE(objects, Kind::Clip, beside);
    if (clip == 0U) {
        return std::nullopt;
    }
    object = FindObjectUVE(objects, machine); // the add may have moved the objects
    std::size_t slot = object->inputs.size();
    if (slot == 1U && object->inputs[0] == 0U) {
        slot = 0U; // a new machine's empty first state takes it
    } else {
        object->inputs.push_back(0U);
    }
    object->inputs[slot] = clip;
    // Named for the state it plays, so the view and the tree read the same.
    FindObjectUVE(objects, clip)->name = "State " + std::to_string(slot + 1U);
    static_cast<void>(SetAnimationStatePositionUVE(objects, machine, slot, position));
    return slot;
}

std::optional<std::size_t> AddAnimationTransitionUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t machine,
                                                     const std::uint32_t from, const std::uint32_t to) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, machine);
    if (object == nullptr || object->kind != Kind::StateMachine || to >= object->inputs.size() || from == to ||
        (from != Scene::kAnyAnimationStateUVE && from >= object->inputs.size()) ||
        object->transitions.size() >= Scene::kMaximumAnimationTransitionsUVE) {
        return std::nullopt;
    }
    Scene::AnimationTransitionUVE transition;
    transition.fromState = from;
    transition.toState = to;
    object->transitions.push_back(transition);
    return object->transitions.size() - 1U;
}

bool RemoveAnimationTransitionUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t machine,
                                  const std::size_t index) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, machine);
    if (object == nullptr || object->kind != Kind::StateMachine || index >= object->transitions.size()) {
        return false;
    }
    object->transitions.erase(object->transitions.begin() + static_cast<std::ptrdiff_t>(index));
    return true;
}

bool MoveAnimationTransitionUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t machine,
                                const std::size_t index, const std::size_t newIndex) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, machine);
    if (object == nullptr || object->kind != Kind::StateMachine || index >= object->transitions.size() ||
        newIndex >= object->transitions.size() || index == newIndex) {
        return false;
    }
    const Scene::AnimationTransitionUVE moved = object->transitions[index];
    object->transitions.erase(object->transitions.begin() + static_cast<std::ptrdiff_t>(index));
    object->transitions.insert(object->transitions.begin() + static_cast<std::ptrdiff_t>(newIndex), moved);
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

std::optional<std::size_t> AddBlendSpacePointUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t space,
                                                 const Math::Vector2UVE position, const Asset::AssetGuidUVE clip) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, space);
    if (object == nullptr || (object->kind != Kind::BlendSpace1D && object->kind != Kind::BlendSpace2D) ||
        object->blendPoints.size() >= Scene::kMaximumAnimationObjectInputsUVE || !std::isfinite(position.x) ||
        !std::isfinite(position.y)) {
        return std::nullopt;
    }
    Scene::AnimationBlendPointUVE point;
    point.position = object->kind == Kind::BlendSpace1D ? Math::Vector2UVE{position.x, 0.0F} : position;
    point.clip = clip;
    if (std::ranges::find(object->blendPoints, point.position, &Scene::AnimationBlendPointUVE::position) !=
        object->blendPoints.end()) {
        return std::nullopt;
    }
    // A line keeps its points rising, so the new one goes where its x belongs.
    std::size_t slot = object->blendPoints.size();
    if (object->kind == Kind::BlendSpace1D) {
        slot = static_cast<std::size_t>(std::ranges::find_if(object->blendPoints, [&point](const Scene::AnimationBlendPointUVE& other) {
                                            return other.position.x > point.position.x;
                                        }) -
                                        object->blendPoints.begin());
    }
    object->blendPoints.insert(object->blendPoints.begin() + static_cast<std::ptrdiff_t>(slot), point);
    return slot;
}

bool RemoveBlendSpacePointUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t space, const std::size_t slot) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, space);
    if (object == nullptr || (object->kind != Kind::BlendSpace1D && object->kind != Kind::BlendSpace2D) ||
        slot >= object->blendPoints.size()) {
        return false;
    }
    object->blendPoints.erase(object->blendPoints.begin() + static_cast<std::ptrdiff_t>(slot));
    return true;
}

bool MoveBlendSpacePointUVE(std::vector<AnimationGraphObjectUVE>& objects, const std::uint32_t space, const std::size_t slot,
                            const Math::Vector2UVE position) {
    AnimationGraphObjectUVE* const object = FindObjectUVE(objects, space);
    if (object == nullptr || (object->kind != Kind::BlendSpace1D && object->kind != Kind::BlendSpace2D) ||
        slot >= object->blendPoints.size() || !std::isfinite(position.x) || !std::isfinite(position.y)) {
        return false;
    }
    std::vector<Scene::AnimationBlendPointUVE>& points = object->blendPoints;
    if (object->kind == Kind::BlendSpace1D) {
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
