// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_tree_uve.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "uve/animation/time_pose_contract_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphNodeKindUVE;
using PoseUVE = Core::TransformPoseUVE;

/// What a node hands its parent: a pose when it has one (a clip may still be loading), and whether
/// its animation reached its end this step - what AtEnd transitions and one-shots wait for.
struct ResultUVE final {
    std::optional<PoseUVE> pose;
    bool atEnd = false;
};

[[nodiscard]] Math::Vector3UVE LerpUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to, const float alpha) noexcept {
    return from + (to - from) * alpha;
}

[[nodiscard]] Math::QuaternionUVE SlerpUVE(const Math::QuaternionUVE& from, const Math::QuaternionUVE& to,
                                           const float alpha) noexcept {
    Math::QuaternionUVE result = to;
    if (!Math::TrySlerpUVE(from, to, alpha, result)) {
        result = alpha < 0.5F ? from : to;
    }
    return result;
}

/// Mixes two results; a side without a pose leaves the other as it is.
[[nodiscard]] std::optional<PoseUVE> MixUVE(const std::optional<PoseUVE>& from, const std::optional<PoseUVE>& to,
                                            const float weight) noexcept {
    if (!from.has_value()) {
        return to;
    }
    if (!to.has_value()) {
        return from;
    }
    const float alpha = std::clamp(weight, 0.0F, 1.0F);
    return PoseUVE{LerpUVE(from->position, to->position, alpha), SlerpUVE(from->rotation, to->rotation, alpha),
                   LerpUVE(from->scale, to->scale, alpha)};
}

class EvaluatorUVE final {
public:
    EvaluatorUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips) : m_tree(tree), m_clips(clips) {
        for (std::size_t index = 0U; index < tree.nodes.size(); ++index) {
            m_indexById.emplace(tree.nodes[index].id, index);
        }
    }

    [[nodiscard]] std::optional<std::size_t> OutputIndexUVE() const {
        for (std::size_t index = 0U; index < m_tree.nodes.size(); ++index) {
            if (m_tree.nodes[index].kind == Kind::Output) {
                return index;
            }
        }
        return std::nullopt;
    }

    [[nodiscard]] ResultUVE EvaluateUVE(const std::size_t index, const float deltaSeconds) {
        const AnimationGraphNodeUVE& node = m_tree.nodes[index];
        AnimationGraphNodeStateUVE& state = m_tree.nodeStates[index];
        switch (node.kind) {
            case Kind::Output:
            case Kind::TimeScale: {
                const float rate = node.kind == Kind::TimeScale ? ReadUVE(node.parameter, node.speed) : 1.0F;
                return EvaluateInputUVE(node, 0U, deltaSeconds * rate);
            }
            case Kind::Clip:
                return EvaluateClipUVE(node, state, deltaSeconds);
            case Kind::Blend2: {
                const float weight = std::clamp(ReadUVE(node.parameter, node.value), 0.0F, 1.0F);
                const ResultUVE first = EvaluateInputUVE(node, 0U, deltaSeconds);
                const ResultUVE second = EvaluateInputUVE(node, 1U, deltaSeconds);
                return ResultUVE{MixUVE(first.pose, second.pose, weight), weight < 0.5F ? first.atEnd : second.atEnd};
            }
            case Kind::BlendSpace1D:
                return EvaluateBlendSpaceUVE(node, deltaSeconds);
            case Kind::Additive:
                return EvaluateAdditiveUVE(node, deltaSeconds);
            case Kind::OneShot:
                return EvaluateOneShotUVE(node, state, deltaSeconds);
            case Kind::StateMachine:
                return EvaluateStateMachineUVE(node, state, deltaSeconds);
        }
        return {};
    }

    void ResetSubtreeUVE(const std::size_t index) {
        const AnimationGraphNodeUVE& node = m_tree.nodes[index];
        AnimationGraphNodeStateUVE& state = m_tree.nodeStates[index];
        state = AnimationGraphNodeStateUVE{};
        state.activeState = node.entryState;
        for (const std::uint32_t input : node.inputs) {
            if (const std::optional<std::size_t> child = IndexOfUVE(input)) {
                ResetSubtreeUVE(*child);
            }
        }
    }

    [[nodiscard]] std::string DescribeActiveStatesUVE() const {
        std::string names;
        for (std::size_t index = 0U; index < m_tree.nodes.size(); ++index) {
            const AnimationGraphNodeUVE& node = m_tree.nodes[index];
            if (node.kind != Kind::StateMachine) {
                continue;
            }
            const std::uint32_t active = m_tree.nodeStates[index].activeState;
            if (active >= node.inputs.size()) {
                continue;
            }
            if (const std::optional<std::size_t> child = IndexOfUVE(node.inputs[active])) {
                const AnimationGraphNodeUVE& state = m_tree.nodes[*child];
                names += names.empty() ? "" : ", ";
                names += state.name.empty() ? "#" + std::to_string(state.id) : state.name;
            }
        }
        return names;
    }

