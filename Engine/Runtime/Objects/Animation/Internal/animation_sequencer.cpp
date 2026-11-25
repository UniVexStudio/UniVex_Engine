// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_sequencer.h"

#include "conformed_playback.h"

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>
#include <unordered_map>
#include <optional>
#include <string_view>

#include "animation_clip_asset.h"
#include "transform_component.h"
#include "i_entity_manager.h"
#include "abstract_animation_objects_3d.h"
#include "quaternion.h"
#include "object_3d.h"
#include "skeleton_3d.h"

namespace UVE::Scene {
namespace {

/// A ping-pong reflects off each end; more reflections than this in one step means a clip far
/// shorter than the step, where the exact phase is meaningless - the time is clamped instead.
constexpr int kMaximumReflectionsPerStep = 64;

using Pose = Core::TransformPose;

[[nodiscard]] Math::Vector3 Lerp(const Math::Vector3& from, const Math::Vector3& to, const float alpha) noexcept {
    return from + (to - from) * alpha;
}

/// Inertialization's fade: 1 at the start, 0 at the end, with zero speed and acceleration at both
/// (the complement of the quintic smoothstep), so the hand-over has no pop and no kink.
[[nodiscard]] float InertialDecay(const float progress) noexcept {
    const float x = std::clamp(progress, 0.0F, 1.0F);
    return 1.0F - x * x * x * (x * (x * 6.0F - 15.0F) + 10.0F);
}

/// Two bone names name the same bone: equal once a namespace ("mixamorig:", "Rig|") is dropped,
/// ignoring case - the rule posing uses, so root motion finds the bone the pose does.
[[nodiscard]] bool SameBoneName(std::string_view a, std::string_view b) noexcept {
    if (const auto pos = a.find_last_of(":|"); pos != std::string_view::npos) {
        a = a.substr(pos + 1U);
    }
    if (const auto pos = b.find_last_of(":|"); pos != std::string_view::npos) {
        b = b.substr(pos + 1U);
    }
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
           });
}

[[nodiscard]] const Asset::AnimationAssetBoneTrack* FindTrackForBone(
    const std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrack*>& tracks,
    const std::string_view boneName) noexcept {
    if (const auto found = tracks.find(boneName); found != tracks.end()) {
        return found->second;
    }
    std::string_view boneClean = boneName;
    if (const auto pos = boneClean.find_last_of(":|"); pos != std::string_view::npos) {
        boneClean = boneClean.substr(pos + 1U);
    }
    for (const auto& [trackBoneName, trackPtr] : tracks) {
        std::string_view trackClean = trackBoneName;
        if (const auto pos = trackClean.find_last_of(":|"); pos != std::string_view::npos) {
            trackClean = trackClean.substr(pos + 1U);
        }
        if (trackClean.size() == boneClean.size() &&
            std::equal(trackClean.begin(), trackClean.end(), boneClean.begin(), [](char a, char b) {
                return std::tolower(static_cast<unsigned char>(a)) ==
                       std::tolower(static_cast<unsigned char>(b));
            })) {
            return trackPtr;
        }
    }
    return nullptr;
}

[[nodiscard]] Math::Quaternion SlerpOrKeep(const Math::Quaternion& from, const Math::Quaternion& to,
                                                 const float alpha) noexcept {
    Math::Quaternion result = to;
    if (!Math::TrySlerp(from, to, alpha, result)) {
        result = alpha < 0.5F ? from : to;
    }
    return result;
}

[[nodiscard]] Pose ToPose(const Asset::AnimationAssetPose& pose) noexcept {
    return Pose{pose.position, pose.rotation, pose.scale};
}

[[nodiscard]] float SafeRatio(const float numerator, const float denominator) noexcept {
    return std::fabs(denominator) > 1e-6F ? numerator / denominator : numerator;
}

[[nodiscard]] Pose StartPose(const AnimationSequencerComponent& player) noexcept {
    return Pose{player.startPosition, player.startRotation, player.startScale};
}

