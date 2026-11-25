// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <array>
#include <cstddef>
#include <mutex>

#include "i_gamepad_input_system.h"

namespace UVE::Input {

class GamepadInputSystem final : public IGamepadInputSystem {
public:
    explicit GamepadInputSystem(float deadZone = 0.15F) noexcept;

    void SetConnected(std::size_t gamepadIndex, bool connected) override;
    void SetAxisState(std::size_t gamepadIndex, GamepadAxis axis, float value) override;
    void SetButtonState(std::size_t gamepadIndex, GamepadButton button, bool isDown) override;
    void Update() override;

    [[nodiscard]] GamepadStateSnapshot GetSnapshot(std::size_t gamepadIndex) const override;
    [[nodiscard]] GamepadStateSnapshot GetPreviousSnapshot(std::size_t gamepadIndex) const override;
    [[nodiscard]] float GetAxisValue(std::size_t gamepadIndex, GamepadAxis axis) const override;
    [[nodiscard]] bool IsButtonDown(std::size_t gamepadIndex, GamepadButton button) const override;
    [[nodiscard]] bool WasButtonPressedThisFrame(std::size_t gamepadIndex,
                                                     GamepadButton button) const override;
    [[nodiscard]] bool WasButtonReleasedThisFrame(std::size_t gamepadIndex,
                                                      GamepadButton button) const override;
    [[nodiscard]] float GetDeadZone() const noexcept override { return m_deadZone; }
    void SetDeadZone(float deadZone) noexcept override;

private:
    using SnapshotArray = std::array<GamepadStateSnapshot, kMaximumGamepadCount>;

    [[nodiscard]] static bool IsValidGamepadIndex(std::size_t gamepadIndex) noexcept;
    [[nodiscard]] static bool IsValidAxis(GamepadAxis axis) noexcept;
    [[nodiscard]] static bool IsValidButton(GamepadButton button) noexcept;
    [[nodiscard]] float NormalizeAxisValue(float value) const noexcept;

    float m_deadZone;

    mutable std::mutex m_liveStateMutex;
    SnapshotArray m_liveState{};
    SnapshotArray m_currentState{};
    SnapshotArray m_previousState{};
    std::uint64_t m_frameNumber = 0U;
};

} // namespace UVE::Input
