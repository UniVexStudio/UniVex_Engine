// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/animation_sequencer_uve.h"

#include "conformed_playback_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string_view>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

namespace UVE::Scene {
namespace {

/// A ping-pong reflects off each end; more reflections than this in one step means a clip far
/// shorter than the step, where the exact phase is meaningless - the time is clamped instead.
constexpr int kMaximumReflectionsPerStepUVE = 64;

using PoseUVE = Core::TransformPoseUVE;

[[nodiscard]] Math::Vector3UVE LerpUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to, const float alpha) noexcept {
    return from + (to - from) * alpha;
}

/// Inertialization's fade: 1 at the start, 0 at the end, with zero speed and acceleration at both
/// (the complement of the quintic smoothstep), so the hand-over has no pop and no kink.
[[nodiscard]] float InertialDecayUVE(const float progress) noexcept {
    const float x = std::clamp(progress, 0.0F, 1.0F);
    return 1.0F - x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

[[nodiscard]] Math::QuaternionUVE SlerpOrKeepUVE(const Math::QuaternionUVE& from, const Math::QuaternionUVE& to,
                                                 const float alpha) noexcept {
    Math::QuaternionUVE result = to;
    if (!Math::TrySlerpUVE(from, to, alpha, result)) {
        result = alpha < 0.5F ? from : to;
    }
    return result;
}

[[nodiscard]] PoseUVE ToPoseUVE(const Asset::AnimationAssetPoseUVE& pose) noexcept {
    return PoseUVE{pose.position, pose.rotation, pose.scale};
}

[[nodiscard]] float SafeRatioUVE(const float numerator, const float denominator) noexcept {
    return std::fabs(denominator) > 1e-6F ? numerator / denominator : numerator;
}

[[nodiscard]] PoseUVE StartPoseUVE(const AnimationPlayerComponentUVE& player) noexcept {
    return PoseUVE{player.startPosition, player.startRotation, player.startScale};
}

/// The clip's motion since its first frame, laid on top of where the target started.
[[nodiscard]] PoseUVE MakeRelativeUVE(const AnimationPlayerComponentUVE& player, const PoseUVE& first,
                                      const PoseUVE& sampled) noexcept {
    PoseUVE result;
    const Math::Vector3UVE offset = sampled.position - first.position;
    result.position = player.startPosition + Math::RotateVectorUVE(player.startRotation, player.startScale * offset);
    Math::QuaternionUVE inverseFirst{};
    if (!Math::TryInverseUVE(first.rotation, inverseFirst)) {
        inverseFirst = Math::QuaternionUVE{};
    }
    result.rotation = Math::MultiplyUVE(player.startRotation, Math::MultiplyUVE(inverseFirst, sampled.rotation));
    result.scale = Math::Vector3UVE{player.startScale.x * SafeRatioUVE(sampled.scale.x, first.scale.x),
                                    player.startScale.y * SafeRatioUVE(sampled.scale.y, first.scale.y),
                                    player.startScale.z * SafeRatioUVE(sampled.scale.z, first.scale.z)};
    return result;
}

void WritePoseUVE(const AnimationMixerComponentUVE& mixer, const PoseUVE& pose, TransformComponentUVE& target) noexcept {
    WriteAnimatedPoseUVE(pose, mixer.animatePosition, mixer.animateRotation, mixer.animateScale, target);
}

/// Moves the clock and applies the loop mode. Returns false when a Once clip reached its end.
[[nodiscard]] bool AdvanceClockUVE(AnimationPlayerComponentUVE& player, const double duration,
                                   const float deltaSeconds) noexcept {
    double time = static_cast<double>(player.currentTimeSeconds) +
                  static_cast<double>(player.speed) * static_cast<double>(player.direction) *
                      static_cast<double>(deltaSeconds);
    bool stillPlaying = true;
    switch (player.loopMode) {
        case AnimationLoopModeUVE::Once:
            if (time >= duration || time <= 0.0) {
                time = std::clamp(time, 0.0, duration);
                stillPlaying = false;
            }
            break;
        case AnimationLoopModeUVE::Loop:
            time = std::fmod(time, duration);
            if (time < 0.0) {
                time += duration;
            }
            break;
        case AnimationLoopModeUVE::PingPong: {
            int reflections = 0;
            while ((time > duration || time < 0.0) && reflections < kMaximumReflectionsPerStepUVE) {
                time = time > duration ? 2.0 * duration - time : -time;
                player.direction = -player.direction;
                ++reflections;
            }
            time = std::clamp(time, 0.0, duration);
            break;
        }
    }
    player.currentTimeSeconds = static_cast<float>(time);
    return stillPlaying;
}

} // namespace

namespace {

/// A track's pose at `timeSeconds`: linear position and scale, spherical rotation, clamped to the
/// first and last sample. `samples` is never empty.
[[nodiscard]] Core::TransformPoseUVE SampleTrackUVE(const std::vector<Asset::AnimationAssetSampleUVE>& samples,
                                                    const double timeSeconds) noexcept {
    const auto upper = std::upper_bound(samples.begin(), samples.end(), timeSeconds,
                                        [](const double value, const Asset::AnimationAssetSampleUVE& sample) {
                                            return value < sample.timeSeconds;
                                        });
    if (upper == samples.begin()) {
        return ToPoseUVE(samples.front().pose);
    }
    if (upper == samples.end()) {
        return ToPoseUVE(samples.back().pose);
    }
    const Asset::AnimationAssetSampleUVE& right = *upper;
    const Asset::AnimationAssetSampleUVE& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const float alpha = span <= 0.0 ? 0.0F : static_cast<float>((timeSeconds - left.timeSeconds) / span);
    return Core::TransformPoseUVE{LerpUVE(left.pose.position, right.pose.position, alpha),
                                  SlerpOrKeepUVE(left.pose.rotation, right.pose.rotation, alpha),
                                  LerpUVE(left.pose.scale, right.pose.scale, alpha)};
}

} // namespace

Core::TransformPoseUVE SampleAnimationTrackUVE(const std::vector<Asset::AnimationAssetSampleUVE>& samples,
                                               const double timeSeconds) noexcept {
    return SampleTrackUVE(samples, timeSeconds);
}

float AnimationInertialDecayUVE(const float progress) noexcept {
    return InertialDecayUVE(progress);
}

Core::TransformPoseUVE SampleAnimationClipAssetUVE(const Asset::AnimationClipAssetUVE& clip,
                                                         const double timeSeconds) noexcept {
    return SampleTrackUVE(clip.samples, timeSeconds);
}

void WriteAnimatedPoseUVE(const Core::TransformPoseUVE& pose, const bool position, const bool rotation, const bool scale,
                          TransformComponentUVE& target) noexcept {
    if (position) {
        target.localPosition = pose.position;
    }
    if (rotation) {
        Math::QuaternionUVE normalized{};
        if (Math::TryNormalizeUVE(pose.rotation, normalized)) {
            target.localRotation = normalized;
            Math::Vector3UVE euler{};
            if (Math::TryToEulerOrderedUVE(normalized, target.eulerOrder, euler)) {
                target.localEulerRadians = euler;
            }
        }
    }
    if (scale) {
        target.localScale = pose.scale;
    }
}

bool IsAnimationSequencerObjectDefinitionValidUVE(const AnimationSequencerObjectDefinitionUVE& value) noexcept {
    return IsAnimationPlayerComponentValidUVE(value.player) && IsAnimationMixerComponentValidUVE(value.mixer);
}

void ApplyAnimationSequencerObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const AnimationSequencerObjectDefinitionUVE& value) {
    // The definition's mixer settings win over the base's defaults: added first, kept by the base.
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationMixerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationMixerComponentUVE>(entity, value.mixer);
    }
    ApplyAnimationMixerBaseUVE(entityManager, entity, AnimationSequencerObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationPlayerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationPlayerComponentUVE>(entity, value.player);
    }
}

void PlayAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const TransformComponentUVE& targetNow,
                            const double clipDurationSeconds) noexcept {
    const double duration = std::isfinite(clipDurationSeconds) ? std::max(clipDurationSeconds, 0.0) : 0.0;
    const double offset = std::min(static_cast<double>(player.startOffsetSeconds), duration);
    // Backwards playback starts from the end, offset from there.
    player.currentTimeSeconds = static_cast<float>(player.speed < 0.0F ? duration - offset : offset);
    player.direction = 1.0F;
    player.blendElapsedSeconds = 0.0F;
    player.finished = false;
    player.isPlaying = true;
    player.hasStartPose = true;
    player.playingClip = player.clip;
    player.inertialPosition.clear();
    player.inertialRotation.clear();
    player.inertialScale.clear();
    player.startPosition = targetNow.localPosition;
    player.startRotation = targetNow.localRotation;
    player.startScale = targetNow.localScale;
}

void StopAnimationPlayerUVE(AnimationPlayerComponentUVE& player) noexcept {
    player.isPlaying = false;
}

bool StepAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const Asset::AnimationClipAssetUVE& clip,
                            const float deltaSeconds, TransformComponentUVE& target,
                            const AnimationMixerComponentUVE& mixer) noexcept {
    if (!player.isPlaying) {
        return false;
    }
    const double duration = clip.durationSeconds;
    if (clip.samples.empty() || !std::isfinite(duration) || duration <= 0.0 || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F) {
        player.isPlaying = false;
        return false;
    }

    const double eventsBefore = static_cast<double>(player.currentTimeSeconds);
    const bool eventsForward = player.speed * player.direction >= 0.0F;
    const bool firstStep = player.blendElapsedSeconds <= 0.0F;
    const bool stillPlaying = AdvanceClockUVE(player, duration, deltaSeconds);
    player.blendElapsedSeconds = std::min(player.blendElapsedSeconds + deltaSeconds, 3600.0F);
    player.firedEvents = CollectPassedAnimationEventsUVE(
        clip.events, eventsBefore, static_cast<double>(player.currentTimeSeconds), duration, eventsForward,
        player.loopMode == AnimationLoopModeUVE::Loop &&
            (eventsForward ? player.currentTimeSeconds < eventsBefore : player.currentTimeSeconds > eventsBefore),
        firstStep);

    if (!stillPlaying) {
        player.isPlaying = false;
        player.finished = true;
        if (player.onFinish == AnimationFinishActionUVE::ReturnToStart && player.hasStartPose) {
            WritePoseUVE(mixer, StartPoseUVE(player), target);
            return true;
        }
    }

    PoseUVE pose = SampleAnimationClipAssetUVE(clip, static_cast<double>(player.currentTimeSeconds));
    if (player.relative && player.hasStartPose) {
        pose = MakeRelativeUVE(player, ToPoseUVE(clip.samples.front().pose), pose);
    }
    if (player.blendInSeconds > 0.0F && player.hasStartPose && player.blendElapsedSeconds < player.blendInSeconds) {
        const float weight = player.blendElapsedSeconds / player.blendInSeconds;
        const PoseUVE start = StartPoseUVE(player);
        pose = PoseUVE{LerpUVE(start.position, pose.position, weight),
                       SlerpOrKeepUVE(start.rotation, pose.rotation, weight), LerpUVE(start.scale, pose.scale, weight)};
    }
    WritePoseUVE(mixer, pose, target);
    return true;
}

