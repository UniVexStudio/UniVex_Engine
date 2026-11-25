// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <cmath>
#include <cstdint>

namespace UVE::Input {

inline constexpr std::size_t kMaximumGamepadCount = 4U;
inline constexpr std::size_t kGamepadAxisCount = 6U;
inline constexpr std::size_t kGamepadButtonCount = 16U;

/// Fixed cross-platform axis vocabulary. Backends map their native layout into this value type.
enum class GamepadAxis : std::uint8_t {
    LeftX = 0,
    LeftY,
    RightX,
    RightY,
    LeftTrigger,
    RightTrigger,
    Count,
};

/// Fixed cross-platform button vocabulary. Backends map their native layout into this value type.
enum class GamepadButton : std::uint8_t {
    South = 0,
    East,
    West,
    North,
    LeftBumper,
    RightBumper,
    Back,
    Start,
    Guide,
    LeftStick,
    RightStick,
    DPadUp,
    DPadDown,
    DPadLeft,
    DPadRight,
    Miscellaneous,
    Count,
};

struct GamepadStateSnapshot final {
    bool connected = false;
    std::array<float, kGamepadAxisCount> axes{};
    std::array<bool, kGamepadButtonCount> buttons{};
    std::uint64_t frameNumber = 0U;

    [[nodiscard]] bool operator==(const GamepadStateSnapshot&) const noexcept = default;
};

enum class GamepadConnectionTransition : std::uint8_t {
    None = 0,
    Connected,
    Disconnected,
};

/// Classifies a copied gamepad connection edge without polling hardware or owning device state.
///
/// Caller scope: the intended caller is a platform input adapter that polls gamepad devices and
/// feeds this helper one previous/current snapshot pair. No such adapter exists in this tree, so
/// this helper currently has no Engine/ caller - that is a scope boundary, not an unfinished
/// integration. See the matching mobile helpers in i_mobile_input_system_uve.h.
[[nodiscard]] inline bool EvaluateGamepadConnectionTransition(
    const GamepadStateSnapshot& previous, const GamepadStateSnapshot& current,
    GamepadConnectionTransition& outTransition) noexcept {
    const auto isValid = [](const GamepadStateSnapshot& snapshot) noexcept {
        for (const float axis : snapshot.axes) {
            if (!std::isfinite(axis) || axis < -1.0F || axis > 1.0F) {
                return false;
            }
        }
        return true;
    };
    if (!isValid(previous) || !isValid(current)) {
        return false;
    }
    const GamepadConnectionTransition transition =
        !previous.connected && current.connected
            ? GamepadConnectionTransition::Connected
            : (previous.connected && !current.connected
                   ? GamepadConnectionTransition::Disconnected
                   : GamepadConnectionTransition::None);
    outTransition = transition;
    return true;
}

/// Injectable, bounded gamepad state service. Set*State() methods update a live copy and are
/// safe from any thread; Update() commits one deterministic current/previous snapshot on the
/// main thread. Axis values are clamped to [-1,1] and normalized through the configured symmetric
/// dead zone. A disconnected pad commits a zeroed snapshot while preserving the frame counter.
class IGamepadInputSystem {
public:
    virtual ~IGamepadInputSystem() = default;

    virtual void SetConnected(std::size_t gamepadIndex, bool connected) = 0;
    virtual void SetAxisState(std::size_t gamepadIndex, GamepadAxis axis, float value) = 0;
    virtual void SetButtonState(std::size_t gamepadIndex, GamepadButton button, bool isDown) = 0;

    /// Commits all live device state exactly once for the frame and increments its copied frame
    /// number. Must be called on the main thread.
    virtual void Update() = 0;

    [[nodiscard]] virtual GamepadStateSnapshot GetSnapshot(std::size_t gamepadIndex) const = 0;
    [[nodiscard]] virtual GamepadStateSnapshot GetPreviousSnapshot(std::size_t gamepadIndex) const = 0;
    [[nodiscard]] virtual float GetAxisValue(std::size_t gamepadIndex, GamepadAxis axis) const = 0;
    [[nodiscard]] virtual bool IsButtonDown(std::size_t gamepadIndex, GamepadButton button) const = 0;
    [[nodiscard]] virtual bool WasButtonPressedThisFrame(std::size_t gamepadIndex,
                                                             GamepadButton button) const = 0;
    [[nodiscard]] virtual bool WasButtonReleasedThisFrame(std::size_t gamepadIndex,
                                                              GamepadButton button) const = 0;
    [[nodiscard]] virtual float GetDeadZone() const noexcept = 0;
    virtual void SetDeadZone(float deadZone) noexcept { static_cast<void>(deadZone); }
};

} // namespace UVE::Input
