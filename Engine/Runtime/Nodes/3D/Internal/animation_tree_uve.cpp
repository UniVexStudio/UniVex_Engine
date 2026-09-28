// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_tree_uve.h"

#include <algorithm>
#include <array>
#include <iterator>
#include <string_view>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "uve/animation/time_pose_contract_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/animation_player_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"
#include "uve/nodes/3d/skeleton_3d_uve.h"

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphNodeKindUVE;
using PoseUVE = Core::TransformPoseUVE;
/// One pose per channel: every bone of the skeleton, or the one node transform.
using ChannelsUVE = std::vector<PoseUVE>;

/// What a node hands its parent: a pose when it has one (a clip may still be loading); whether its
/// animation reached its end this step (what AtEnd transitions and one-shots wait for); the root
/// bone's ground travel this step; and where its leading clip is, 0..1, for syncing.
struct ResultUVE final {
    std::optional<ChannelsUVE> pose;
    bool atEnd = false;
    Math::Vector3UVE rootMotion{};
    std::optional<float> phase;
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

[[nodiscard]] Math::QuaternionUVE InverseOrIdentityUVE(const Math::QuaternionUVE& value) noexcept {
    Math::QuaternionUVE inverse{};
    return Math::TryInverseUVE(value, inverse) ? inverse : Math::QuaternionUVE{};
}

[[nodiscard]] PoseUVE MixPoseUVE(const PoseUVE& from, const PoseUVE& to, const float alpha) noexcept {
    return PoseUVE{LerpUVE(from.position, to.position, alpha), SlerpUVE(from.rotation, to.rotation, alpha),
                   LerpUVE(from.scale, to.scale, alpha)};
}

/// Mixes two results channel by channel; a side without a pose leaves the other as it is.
[[nodiscard]] ResultUVE MixUVE(const ResultUVE& from, const ResultUVE& to, const float weight) {
    const float alpha = std::clamp(weight, 0.0F, 1.0F);
    ResultUVE mixed;
    mixed.atEnd = alpha < 0.5F ? from.atEnd : to.atEnd;
    mixed.phase = alpha < 0.5F ? (from.phase ? from.phase : to.phase) : (to.phase ? to.phase : from.phase);
    if (!from.pose.has_value()) {
        mixed.pose = to.pose;
        mixed.rootMotion = to.rootMotion;
        return mixed;
    }
    if (!to.pose.has_value() || to.pose->size() != from.pose->size()) {
        mixed.pose = from.pose;
        mixed.rootMotion = from.rootMotion;
        return mixed;
    }
    ChannelsUVE channels(from.pose->size());
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        channels[index] = MixPoseUVE((*from.pose)[index], (*to.pose)[index], alpha);
    }
    mixed.pose = std::move(channels);
    mixed.rootMotion = LerpUVE(from.rootMotion, to.rootMotion, alpha);
    return mixed;
}

