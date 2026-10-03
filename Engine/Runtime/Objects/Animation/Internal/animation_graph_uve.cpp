// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/animation_graph_uve.h"

#include "conformed_playback_uve.h"

#include <algorithm>
#include <array>
#include <cfloat>
#include <iterator>
#include <string_view>
#include <cmath>
#include <optional>
#include <unordered_map>

#include "uve/animation/time_pose_contract_uve.h"
#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/animation_sequencer_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {
namespace {

using Kind = AnimationGraphObjectKindUVE;
using PoseUVE = Core::TransformPoseUVE;
/// One pose per channel: every bone of the skeleton, or the one object transform.
using ChannelsUVE = std::vector<PoseUVE>;

/// What an object hands its parent: a pose when it has one (a clip may still be loading); whether its
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

/// Mixes many results by weights that need not add up to one: each joins the running mix in
/// proportion to its share so far, so the outcome is the weighted average.
[[nodiscard]] ResultUVE MixManyUVE(const std::vector<ResultUVE>& results, const std::vector<float>& weights) {
    ResultUVE mixed;
    float total = 0.0F;
    float heaviest = -1.0F;
    for (std::size_t index = 0U; index < results.size(); ++index) {
        const float weight = weights[index];
        if (weight <= 0.0F || !results[index].pose.has_value()) {
            continue;
        }
        total += weight;
        mixed = mixed.pose.has_value() ? MixUVE(mixed, results[index], weight / total) : results[index];
        if (weight > heaviest) {
            heaviest = weight;
            mixed.atEnd = results[index].atEnd;
            mixed.phase = results[index].phase;
        }
    }
    return mixed;
}

class EvaluatorUVE final {
public:
    /// `skeleton` null: the graph animates one object, its clips' own object track.
    EvaluatorUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                 const Skeleton3DComponentUVE* const skeleton, const AnimationMixerComponentUVE& mixer)
        : m_tree(tree), m_clips(clips), m_skeleton(skeleton), m_mixer(mixer) {
        for (std::size_t index = 0U; index < tree.objects.size(); ++index) {
            m_indexById.emplace(tree.objects[index].id, index);
        }
    }

    [[nodiscard]] std::optional<std::size_t> OutputIndexUVE() const {
        for (std::size_t index = 0U; index < m_tree.objects.size(); ++index) {
            if (m_tree.objects[index].kind == Kind::Output) {
                return index;
            }
        }
        return std::nullopt;
    }

    /// `weight` is how much this object counts in the final pose; `phase`, when set, is where a
    /// syncing parent wants this object's clips to be instead of advancing on their own.
    [[nodiscard]] ResultUVE EvaluateUVE(const std::size_t index, const float deltaSeconds, const float weight,
                                        const std::optional<float> phase) {
        const AnimationGraphObjectUVE& object = m_tree.objects[index];
        AnimationGraphObjectStateUVE& state = m_tree.objectStates[index];
        state.weight = std::clamp(weight, 0.0F, 1.0F);
        switch (object.kind) {
            case Kind::Output:
                return EvaluateInputUVE(object, 0U, deltaSeconds, weight, phase);
            case Kind::TimeScale:
                return EvaluateInputUVE(object, 0U, deltaSeconds * ReadUVE(object.parameter, object.speed), weight, phase);
            case Kind::Clip:
                return EvaluateClipUVE(object, state, deltaSeconds, weight, phase);
            case Kind::Blend2:
                return EvaluatePairUVE(object, deltaSeconds, weight, phase,
                                       std::clamp(ReadUVE(object.parameter, object.value), 0.0F, 1.0F));
            case Kind::BlendSpace1D:
                return EvaluateBlendSpaceUVE(object, state, deltaSeconds, weight, phase);
            case Kind::Additive:
                return EvaluateAdditiveUVE(object, deltaSeconds, weight, phase);
            case Kind::OneShot:
                return EvaluateOneShotUVE(object, state, deltaSeconds, weight);
            case Kind::StateMachine:
                return EvaluateStateMachineUVE(object, state, deltaSeconds, weight);
            case Kind::BlendSpace2D:
                return EvaluateBlendSpaceUVE(object, state, deltaSeconds, weight, phase);
            case Kind::Select:
                return EvaluateSelectUVE(object, state, deltaSeconds, weight);
            case Kind::LayeredBlend:
                return EvaluateLayeredBlendUVE(index, object, deltaSeconds, weight, phase);
            case Kind::TimeSeek:
                return EvaluateTimeSeekUVE(object, deltaSeconds, weight, phase);
        }
        return {};
    }

    void ResetSubtreeUVE(const std::size_t index) {
        const AnimationGraphObjectUVE& object = m_tree.objects[index];
        AnimationGraphObjectStateUVE& state = m_tree.objectStates[index];
        state = AnimationGraphObjectStateUVE{};
        state.activeState = object.entryState;
        for (const std::uint32_t input : object.inputs) {
            if (const std::optional<std::size_t> child = IndexOfUVE(input)) {
                ResetSubtreeUVE(*child);
            }
        }
    }

