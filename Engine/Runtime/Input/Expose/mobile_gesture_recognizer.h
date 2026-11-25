// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

#include "i_mobile_input_system.h"

namespace UVE::Input {

enum class MobileGestureType : std::uint8_t {
    Tap,
    Swipe,
};

enum class MobileSwipeDirection : std::uint8_t {
    PositiveX,
    NegativeX,
    PositiveY,
    NegativeY,
};

enum class TouchChordLifecycleTransition : std::uint8_t {
    None = 0,
    Began,
    Moved,
    Ended,
    Replaced,
};

/// Classifies an exact-count multi-touch chord across copied previous/current snapshots.
/// The policy owns no gesture lifecycle, platform framework, touch polling, or ECS state.
[[nodiscard]] bool EvaluateTouchChordLifecycleTransition(
    const MobileInputSnapshot& previous, const MobileInputSnapshot& current,
    std::size_t requiredTouchCount, TouchChordLifecycleTransition& outTransition) noexcept;

/// Evaluates an exact-count multi-touch chord over one copied snapshot and publishes its centroid.
/// Non-finite running sums fail closed without changing the caller's prior centroid. The policy owns
/// no gesture lifecycle, platform framework, touch polling, or ECS state.
[[nodiscard]] bool EvaluateTouchChord(const MobileInputSnapshot& snapshot,
                                          std::size_t requiredTouchCount,
                                          Math::Vector2& outCentroid) noexcept;

struct MobileGestureEvent final {
    MobileGestureType type = MobileGestureType::Tap;
    MobileSwipeDirection direction = MobileSwipeDirection::PositiveX;
    std::uint64_t touchIdentifier = 0U;
    Math::Vector2 startPosition{};
    Math::Vector2 endPosition{};
    Math::Vector2 delta{};
    float durationSeconds = 0.0F;

    [[nodiscard]] bool operator==(const MobileGestureEvent&) const noexcept = default;
};

struct MobileGestureReport final {
    std::array<MobileGestureEvent, kMaximumTouchCount> events{};
    std::size_t count = 0U;
    bool truncated = false;

    [[nodiscard]] bool operator==(const MobileGestureReport&) const noexcept = default;
};

struct MobileGestureRecognizerConfig final {
    float maximumTapDurationSeconds = 0.25F;
    float maximumTapDistance = 24.0F;
    float minimumSwipeDistance = 48.0F;
    float maximumSwipeDurationSeconds = 0.75F;
};

/// Bounded frame-driven tap/swipe classification over copied mobile snapshots. The caller owns
/// platform coordinate mapping and supplies one finite frame delta per new snapshot. The recognizer
/// retains only capped per-touch tracking state and emits gestures when a tracked touch is released
/// or its identifier changes; it does not own device polling, application lifecycle, or ECS state.
class MobileGestureRecognizer final {
public:
    explicit MobileGestureRecognizer(MobileGestureRecognizerConfig config = {}) noexcept;

    [[nodiscard]] MobileGestureReport ConsumeSnapshot(const MobileInputSnapshot& snapshot,
                                                             float frameDeltaSeconds) noexcept;
    void Reset() noexcept;

private:
    struct TouchTrack final {
        bool active = false;
        std::uint64_t identifier = 0U;
        Math::Vector2 startPosition{};
        Math::Vector2 lastPosition{};
        float durationSeconds = 0.0F;
    };

    [[nodiscard]] static MobileGestureRecognizerConfig SanitizeConfig(
        MobileGestureRecognizerConfig config) noexcept;
    [[nodiscard]] static bool IsFiniteVector(Math::Vector2 value) noexcept;
    [[nodiscard]] static MobileSwipeDirection GetSwipeDirection(Math::Vector2 delta) noexcept;
    [[nodiscard]] static MobileGestureEvent BuildGestureEvent(
        const TouchTrack& track, MobileGestureType type,
        MobileSwipeDirection direction) noexcept;
    void AppendReleaseGesture(const TouchTrack& track, MobileGestureReport& report) const noexcept;

    MobileGestureRecognizerConfig m_config;
    std::array<TouchTrack, kMaximumTouchCount> m_tracks{};
    std::uint64_t m_lastFrameNumber = 0U;
};

} // namespace UVE::Input