bool StepSkeletalAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const Asset::AnimationClipAssetUVE& clip,
                                    const float deltaSeconds, Skeleton3DComponentUVE& skeleton,
                                    const AnimationMixerComponentUVE& mixer) {
    if (!player.isPlaying) {
        return false;
    }
    const double duration = clip.durationSeconds;
    if (!clip.IsSkeletalUVE() || skeleton.bones.empty() || !std::isfinite(duration) || duration <= 0.0 ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        player.isPlaying = false;
        return false;
    }
    std::vector<SkeletonBonePoseUVE> current = GetSkeletonCurrentPoseUVE(skeleton);
    const float blendBefore = player.blendElapsedSeconds;
    const double timeBefore = static_cast<double>(player.currentTimeSeconds);
    const bool forward = player.speed * player.direction >= 0.0F;
    const bool firstStep = player.blendElapsedSeconds <= 0.0F;
    const bool stillPlaying = AdvanceClockUVE(player, duration, deltaSeconds);
    player.firedEvents = CollectPassedAnimationEventsUVE(
        clip.events, timeBefore, static_cast<double>(player.currentTimeSeconds), duration, forward,
        player.loopMode == AnimationLoopModeUVE::Loop &&
            (forward ? player.currentTimeSeconds < timeBefore : player.currentTimeSeconds > timeBefore),
        firstStep);
    player.blendElapsedSeconds = std::min(player.blendElapsedSeconds + deltaSeconds, 3600.0F);

    std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrackUVE*> tracks;
    tracks.reserve(clip.bones.size());
    for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
        if (!track.samples.empty()) {
            tracks.emplace(track.bone, &track);
        }
    }
    const bool returnToRest = !stillPlaying && player.onFinish == AnimationFinishActionUVE::ReturnToStart;
    const Retarget::ConformedPlaybackUVE conformed = PlanConformedPlaybackForUVE(clip, skeleton);
    // Blend In eases from where the skeleton was: each step covers this step's share of what is
    // left of the blend, so the pose arrives exactly when the blend ends.
    float weight = 1.0F;
    if (player.blendInSeconds > 0.0F && blendBefore < player.blendInSeconds) {
        const float remaining = player.blendInSeconds - blendBefore;
        weight = remaining > 0.0F ? std::clamp(deltaSeconds / remaining, 0.0F, 1.0F) : 1.0F;
    }
    const double time = static_cast<double>(player.currentTimeSeconds);
    // Inertialization: on the blend's first step, remember how far the pose that was there is from
    // the new clip; then play the clip at full weight with that difference fading out.
    const bool inertial = mixer.transition == AnimationTransitionModeUVE::Inertialize && player.blendInSeconds > 0.0F;
    const std::size_t boneCount = skeleton.bones.size();
    const bool captureOffsets = inertial && blendBefore <= 0.0F;
    if (captureOffsets) {
        player.inertialPosition.assign(boneCount, Math::Vector3UVE{});
        player.inertialRotation.assign(boneCount, Math::QuaternionUVE{});
        player.inertialScale.assign(boneCount, Math::Vector3UVE{});
    }
    const bool haveOffsets = inertial && player.inertialPosition.size() == boneCount &&
                             player.inertialRotation.size() == boneCount && player.inertialScale.size() == boneCount;
    const float decay = haveOffsets ? InertialDecayUVE(player.blendElapsedSeconds / player.blendInSeconds) : 0.0F;
    for (std::size_t index = 0U; index < boneCount; ++index) {
        const SkeletonBoneUVE& bone = skeleton.bones[index];
        PoseUVE target{bone.localPosition, bone.localRotation, bone.localScale};
        if (!returnToRest) {
            if (const auto found = tracks.find(bone.name); found != tracks.end()) {
                target = SampleTrackUVE(found->second->samples, time);
                target.position = conformed.PositionUVE(bone.name, target.position, bone.localPosition);
                target.scale = conformed.ScaleUVE(target.scale, bone.localScale);
            }
        }
        SkeletonBonePoseUVE& out = current[index];
        if (captureOffsets) {
            Math::QuaternionUVE inverse{};
            if (!Math::TryInverseUVE(target.rotation, inverse)) {
                inverse = Math::QuaternionUVE{};
            }
            player.inertialPosition[index] = out.position - target.position;
            player.inertialRotation[index] = Math::MultiplyUVE(out.rotation, inverse);
            player.inertialScale[index] = out.scale - target.scale;
        }
        PoseUVE blended;
        if (haveOffsets) {
            blended = PoseUVE{target.position + player.inertialPosition[index] * decay,
                              Math::MultiplyUVE(SlerpOrKeepUVE(Math::QuaternionUVE{}, player.inertialRotation[index], decay),
                                                target.rotation),
                              target.scale + player.inertialScale[index] * decay};
        } else {
            blended = PoseUVE{LerpUVE(out.position, target.position, weight),
                              SlerpOrKeepUVE(out.rotation, target.rotation, weight),
                              LerpUVE(out.scale, target.scale, weight)};
        }
        if (mixer.animatePosition) {
            out.position = blended.position;
        }
        if (mixer.animateRotation) {
            Math::QuaternionUVE normalized{};
            if (Math::TryNormalizeUVE(blended.rotation, normalized)) {
                out.rotation = normalized;
            }
        }
        if (mixer.animateScale) {
            out.scale = blended.scale;
        }
    }
    if (haveOffsets && player.blendElapsedSeconds >= player.blendInSeconds) {
        player.inertialPosition.clear();
        player.inertialRotation.clear();
        player.inertialScale.clear();
    }
    // Root motion: the root bone's ground travel over this step, across a loop's wrap too, is
    // handed to the caller; the pose keeps the bone over its first frame's ground position.
    player.rootMotionDelta = Math::Vector3UVE{};
    if (mixer.rootMotion != AnimationRootMotionModeUVE::Off && mixer.animatePosition) {
        if (const std::optional<std::size_t> rootIndex = ResolveRootMotionBoneUVE(skeleton, clip, mixer.rootMotionBone)) {
            const auto found = tracks.find(skeleton.bones[*rootIndex].name);
            if (found != tracks.end()) {
                const auto& samples = found->second->samples;
                const std::string& rootName = skeleton.bones[*rootIndex].name;
                const auto at = [&](const double t) {
                    return conformed.PositionUVE(rootName, SampleTrackUVE(samples, t).position, Math::Vector3UVE{});
                };
                const Math::Vector3UVE first = at(0.0);
                if (!returnToRest) {
                    Math::Vector3UVE travel = at(time) - at(timeBefore);
                    if (player.loopMode == AnimationLoopModeUVE::Loop) {
                        if (forward && time < timeBefore) {
                            travel = (at(duration) - at(timeBefore)) + (at(time) - first);
                        } else if (!forward && time > timeBefore) {
                            travel = (first - at(timeBefore)) + (at(time) - at(duration));
                        }
                    }
                    player.rootMotionDelta = Math::Vector3UVE{travel.x * weight, 0.0F, travel.z * weight};
                }
                SkeletonBonePoseUVE& root = current[*rootIndex];
                root.position.x = first.x;
                root.position.z = first.z;
            }
        }
    }
    skeleton.pose = std::move(current);
    if (!stillPlaying) {
        player.isPlaying = false;
        player.finished = true;
    }
    return true;
}