    [[nodiscard]] std::string DescribeActiveStatesUVE() const {
        std::string names;
        for (std::size_t index = 0U; index < m_tree.objects.size(); ++index) {
            const AnimationGraphObjectUVE& object = m_tree.objects[index];
            if (object.kind != Kind::StateMachine) {
                continue;
            }
            const std::uint32_t active = m_tree.objectStates[index].activeState;
            if (active >= object.inputs.size()) {
                continue;
            }
            if (const std::optional<std::size_t> child = IndexOfUVE(object.inputs[active])) {
                const AnimationGraphObjectUVE& state = m_tree.objects[*child];
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

    [[nodiscard]] ResultUVE EvaluateInputUVE(const AnimationGraphObjectUVE& object, const std::size_t slot,
                                             const float deltaSeconds, const float weight,
                                             const std::optional<float> phase) {
        if (slot >= object.inputs.size()) {
            return {};
        }
        const std::optional<std::size_t> child = IndexOfUVE(object.inputs[slot]);
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

    [[nodiscard]] ResultUVE EvaluateClipUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                            const float deltaSeconds, const float weight,
                                            const std::optional<float> phase) {
        return PlayClipUVE(object.clip, object.speed, object.loop, state.timeSeconds, state.started, deltaSeconds, weight, phase);
    }

    /// Plays a clip on its own clock (`timeSeconds`, `started`): a Clip object's, or a blend point's.
    [[nodiscard]] ResultUVE PlayClipUVE(const Asset::AssetGuidUVE clipGuid, const float speed, const bool loop,
                                        double& timeSeconds, bool& started, const float deltaSeconds, const float weight,
                                        const std::optional<float> phase) {
        const Asset::AnimationClipAssetUVE* const clip = m_clips ? m_clips(clipGuid) : nullptr;
        const bool skeletal = m_skeleton != nullptr;
        if (clip == nullptr || !(clip->durationSeconds > 0.0) || (skeletal ? !clip->IsSkeletalUVE() : clip->samples.empty())) {
            return {};
        }
        const double duration = clip->durationSeconds;
        const double before = timeSeconds;
        const bool forward = speed >= 0.0F;
        double time = before;
        bool atEnd = false;
        bool wrapped = false;
        if (phase.has_value()) {
            // Led by a syncing parent: this clip is where the leader is, in proportion.
            time = std::clamp(static_cast<double>(*phase), 0.0, 1.0) * duration;
            wrapped = loop && (forward ? time < before : time > before);
        } else {
            time = before + static_cast<double>(deltaSeconds) * static_cast<double>(speed);
            if (loop) {
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
        const bool firstStep = !started;
        started = true;
        timeSeconds = time;
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
        const Retarget::ConformedPlaybackUVE conformed = PlanConformedPlaybackForUVE(*clip, *m_skeleton);
        ChannelsUVE pose(m_skeleton->bones.size());
        for (std::size_t bone = 0U; bone < pose.size(); ++bone) {
            const SkeletonBoneUVE& rest = m_skeleton->bones[bone];
            const auto found = tracks.find(rest.name);
            pose[bone] = found != tracks.end() ? SampleAnimationTrackUVE(found->second->samples, time)
                                               : PoseUVE{rest.localPosition, rest.localRotation, rest.localScale};
            if (found != tracks.end()) {
                pose[bone].position = conformed.PositionUVE(rest.name, pose[bone].position, rest.localPosition);
                pose[bone].scale = conformed.ScaleUVE(pose[bone].scale, rest.localScale);
            }
        }
        // Root motion: the root bone stays over its first frame's ground position and its travel
        // this step - across a loop's wrap too - is handed up to be mixed like the pose.
        if (m_mixer.rootMotion != AnimationRootMotionModeUVE::Off && m_mixer.animatePosition) {
            if (const std::optional<std::size_t> root = ResolveRootMotionBoneUVE(*m_skeleton, *clip, m_mixer.rootMotionBone)) {
                const auto& samples = tracks.at(m_skeleton->bones[*root].name)->samples;
                const std::string& rootName = m_skeleton->bones[*root].name;
                const auto at = [&](const double t) {
                    return conformed.PositionUVE(rootName, SampleAnimationTrackUVE(samples, t).position, Math::Vector3UVE{});
                };
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
    [[nodiscard]] ResultUVE EvaluatePairUVE(const AnimationGraphObjectUVE& object, const float deltaSeconds,
                                            const float weight, const std::optional<float> phase, const float mix) {
        const std::array<float, 2> share{1.0F - mix, mix};
        std::array<ResultUVE, 2> results;
        if (object.sync && !phase.has_value()) {
            const std::size_t leader = mix < 0.5F ? 0U : 1U;
            results[leader] = EvaluateInputUVE(object, leader, deltaSeconds, weight * share[leader], std::nullopt);
            results[1U - leader] =
                EvaluateInputUVE(object, 1U - leader, deltaSeconds, weight * share[1U - leader], results[leader].phase);
        } else {
            for (std::size_t slot = 0U; slot < 2U; ++slot) {
                results[slot] = EvaluateInputUVE(object, slot, deltaSeconds, weight * share[slot], phase);
            }
        }
        return MixUVE(results[0], results[1], mix);
    }

    /// Plays a blend space's points by `weights`: every point's clock runs (so each keeps its place
    /// in its cycle while unseen), or with sync the heaviest leads and the others take its phase.
    [[nodiscard]] ResultUVE PlayBlendPointsUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                               const std::vector<float>& weights, const float deltaSeconds,
                                               const float weight, const std::optional<float> phase) {
        const std::size_t count = object.blendPoints.size();
        if (count == 0U) {
            return {};
        }
        if (state.pointTimes.size() != count) {
            state.pointTimes.assign(count, 0.0);
            state.pointStarted.assign(count, false);
        }
        std::vector<ResultUVE> results(count);
        const auto play = [&](const std::size_t slot, const std::optional<float> at) {
            const AnimationBlendPointUVE& point = object.blendPoints[slot];
            bool started = state.pointStarted[slot];
            results[slot] = PlayClipUVE(point.clip, point.speed, point.loop, state.pointTimes[slot], started, deltaSeconds,
                                        weight * weights[slot], at);
            state.pointStarted[slot] = started;
        };
        if (object.sync && !phase.has_value()) {
            const auto leader = static_cast<std::size_t>(std::max_element(weights.begin(), weights.end()) - weights.begin());
            play(leader, std::nullopt);
            for (std::size_t slot = 0U; slot < count; ++slot) {
                if (slot != leader) {
                    play(slot, results[leader].phase);
                }
            }
        } else {
            for (std::size_t slot = 0U; slot < count; ++slot) {
                play(slot, phase);
            }
        }
        return MixManyUVE(results, weights);
    }

    /// A Blend Space, 1D or 2D: its position follows the parameters (smoothed), then its blend mode
    /// turns that into weights - a mix, or the nearest point alone with a hand-over.
    [[nodiscard]] ResultUVE EvaluateBlendSpaceUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                                  const float deltaSeconds, const float weight,
                                                  const std::optional<float> phase) {
        const bool twoD = object.kind == Kind::BlendSpace2D;
        const std::size_t count = object.blendPoints.size();
        std::vector<Math::Vector2UVE> positions;
        positions.reserve(count);
        for (const AnimationBlendPointUVE& point : object.blendPoints) {
            positions.push_back(twoD ? point.position : Math::Vector2UVE{point.position.x, 0.0F});
        }
        const Math::Vector2UVE goal{ReadUVE(object.parameter, object.value), twoD ? ReadUVE(object.parameterY, object.valueY) : 0.0F};
        if (!state.blendAtSet) {
            state.blendAt = goal;
            state.blendVelocity = Math::Vector2UVE{};
            state.blendAtSet = true;
        } else {
            SmoothBlendPositionUVE(state.blendAt, state.blendVelocity, goal, object.smoothingSeconds, deltaSeconds);
        }
        std::vector<float> weights;
        if (twoD) {
            if (state.triangulatedPoints != positions) {
                state.triangles = TriangulateBlendSpaceUVE(positions);
                state.triangulatedPoints = positions;
            }
            weights = AnimationBlendSpace2DWeightsUVE(positions, state.triangles, state.blendAt);
        } else {
            std::vector<float> xs;
            xs.reserve(count);
            for (const Math::Vector2UVE& at : positions) {
                xs.push_back(at.x);
            }
            weights = AnimationBlendSpace1DWeightsUVE(xs, state.blendAt.x);
        }
        // A plane without a triangle blends nothing: there is no pose until three points make one.
        if (std::ranges::none_of(weights, [](const float w) { return w > 0.0F; })) {
            state.pointWeights.assign(count, 0.0F);
            return {};
        }
        if (object.blendMode == AnimationBlendModeUVE::Blend) {
            state.pointWeights = weights;
            return PlayBlendPointsUVE(object, state, weights, deltaSeconds, weight, phase);
        }
        return PlayNearestPointUVE(object, state, positions, deltaSeconds, weight, phase);
    }

    /// Nearest and Nearest In Step: one point plays. Another takes over once the position is
    /// clearly nearer to it (a tenth closer, so standing on the border does not flicker), handed
    /// over as the mixer's transitions are: inertialized, or crossfaded through the weights.
    [[nodiscard]] ResultUVE PlayNearestPointUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                                const std::vector<Math::Vector2UVE>& positions, const float deltaSeconds,
                                                const float weight, const std::optional<float> phase) {
        const std::size_t count = positions.size();
        const auto distanceTo = [&](const std::size_t slot) {
            const Math::Vector2UVE gap = state.blendAt - positions[slot];
            return gap.x * gap.x + gap.y * gap.y;
        };
        std::size_t nearest = 0U;
        for (std::size_t slot = 1U; slot < count; ++slot) {
            nearest = distanceTo(slot) < distanceTo(nearest) ? slot : nearest;
        }
        if (state.pointTimes.size() != count) {
            state.pointTimes.assign(count, 0.0);
            state.pointStarted.assign(count, false);
        }
        if (state.nearestPoint >= count) {
            state.nearestPoint = static_cast<std::uint32_t>(nearest);
            state.previousState = kAnyAnimationStateUVE;
            ClearInertialUVE(state);
        }
        const bool fading = state.previousState < count;
        float fade = 1.0F;
        if (fading) {
            state.fadeElapsedSeconds += deltaSeconds;
            fade = state.fadeSeconds > 0.0F ? std::min(state.fadeElapsedSeconds / state.fadeSeconds, 1.0F) : 1.0F;
        }
        std::vector<float> weights(count, 0.0F);
        weights[state.nearestPoint] = fade;
        if (fading) {
            weights[state.previousState] = 1.0F - fade;
        }
        state.pointWeights = weights;
        ResultUVE result = PlayBlendPointsUVE(object, state, weights, deltaSeconds, weight, phase);
        if (fading && fade >= 1.0F) {
            state.previousState = kAnyAnimationStateUVE;
        }
        ApplyInertialUVE(state, result, deltaSeconds);
        constexpr float kClearlyNearer = 0.81F; // (9/10)^2: distances are squared
        if (nearest != state.nearestPoint && distanceTo(nearest) < distanceTo(state.nearestPoint) * kClearlyNearer) {
            SwitchNearestPointUVE(object, state, static_cast<std::uint32_t>(nearest), result);
        }
        return result;
    }

    /// Makes point `to` the one playing, from its start or, In Step, at the phase just shown.
    void SwitchNearestPointUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state, const std::uint32_t to,
                               const ResultUVE& showing) {
        const std::uint32_t leaving = state.nearestPoint;
        const AnimationBlendPointUVE& point = object.blendPoints[to];
        double& time = state.pointTimes[to];
        bool started = false;
        time = 0.0;
        const std::optional<float> startPhase =
            object.blendMode == AnimationBlendModeUVE::NearestInStep ? showing.phase : std::nullopt;
        // Put the point where it starts; weight 0 so no events fire for the placement.
        const ResultUVE fresh = PlayClipUVE(point.clip, point.speed, point.loop, time, started, 0.0F, 0.0F, startPhase);
        state.pointStarted[to] = started;
        state.nearestPoint = to;
        state.fadeElapsedSeconds = 0.0F;
        state.fadeSeconds = object.fadeSeconds;
        ClearInertialUVE(state);
        const bool inertialize = m_mixer.transition == AnimationTransitionModeUVE::Inertialize && object.fadeSeconds > 0.0F;
        if (inertialize) {
            state.previousState = kAnyAnimationStateUVE;
            CaptureInertialUVE(state, showing, fresh, object.fadeSeconds);
        } else {
            state.previousState = object.fadeSeconds > 0.0F ? leaving : kAnyAnimationStateUVE;
        }
    }

    [[nodiscard]] ResultUVE EvaluateAdditiveUVE(const AnimationGraphObjectUVE& object, const float deltaSeconds,
                                                const float weight, const std::optional<float> phase) {
        const float amount = std::clamp(ReadUVE(object.parameter, object.value), 0.0F, 1.0F);
        const ResultUVE base = EvaluateInputUVE(object, 0U, deltaSeconds, weight, phase);
        const ResultUVE layer = EvaluateInputUVE(object, 1U, deltaSeconds, weight * amount,
                                                 object.sync && !phase.has_value() ? base.phase : phase);
        if (!base.pose.has_value() || !layer.pose.has_value() || layer.pose->size() != base.pose->size()) {
            return base;
        }
        // The layer is a difference: from the bone's rest pose on a skeleton, as it stands on a
        // object. Its position adds, its rotation turns, its scale multiplies.
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

    [[nodiscard]] ResultUVE EvaluateOneShotUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                               const float deltaSeconds, const float weight) {
        const bool fadingOut = !state.shotActive && state.fadeElapsedSeconds < state.fadeSeconds;
        if (!state.shotActive && ConsumeTriggerUVE(object.parameter)) {
            state.shotActive = true;
            state.shotElapsedSeconds = 0.0F;
            state.fadeSeconds = 0.0F;
            if (const std::optional<std::size_t> shot = IndexOfUVE(object.inputs[1])) {
                ResetSubtreeUVE(*shot);
            }
        }
        // The shot's weight this step, from its fade in or out.
        float shotWeight = 0.0F;
        if (state.shotActive) {
            const float elapsed = state.shotElapsedSeconds + deltaSeconds;
            shotWeight = object.fadeSeconds > 0.0F ? std::min(elapsed / object.fadeSeconds, 1.0F) : 1.0F;
        } else if (fadingOut) {
            shotWeight = 1.0F - std::min((state.fadeElapsedSeconds + deltaSeconds) / state.fadeSeconds, 1.0F);
        }
        const ResultUVE base = EvaluateInputUVE(object, 0U, deltaSeconds, weight * (1.0F - shotWeight), std::nullopt);
        if (!state.shotActive && !fadingOut) {
            return base;
        }
        const ResultUVE shot = EvaluateInputUVE(object, 1U, deltaSeconds, weight * shotWeight, std::nullopt);
        if (state.shotActive) {
            state.shotElapsedSeconds += deltaSeconds;
            if (shot.atEnd) {
                // Ends here; the shot's last pose fades back out into the base.
                state.shotActive = false;
                state.fadeSeconds = object.fadeSeconds;
                state.fadeElapsedSeconds = 0.0F;
            }
        } else {
            state.fadeElapsedSeconds += deltaSeconds;
        }
        ResultUVE mixed = MixUVE(base, shot, shotWeight);
        mixed.atEnd = base.atEnd;
        return mixed;
    }

    /// Whether one of a transition's tests holds. A trigger is only looked at here: it is used up
    /// when the transition is taken, so a transition whose other tests fail leaves it for later.
    [[nodiscard]] bool TestHoldsUVE(const AnimationTransitionConditionUVE& test, const bool activeAtEnd) {
        switch (test.condition) {
            case AnimationConditionUVE::Always:
                return true;
            case AnimationConditionUVE::AtEnd:
                return activeAtEnd;
            case AnimationConditionUVE::ParameterGreater:
                return ReadUVE(test.parameter, 0.0F) > test.threshold;
            case AnimationConditionUVE::ParameterLess:
                return ReadUVE(test.parameter, 0.0F) < test.threshold;
            case AnimationConditionUVE::ParameterTrue:
            case AnimationConditionUVE::Triggered:
                return ReadUVE(test.parameter, 0.0F) >= 0.5F;
            case AnimationConditionUVE::ParameterFalse:
                return ReadUVE(test.parameter, 0.0F) < 0.5F;
        }
        return false;
    }

    /// A transition may be taken now: it is on, the From state has played far enough, and every
    /// test holds.
    [[nodiscard]] bool TransitionReadyUVE(const AnimationTransitionUVE& transition, const ResultUVE& active) {
        if (!transition.enabled) {
            return false;
        }
        if (transition.exitPhase >= 0.0F && !active.atEnd &&
            (!active.phase.has_value() || *active.phase < transition.exitPhase)) {
            return false;
        }
        return std::ranges::all_of(transition.conditions, [&](const AnimationTransitionConditionUVE& test) {
            return TestHoldsUVE(test, active.atEnd);
        });
    }

    /// The input a switching object (StateMachine, Select) shows: the active one, still crossfading
    /// from the one it left, or inertializing the difference from the pose it replaced.
    [[nodiscard]] ResultUVE PlayActiveInputUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                               const float deltaSeconds, const float weight) {
        float fade = 1.0F;
        if (state.previousState != kAnyAnimationStateUVE) {
            state.fadeElapsedSeconds += deltaSeconds;
            fade = AnimationTransitionCurveWeightUVE(
                state.fadeCurve, state.fadeSeconds > 0.0F ? std::min(state.fadeElapsedSeconds / state.fadeSeconds, 1.0F) : 1.0F);
        }
        state.stateSeconds += deltaSeconds;
        ResultUVE result = EvaluateInputUVE(object, state.activeState, deltaSeconds, weight * fade, std::nullopt);
        const bool activeAtEnd = result.atEnd;
        if (state.previousState != kAnyAnimationStateUVE) {
            const ResultUVE previous =
                EvaluateInputUVE(object, state.previousState, deltaSeconds, weight * (1.0F - fade), std::nullopt);
            result = MixUVE(previous, result, fade);
            if (fade >= 1.0F) {
                state.previousState = kAnyAnimationStateUVE;
            }
        }
        result.atEnd = activeAtEnd;
        ApplyInertialUVE(state, result, deltaSeconds);
        return result;
    }

    /// Adds what is left of an inertialized hand-over to `result`, fading it out; clears it when done.
    static void ApplyInertialUVE(AnimationGraphObjectStateUVE& state, ResultUVE& result, const float deltaSeconds) {
        if (!(state.inertialSeconds > 0.0F) || !result.pose.has_value() || state.inertialPosition.size() != result.pose->size()) {
            return;
        }
        state.inertialElapsedSeconds += deltaSeconds;
        const float decay = AnimationInertialDecayUVE(state.inertialElapsedSeconds / state.inertialSeconds);
        for (std::size_t index = 0U; index < result.pose->size(); ++index) {
            PoseUVE& out = (*result.pose)[index];
            out.position = out.position + state.inertialPosition[index] * decay;
            out.rotation = Math::MultiplyUVE(SlerpUVE(Math::QuaternionUVE{}, state.inertialRotation[index], decay), out.rotation);
            out.scale = out.scale + state.inertialScale[index] * decay;
        }
        if (state.inertialElapsedSeconds >= state.inertialSeconds) {
            ClearInertialUVE(state);
        }
    }

    /// Starts an inertialized hand-over: the gap from `fresh` (where the new source starts) to
    /// `showing` (the pose shown so far) is carried and fades out over `seconds`.
    static void CaptureInertialUVE(AnimationGraphObjectStateUVE& state, const ResultUVE& showing, const ResultUVE& fresh,
                                   const float seconds) {
        ClearInertialUVE(state);
        if (!showing.pose.has_value() || !fresh.pose.has_value() || fresh.pose->size() != showing.pose->size()) {
            return;
        }
        const std::size_t count = showing.pose->size();
        state.inertialPosition.resize(count);
        state.inertialRotation.resize(count);
        state.inertialScale.resize(count);
        for (std::size_t index = 0U; index < count; ++index) {
            const PoseUVE& from = (*showing.pose)[index];
            const PoseUVE& onto = (*fresh.pose)[index];
            state.inertialPosition[index] = from.position - onto.position;
            state.inertialRotation[index] = Math::MultiplyUVE(from.rotation, InverseOrIdentityUVE(onto.rotation));
            state.inertialScale[index] = from.scale - onto.scale;
        }
        state.inertialSeconds = seconds;
    }

    static void ClearInertialUVE(AnimationGraphObjectStateUVE& state) {
        state.inertialSeconds = 0.0F;
        state.inertialElapsedSeconds = 0.0F;
        state.inertialPosition.clear();
        state.inertialRotation.clear();
        state.inertialScale.clear();
    }

    /// Makes input `to` the active one over `fadeSeconds`, handing over from `showing` (the pose
    /// shown this step): inertialized or crossfaded as the mixer says. `start` places the input
    /// entered: from its beginning, at the phase `showing` had reached, or where it was left.
    void SwitchInputUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state, const std::uint32_t to,
                        const float fadeSeconds, const AnimationTransitionStartUVE start, const ResultUVE& showing,
                        const AnimationTransitionCurveUVE curve = AnimationTransitionCurveUVE::Linear,
                        const bool interruptible = true) {
        const std::uint32_t leaving = state.activeState;
        const std::optional<std::size_t> entered = to < object.inputs.size() ? IndexOfUVE(object.inputs[to]) : std::nullopt;
        if (entered.has_value() && start != AnimationTransitionStartUVE::Continue) {
            ResetSubtreeUVE(*entered);
            if (start == AnimationTransitionStartUVE::InStep && showing.phase.has_value()) {
                // Weight 0: placing it fires no events.
                static_cast<void>(EvaluateUVE(*entered, 0.0F, 0.0F, showing.phase));
            }
        }
        state.activeState = to;
        state.fadeElapsedSeconds = 0.0F;
        state.fadeSeconds = fadeSeconds;
        state.fadeCurve = curve;
        state.fadeInterruptible = interruptible;
        state.stateSeconds = 0.0F;
        ClearInertialUVE(state);
        const bool inertialize = m_mixer.transition == AnimationTransitionModeUVE::Inertialize && fadeSeconds > 0.0F;
        if (inertialize) {
            state.previousState = kAnyAnimationStateUVE;
            if (entered.has_value() && showing.pose.has_value()) {
                // Hand over now: how far the pose showing is from where the new input starts.
                CaptureInertialUVE(state, showing, EvaluateUVE(*entered, 0.0F, 0.0F, std::nullopt), fadeSeconds);
            }
        } else {
            state.previousState = fadeSeconds > 0.0F && leaving != to ? leaving : kAnyAnimationStateUVE;
        }
    }

    [[nodiscard]] ResultUVE EvaluateStateMachineUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                                    const float deltaSeconds, const float weight) {
        if (!state.started) {
            state.started = true;
            state.activeState = object.entryState;
            state.previousState = kAnyAnimationStateUVE;
            state.lastTransition = kAnyAnimationStateUVE;
            state.stateSeconds = 0.0F;
        }
        const ResultUVE result = PlayActiveInputUVE(object, state, deltaSeconds, weight);
        // A transition that may not be interrupted holds the machine until its fade is over.
        const bool handingOver = state.previousState != kAnyAnimationStateUVE || state.inertialSeconds > 0.0F;
        if (handingOver && !state.fadeInterruptible) {
            return result;
        }
        // The first transition out of the active state that is ready is taken; the list order is
        // their priority. A move to the state already active is ignored, so an "any state"
        // transition cannot restart itself.
        for (std::size_t index = 0U; index < object.transitions.size(); ++index) {
            const AnimationTransitionUVE& transition = object.transitions[index];
            const bool fromHere =
                transition.fromState == kAnyAnimationStateUVE || transition.fromState == state.activeState;
            if (!fromHere || transition.toState == state.activeState || !TransitionReadyUVE(transition, result)) {
                continue;
            }
            for (const AnimationTransitionConditionUVE& test : transition.conditions) {
                if (test.condition == AnimationConditionUVE::Triggered) {
                    static_cast<void>(ConsumeTriggerUVE(test.parameter));
                }
            }
            SwitchInputUVE(object, state, transition.toState, transition.fadeSeconds, transition.start, result,
                           transition.curve, transition.interruptible);
            state.lastTransition = static_cast<std::uint32_t>(index);
            break;
        }
        return result;
    }

    [[nodiscard]] ResultUVE EvaluateSelectUVE(const AnimationGraphObjectUVE& object, AnimationGraphObjectStateUVE& state,
                                              const float deltaSeconds, const float weight) {
        const float value = ReadUVE(object.parameter, object.value);
        const auto last = static_cast<float>(object.inputs.size() - 1U);
        const auto picked = static_cast<std::uint32_t>(std::clamp(std::round(std::isfinite(value) ? value : 0.0F), 0.0F, last));
        if (!state.started) {
            state.started = true;
            state.activeState = picked;
            state.previousState = kAnyAnimationStateUVE;
        }
        const ResultUVE result = PlayActiveInputUVE(object, state, deltaSeconds, weight);
        if (picked != state.activeState) {
            SwitchInputUVE(object, state, picked, object.fadeSeconds,
                           object.restart ? AnimationTransitionStartUVE::Restart : AnimationTransitionStartUVE::Continue, result);
        }
        return result;
    }

    /// 1 for a bone the layer reaches (a named bone or any bone under one), else 0; the one object
    /// channel always. Bones are parents first, so one pass settles every branch.
    [[nodiscard]] const std::vector<float>& LayerMaskUVE(const std::size_t index, const AnimationGraphObjectUVE& object) {
        auto [it, added] = m_masks.try_emplace(index);
        if (!added) {
            return it->second;
        }
        std::vector<float>& mask = it->second;
        if (m_skeleton == nullptr) {
            mask.assign(1U, 1.0F);
            return mask;
        }
        const std::vector<SkeletonBoneUVE>& bones = m_skeleton->bones;
        mask.assign(bones.size(), object.bones.empty() ? 1.0F : 0.0F);
        if (object.bones.empty()) {
            return mask;
        }
        for (std::size_t bone = 0U; bone < bones.size(); ++bone) {
            const bool named = std::ranges::find(object.bones, bones[bone].name) != object.bones.end();
            const std::int32_t parent = bones[bone].parentIndex;
            const bool underNamed = parent >= 0 && static_cast<std::size_t>(parent) < bone && mask[static_cast<std::size_t>(parent)] > 0.0F;
            mask[bone] = named || underNamed ? 1.0F : 0.0F;
        }
        return mask;
    }

    [[nodiscard]] ResultUVE EvaluateLayeredBlendUVE(const std::size_t index, const AnimationGraphObjectUVE& object,
                                                    const float deltaSeconds, const float weight,
                                                    const std::optional<float> phase) {
        const float amount = std::clamp(ReadUVE(object.parameter, object.value), 0.0F, 1.0F);
        const ResultUVE base = EvaluateInputUVE(object, 0U, deltaSeconds, weight, phase);
        const ResultUVE layer = EvaluateInputUVE(object, 1U, deltaSeconds, weight * amount,
                                                 object.sync && !phase.has_value() ? base.phase : phase);
        if (!base.pose.has_value()) {
            return layer;
        }
        if (!layer.pose.has_value() || layer.pose->size() != base.pose->size() || amount <= 0.0F) {
            return base;
        }
        const std::vector<float>& mask = LayerMaskUVE(index, object);
        ResultUVE result = base;
        for (std::size_t channel = 0U; channel < result.pose->size() && channel < mask.size(); ++channel) {
            if (mask[channel] > 0.0F) {
                (*result.pose)[channel] = MixPoseUVE((*base.pose)[channel], (*layer.pose)[channel], amount * mask[channel]);
            }
        }
        return result;
    }

    /// Puts every clip under `index` at `seconds` (clamped to each clip), as if it had played there.
    void SeekSubtreeUVE(const std::size_t index, const double seconds) {
        const AnimationGraphObjectUVE& object = m_tree.objects[index];
        AnimationGraphObjectStateUVE& state = m_tree.objectStates[index];
        if (object.kind == Kind::Clip) {
            const Asset::AnimationClipAssetUVE* const clip = m_clips ? m_clips(object.clip) : nullptr;
            const double duration = clip != nullptr ? std::max(clip->durationSeconds, 0.0) : 0.0;
            state.timeSeconds = std::clamp(seconds, 0.0, duration);
            state.started = true;
            return;
        }
        for (const std::uint32_t input : object.inputs) {
            if (const std::optional<std::size_t> child = IndexOfUVE(input)) {
                SeekSubtreeUVE(*child, seconds);
            }
        }
    }

    [[nodiscard]] ResultUVE EvaluateTimeSeekUVE(const AnimationGraphObjectUVE& object, const float deltaSeconds,
                                                const float weight, const std::optional<float> phase) {
        if (ConsumeTriggerUVE(object.parameter)) {
            if (const std::optional<std::size_t> child = IndexOfUVE(object.inputs[0])) {
                SeekSubtreeUVE(*child, static_cast<double>(std::max(object.value, 0.0F)));
                return EvaluateUVE(*child, 0.0F, weight, std::nullopt);
            }
        }
        return EvaluateInputUVE(object, 0U, deltaSeconds, weight, phase);
    }

    AnimationTreeComponentUVE& m_tree;
    const AnimationClipResolverUVE& m_clips;
    const Skeleton3DComponentUVE* m_skeleton = nullptr;
    const AnimationMixerComponentUVE& m_mixer;
    std::unordered_map<std::uint32_t, std::size_t> m_indexById;
    std::unordered_map<const Asset::AnimationClipAssetUVE*,
                       std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrackUVE*>>
        m_tracks;
    std::vector<std::string> m_events;
    std::unordered_map<std::size_t, std::vector<float>> m_masks;
};

/// Runs one step: shape check, evaluation, and the tree's per-step outputs. The pose, or nothing.
[[nodiscard]] std::optional<ChannelsUVE> RunTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                                    const float deltaSeconds, const Skeleton3DComponentUVE* skeleton,
                                                    const AnimationMixerComponentUVE& mixer) {
    tree.rootMotionDelta = Math::Vector3UVE{};
    tree.firedEvents.clear();
    if (!mixer.active || !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        return std::nullopt;
    }
    if (tree.objectStates.size() != tree.objects.size()) {
        // A new or reshaped graph: checked once here rather than every frame, then started over.
        if (!IsAnimationTreeComponentValidUVE(tree)) {
            return std::nullopt;
        }
        ResetAnimationTreeUVE(tree);
    }
    for (AnimationGraphObjectStateUVE& state : tree.objectStates) {
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

bool IsAnimationGraphObjectDefinitionValidUVE(const AnimationGraphObjectDefinitionUVE& value) {
    return IsAnimationTreeComponentValidUVE(value.tree) && IsAnimationMixerComponentValidUVE(value.mixer);
}

void ApplyAnimationGraphObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                         const AnimationGraphObjectDefinitionUVE& value) {
    // The definition's mixer settings win over the base's defaults: added first, kept by the base.
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationMixerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationMixerComponentUVE>(entity, value.mixer);
    }
    ApplyAnimationMixerBaseUVE(entityManager, entity, AnimationGraphObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationTreeComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationTreeComponentUVE>(entity, value.tree);
    }
}