private:
    [[nodiscard]] std::optional<std::size_t> IndexOfUVE(const std::uint32_t id) const {
        const auto found = m_indexById.find(id);
        return found == m_indexById.end() ? std::nullopt : std::optional<std::size_t>{found->second};
    }

    [[nodiscard]] AnimationParameterUVE* FindParameterUVE(const std::string& name) {
        if (name.empty()) {
            return nullptr;
        }
        const auto found = std::find_if(m_tree.parameters.begin(), m_tree.parameters.end(),
                                        [&name](const AnimationParameterUVE& parameter) { return parameter.name == name; });
        return found == m_tree.parameters.end() ? nullptr : &*found;
    }

    [[nodiscard]] float ReadUVE(const std::string& name, const float fallback) {
        const AnimationParameterUVE* const parameter = FindParameterUVE(name);
        return parameter == nullptr ? fallback : parameter->value;
    }

    /// Reads a trigger and clears it: the first reader gets it.
    [[nodiscard]] bool ConsumeTriggerUVE(const std::string& name) {
        AnimationParameterUVE* const parameter = FindParameterUVE(name);
        if (parameter == nullptr || parameter->value < 0.5F) {
            return false;
        }
        if (parameter->type == AnimationParameterTypeUVE::Trigger) {
            parameter->value = 0.0F;
        }
        return true;
    }

    [[nodiscard]] ResultUVE EvaluateInputUVE(const AnimationGraphNodeUVE& node, const std::size_t slot,
                                             const float deltaSeconds) {
        if (slot >= node.inputs.size()) {
            return {};
        }
        const std::optional<std::size_t> child = IndexOfUVE(node.inputs[slot]);
        return child.has_value() ? EvaluateUVE(*child, deltaSeconds) : ResultUVE{};
    }

    [[nodiscard]] ResultUVE EvaluateClipUVE(const AnimationGraphNodeUVE& node, AnimationGraphNodeStateUVE& state,
                                            const float deltaSeconds) {
        const Asset::AnimationClipAssetUVE* const clip = m_clips ? m_clips(node.clip) : nullptr;
        if (clip == nullptr || clip->samples.empty() || !(clip->durationSeconds > 0.0)) {
            return {};
        }
        const double duration = clip->durationSeconds;
        double time = state.timeSeconds + static_cast<double>(deltaSeconds) * static_cast<double>(node.speed);
        bool atEnd = false;
        if (node.loop) {
            atEnd = time >= duration || time < 0.0;
            time = std::fmod(time, duration);
            if (time < 0.0) {
                time += duration;
            }
        } else {
            atEnd = node.speed >= 0.0F ? time >= duration : time <= 0.0;
            time = std::clamp(time, 0.0, duration);
        }
        state.timeSeconds = time;
        return ResultUVE{SampleAnimationClipAssetUVE(*clip, time), atEnd};
    }

    [[nodiscard]] ResultUVE EvaluateBlendSpaceUVE(const AnimationGraphNodeUVE& node, const float deltaSeconds) {
        // Every input advances, so each keeps its place in its cycle while it is not being shown.
        std::vector<ResultUVE> results;
        results.reserve(node.inputs.size());
        for (std::size_t slot = 0U; slot < node.inputs.size(); ++slot) {
            results.push_back(EvaluateInputUVE(node, slot, deltaSeconds));
        }
        const float x = ReadUVE(node.parameter, node.value);
        const std::vector<float>& points = node.points;
        if (x <= points.front()) {
            return results.front();
        }
        if (x >= points.back()) {
            return results.back();
        }
        const std::size_t right = static_cast<std::size_t>(
            std::upper_bound(points.begin(), points.end(), x) - points.begin());
        const std::size_t left = right - 1U;
        const float weight = (x - points[left]) / (points[right] - points[left]);
        return ResultUVE{MixUVE(results[left].pose, results[right].pose, weight),
                         weight < 0.5F ? results[left].atEnd : results[right].atEnd};
    }

    [[nodiscard]] ResultUVE EvaluateAdditiveUVE(const AnimationGraphNodeUVE& node, const float deltaSeconds) {
        const ResultUVE base = EvaluateInputUVE(node, 0U, deltaSeconds);
        const ResultUVE layer = EvaluateInputUVE(node, 1U, deltaSeconds);
        if (!base.pose.has_value() || !layer.pose.has_value()) {
            return base;
        }
        // The layer is a difference: its position adds, its rotation turns, its scale multiplies.
        const float weight = std::clamp(ReadUVE(node.parameter, node.value), 0.0F, 1.0F);
        PoseUVE pose = *base.pose;
        pose.position = pose.position + layer.pose->position * weight;
        pose.rotation = Math::MultiplyUVE(pose.rotation, SlerpUVE(Math::QuaternionUVE{}, layer.pose->rotation, weight));
        pose.scale = pose.scale * LerpUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}, layer.pose->scale, weight);
        return ResultUVE{pose, base.atEnd};
    }

    [[nodiscard]] ResultUVE EvaluateOneShotUVE(const AnimationGraphNodeUVE& node, AnimationGraphNodeStateUVE& state,
                                               const float deltaSeconds) {
        const bool fadingOut = !state.shotActive && state.fadeElapsedSeconds < state.fadeSeconds;
        if (!state.shotActive && ConsumeTriggerUVE(node.parameter)) {
            state.shotActive = true;
            state.shotElapsedSeconds = 0.0F;
            state.fadeSeconds = 0.0F;
            if (const std::optional<std::size_t> shot = IndexOfUVE(node.inputs[1])) {
                ResetSubtreeUVE(*shot);
            }
        }
        const ResultUVE base = EvaluateInputUVE(node, 0U, deltaSeconds);
        if (!state.shotActive && !fadingOut) {
            return base;
        }
        const ResultUVE shot = EvaluateInputUVE(node, 1U, deltaSeconds);
        float weight = 1.0F;
        if (state.shotActive) {
            state.shotElapsedSeconds += deltaSeconds;
            weight = node.fadeSeconds > 0.0F ? std::min(state.shotElapsedSeconds / node.fadeSeconds, 1.0F) : 1.0F;
            if (shot.atEnd) {
                // Ends here; the shot's last pose fades back out into the base.
                state.shotActive = false;
                state.fadeSeconds = node.fadeSeconds;
                state.fadeElapsedSeconds = 0.0F;
            }
        } else {
            state.fadeElapsedSeconds += deltaSeconds;
            weight = 1.0F - std::min(state.fadeElapsedSeconds / state.fadeSeconds, 1.0F);
        }
        return ResultUVE{MixUVE(base.pose, shot.pose, weight), base.atEnd};
    }

    [[nodiscard]] bool ConditionHoldsUVE(const AnimationTransitionUVE& transition, const bool activeAtEnd) {
        switch (transition.condition) {
            case AnimationConditionUVE::Always:
                return true;
            case AnimationConditionUVE::AtEnd:
                return activeAtEnd;
            case AnimationConditionUVE::ParameterGreater:
                return ReadUVE(transition.parameter, 0.0F) > transition.threshold;
            case AnimationConditionUVE::ParameterLess:
                return ReadUVE(transition.parameter, 0.0F) < transition.threshold;
            case AnimationConditionUVE::ParameterTrue:
                return ReadUVE(transition.parameter, 0.0F) >= 0.5F;
            case AnimationConditionUVE::ParameterFalse:
                return ReadUVE(transition.parameter, 0.0F) < 0.5F;
            case AnimationConditionUVE::Triggered:
                return ConsumeTriggerUVE(transition.parameter);
        }
        return false;
    }

    [[nodiscard]] ResultUVE EvaluateStateMachineUVE(const AnimationGraphNodeUVE& node, AnimationGraphNodeStateUVE& state,
                                                    const float deltaSeconds) {
        if (!state.started) {
            state.started = true;
            state.activeState = node.entryState;
            state.previousState = kAnyAnimationStateUVE;
        }
        const ResultUVE active = EvaluateInputUVE(node, state.activeState, deltaSeconds);
        std::optional<PoseUVE> pose = active.pose;
        if (state.previousState != kAnyAnimationStateUVE) {
            const ResultUVE previous = EvaluateInputUVE(node, state.previousState, deltaSeconds);
            state.fadeElapsedSeconds += deltaSeconds;
            const float weight =
                state.fadeSeconds > 0.0F ? std::min(state.fadeElapsedSeconds / state.fadeSeconds, 1.0F) : 1.0F;
            pose = MixUVE(previous.pose, active.pose, weight);
            if (weight >= 1.0F) {
                state.previousState = kAnyAnimationStateUVE;
            }
        }

        // The first transition out of the active state whose condition holds is taken. A move to
        // the state already active is ignored, so an "any state" transition cannot restart itself.
        for (const AnimationTransitionUVE& transition : node.transitions) {
            const bool fromHere =
                transition.fromState == kAnyAnimationStateUVE || transition.fromState == state.activeState;
            if (!fromHere || transition.toState == state.activeState || !ConditionHoldsUVE(transition, active.atEnd)) {
                continue;
            }
            state.previousState = transition.fadeSeconds > 0.0F ? state.activeState : kAnyAnimationStateUVE;
            state.activeState = transition.toState;
            state.fadeSeconds = transition.fadeSeconds;
            state.fadeElapsedSeconds = 0.0F;
            if (const std::optional<std::size_t> entered = IndexOfUVE(node.inputs[transition.toState])) {
                ResetSubtreeUVE(*entered);
            }
            break;
        }
        return ResultUVE{pose, active.atEnd};
    }

    AnimationTreeComponentUVE& m_tree;
    const AnimationClipResolverUVE& m_clips;
    std::unordered_map<std::uint32_t, std::size_t> m_indexById;
};

} // namespace

bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) {
    return IsAnimationTreeComponentValidUVE(value.tree);
}

void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value) {
    EnsureNodeBaselineUVE(entityManager, entity, AnimationTreeNodeDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationTreeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationTreeComponentUVE>(entity, value.tree);
    }
}

void ResetAnimationTreeUVE(AnimationTreeComponentUVE& tree) {
    tree.nodeStates.assign(tree.nodes.size(), AnimationGraphNodeStateUVE{});
    for (std::size_t index = 0U; index < tree.nodes.size(); ++index) {
        tree.nodeStates[index].activeState = tree.nodes[index].entryState;
    }
    tree.activeStates.clear();
}

bool SetAnimationTreeParameterUVE(AnimationTreeComponentUVE& tree, const std::string_view name, const float value) {
    const auto found = std::find_if(tree.parameters.begin(), tree.parameters.end(),
                                    [name](const AnimationParameterUVE& parameter) { return parameter.name == name; });
    if (found == tree.parameters.end() || !std::isfinite(value)) {
        return false;
    }
    found->value = value;
    return true;
}

bool StepAnimationTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                          const float deltaSeconds, TransformComponentUVE& target) {
    if (!tree.active || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return false;
    }
    if (tree.nodeStates.size() != tree.nodes.size()) {
        // A new or reshaped graph: checked once here rather than every frame, then started over.
        if (!IsAnimationTreeComponentValidUVE(tree)) {
            return false;
        }
        ResetAnimationTreeUVE(tree);
    }
    EvaluatorUVE evaluator(tree, clips);
    const std::optional<std::size_t> output = evaluator.OutputIndexUVE();
    if (!output.has_value()) {
        return false;
    }
    const ResultUVE result = evaluator.EvaluateUVE(*output, deltaSeconds);
    tree.activeStates = evaluator.DescribeActiveStatesUVE();
    if (!result.pose.has_value()) {
        return false;
    }
    WriteAnimatedPoseUVE(*result.pose, tree.animatePosition, tree.animateRotation, tree.animateScale, target);
    return true;
}

} // namespace UVE::Scene