std::optional<std::size_t> ResolveRootMotionBoneUVE(const Skeleton3DComponentUVE& skeleton,
                                                    const Asset::AnimationClipAssetUVE& clip,
                                                    const std::string_view boneName) {
    const auto trackOf = [&clip](const std::string_view name) -> const Asset::AnimationAssetBoneTrackUVE* {
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            if (track.bone == name && !track.samples.empty()) {
                return &track;
            }
        }
        return nullptr;
    };
    if (!boneName.empty()) {
        for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
            if (skeleton.bones[index].name == boneName) {
                return trackOf(boneName) != nullptr ? std::optional<std::size_t>{index} : std::nullopt;
            }
        }
        return std::nullopt;
    }
    // Bones are parents first, so the first one that travels is the highest in the rig.
    constexpr float kMinimumTravelMetresUVE = 0.01F;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const Asset::AnimationAssetBoneTrackUVE* const track = trackOf(skeleton.bones[index].name);
        if (track == nullptr) {
            continue;
        }
        const Math::Vector3UVE first = track->samples.front().pose.position;
        for (const Asset::AnimationAssetSampleUVE& sample : track->samples) {
            const float dx = sample.pose.position.x - first.x;
            const float dz = sample.pose.position.z - first.z;
            if (dx * dx + dz * dz > kMinimumTravelMetresUVE * kMinimumTravelMetresUVE) {
                return index;
            }
        }
    }
    return std::nullopt;
}