void ResetAnimationTreeUVE(AnimationTreeComponentUVE& tree) {
    tree.objectStates.assign(tree.objects.size(), AnimationGraphObjectStateUVE{});
    for (std::size_t index = 0U; index < tree.objects.size(); ++index) {
        tree.objectStates[index].activeState = tree.objects[index].entryState;
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
                                  const float deltaSeconds, Skeleton3DComponentUVE& skeleton,
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

std::vector<std::array<std::uint32_t, 3>> TriangulateBlendSpaceUVE(const std::vector<Math::Vector2UVE>& points) {
    using TriangleUVE = std::array<std::uint32_t, 3>;
    std::vector<TriangleUVE> result;
    const std::size_t count = points.size();
    if (count < 3U) {
        return result;
    }
    // Bowyer-Watson in doubles, inside a triangle far larger than the points (its corners are the
    // indices count..count+2); every triangle touching it is dropped at the end.
    double minX = points[0].x;
    double minY = points[0].y;
    double maxX = minX;
    double maxY = minY;
    for (const Math::Vector2UVE& point : points) {
        minX = std::min(minX, static_cast<double>(point.x));
        minY = std::min(minY, static_cast<double>(point.y));
        maxX = std::max(maxX, static_cast<double>(point.x));
        maxY = std::max(maxY, static_cast<double>(point.y));
    }
    const double span = std::max({maxX - minX, maxY - minY, 1.0e-3});
    const double midX = (minX + maxX) * 0.5;
    const double midY = (minY + maxY) * 0.5;
    struct PointUVE final {
        double x;
        double y;
    };
    std::vector<PointUVE> at;
    at.reserve(count + 3U);
    for (const Math::Vector2UVE& point : points) {
        at.push_back(PointUVE{point.x, point.y});
    }
    at.push_back(PointUVE{midX - 1000.0 * span, midY - 1000.0 * span});
    at.push_back(PointUVE{midX + 1000.0 * span, midY - 1000.0 * span});
    at.push_back(PointUVE{midX, midY + 1000.0 * span});
    const auto super = static_cast<std::uint32_t>(count);
    std::vector<TriangleUVE> triangles{TriangleUVE{super, super + 1U, super + 2U}};
    const auto inCircle = [&at](const TriangleUVE& t, const PointUVE& p) {
        // The circumcircle test, with the triangle's winding folded in.
        const PointUVE& a = at[t[0]];
        const PointUVE& b = at[t[1]];
        const PointUVE& c = at[t[2]];
        const double ax = a.x - p.x;
        const double ay = a.y - p.y;
        const double bx = b.x - p.x;
        const double by = b.y - p.y;
        const double cx = c.x - p.x;
        const double cy = c.y - p.y;
        const double det = (ax * ax + ay * ay) * (bx * cy - cx * by) - (bx * bx + by * by) * (ax * cy - cx * ay) +
                           (cx * cx + cy * cy) * (ax * by - bx * ay);
        const double orientation = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        return orientation > 0.0 ? det > 1.0e-12 : det < -1.0e-12;
    };
    for (std::uint32_t index = 0U; index < super; ++index) {
        const PointUVE& p = at[index];
        std::vector<std::array<std::uint32_t, 2>> edges;
        std::vector<TriangleUVE> kept;
        kept.reserve(triangles.size());
        for (const TriangleUVE& t : triangles) {
            if (!inCircle(t, p)) {
                kept.push_back(t);
                continue;
            }
            for (std::size_t e = 0U; e < 3U; ++e) {
                std::array<std::uint32_t, 2> edge{t[e], t[(e + 1U) % 3U]};
                if (edge[0] > edge[1]) {
                    std::swap(edge[0], edge[1]);
                }
                // An edge two removed triangles share is inside the hole; one used once is its rim.
                const auto shared = std::ranges::find(edges, edge);
                if (shared != edges.end()) {
                    edges.erase(shared);
                } else {
                    edges.push_back(edge);
                }
            }
        }
        for (const auto& edge : edges) {
            kept.push_back(TriangleUVE{edge[0], edge[1], index});
        }
        triangles = std::move(kept);
    }
    for (const TriangleUVE& t : triangles) {
        if (t[0] >= super || t[1] >= super || t[2] >= super) {
            continue;
        }
        const PointUVE& a = at[t[0]];
        const PointUVE& b = at[t[1]];
        const PointUVE& c = at[t[2]];
        const double area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) <= 1.0e-9 * span * span) {
            continue; // a sliver along a line blends nothing
        }
        // Counter-clockwise, lowest index first: the same points always give the same list.
        TriangleUVE ordered = area > 0.0 ? t : TriangleUVE{t[0], t[2], t[1]};
        while (ordered[0] > ordered[1] || ordered[0] > ordered[2]) {
            ordered = TriangleUVE{ordered[1], ordered[2], ordered[0]};
        }
        result.push_back(ordered);
    }
    std::ranges::sort(result);
    return result;
}

