// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>

#include "vector2.h"
#include "vector3.h"

// Mobile snapshots and gesture policies are platform-neutral. The Android native host
// forwards motion events through AndroidTouchRouter and cancels contacts on pause,
// focus loss and surface destruction. The Evaluate* helpers remain reusable policy functions;
// the NativeActivity host owns the concrete platform lifecycle and frame gating.

namespace UVE::Input {

inline constexpr std::size_t kMaximumTouchCount = 10U;

/// One copied touch-slot value committed by MobileInputSystem.
/// Active contacts require a nonzero identifier that remains stable for the lifetime of a platform
/// touch contact; `delta` is computed only when the same identifier occupies the same slot in adjacent
/// committed frames.
struct TouchPointState final {
    bool active = false;
    std::uint64_t identifier = 0U;
    Math::Vector2 position{};
    Math::Vector2 delta{};
    float pressure = 0.0F;

    [[nodiscard]] bool operator==(const TouchPointState&) const noexcept = default;
};

/// Bounded, backend-neutral mobile input snapshot. Gyroscope values are rotation rates in radians
/// per second, normalized only for finite input; platform adapters own coordinate-system mapping.
enum class TouchLifecycleTransition : std::uint8_t {
    None = 0,
    Began,
    Moved,
    Ended,
    Replaced,
};

enum class MobileLifecycleState : std::uint8_t {
    Inactive = 0,
    Active,
    Suspended,
    Terminated,
    Count,
};

enum class MobileLifecycleTransition : std::uint8_t {
    None = 0,
    Activated,
    Resumed,
    Suspended,
    Deactivated,
    Terminated,
    Reinitialized,
};

enum class MobileInputAvailability : std::uint8_t {
    Disabled = 0,
    TouchOnly,
    TouchAndGyroscope,
};

/// Evaluates caller-owned mobile input acceptance policy from copied lifecycle/focus facts. Native
/// callbacks, background execution, and platform-specific sensor policy remain outside this helper.
[[nodiscard]] inline bool EvaluateMobileInputPolicy(
    const MobileLifecycleState lifecycle, const bool focused, const bool allowBackgroundTouch,
    const bool allowBackgroundGyroscope, MobileInputAvailability& outAvailability) noexcept {
    if (static_cast<std::uint8_t>(lifecycle) >= static_cast<std::uint8_t>(MobileLifecycleState::Count)) {
        return false;
    }
    MobileInputAvailability availability = MobileInputAvailability::Disabled;
    if (lifecycle == MobileLifecycleState::Active) {
        const bool touchEnabled = focused || allowBackgroundTouch;
        const bool gyroscopeEnabled = focused || allowBackgroundGyroscope;
        availability = gyroscopeEnabled ? MobileInputAvailability::TouchAndGyroscope
                                        : (touchEnabled ? MobileInputAvailability::TouchOnly
                                                        : MobileInputAvailability::Disabled);
    }
    outAvailability = availability;
    return true;
}

/// Classifies a copied application-lifecycle edge for Android/iOS adapters without owning native
/// lifecycle callbacks, process state, or platform shutdown policy.
[[nodiscard]] inline bool EvaluateMobileLifecycleTransition(
    const MobileLifecycleState previous, const MobileLifecycleState current,
    MobileLifecycleTransition& outTransition) noexcept {
    const auto isValid = [](const MobileLifecycleState state) noexcept {
        return static_cast<std::uint8_t>(state) < static_cast<std::uint8_t>(MobileLifecycleState::Count);
    };
    if (!isValid(previous) || !isValid(current)) {
        return false;
    }
    MobileLifecycleTransition transition = MobileLifecycleTransition::None;
    if (previous != current) {
        switch (current) {
        case MobileLifecycleState::Active:
            transition = previous == MobileLifecycleState::Suspended
                             ? MobileLifecycleTransition::Resumed
                             : (previous == MobileLifecycleState::Terminated
                                    ? MobileLifecycleTransition::Reinitialized
                                    : MobileLifecycleTransition::Activated);
            break;
        case MobileLifecycleState::Suspended:
            transition = MobileLifecycleTransition::Suspended;
            break;
        case MobileLifecycleState::Inactive:
            transition = MobileLifecycleTransition::Deactivated;
            break;
        case MobileLifecycleState::Terminated:
            transition = MobileLifecycleTransition::Terminated;
            break;
        case MobileLifecycleState::Count:
            return false;
        }
    }
    outTransition = transition;
    return true;
}

/// Classifies one copied touch-slot transition. Replaced means two active frames have different
/// identifiers in the same slot; the helper does not poll hardware, own lifecycle state, or mutate snapshots.
[[nodiscard]] inline bool EvaluateTouchLifecycleTransition(
    const TouchPointState& previous, const TouchPointState& current,
    TouchLifecycleTransition& outTransition) noexcept {
    const auto isValid = [](const TouchPointState& touch) noexcept {
        return !touch.active || (touch.identifier != 0U && std::isfinite(touch.position.x) &&
                                 std::isfinite(touch.position.y) && std::isfinite(touch.delta.x) &&
                                 std::isfinite(touch.delta.y) && std::isfinite(touch.pressure) &&
                                 touch.pressure >= 0.0F && touch.pressure <= 1.0F);
    };
    if (!isValid(previous) || !isValid(current)) return false;
    TouchLifecycleTransition transition = TouchLifecycleTransition::None;
    if (!previous.active && current.active) {
        transition = TouchLifecycleTransition::Began;
    } else if (previous.active && !current.active) {
        transition = TouchLifecycleTransition::Ended;
    } else if (previous.active && current.active) {
        transition = previous.identifier == current.identifier
                         ? ((previous.position == current.position && previous.pressure == current.pressure)
                                ? TouchLifecycleTransition::None
                                : TouchLifecycleTransition::Moved)
                         : TouchLifecycleTransition::Replaced;
    }
    outTransition = transition;
    return true;
}

struct MobileInputSnapshot final {
    std::array<TouchPointState, kMaximumTouchCount> touches{};
    Math::Vector3 gyroscopeRotationRate{};
    std::uint64_t frameNumber = 0U;

    [[nodiscard]] bool operator==(const MobileInputSnapshot&) const noexcept = default;
};

/// Injectable mobile input value service. It owns only live/current/previous copied snapshots and
/// does not poll Android/iOS APIs, own an application lifecycle, or select a platform backend.
/// Set*State() methods are safe from any thread; Update() and snapshot queries are main-thread-only.
class IMobileInputSystem {
public:
    virtual ~IMobileInputSystem() = default;

    virtual void SetTouchState(std::size_t touchSlot, bool active, std::uint64_t identifier,
                                  Math::Vector2 position, float pressure) = 0;
    virtual void SetGyroscopeRotationRate(Math::Vector3 rotationRate) = 0;
    virtual void Update() = 0;

    [[nodiscard]] virtual MobileInputSnapshot GetSnapshot() const = 0;
    [[nodiscard]] virtual MobileInputSnapshot GetPreviousSnapshot() const = 0;
};

} // namespace UVE::Input
