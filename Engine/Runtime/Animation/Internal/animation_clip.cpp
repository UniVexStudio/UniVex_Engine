// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "animation_clip.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace UVE::Core {
namespace {

[[nodiscard]] bool IsFiniteNonNegative(const double value) noexcept {
    return std::isfinite(value) && value >= 0.0;
}

[[nodiscard]] double NormalizeSampleTime(const double timeSeconds, const double durationSeconds,
                                            const bool looping) noexcept {
    if (!looping || durationSeconds <= 0.0) {
        return std::clamp(timeSeconds, 0.0, durationSeconds);
    }
    double normalized = std::fmod(timeSeconds, durationSeconds);
    if (normalized < 0.0) {
        normalized += durationSeconds;
    }
    return normalized;
}

[[nodiscard]] TransformPose InterpolatePose(const TransformPose& left,
                                                  const TransformPose& right,
                                                  const double alpha) noexcept {
    TransformPose result;
    const float factor = static_cast<float>(std::clamp(alpha, 0.0, 1.0));
    result.position = left.position * (1.0F - factor) + right.position * factor;
    result.scale = left.scale * (1.0F - factor) + right.scale * factor;
    result.rotation = Math::Quaternion{
        left.rotation.x * (1.0F - factor) + right.rotation.x * factor,
        left.rotation.y * (1.0F - factor) + right.rotation.y * factor,
        left.rotation.z * (1.0F - factor) + right.rotation.z * factor,
        left.rotation.w * (1.0F - factor) + right.rotation.w * factor,
    };
    TransformPose normalized;
    return TryNormalizeTransformPose(result, normalized) ? normalized : left;
}

} // namespace

AnimationClipValidationResult ValidateAnimationClip(const AnimationClip& clip) noexcept {
    if (clip.clipId.empty() || clip.clipId.size() > AnimationClip::kMaximumIdentifierBytes) {
        return {AnimationClipValidationCode::InvalidIdentifier, 0U,
                "Animation clip identifier is empty or exceeds the bounded length."};
    }
    if (!IsFiniteNonNegative(clip.durationSeconds) || clip.durationSeconds <= 0.0) {
        return {AnimationClipValidationCode::InvalidDuration, 0U,
                "Animation clip duration must be finite and greater than zero."};
    }
    if (clip.samples.empty() || clip.samples.size() > AnimationClip::kMaximumSamples ||
        clip.events.size() > AnimationClip::kMaximumEvents) {
        return {AnimationClipValidationCode::CapacityExceeded, 0U,
                "Animation clip samples/events exceed the bounded contract."};
    }
    double previousTime = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < clip.samples.size(); ++index) {
        const PoseSample& sample = clip.samples[index];
        if (!IsFiniteNonNegative(sample.timeSeconds) || sample.timeSeconds > clip.durationSeconds) {
            return {AnimationClipValidationCode::InvalidSampleTime, index,
                    "Animation sample time is outside the clip duration."};
        }
        if (sample.timeSeconds < previousTime) {
            return {AnimationClipValidationCode::UnsortedSamples, index,
                    "Animation samples must be sorted by non-decreasing time."};
        }
        if (!IsFiniteTransformPose(sample.pose)) {
            return {AnimationClipValidationCode::InvalidPose, index,
                    "Animation sample pose contains non-finite values."};
        }
        TransformPose normalized;
        if (!TryNormalizeTransformPose(sample.pose, normalized)) {
            return {AnimationClipValidationCode::InvalidPose, index,
                    "Animation sample rotation must be finite and non-zero."};
        }
        previousTime = sample.timeSeconds;
    }
    previousTime = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0U; index < clip.events.size(); ++index) {
        const AnimationEvent& event = clip.events[index];
        if (!IsFiniteNonNegative(event.timeSeconds) || event.timeSeconds > clip.durationSeconds) {
            return {AnimationClipValidationCode::InvalidEventTime, index,
                    "Animation event time is outside the clip duration."};
        }
        if (event.timeSeconds < previousTime) {
            return {AnimationClipValidationCode::UnsortedEvents, index,
                    "Animation events must be sorted by non-decreasing time."};
        }
        if (event.eventId.empty() || event.eventId.size() > AnimationClip::kMaximumIdentifierBytes) {
            return {AnimationClipValidationCode::InvalidEventIdentifier, index,
                    "Animation event identifier is empty or exceeds the bounded length."};
        }
        previousTime = event.timeSeconds;
    }
    return {AnimationClipValidationCode::Valid, 0U, "Animation clip is valid."};
}

