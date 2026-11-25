// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include "time_pose_contract.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace UVE::Core {

struct AnimationEvent final {
    double timeSeconds = 0.0;
    std::string eventId;

    [[nodiscard]] bool operator==(const AnimationEvent&) const = default;
};

struct AnimationClip final {
    static constexpr std::size_t kMaximumSamples = 32768U;
    static constexpr std::size_t kMaximumEvents = 1024U;
    static constexpr std::size_t kMaximumCollectedEvents = 1024U;
    static constexpr std::size_t kMaximumIdentifierBytes = 128U;

    std::string clipId;
    double durationSeconds = 0.0;
    std::vector<PoseSample> samples;
    std::vector<AnimationEvent> events;
};

enum class AnimationClipValidationCode : std::uint8_t {
    Valid = 0,
    InvalidIdentifier,
    InvalidDuration,
    CapacityExceeded,
    InvalidSampleTime,
    UnsortedSamples,
    InvalidPose,
    InvalidEventTime,
    UnsortedEvents,
    InvalidEventIdentifier,
};

struct AnimationClipValidationResult final {
    AnimationClipValidationCode code = AnimationClipValidationCode::InvalidIdentifier;
    std::size_t index = 0U;
    std::string message;

    [[nodiscard]] bool IsValid() const noexcept {
        return code == AnimationClipValidationCode::Valid;
    }
};

[[nodiscard]] AnimationClipValidationResult ValidateAnimationClip(
    const AnimationClip& clip) noexcept;

[[nodiscard]] bool TrySampleAnimationClip(const AnimationClip& clip, double timeSeconds,
                                             bool looping, TransformPose& outPose) noexcept;

/// Collects events in chronological loop order and caps copied output at
/// `AnimationClip::kMaximumCollectedEvents`; excessively large loop indices/ranges fail closed.
[[nodiscard]] std::vector<AnimationEvent> CollectAnimationEvents(
    const AnimationClip& clip, double startSeconds, double endSeconds, bool looping);

} // namespace UVE::Core