bool PoseSkeletonAtTimeUVE(const Asset::AnimationClipAssetUVE& clip, const double timeSeconds,
                           Skeleton3DComponentUVE& skeleton, const AnimationMixerComponentUVE& mixer) {
    if (!clip.IsSkeletalUVE() || skeleton.bones.empty() || !std::isfinite(timeSeconds)) {
        return false;
    }
    const double time = std::clamp(timeSeconds, 0.0, std::max(clip.durationSeconds, 0.0));
    const Retarget::ConformedPlaybackUVE conformed = PlanConformedPlaybackForUVE(clip, skeleton);
    std::vector<SkeletonBonePoseUVE> pose(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const SkeletonBoneUVE& bone = skeleton.bones[index];
        SkeletonBonePoseUVE& out = pose[index];
        out = SkeletonBonePoseUVE{bone.localPosition, bone.localRotation, bone.localScale};
        for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
            if (track.bone != bone.name || track.samples.empty()) {
                continue;
            }
            const PoseUVE sampled = SampleTrackUVE(track.samples, time);
            if (mixer.animatePosition) {
                out.position = conformed.PositionUVE(bone.name, sampled.position, bone.localPosition);
            }
            Math::QuaternionUVE normalized{};
            if (mixer.animateRotation && Math::TryNormalizeUVE(sampled.rotation, normalized)) {
                out.rotation = normalized;
            }
            if (mixer.animateScale) {
                out.scale = conformed.ScaleUVE(sampled.scale, bone.localScale);
            }
            break;
        }
    }
    if (mixer.rootMotion != AnimationRootMotionModeUVE::Off && mixer.animatePosition) {
        if (const std::optional<std::size_t> rootIndex = ResolveRootMotionBoneUVE(skeleton, clip, mixer.rootMotionBone)) {
            for (const Asset::AnimationAssetBoneTrackUVE& track : clip.bones) {
                if (track.bone == skeleton.bones[*rootIndex].name && !track.samples.empty()) {
                    const Math::Vector3UVE first = conformed.PositionUVE(track.bone, SampleTrackUVE(track.samples, 0.0).position, Math::Vector3UVE{});
                    pose[*rootIndex].position.x = first.x;
                    pose[*rootIndex].position.z = first.z;
                    break;
                }
            }
        }
    }
    skeleton.pose = std::move(pose);
    return true;
}