/// The clip's motion since its first frame, laid on top of where the target started.
[[nodiscard]] Pose MakeRelative(const AnimationSequencerComponent& player, const Pose& first,
                                      const Pose& sampled) noexcept {
    Pose result;
    const Math::Vector3 offset = sampled.position - first.position;
    result.position = player.startPosition + Math::RotateVector(player.startRotation, player.startScale * offset);
    Math::Quaternion inverseFirst{};
    if (!Math::TryInverse(first.rotation, inverseFirst)) {
        inverseFirst = Math::Quaternion{};
    }
    result.rotation = Math::Multiply(player.startRotation, Math::Multiply(inverseFirst, sampled.rotation));
    result.scale = Math::Vector3{player.startScale.x * SafeRatio(sampled.scale.x, first.scale.x),
                                    player.startScale.y * SafeRatio(sampled.scale.y, first.scale.y),
                                    player.startScale.z * SafeRatio(sampled.scale.z, first.scale.z)};
    return result;
}

void WritePose(const AnimationDriverComponent& mixer, const Pose& pose, TransformComponent& target) noexcept {
    WriteAnimatedPose(pose, mixer.animatePosition, mixer.animateRotation, mixer.animateScale, target);
}

/// Moves the clock and applies the loop mode. Returns false when a Once clip reached its end.
[[nodiscard]] bool AdvanceClock(AnimationSequencerComponent& player, const double duration,
                                   const float deltaSeconds) noexcept {
    double time = static_cast<double>(player.currentTimeSeconds) +
                  static_cast<double>(player.speed) * static_cast<double>(player.direction) *
                      static_cast<double>(deltaSeconds);
    bool stillPlaying = true;
    switch (player.loopMode) {
        case AnimationLoopMode::Once:
            if (time >= duration || time <= 0.0) {
                time = std::clamp(time, 0.0, duration);
                stillPlaying = false;
            }
            break;
        case AnimationLoopMode::Loop:
            time = std::fmod(time, duration);
            if (time < 0.0) {
                time += duration;
            }
            break;
        case AnimationLoopMode::PingPong: {
            int reflections = 0;
            while ((time > duration || time < 0.0) && reflections < kMaximumReflectionsPerStep) {
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
[[nodiscard]] Core::TransformPose SampleTrack(const std::vector<Asset::AnimationAssetSample>& samples,
                                                    const double timeSeconds) noexcept {
    const auto upper = std::upper_bound(samples.begin(), samples.end(), timeSeconds,
                                        [](const double value, const Asset::AnimationAssetSample& sample) {
                                            return value < sample.timeSeconds;
                                        });
    if (upper == samples.begin()) {
        return ToPose(samples.front().pose);
    }
    if (upper == samples.end()) {
        return ToPose(samples.back().pose);
    }
    const Asset::AnimationAssetSample& right = *upper;
    const Asset::AnimationAssetSample& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const float alpha = span <= 0.0 ? 0.0F : static_cast<float>((timeSeconds - left.timeSeconds) / span);
    return Core::TransformPose{Lerp(left.pose.position, right.pose.position, alpha),
                                  SlerpOrKeep(left.pose.rotation, right.pose.rotation, alpha),
                                  Lerp(left.pose.scale, right.pose.scale, alpha)};
}

} // namespace

Core::TransformPose SampleAnimationTrack(const std::vector<Asset::AnimationAssetSample>& samples,
                                               const double timeSeconds) noexcept {
    return SampleTrack(samples, timeSeconds);
}

bool SampleMorphWeightTrack(const Asset::AnimationAssetMorphTrack& track, const double timeSeconds,
                              std::vector<float>& outWeights) {
    const std::vector<Asset::AnimationAssetMorphWeightSample>& samples = track.samples;
    if (samples.empty()) {
        return false;
    }
    if (samples.size() == 1U || timeSeconds <= samples.front().timeSeconds) {
        outWeights = samples.front().weights;
        return true;
    }
    if (timeSeconds >= samples.back().timeSeconds) {
        outWeights = samples.back().weights;
        return true;
    }
    // upper_bound finds the first key strictly after the time, so `before` is always a valid
    // predecessor: the two early returns above guarantee the time lies inside the key range.
    const auto after = std::upper_bound(
        samples.begin(), samples.end(), timeSeconds,
        [](const double time, const Asset::AnimationAssetMorphWeightSample& sample) {
            return time < sample.timeSeconds;
        });
    const auto before = std::prev(after);
    const double span = after->timeSeconds - before->timeSeconds;
    if (span <= 0.0) {
        // Duplicate keys cannot come from a validated clip, but a hand-built track can have them.
        // Taking the later key is the only answer that does not divide by zero.
        outWeights = after->weights;
        return true;
    }
    const auto alpha = static_cast<float>((timeSeconds - before->timeSeconds) / span);
    std::vector<float> blended = before->weights;
    if (blended.size() != after->weights.size()) {
        return false;
    }
    for (std::size_t index = 0U; index < blended.size(); ++index) {
        blended[index] += (after->weights[index] - before->weights[index]) * alpha;
    }
    outWeights = std::move(blended);
    return true;
}

float AnimationInertialDecay(const float progress) noexcept {
    return InertialDecay(progress);
}

Core::TransformPose SampleAnimationClipAsset(const Asset::AnimationClipAsset& clip,
                                                         const double timeSeconds) noexcept {
    return SampleTrack(clip.samples, timeSeconds);
}

void WriteAnimatedPose(const Core::TransformPose& pose, const bool position, const bool rotation, const bool scale,
                          TransformComponent& target) noexcept {
    if (position) {
        target.localPosition = pose.position;
    }
    if (rotation) {
        Math::Quaternion normalized{};
        if (Math::TryNormalize(pose.rotation, normalized)) {
            target.localRotation = normalized;
            Math::Vector3 euler{};
            if (Math::TryToEulerOrdered(normalized, target.eulerOrder, euler)) {
                target.localEulerRadians = euler;
            }
        }
    }
    if (scale) {
        target.localScale = pose.scale;
    }
}

bool IsAnimationSequencerObjectDefinitionValid(const AnimationSequencerObjectDefinition& value) noexcept {
    return IsAnimationSequencerComponentValid(value.player) && IsAnimationDriverComponentValid(value.mixer);
}

void ApplyAnimationSequencerObjectDefinition(IEntityManager& entityManager, const Entity entity,
                                           const AnimationSequencerObjectDefinition& value) {
    // The definition's mixer settings win over the base's defaults: added first, kept by the base.
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<AnimationDriverComponent>(entity)) {
        entityManager.AddComponent<AnimationDriverComponent>(entity, value.mixer);
    }
    ApplyAnimationDriverBase(entityManager, entity, AnimationSequencerObjectDefinition::defaultName);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<AnimationSequencerComponent>(entity)) {
        entityManager.AddComponent<AnimationSequencerComponent>(entity, value.player);
    }
}

void PlayAnimationSequencer(AnimationSequencerComponent& player, const TransformComponent& targetNow,
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

void StopAnimationSequencer(AnimationSequencerComponent& player) noexcept {
    player.isPlaying = false;
}

bool StepAnimationSequencer(AnimationSequencerComponent& player, const Asset::AnimationClipAsset& clip,
                            const float deltaSeconds, TransformComponent& target,
                            const AnimationDriverComponent& mixer) noexcept {
    if (!player.isPlaying) {
        return false;
    }
    const double duration = clip.durationSeconds;
    if ((clip.samples.empty() && clip.objects.empty()) || !std::isfinite(duration) || duration <= 0.0 ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        player.isPlaying = false;
        return false;
    }
    // Only other objects' tracks: the clock runs (the caller poses them), the target stays put.
    const bool movesTarget = !clip.samples.empty();

    const double eventsBefore = static_cast<double>(player.currentTimeSeconds);
    const bool eventsForward = player.speed * player.direction >= 0.0F;
    const bool firstStep = player.blendElapsedSeconds <= 0.0F;
    const bool stillPlaying = AdvanceClock(player, duration, deltaSeconds);
    player.blendElapsedSeconds = std::min(player.blendElapsedSeconds + deltaSeconds, 3600.0F);
    player.firedEvents = CollectPassedAnimationEvents(
        clip.events, eventsBefore, static_cast<double>(player.currentTimeSeconds), duration, eventsForward,
        player.loopMode == AnimationLoopMode::Loop &&
            (eventsForward ? player.currentTimeSeconds < eventsBefore : player.currentTimeSeconds > eventsBefore),
        firstStep);

    if (!stillPlaying) {
        player.isPlaying = false;
        player.finished = true;
        if (movesTarget && player.onFinish == AnimationFinishAction::ReturnToStart && player.hasStartPose) {
            WritePose(mixer, StartPose(player), target);
            return true;
        }
    }
    if (!movesTarget) {
        return true;
    }

    Pose pose = SampleAnimationClipAsset(clip, static_cast<double>(player.currentTimeSeconds));
    if (player.relative && player.hasStartPose) {
        pose = MakeRelative(player, ToPose(clip.samples.front().pose), pose);
    }
    if (player.blendInSeconds > 0.0F && player.hasStartPose && player.blendElapsedSeconds < player.blendInSeconds) {
        const float weight = player.blendElapsedSeconds / player.blendInSeconds;
        const Pose start = StartPose(player);
        pose = Pose{Lerp(start.position, pose.position, weight),
                       SlerpOrKeep(start.rotation, pose.rotation, weight), Lerp(start.scale, pose.scale, weight)};
    }
    WritePose(mixer, pose, target);
    return true;
}

bool StepSkeletalAnimationSequencer(AnimationSequencerComponent& player, const Asset::AnimationClipAsset& clip,
                                    const float deltaSeconds, Skeleton3DComponent& skeleton,
                                    const AnimationDriverComponent& mixer) {
    if (!player.isPlaying) {
        return false;
    }
    const double duration = clip.durationSeconds;
    if (!clip.IsSkeletal() || skeleton.bones.empty() || !std::isfinite(duration) || duration <= 0.0 ||
        !std::isfinite(deltaSeconds) || deltaSeconds < 0.0F) {
        player.isPlaying = false;
        return false;
    }
    std::vector<SkeletonBonePose> current = GetSkeletonCurrentPose(skeleton);
    const float blendBefore = player.blendElapsedSeconds;
    const double timeBefore = static_cast<double>(player.currentTimeSeconds);
    const bool forward = player.speed * player.direction >= 0.0F;
    const bool firstStep = player.blendElapsedSeconds <= 0.0F;
    const bool stillPlaying = AdvanceClock(player, duration, deltaSeconds);
    player.firedEvents = CollectPassedAnimationEvents(
        clip.events, timeBefore, static_cast<double>(player.currentTimeSeconds), duration, forward,
        player.loopMode == AnimationLoopMode::Loop &&
            (forward ? player.currentTimeSeconds < timeBefore : player.currentTimeSeconds > timeBefore),
        firstStep);
    player.blendElapsedSeconds = std::min(player.blendElapsedSeconds + deltaSeconds, 3600.0F);

    std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrack*> tracks;
    tracks.reserve(clip.bones.size());
    for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
        if (!track.samples.empty()) {
            tracks.emplace(track.bone, &track);
        }
    }
    const bool returnToRest = !stillPlaying && player.onFinish == AnimationFinishAction::ReturnToStart;
    const Retarget::ConformedPlayback conformed = PlanConformedPlaybackFor(clip, skeleton);
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
    const bool inertial = mixer.transition == AnimationTransitionMode::Inertialize && player.blendInSeconds > 0.0F;
    const std::size_t boneCount = skeleton.bones.size();
    const bool captureOffsets = inertial && blendBefore <= 0.0F;
    if (captureOffsets) {
        player.inertialPosition.assign(boneCount, Math::Vector3{});
        player.inertialRotation.assign(boneCount, Math::Quaternion{});
        player.inertialScale.assign(boneCount, Math::Vector3{});
    }
    const bool haveOffsets = inertial && player.inertialPosition.size() == boneCount &&
                             player.inertialRotation.size() == boneCount && player.inertialScale.size() == boneCount;
    const float decay = haveOffsets ? InertialDecay(player.blendElapsedSeconds / player.blendInSeconds) : 0.0F;
    for (std::size_t index = 0U; index < boneCount; ++index) {
        const SkeletonBone& bone = skeleton.bones[index];
        Pose target{bone.localPosition, bone.localRotation, bone.localScale};
        if (!returnToRest) {
            if (const auto* track = FindTrackForBone(tracks, bone.name); track != nullptr) {
                target = SampleTrack(track->samples, time);
                target.position = conformed.Position(bone.name, target.position, bone.localPosition);
                target.scale = conformed.Scale(target.scale, bone.localScale);
            }
        }
        SkeletonBonePose& out = current[index];
        if (captureOffsets) {
            Math::Quaternion inverse{};
            if (!Math::TryInverse(target.rotation, inverse)) {
                inverse = Math::Quaternion{};
            }
            player.inertialPosition[index] = out.position - target.position;
            player.inertialRotation[index] = Math::Multiply(out.rotation, inverse);
            player.inertialScale[index] = out.scale - target.scale;
        }
        Pose blended;
        if (haveOffsets) {
            blended = Pose{target.position + player.inertialPosition[index] * decay,
                              Math::Multiply(SlerpOrKeep(Math::Quaternion{}, player.inertialRotation[index], decay),
                                                target.rotation),
                              target.scale + player.inertialScale[index] * decay};
        } else {
            blended = Pose{Lerp(out.position, target.position, weight),
                              SlerpOrKeep(out.rotation, target.rotation, weight),
                              Lerp(out.scale, target.scale, weight)};
        }
        if (mixer.animatePosition) {
            out.position = blended.position;
        }
        if (mixer.animateRotation) {
            Math::Quaternion normalized{};
            if (Math::TryNormalize(blended.rotation, normalized)) {
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
    player.rootMotionDelta = Math::Vector3{};
    if (mixer.rootMotion != AnimationRootMotionMode::Off && mixer.animatePosition) {
        if (const std::optional<std::size_t> rootIndex = ResolveRootMotionBone(skeleton, clip, mixer.rootMotionBone)) {
            if (const Asset::AnimationAssetBoneTrack* const rootTrack = FindTrackForBone(tracks, skeleton.bones[*rootIndex].name)) {
                const auto& samples = rootTrack->samples;
                const std::string& rootName = skeleton.bones[*rootIndex].name;
                const auto at = [&](const double t) {
                    return conformed.Position(rootName, SampleTrack(samples, t).position, Math::Vector3{});
                };
                const Math::Vector3 first = at(0.0);
                if (!returnToRest) {
                    Math::Vector3 travel = at(time) - at(timeBefore);
                    if (player.loopMode == AnimationLoopMode::Loop) {
                        if (forward && time < timeBefore) {
                            travel = (at(duration) - at(timeBefore)) + (at(time) - first);
                        } else if (!forward && time > timeBefore) {
                            travel = (first - at(timeBefore)) + (at(time) - at(duration));
                        }
                    }
                    player.rootMotionDelta = Math::Vector3{travel.x * weight, 0.0F, travel.z * weight};
                }
                SkeletonBonePose& root = current[*rootIndex];
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

std::optional<std::size_t> ResolveRootMotionBone(const Skeleton3DComponent& skeleton,
                                                    const Asset::AnimationClipAsset& clip,
                                                    const std::string_view boneName) {
    const auto trackOf = [&clip](const std::string_view name) -> const Asset::AnimationAssetBoneTrack* {
        for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
            if (track.bone == name && !track.samples.empty()) {
                return &track;
            }
        }
        for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
            if (!track.samples.empty() && SameBoneName(track.bone, name)) {
                return &track;
            }
        }
        return nullptr;
    };
    if (!boneName.empty()) {
        for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
            if (SameBoneName(skeleton.bones[index].name, boneName)) {
                return trackOf(skeleton.bones[index].name) != nullptr ? std::optional<std::size_t>{index} : std::nullopt;
            }
        }
        return std::nullopt;
    }
    // Bones are parents first, so the first one that travels is the highest in the rig.
    constexpr float kMinimumTravelMetres = 0.01F;
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const Asset::AnimationAssetBoneTrack* const track = trackOf(skeleton.bones[index].name);
        if (track == nullptr) {
            continue;
        }
        const Math::Vector3 first = track->samples.front().pose.position;
        for (const Asset::AnimationAssetSample& sample : track->samples) {
            const float dx = sample.pose.position.x - first.x;
            const float dz = sample.pose.position.z - first.z;
            if (dx * dx + dz * dz > kMinimumTravelMetres * kMinimumTravelMetres) {
                return index;
            }
        }
    }
    return std::nullopt;
}

bool PoseSkeletonAtTime(const Asset::AnimationClipAsset& clip, const double timeSeconds,
                           Skeleton3DComponent& skeleton, const AnimationDriverComponent& mixer) {
    if (!clip.IsSkeletal() || skeleton.bones.empty() || !std::isfinite(timeSeconds)) {
        return false;
    }
    const double time = std::clamp(timeSeconds, 0.0, std::max(clip.durationSeconds, 0.0));
    const Retarget::ConformedPlayback conformed = PlanConformedPlaybackFor(clip, skeleton);

    std::unordered_map<std::string_view, const Asset::AnimationAssetBoneTrack*> tracks;
    tracks.reserve(clip.bones.size());
    for (const Asset::AnimationAssetBoneTrack& track : clip.bones) {
        if (!track.samples.empty()) {
            tracks.emplace(track.bone, &track);
        }
    }

    std::vector<SkeletonBonePose> pose(skeleton.bones.size());
    for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
        const SkeletonBone& bone = skeleton.bones[index];
        SkeletonBonePose& out = pose[index];
        out = SkeletonBonePose{bone.localPosition, bone.localRotation, bone.localScale};
        if (const auto* track = FindTrackForBone(tracks, bone.name); track != nullptr) {
            const Pose sampled = SampleTrack(track->samples, time);
            if (mixer.animatePosition) {
                out.position = conformed.Position(bone.name, sampled.position, bone.localPosition);
            }
            Math::Quaternion normalized{};
            if (mixer.animateRotation && Math::TryNormalize(sampled.rotation, normalized)) {
                out.rotation = normalized;
            }
            if (mixer.animateScale) {
                out.scale = conformed.Scale(sampled.scale, bone.localScale);
            }
        }
    }
    if (mixer.rootMotion != AnimationRootMotionMode::Off && mixer.animatePosition) {
        if (const std::optional<std::size_t> rootIndex = ResolveRootMotionBone(skeleton, clip, mixer.rootMotionBone)) {
            const std::string& rootName = skeleton.bones[*rootIndex].name;
            if (const auto* track = FindTrackForBone(tracks, rootName); track != nullptr && !track->samples.empty()) {
                const Math::Vector3 first = conformed.Position(track->bone, SampleTrack(track->samples, 0.0).position, Math::Vector3{});
                pose[*rootIndex].position.x = first.x;
                pose[*rootIndex].position.z = first.z;
            }
        }
    }
    skeleton.pose = std::move(pose);
    return true;
}

std::vector<std::string> CollectPassedAnimationEvents(const std::vector<Asset::AnimationAssetEvent>& events,
                                                         const double beforeSeconds, const double afterSeconds,
                                                         const double durationSeconds, const bool forward,
                                                         const bool wrapped, const bool includeStart) {
    // Passing order: forwards the events sorted by time, backwards reversed. A wrap passes the rest
    // of the clip first, then the part from the other end.
    std::vector<const Asset::AnimationAssetEvent*> sorted;
    sorted.reserve(events.size());
    for (const Asset::AnimationAssetEvent& event : events) {
        sorted.push_back(&event);
    }
    std::ranges::stable_sort(sorted, [](const auto* a, const auto* b) { return a->timeSeconds < b->timeSeconds; });
    if (!forward) {
        std::ranges::reverse(sorted);
    }
    std::vector<std::string> passed;
    const auto collect = [&](const double from, const double to, const bool fromInclusive) {
        for (const Asset::AnimationAssetEvent* event : sorted) {
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