bool TrySampleAnimationClip(const AnimationClip& clip, const double timeSeconds,
                              const bool looping, TransformPose& outPose) noexcept {
    if (!ValidateAnimationClip(clip).IsValid() || !std::isfinite(timeSeconds)) {
        return false;
    }
    const double sampleTime = NormalizeSampleTime(timeSeconds, clip.durationSeconds, looping);
    const auto upper = std::upper_bound(clip.samples.begin(), clip.samples.end(), sampleTime,
                                        [](const double value, const PoseSample& sample) {
                                            return value < sample.timeSeconds;
                                        });
    if (upper == clip.samples.begin()) {
        return TryNormalizeTransformPose(clip.samples.front().pose, outPose);
    }
    if (upper == clip.samples.end()) {
        return TryNormalizeTransformPose(clip.samples.back().pose, outPose);
    }
    const PoseSample& right = *upper;
    const PoseSample& left = *(upper - 1);
    const double span = right.timeSeconds - left.timeSeconds;
    const double alpha = span <= 0.0 ? 0.0 : (sampleTime - left.timeSeconds) / span;
    outPose = InterpolatePose(left.pose, right.pose, alpha);
    return true;
}

std::vector<AnimationEvent> CollectAnimationEvents(const AnimationClip& clip,
                                                         const double startSeconds,
                                                         const double endSeconds,
                                                         const bool looping) {
    std::vector<AnimationEvent> result;
    if (!ValidateAnimationClip(clip).IsValid() || !std::isfinite(startSeconds) ||
        !std::isfinite(endSeconds) || endSeconds < startSeconds) {
        return result;
    }
    if (!looping || endSeconds <= clip.durationSeconds) {
        for (const AnimationEvent& event : clip.events) {
            if (event.timeSeconds > startSeconds && event.timeSeconds <= endSeconds) {
                result.push_back(event);
            }
        }
        return result;
    }
    const double firstLoop = std::floor(startSeconds / clip.durationSeconds);
    const double lastLoop = std::floor(endSeconds / clip.durationSeconds);
    if (!std::isfinite(firstLoop) || !std::isfinite(lastLoop) ||
        firstLoop < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        firstLoop >= static_cast<double>(std::numeric_limits<std::int64_t>::max()) ||
        lastLoop < static_cast<double>(std::numeric_limits<std::int64_t>::min()) ||
        lastLoop >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return result;
    }
    const std::int64_t firstLoopIndex = static_cast<std::int64_t>(firstLoop);
    const std::int64_t lastLoopIndex = static_cast<std::int64_t>(lastLoop);
    const std::uint64_t loopSpan = static_cast<std::uint64_t>(lastLoopIndex) -
                                   static_cast<std::uint64_t>(firstLoopIndex);
    const std::uint64_t maximumLoops =
        (AnimationClip::kMaximumCollectedEvents / std::max<std::size_t>(clip.events.size(), 1U)) + 1U;
    if (loopSpan >= maximumLoops) {
        return result;
    }
    result.reserve(std::min(AnimationClip::kMaximumCollectedEvents,
                            clip.events.size() * static_cast<std::size_t>(loopSpan + 1U)));
    for (std::int64_t loop = firstLoopIndex;; ++loop) {
        const double offset = static_cast<double>(loop) * clip.durationSeconds;
        for (const AnimationEvent& event : clip.events) {
            const double occurrence = offset + event.timeSeconds;
            if (occurrence > startSeconds && occurrence <= endSeconds) {
                if (result.size() >= AnimationClip::kMaximumCollectedEvents) {
                    return result;
                }
                result.push_back(event);
            }
        }
        if (loop == lastLoopIndex) {
            break;
        }
    }
    return result;
}

} // namespace UVE::Core
