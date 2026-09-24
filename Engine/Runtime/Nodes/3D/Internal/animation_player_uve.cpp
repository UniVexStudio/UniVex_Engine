// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_player_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/asset/animation_clip_asset_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {
namespace {

/// A ping-pong reflects off each end; more reflections than this in one step means a clip far
/// shorter than the step, where the exact phase is meaningless - the time is clamped instead.
constexpr int kMaximumReflectionsPerStepUVE = 64;

using PoseUVE = Core::TransformPoseUVE;

[[nodiscard]] Math::Vector3UVE LerpUVE(const Math::Vector3UVE& from, const Math::Vector3UVE& to, const float alpha) noexcept {
    return from + (to - from) * alpha;
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

void WritePoseUVE(const AnimationPlayerComponentUVE& player, const PoseUVE& pose, TransformComponentUVE& target) noexcept {
    WriteAnimatedPoseUVE(pose, player.animatePosition, player.animateRotation, player.animateScale, target);
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

Core::TransformPoseUVE SampleAnimationClipAssetUVE(const Asset::AnimationClipAssetUVE& clip,
                                                         const double timeSeconds) noexcept {
    const auto upper = std::upper_bound(clip.samples.begin(), clip.samples.end(), timeSeconds,
                                        [](const double value, const Asset::AnimationAssetSampleUVE& sample) {
                                            return value < sample.timeSeconds;
                                        });
    if (upper == clip.samples.begin()) {
        return ToPoseUVE(clip.samples.front().pose);
    }
    if (upper == clip.samples.end()) {
        return ToPoseUVE(clip.samples.back().pose);
    }
    const Asset::AnimationAssetSampleUVE& right = *upper;
    const Asset::AnimationAssetSampleUVE& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const float alpha = span <= 0.0 ? 0.0F : static_cast<float>((timeSeconds - left.timeSeconds) / span);
    return Core::TransformPoseUVE{LerpUVE(left.pose.position, right.pose.position, alpha),
                                  SlerpOrKeepUVE(left.pose.rotation, right.pose.rotation, alpha),
                                  LerpUVE(left.pose.scale, right.pose.scale, alpha)};
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

bool IsAnimationPlayerNodeDefinitionValidUVE(const AnimationPlayerNodeDefinitionUVE& value) noexcept {
    return IsAnimationPlayerComponentValidUVE(value.player);
}

void ApplyAnimationPlayerNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                           const AnimationPlayerNodeDefinitionUVE& value) {
    EnsureNodeBaselineUVE(entityManager, entity, AnimationPlayerNodeDefinitionUVE::defaultName);
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
    player.startPosition = targetNow.localPosition;
    player.startRotation = targetNow.localRotation;
    player.startScale = targetNow.localScale;
}

void StopAnimationPlayerUVE(AnimationPlayerComponentUVE& player) noexcept {
    player.isPlaying = false;
}

bool StepAnimationPlayerUVE(AnimationPlayerComponentUVE& player, const Asset::AnimationClipAssetUVE& clip,
                            const float deltaSeconds, TransformComponentUVE& target) noexcept {
    if (!player.isPlaying) {
        return false;
    }
    const double duration = clip.durationSeconds;
    if (clip.samples.empty() || !std::isfinite(duration) || duration <= 0.0 || !std::isfinite(deltaSeconds) ||
        deltaSeconds < 0.0F) {
        player.isPlaying = false;
        return false;
    }

    const bool stillPlaying = AdvanceClockUVE(player, duration, deltaSeconds);
    player.blendElapsedSeconds = std::min(player.blendElapsedSeconds + deltaSeconds, 3600.0F);

    if (!stillPlaying) {
        player.isPlaying = false;
        player.finished = true;
        if (player.onFinish == AnimationFinishActionUVE::ReturnToStart && player.hasStartPose) {
            WritePoseUVE(player, StartPoseUVE(player), target);
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
    WritePoseUVE(player, pose, target);
    return true;
}

} // namespace UVE::Scene