std::vector<std::string> CollectPassedAnimationEventsUVE(const std::vector<Asset::AnimationAssetEventUVE>& events,
                                                         const double beforeSeconds, const double afterSeconds,
                                                         const double durationSeconds, const bool forward,
                                                         const bool wrapped, const bool includeStart) {
    // Passing order: forwards the events sorted by time, backwards reversed. A wrap passes the rest
    // of the clip first, then the part from the other end.
    std::vector<const Asset::AnimationAssetEventUVE*> sorted;
    sorted.reserve(events.size());
    for (const Asset::AnimationAssetEventUVE& event : events) {
        sorted.push_back(&event);
    }
    std::ranges::stable_sort(sorted, [](const auto* a, const auto* b) { return a->timeSeconds < b->timeSeconds; });
    if (!forward) {
        std::ranges::reverse(sorted);
    }
    std::vector<std::string> passed;
    const auto collect = [&](const double from, const double to, const bool fromInclusive) {
        for (const Asset::AnimationAssetEventUVE* event : sorted) {
            const double t = event->timeSeconds;
            const bool inside = forward ? ((fromInclusive ? t >= from : t > from) && t <= to)
                                        : ((fromInclusive ? t <= from : t < from) && t >= to);
            if (inside) {
                passed.push_back(event->eventId);
            }
        }
    };
    if (!wrapped) {
        collect(beforeSeconds, afterSeconds, includeStart);
    } else if (forward) {
        collect(beforeSeconds, durationSeconds, includeStart);
        collect(0.0, afterSeconds, true);
    } else {
        collect(beforeSeconds, 0.0, includeStart);
        collect(durationSeconds, afterSeconds, true);
    }
    return passed;
}

} // namespace UVE::Scene