std::vector<float> AnimationBlendSpace2DWeightsUVE(const std::vector<Math::Vector2UVE>& points,
                                                  const std::vector<std::array<std::uint32_t, 3>>& triangles,
                                                  const Math::Vector2UVE at) {
    std::vector<float> weights(points.size(), 0.0F);
    float bestDistance = FLT_MAX;
    std::array<std::uint32_t, 2> bestEdge{};
    float bestAlong = 0.0F;
    for (const auto& t : triangles) {
        if (t[0] >= points.size() || t[1] >= points.size() || t[2] >= points.size()) {
            continue;
        }
        const Math::Vector2UVE a = points[t[0]];
        const Math::Vector2UVE b = points[t[1]];
        const Math::Vector2UVE c = points[t[2]];
        const float area = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
        if (std::abs(area) <= 1.0e-12F) {
            continue;
        }
        const float wb = ((at.x - a.x) * (c.y - a.y) - (at.y - a.y) * (c.x - a.x)) / area;
        const float wc = ((b.x - a.x) * (at.y - a.y) - (b.y - a.y) * (at.x - a.x)) / area;
        const float wa = 1.0F - wb - wc;
        constexpr float kInside = -1.0e-5F;
        if (wa >= kInside && wb >= kInside && wc >= kInside) {
            const float total = std::max(wa, 0.0F) + std::max(wb, 0.0F) + std::max(wc, 0.0F);
            weights[t[0]] = std::max(wa, 0.0F) / total;
            weights[t[1]] = std::max(wb, 0.0F) / total;
            weights[t[2]] = std::max(wc, 0.0F) / total;
            return weights;
        }
        // Outside this one: remember the nearest place on its edges, in case it is outside all.
        for (std::size_t e = 0U; e < 3U; ++e) {
            const std::uint32_t from = t[e];
            const std::uint32_t to = t[(e + 1U) % 3U];
            const Math::Vector2UVE edge = points[to] - points[from];
            const Math::Vector2UVE offset = at - points[from];
            const float lengthSquared = edge.x * edge.x + edge.y * edge.y;
            const float along = std::clamp((offset.x * edge.x + offset.y * edge.y) / lengthSquared, 0.0F, 1.0F);
            const Math::Vector2UVE gap = offset - edge * along;
            const float distance = gap.x * gap.x + gap.y * gap.y;
            if (distance < bestDistance) {
                bestDistance = distance;
                bestEdge = {from, to};
                bestAlong = along;
            }
        }
    }
    if (bestDistance < FLT_MAX) {
        weights[bestEdge[0]] += 1.0F - bestAlong;
        weights[bestEdge[1]] += bestAlong;
    }
    return weights;
}