class EvaluatorUVE final {
public:
    /// `skeleton` null: the graph animates one node, its clips' own node track.
    EvaluatorUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                 const Skeleton3DNodeComponentUVE* const skeleton, const AnimationMixerComponentUVE& mixer)
        : m_tree(tree), m_clips(clips), m_skeleton(skeleton), m_mixer(mixer) {
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

    /// `weight` is how much this node counts in the final pose; `phase`, when set, is where a
    /// syncing parent wants this node's clips to be instead of advancing on their own.
    [[nodiscard]] ResultUVE EvaluateUVE(const std::size_t index, const float deltaSeconds, const float weight,
                                        const std::optional<float> phase) {
        const AnimationGraphNodeUVE& node = m_tree.nodes[index];
        AnimationGraphNodeStateUVE& state = m_tree.nodeStates[index];
        state.weight = std::clamp(weight, 0.0F, 1.0F);
        switch (node.kind) {
            case Kind::Output:
                return EvaluateInputUVE(node, 0U, deltaSeconds, weight, phase);
            case Kind::TimeScale:
                return EvaluateInputUVE(node, 0U, deltaSeconds * ReadUVE(node.parameter, node.speed), weight, phase);
            case Kind::Clip:
                return EvaluateClipUVE(node, state, deltaSeconds, weight, phase);
            case Kind::Blend2:
                return EvaluatePairUVE(node, deltaSeconds, weight, phase,
                                       std::clamp(ReadUVE(node.parameter, node.value), 0.0F, 1.0F));
            case Kind::BlendSpace1D:
                return EvaluateBlendSpaceUVE(node, deltaSeconds, weight, phase);
            case Kind::Additive:
                return EvaluateAdditiveUVE(node, deltaSeconds, weight, phase);
            case Kind::OneShot:
                return EvaluateOneShotUVE(node, state, deltaSeconds, weight);
            case Kind::StateMachine:
                return EvaluateStateMachineUVE(node, state, deltaSeconds, weight);
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

    [[nodiscard]] std::vector<std::string> TakeEventsUVE() { return std::move(m_events); }

private:
    [[nodiscard]] std::size_t ChannelCountUVE() const noexcept {
        return m_skeleton != nullptr ? m_skeleton->bones.size() : 1U;
    }

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
                                             const float deltaSeconds, const float weight,
                                             const std::optional<float> phase) {
        if (slot >= node.inputs.size()) {
            return {};
        }
        const std::optional<std::size_t> child = IndexOfUVE(node.inputs[slot]);
        return child.has_value() ? EvaluateUVE(*child, deltaSeconds, weight, phase) : ResultUVE{};
    }

    /// Bone name to track, built once per clip per step.
    [[nodiscard]] const std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrackUVE*>& TracksOfUVE(
        const Asset::AnimationClipAssetUVE& clip) {
        auto [it, added] = m_tracks.try_emplace(&clip);
        if (added) {
            for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
                if (!track.samples.empty()) {
                    it->second.emplace(track.bone, &track);
                }
            }
        }
        return it->second;
    }

    [[nodiscard]] ResultUVE EvaluateClipUVE(const AnimationGraphNodeUVE& node, AnimationGraphNodeStateUVE& state,
                                            const float deltaSeconds, const float weight,
                                            const std::optional<float> phase) {
        const Asset::AnimationClipAssetUVE* const clip = m_clips ? m_clips(node.clip) : nullptr;
        const bool skeletal = m_skeleton != nullptr;
        if (clip == nullptr || !(clip->durationSeconds > 0.0) || (skeletal ? !clip->IsSkeletalUVE() : clip->samples.empty())) {
            return {};
        }
        const double duration = clip->durationSeconds;
        const double before = state.timeSeconds;
        const bool forward = node.speed >= 0.0F;
        double time = before;
        bool atEnd = false;
        bool wrapped = false;
        if (phase.has_value()) {
            // Led by a syncing parent: this clip is where the leader is, in proportion.
            time = std::clamp(static_cast<double>(*phase), 0.0, 1.0) * duration;
            wrapped = node.loop && (forward ? time < before : time > before);
        } else {
            time = before + static_cast<double>(deltaSeconds) * static_cast<double>(node.speed);
            if (node.loop) {
                wrapped = time >= duration || time < 0.0;
                atEnd = wrapped;
                time = std::fmod(time, duration);
                if (time < 0.0) {
                    time += duration;
                }
            } else {
                atEnd = forward ? time >= duration : time <= 0.0;
                time = std::clamp(time, 0.0, duration);
            }
        }
        const bool firstStep = !state.started;
        state.started = true;
        state.timeSeconds = time;
        if (weight >= 0.5F) {
            std::vector<std::string> passed =
                CollectPassedAnimationEventsUVE(clip->events, before, time, duration, forward, wrapped, firstStep);
            m_events.insert(m_events.end(), std::make_move_iterator(passed.begin()), std::make_move_iterator(passed.end()));
        }

        ResultUVE result;
        result.atEnd = atEnd;
        result.phase = static_cast<float>(time / duration);
        if (!skeletal) {
            result.pose = ChannelsUVE{SampleAnimationClipAssetUVE(*clip, time)};
            return result;
        }
        const auto& tracks = TracksOfUVE(*clip);
        ChannelsUVE pose(m_skeleton->bones.size());
        for (std::size_t bone = 0U; bone < pose.size(); ++bone) {
            const SkeletonBoneUVE& rest = m_skeleton->bones[bone];
            const auto found = tracks.find(rest.name);
            pose[bone] = found != tracks.end() ? SampleAnimationTrackUVE(found->second->samples, time)
                                               : PoseUVE{rest.localPosition, rest.localRotation, rest.localScale};
        }
        // Root motion: the root bone stays over its first frame's ground position and its travel
        // this step - across a loop's wrap too - is handed up to be mixed like the pose.
        if (m_mixer.rootMotion != AnimationRootMotionModeUVE::Off && m_mixer.animatePosition) {
            if (const std::optional<std::size_t> root = ResolveRootMotionBoneUVE(*m_skeleton, *clip, m_mixer.rootMotionBone)) {
                const auto& samples = tracks.at(m_skeleton->bones[*root].name)->samples;
                const auto at = [&samples](const double t) { return SampleAnimationTrackUVE(samples, t).position; };
                const Math::Vector3UVE first = at(0.0);
                Math::Vector3UVE travel = at(time) - at(before);
                if (wrapped) {
                    travel = forward ? (at(duration) - at(before)) + (at(time) - first)
                                     : (first - at(before)) + (at(time) - at(duration));
                }
                if (!firstStep) {
                    result.rootMotion = Math::Vector3UVE{travel.x, 0.0F, travel.z};
                }
                pose[*root].position.x = first.x;
                pose[*root].position.z = first.z;
            }
        }
        result.pose = std::move(pose);
        return result;
    }

    /// Two inputs mixed by `mix` (0 the first, 1 the second). With sync the heavier input leads and
    /// the other plays at its phase.
    [[nodiscard]] ResultUVE EvaluatePairUVE(const AnimationGraphNodeUVE& node, const float deltaSeconds,
                                            const float weight, const std::optional<float> phase, const float mix) {
        const std::array<float, 2> share{1.0F - mix, mix};
        std::array<ResultUVE, 2> results;
        if (node.sync && !phase.has_value()) {
            const std::size_t leader = mix < 0.5F ? 0U : 1U;
            results[leader] = EvaluateInputUVE(node, leader, deltaSeconds, weight * share[leader], std::nullopt);
            results[1U - leader] =
                EvaluateInputUVE(node, 1U - leader, deltaSeconds, weight * share[1U - leader], results[leader].phase);
        } else {
            for (std::size_t slot = 0U; slot < 2U; ++slot) {
                results[slot] = EvaluateInputUVE(node, slot, deltaSeconds, weight * share[slot], phase);
            }
        }
        return MixUVE(results[0], results[1], mix);
    }

    [[nodiscard]] ResultUVE EvaluateBlendSpaceUVE(const AnimationGraphNodeUVE& node, const float deltaSeconds,
                                                  const float weight, const std::optional<float> phase) {
        const std::vector<float>& points = node.points;
        const float x = ReadUVE(node.parameter, node.value);
        std::size_t left = 0U;
        std::size_t right = 0U;
        float mix = 0.0F;
        if (x >= points.back()) {
            left = right = points.size() - 1U;
        } else if (x > points.front()) {
            right = static_cast<std::size_t>(std::upper_bound(points.begin(), points.end(), x) - points.begin());
            left = right - 1U;
            mix = (x - points[left]) / (points[right] - points[left]);
        }
        const auto shareOf = [&](const std::size_t slot) {
            float share = 0.0F;
            if (slot == left) {
                share += left == right ? 1.0F : 1.0F - mix;
            }
            if (slot == right && right != left) {
                share += mix;
            }
            return share;
        };
        // Every input advances, so each keeps its place in its cycle while it is not being shown;
        // with sync they all follow the heaviest one instead.
        std::vector<ResultUVE> results(node.inputs.size());
        std::optional<float> leadPhase = phase;
        if (node.sync && !phase.has_value()) {
            const std::size_t leader = mix < 0.5F ? left : right;
            results[leader] = EvaluateInputUVE(node, leader, deltaSeconds, weight * shareOf(leader), std::nullopt);
            leadPhase = results[leader].phase;
            for (std::size_t slot = 0U; slot < results.size(); ++slot) {
                if (slot != leader) {
                    results[slot] = EvaluateInputUVE(node, slot, deltaSeconds, weight * shareOf(slot), leadPhase);
                }
            }
        } else {
            for (std::size_t slot = 0U; slot < results.size(); ++slot) {
                results[slot] = EvaluateInputUVE(node, slot, deltaSeconds, weight * shareOf(slot), leadPhase);
            }
        }
        return left == right ? results[left] : MixUVE(results[left], results[right], mix);
    }

    [[nodiscard]] ResultUVE EvaluateAdditiveUVE(const AnimationGraphNodeUVE& node, const float deltaSeconds,
                                                const float weight, const std::optional<float> phase) {
        const float amount = std::clamp(ReadUVE(node.parameter, node.value), 0.0F, 1.0F);
        const ResultUVE base = EvaluateInputUVE(node, 0U, deltaSeconds, weight, phase);
        const ResultUVE layer = EvaluateInputUVE(node, 1U, deltaSeconds, weight * amount,
                                                 node.sync && !phase.has_value() ? base.phase : phase);
        if (!base.pose.has_value() || !layer.pose.has_value() || layer.pose->size() != base.pose->size()) {
            return base;
        }
        // The layer is a difference: from the bone's rest pose on a skeleton, as it stands on a
        // node. Its position adds, its rotation turns, its scale multiplies.
        ResultUVE result = base;
        for (std::size_t index = 0U; index < result.pose->size(); ++index) {
            PoseUVE delta = (*layer.pose)[index];
            if (m_skeleton != nullptr) {
                const SkeletonBoneUVE& rest = m_skeleton->bones[index];
                const auto safe = [](const float value) { return std::abs(value) > 1.0e-6F ? value : 1.0F; };
                delta.position = delta.position - rest.localPosition;
                delta.rotation = Math::MultiplyUVE(delta.rotation, InverseOrIdentityUVE(rest.localRotation));
                delta.scale = Math::Vector3UVE{delta.scale.x / safe(rest.localScale.x), delta.scale.y / safe(rest.localScale.y),
                                               delta.scale.z / safe(rest.localScale.z)};
            }
            PoseUVE& out = (*result.pose)[index];
            out.position = out.position + delta.position * amount;
            out.rotation = Math::MultiplyUVE(SlerpUVE(Math::QuaternionUVE{}, delta.rotation, amount), out.rotation);
            out.scale = out.scale * LerpUVE(Math::Vector3UVE{1.0F, 1.0F, 1.0F}, delta.scale, amount);
        }
        return result;
    }

    [[nodiscard]] ResultUVE EvaluateOneShotUVE(const AnimationGraphNodeUVE& node, AnimationGraphNodeStateUVE& state,
                                               const float deltaSeconds, const float weight) {
        const bool fadingOut = !state.shotActive && state.fadeElapsedSeconds < state.fadeSeconds;
        if (!state.shotActive && ConsumeTriggerUVE(node.parameter)) {
            state.shotActive = true;
            state.shotElapsedSeconds = 0.0F;
            state.fadeSeconds = 0.0F;
            if (const std::optional<std::size_t> shot = IndexOfUVE(node.inputs[1])) {
                ResetSubtreeUVE(*shot);
            }
        }
        // The shot's weight this step, from its fade in or out.
        float shotWeight = 0.0F;
        if (state.shotActive) {
            const float elapsed = state.shotElapsedSeconds + deltaSeconds;
            shotWeight = node.fadeSeconds > 0.0F ? std::min(elapsed / node.fadeSeconds, 1.0F) : 1.0F;
        } else if (fadingOut) {
            shotWeight = 1.0F - std::min((state.fadeElapsedSeconds + deltaSeconds) / state.fadeSeconds, 1.0F);
        }
        const ResultUVE base = EvaluateInputUVE(node, 0U, deltaSeconds, weight * (1.0F - shotWeight), std::nullopt);
        if (!state.shotActive && !fadingOut) {
            return base;
        }
        const ResultUVE shot = EvaluateInputUVE(node, 1U, deltaSeconds, weight * shotWeight, std::nullopt);
        if (state.shotActive) {
            state.shotElapsedSeconds += deltaSeconds;
            if (shot.atEnd) {
                // Ends here; the shot's last pose fades back out into the base.
                state.shotActive = false;
                state.fadeSeconds = node.fadeSeconds;
                state.fadeElapsedSeconds = 0.0F;
            }
        } else {
            state.fadeElapsedSeconds += deltaSeconds;
        }
        ResultUVE mixed = MixUVE(base, shot, shotWeight);
        mixed.atEnd = base.atEnd;
        return mixed;
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
                                                    const float deltaSeconds, const float weight) {
        if (!state.started) {
            state.started = true;
            state.activeState = node.entryState;
            state.previousState = kAnyAnimationStateUVE;
        }
        // Crossfading: the state being left still plays, fading out.
        float fade = 1.0F;
        if (state.previousState != kAnyAnimationStateUVE) {
            state.fadeElapsedSeconds += deltaSeconds;
            fade = state.fadeSeconds > 0.0F ? std::min(state.fadeElapsedSeconds / state.fadeSeconds, 1.0F) : 1.0F;
        }
        ResultUVE result = EvaluateInputUVE(node, state.activeState, deltaSeconds, weight * fade, std::nullopt);
        const bool activeAtEnd = result.atEnd;
        if (state.previousState != kAnyAnimationStateUVE) {
            const ResultUVE previous =
                EvaluateInputUVE(node, state.previousState, deltaSeconds, weight * (1.0F - fade), std::nullopt);
            result = MixUVE(previous, result, fade);
            if (fade >= 1.0F) {
                state.previousState = kAnyAnimationStateUVE;
            }
        }
        // Inertialized: the entered state plays at full weight with the old pose's difference fading.
        if (state.inertialSeconds > 0.0F && result.pose.has_value() && state.inertialPosition.size() == result.pose->size()) {
            state.inertialElapsedSeconds += deltaSeconds;
            const float decay = AnimationInertialDecayUVE(state.inertialElapsedSeconds / state.inertialSeconds);
            for (std::size_t index = 0U; index < result.pose->size(); ++index) {
                PoseUVE& out = (*result.pose)[index];
                out.position = out.position + state.inertialPosition[index] * decay;
                out.rotation = Math::MultiplyUVE(SlerpUVE(Math::QuaternionUVE{}, state.inertialRotation[index], decay), out.rotation);
                out.scale = out.scale + state.inertialScale[index] * decay;
            }
            if (state.inertialElapsedSeconds >= state.inertialSeconds) {
                state.inertialSeconds = 0.0F;
                state.inertialPosition.clear();
                state.inertialRotation.clear();
                state.inertialScale.clear();
            }
        }

        // The first transition out of the active state whose condition holds is taken. A move to
        // the state already active is ignored, so an "any state" transition cannot restart itself.
        for (const AnimationTransitionUVE& transition : node.transitions) {
            const bool fromHere =
                transition.fromState == kAnyAnimationStateUVE || transition.fromState == state.activeState;
            if (!fromHere || transition.toState == state.activeState || !ConditionHoldsUVE(transition, activeAtEnd)) {
                continue;
            }
            const std::uint32_t leaving = state.activeState;
            const std::optional<std::size_t> entered = IndexOfUVE(node.inputs[transition.toState]);
            if (entered.has_value()) {
                ResetSubtreeUVE(*entered);
            }
            state.activeState = transition.toState;
            state.fadeElapsedSeconds = 0.0F;
            state.fadeSeconds = transition.fadeSeconds;
            state.inertialSeconds = 0.0F;
            state.inertialPosition.clear();
            state.inertialRotation.clear();
            state.inertialScale.clear();
            const bool inertialize =
                m_mixer.transition == AnimationTransitionModeUVE::Inertialize && transition.fadeSeconds > 0.0F;
            if (inertialize && entered.has_value() && result.pose.has_value()) {
                // Hand over now: how far the pose showing is from where the new state starts.
                const ResultUVE fresh = EvaluateUVE(*entered, 0.0F, 0.0F, std::nullopt);
                if (fresh.pose.has_value() && fresh.pose->size() == result.pose->size()) {
                    const std::size_t count = result.pose->size();
                    state.inertialPosition.resize(count);
                    state.inertialRotation.resize(count);
                    state.inertialScale.resize(count);
                    for (std::size_t index = 0U; index < count; ++index) {
                        const PoseUVE& from = (*result.pose)[index];
                        const PoseUVE& to = (*fresh.pose)[index];
                        state.inertialPosition[index] = from.position - to.position;
                        state.inertialRotation[index] = Math::MultiplyUVE(from.rotation, InverseOrIdentityUVE(to.rotation));
                        state.inertialScale[index] = from.scale - to.scale;
                    }
                    state.inertialSeconds = transition.fadeSeconds;
                    state.inertialElapsedSeconds = 0.0F;
                }
                state.previousState = kAnyAnimationStateUVE;
            } else {
                state.previousState = transition.fadeSeconds > 0.0F ? leaving : kAnyAnimationStateUVE;
            }
            break;
        }
        return result;
    }

    AnimationTreeComponentUVE& m_tree;
    const AnimationClipResolverUVE& m_clips;
    const Skeleton3DNodeComponentUVE* m_skeleton = nullptr;
    const AnimationMixerComponentUVE& m_mixer;
    std::unordered_map<std::uint32_t, std::size_t> m_indexById;
    std::unordered_map<const Asset::AnimationClipAssetUVE*,
                       std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrackUVE*>>
        m_tracks;
    std::vector<std::string> m_events;
};

