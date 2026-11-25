// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "mobile_gesture_recognizer.h"

#include <algorithm>
#include <cmath>

namespace UVE::Input {
namespace {

constexpr MobileGestureRecognizerConfig kDefaultGestureConfig{};

} // namespace

bool EvaluateTouchChordLifecycleTransition(
    const MobileInputSnapshot& previous, const MobileInputSnapshot& current,
    const std::size_t requiredTouchCount,
    TouchChordLifecycleTransition& outTransition) noexcept {
    if (requiredTouchCount < 2U || requiredTouchCount > kMaximumTouchCount) {
        return false;
    }

    const auto validateSnapshot = [](const MobileInputSnapshot& snapshot,
                                     std::size_t& outActiveCount) noexcept {
        std::size_t activeCount = 0U;
        for (std::size_t slot = 0U; slot < kMaximumTouchCount; ++slot) {
            const TouchPointState& touch = snapshot.touches[slot];
            if (!touch.active) {
                continue;
            }
            if (touch.identifier == 0U || !std::isfinite(touch.position.x) ||
                !std::isfinite(touch.position.y) || !std::isfinite(touch.delta.x) ||
                !std::isfinite(touch.delta.y) || !std::isfinite(touch.pressure) ||
                touch.pressure < 0.0F || touch.pressure > 1.0F) {
                return false;
            }
            for (std::size_t previousSlot = 0U; previousSlot < slot; ++previousSlot) {
                if (snapshot.touches[previousSlot].active &&
                    snapshot.touches[previousSlot].identifier == touch.identifier) {
                    return false;
                }
            }
            ++activeCount;
        }
        outActiveCount = activeCount;
        return true;
    };

    std::size_t previousCount = 0U;
    std::size_t currentCount = 0U;
    if (!validateSnapshot(previous, previousCount) ||
        !validateSnapshot(current, currentCount)) {
        return false;
    }

    const bool previousMatches = previousCount == requiredTouchCount;
    const bool currentMatches = currentCount == requiredTouchCount;
    TouchChordLifecycleTransition transition = TouchChordLifecycleTransition::None;
    if (!previousMatches && currentMatches) {
        transition = TouchChordLifecycleTransition::Began;
    } else if (previousMatches && !currentMatches) {
        transition = TouchChordLifecycleTransition::Ended;
    } else if (previousMatches && currentMatches) {
        bool sameIdentifiers = true;
        bool moved = false;
        for (std::size_t currentSlot = 0U; currentSlot < kMaximumTouchCount; ++currentSlot) {
            const TouchPointState& currentTouch = current.touches[currentSlot];
            if (!currentTouch.active) {
                continue;
            }
            const TouchPointState* previousTouch = nullptr;
            for (std::size_t previousSlot = 0U; previousSlot < kMaximumTouchCount; ++previousSlot) {
                const TouchPointState& candidate = previous.touches[previousSlot];
                if (candidate.active && candidate.identifier == currentTouch.identifier) {
                    previousTouch = &candidate;
                    break;
                }
            }
            if (previousTouch == nullptr) {
                sameIdentifiers = false;
                continue;
            }
            moved = moved || previousTouch->position != currentTouch.position ||
                    previousTouch->pressure != currentTouch.pressure;
        }
        transition = !sameIdentifiers
                         ? TouchChordLifecycleTransition::Replaced
                         : (moved ? TouchChordLifecycleTransition::Moved
                                  : TouchChordLifecycleTransition::None);
    }
    outTransition = transition;
    return true;
}

bool EvaluateTouchChord(const MobileInputSnapshot& snapshot,
                           const std::size_t requiredTouchCount,
                           Math::Vector2& outCentroid) noexcept {
    if (requiredTouchCount < 2U || requiredTouchCount > kMaximumTouchCount) {
        return false;
    }
    Math::Vector2 sum{};
    std::size_t activeCount = 0U;
    for (std::size_t slot = 0U; slot < kMaximumTouchCount; ++slot) {
        const TouchPointState& touch = snapshot.touches[slot];
        if (!touch.active) {
            continue;
        }
        if (touch.identifier == 0U || !std::isfinite(touch.position.x) ||
            !std::isfinite(touch.position.y)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < slot; ++previous) {
            if (snapshot.touches[previous].active &&
                snapshot.touches[previous].identifier == touch.identifier) {
                return false;
            }
        }
        const float candidateX = sum.x + touch.position.x;
        const float candidateY = sum.y + touch.position.y;
        if (!std::isfinite(candidateX) || !std::isfinite(candidateY)) {
            return false;
        }
        sum.x = candidateX;
        sum.y = candidateY;
        ++activeCount;
    }
    if (activeCount != requiredTouchCount) {
        return false;
    }
    const float inverseCount = 1.0F / static_cast<float>(activeCount);
    const Math::Vector2 centroid{sum.x * inverseCount, sum.y * inverseCount};
    if (!std::isfinite(centroid.x) || !std::isfinite(centroid.y)) {
        return false;
    }
    outCentroid = centroid;
    return true;
}

MobileGestureRecognizerConfig MobileGestureRecognizer::SanitizeConfig(
    MobileGestureRecognizerConfig config) noexcept {
    if (!std::isfinite(config.maximumTapDurationSeconds) || config.maximumTapDurationSeconds <= 0.0F) {
        config.maximumTapDurationSeconds = kDefaultGestureConfig.maximumTapDurationSeconds;
    }
    if (!std::isfinite(config.maximumTapDistance) || config.maximumTapDistance <= 0.0F) {
        config.maximumTapDistance = kDefaultGestureConfig.maximumTapDistance;
    }
    if (!std::isfinite(config.minimumSwipeDistance) || config.minimumSwipeDistance <= 0.0F) {
        config.minimumSwipeDistance = kDefaultGestureConfig.minimumSwipeDistance;
    }
    if (!std::isfinite(config.maximumSwipeDurationSeconds) || config.maximumSwipeDurationSeconds <= 0.0F) {
        config.maximumSwipeDurationSeconds = kDefaultGestureConfig.maximumSwipeDurationSeconds;
    }
    if (config.minimumSwipeDistance < config.maximumTapDistance) {
        config.minimumSwipeDistance = config.maximumTapDistance;
    }
    return config;
}

MobileGestureRecognizer::MobileGestureRecognizer(
    const MobileGestureRecognizerConfig config) noexcept
    : m_config(SanitizeConfig(config)) {}

bool MobileGestureRecognizer::IsFiniteVector(const Math::Vector2 value) noexcept {
    return std::isfinite(value.x) && std::isfinite(value.y);
}

namespace {

[[nodiscard]] bool IsValidGestureSnapshot(const MobileInputSnapshot& snapshot) noexcept {
    for (std::size_t slot = 0U; slot < kMaximumTouchCount; ++slot) {
        const TouchPointState& touch = snapshot.touches[slot];
        if (!touch.active) {
            continue;
        }
        if (touch.identifier == 0U || !std::isfinite(touch.position.x) ||
            !std::isfinite(touch.position.y) || !std::isfinite(touch.delta.x) ||
            !std::isfinite(touch.delta.y) || !std::isfinite(touch.pressure) ||
            touch.pressure < 0.0F || touch.pressure > 1.0F) {
            return false;
        }
        for (std::size_t previousSlot = 0U; previousSlot < slot; ++previousSlot) {
            if (snapshot.touches[previousSlot].active &&
                snapshot.touches[previousSlot].identifier == touch.identifier) {
                return false;
            }
        }
    }
    return true;
}

} // namespace

MobileSwipeDirection MobileGestureRecognizer::GetSwipeDirection(
    const Math::Vector2 delta) noexcept {
    if (std::fabs(delta.x) >= std::fabs(delta.y)) {
        return delta.x >= 0.0F ? MobileSwipeDirection::PositiveX : MobileSwipeDirection::NegativeX;
    }
    return delta.y >= 0.0F ? MobileSwipeDirection::PositiveY : MobileSwipeDirection::NegativeY;
}

MobileGestureEvent MobileGestureRecognizer::BuildGestureEvent(
    const TouchTrack& track, const MobileGestureType type,
    const MobileSwipeDirection direction) noexcept {
    return MobileGestureEvent{type,
                                 direction,
                                 track.identifier,
                                 track.startPosition,
                                 track.lastPosition,
                                 track.lastPosition - track.startPosition,
                                 track.durationSeconds};
}

void MobileGestureRecognizer::AppendReleaseGesture(const TouchTrack& track,
                                                          MobileGestureReport& report) const noexcept {
    if (report.count >= report.events.size()) {
        report.truncated = true;
        return;
    }
    const Math::Vector2 delta = track.lastPosition - track.startPosition;
    if (!IsFiniteVector(delta)) {
        return;
    }
    const double deltaX = static_cast<double>(delta.x);
    const double deltaY = static_cast<double>(delta.y);
    const double distanceSquared = deltaX * deltaX + deltaY * deltaY;
    const double tapDistanceSquared = static_cast<double>(m_config.maximumTapDistance) *
                                      static_cast<double>(m_config.maximumTapDistance);
    const double swipeDistanceSquared = static_cast<double>(m_config.minimumSwipeDistance) *
                                        static_cast<double>(m_config.minimumSwipeDistance);
    MobileGestureType type = MobileGestureType::Tap;
    MobileSwipeDirection direction = MobileSwipeDirection::PositiveX;
    if (track.durationSeconds <= m_config.maximumTapDurationSeconds && distanceSquared <= tapDistanceSquared) {
        type = MobileGestureType::Tap;
    } else if (track.durationSeconds <= m_config.maximumSwipeDurationSeconds &&
               distanceSquared >= swipeDistanceSquared) {
        type = MobileGestureType::Swipe;
        direction = GetSwipeDirection(delta);
    } else {
        return;
    }
    report.events[report.count++] = BuildGestureEvent(track, type, direction);
}

MobileGestureReport MobileGestureRecognizer::ConsumeSnapshot(
    const MobileInputSnapshot& snapshot, const float frameDeltaSeconds) noexcept {
    MobileGestureReport report{};
    if (snapshot.frameNumber == 0U || snapshot.frameNumber <= m_lastFrameNumber ||
        !std::isfinite(frameDeltaSeconds) || frameDeltaSeconds < 0.0F ||
        !IsValidGestureSnapshot(snapshot)) {
        return report;
    }
    m_lastFrameNumber = snapshot.frameNumber;
    const float deltaSeconds = std::clamp(frameDeltaSeconds, 0.0F, 10.0F);

    for (std::size_t touchSlot = 0U; touchSlot < kMaximumTouchCount; ++touchSlot) {
        const TouchPointState& touch = snapshot.touches[touchSlot];
        TouchTrack& track = m_tracks[touchSlot];
        if (touch.active && IsFiniteVector(touch.position)) {
            if (!track.active || track.identifier != touch.identifier) {
                if (track.active) {
                    AppendReleaseGesture(track, report);
                }
                track = TouchTrack{true, touch.identifier, touch.position, touch.position, 0.0F};
            } else {
                track.lastPosition = touch.position;
                track.durationSeconds += deltaSeconds;
            }
        } else if (track.active) {
            AppendReleaseGesture(track, report);
            track = {};
        }
    }
    return report;
}

void MobileGestureRecognizer::Reset() noexcept {
    m_tracks.fill({});
    m_lastFrameNumber = 0U;
}

} // namespace UVE::Input