float AnimationTransitionCurveWeightUVE(const AnimationTransitionCurveUVE curve, const float progress) noexcept {
    const float t = std::clamp(std::isfinite(progress) ? progress : 1.0F, 0.0F, 1.0F);
    switch (curve) {
        case AnimationTransitionCurveUVE::Linear:
            return t;
        case AnimationTransitionCurveUVE::EaseIn:
            return t * t;
        case AnimationTransitionCurveUVE::EaseOut:
            return 1.0F - (1.0F - t) * (1.0F - t);
        case AnimationTransitionCurveUVE::EaseInOut:
            return t * t * (3.0F - 2.0F * t);
    }
    return t;
}

void SmoothBlendPositionUVE(Math::Vector2UVE& value, Math::Vector2UVE& velocity, const Math::Vector2UVE goal,
                            const float halfLifeSeconds, const float deltaSeconds) noexcept {
    if (!(halfLifeSeconds > 0.0F)) {
        value = goal;
        velocity = Math::Vector2UVE{};
        return;
    }
    // The exact step of a critically damped spring from rest toward a goal: the gap is
    // e^(-yt)(1 + yt), which is one half at yt = 1.67835, so y puts that at the half-life.
    const float y = 1.6783470F / halfLifeSeconds;
    const float decay = std::exp(-y * deltaSeconds);
    const Math::Vector2UVE j0 = value - goal;
    const Math::Vector2UVE j1 = velocity + j0 * y;
    value = (j0 + j1 * deltaSeconds) * decay + goal;
    velocity = (velocity - j1 * (y * deltaSeconds)) * decay;
}

std::vector<float> AnimationBlendSpace1DWeightsUVE(const std::vector<float>& points, const float at) {
    std::vector<float> weights(points.size(), 0.0F);
    if (points.empty()) {
        return weights;
    }
    if (at <= points.front()) {
        weights.front() = 1.0F;
    } else if (at >= points.back()) {
        weights.back() = 1.0F;
    } else {
        const auto right = static_cast<std::size_t>(std::upper_bound(points.begin(), points.end(), at) - points.begin());
        const float mix = (at - points[right - 1U]) / (points[right] - points[right - 1U]);
        weights[right - 1U] = 1.0F - mix;
        weights[right] = mix;
    }
    return weights;
}

} // namespace UVE::Scene