/// Runs one step: shape check, evaluation, and the tree's per-step outputs. The pose, or nothing.
[[nodiscard]] std::optional<ChannelsUVE> RunTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                                    const float deltaSeconds, const Skeleton3DNodeComponentUVE* skeleton,
                                                    const AnimationMixerComponentUVE& mixer) {
    tree.rootMotionDelta = Math::Vector3UVE{};
    tree.firedEvents.clear();
    if (!mixer.active || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return std::nullopt;
    }
    if (tree.nodeStates.size() != tree.nodes.size()) {
        // A new or reshaped graph: checked once here rather than every frame, then started over.
        if (!IsAnimationTreeComponentValidUVE(tree)) {
            return std::nullopt;
        }
        ResetAnimationTreeUVE(tree);
    }
    for (AnimationGraphNodeStateUVE& state : tree.nodeStates) {
        state.weight = 0.0F;
    }
    EvaluatorUVE evaluator(tree, clips, skeleton, mixer);
    const std::optional<std::size_t> output = evaluator.OutputIndexUVE();
    if (!output.has_value()) {
        return std::nullopt;
    }
    ResultUVE result = evaluator.EvaluateUVE(*output, deltaSeconds, 1.0F, std::nullopt);
    tree.activeStates = evaluator.DescribeActiveStatesUVE();
    tree.firedEvents = evaluator.TakeEventsUVE();
    tree.rootMotionDelta = result.rootMotion;
    return std::move(result.pose);
}

} // namespace

bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) {
    return IsAnimationTreeComponentValidUVE(value.tree) && IsAnimationMixerComponentValidUVE(value.mixer);
}

void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value) {
    // The definition's mixer settings win over the base's defaults: added first, kept by the base.
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationMixerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationMixerComponentUVE>(entity, value.mixer);
    }
    ApplyAnimationMixerBaseUVE(entityManager, entity, AnimationTreeNodeDefinitionUVE::defaultName);
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
                          const float deltaSeconds, TransformComponentUVE& target,
                          const AnimationMixerComponentUVE& mixer) {
    const std::optional<ChannelsUVE> pose = RunTreeUVE(tree, clips, deltaSeconds, nullptr, mixer);
    if (!pose.has_value() || pose->empty()) {
        return false;
    }
    WriteAnimatedPoseUVE(pose->front(), mixer.animatePosition, mixer.animateRotation, mixer.animateScale, target);
    return true;
}

bool StepSkeletalAnimationTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                  const float deltaSeconds, Skeleton3DNodeComponentUVE& skeleton,
                                  const AnimationMixerComponentUVE& mixer) {
    if (skeleton.bones.empty()) {
        return false;
    }
    const std::optional<ChannelsUVE> pose = RunTreeUVE(tree, clips, deltaSeconds, &skeleton, mixer);
    if (!pose.has_value() || pose->size() != skeleton.bones.size()) {
        return false;
    }
    // Through the channel masks: a channel the mixer leaves alone keeps what the skeleton had.
    std::vector<SkeletonBonePoseUVE> current = GetSkeletonCurrentPoseUVE(skeleton);
    for (std::size_t index = 0U; index < current.size(); ++index) {
        const PoseUVE& bone = (*pose)[index];
        SkeletonBonePoseUVE& out = current[index];
        if (mixer.animatePosition) {
            out.position = bone.position;
        }
        Math::QuaternionUVE normalized{};
        if (mixer.animateRotation && Math::TryNormalizeUVE(bone.rotation, normalized)) {
            out.rotation = normalized;
        }
        if (mixer.animateScale) {
            out.scale = bone.scale;
        }
    }
    skeleton.pose = std::move(current);
    return true;
}

} // namespace UVE::Scene
