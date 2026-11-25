// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "gamepad_input_system.h"
#include "input_frame_counter.h"

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace UVE::Input {

GamepadInputSystem::GamepadInputSystem(const float deadZone) noexcept
    : m_deadZone(std::isfinite(deadZone) && deadZone >= 0.0F && deadZone < 1.0F ? deadZone : 0.15F) {}

void GamepadInputSystem::SetDeadZone(const float deadZone) noexcept {
    if (std::isfinite(deadZone) && deadZone >= 0.0F && deadZone < 1.0F) {
        m_deadZone = deadZone;
    }
}

bool GamepadInputSystem::IsValidGamepadIndex(const std::size_t gamepadIndex) noexcept {
    return gamepadIndex < kMaximumGamepadCount;
}

bool GamepadInputSystem::IsValidAxis(const GamepadAxis axis) noexcept {
    return static_cast<std::size_t>(axis) < kGamepadAxisCount;
}

bool GamepadInputSystem::IsValidButton(const GamepadButton button) noexcept {
    return static_cast<std::size_t>(button) < kGamepadButtonCount;
}

float GamepadInputSystem::NormalizeAxisValue(const float value) const noexcept {
    if (!std::isfinite(value)) {
        return 0.0F;
    }
    const float clamped = std::clamp(value, -1.0F, 1.0F);
    const float magnitude = std::fabs(clamped);
    if (magnitude <= m_deadZone) {
        return 0.0F;
    }
    const float normalizedMagnitude = (magnitude - m_deadZone) / (1.0F - m_deadZone);
    return std::copysign(normalizedMagnitude, clamped);
}

void GamepadInputSystem::SetConnected(const std::size_t gamepadIndex, const bool connected) {
    if (!IsValidGamepadIndex(gamepadIndex)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_liveState[gamepadIndex].connected = connected;
    if (!connected) {
        m_liveState[gamepadIndex].axes.fill(0.0F);
        m_liveState[gamepadIndex].buttons.fill(false);
    }
}

void GamepadInputSystem::SetAxisState(const std::size_t gamepadIndex, const GamepadAxis axis,
                                            const float value) {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidAxis(axis)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    if (!m_liveState[gamepadIndex].connected) {
        return;
    }
    m_liveState[gamepadIndex].axes[static_cast<std::size_t>(axis)] = value;
}

void GamepadInputSystem::SetButtonState(const std::size_t gamepadIndex, const GamepadButton button,
                                              const bool isDown) {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidButton(button)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    if (!m_liveState[gamepadIndex].connected) {
        return;
    }
    m_liveState[gamepadIndex].buttons[static_cast<std::size_t>(button)] = isDown;
}

void GamepadInputSystem::Update() {
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_previousState = m_currentState;
    AdvanceInputFrameNumber(m_frameNumber);

    for (std::size_t gamepadIndex = 0U; gamepadIndex < kMaximumGamepadCount; ++gamepadIndex) {
        GamepadStateSnapshot committed{};
        committed.frameNumber = m_frameNumber;
        committed.connected = m_liveState[gamepadIndex].connected;
        if (committed.connected) {
            for (std::size_t axisIndex = 0U; axisIndex < kGamepadAxisCount; ++axisIndex) {
                committed.axes[axisIndex] = NormalizeAxisValue(m_liveState[gamepadIndex].axes[axisIndex]);
            }
            committed.buttons = m_liveState[gamepadIndex].buttons;
        }
        m_currentState[gamepadIndex] = committed;
    }
}

GamepadStateSnapshot GamepadInputSystem::GetSnapshot(const std::size_t gamepadIndex) const {
    if (!IsValidGamepadIndex(gamepadIndex)) {
        return {};
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_currentState[gamepadIndex];
}

GamepadStateSnapshot GamepadInputSystem::GetPreviousSnapshot(const std::size_t gamepadIndex) const {
    if (!IsValidGamepadIndex(gamepadIndex)) {
        return {};
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_previousState[gamepadIndex];
}

float GamepadInputSystem::GetAxisValue(const std::size_t gamepadIndex, const GamepadAxis axis) const {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidAxis(axis)) {
        return 0.0F;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_currentState[gamepadIndex].axes[static_cast<std::size_t>(axis)];
}

bool GamepadInputSystem::IsButtonDown(const std::size_t gamepadIndex, const GamepadButton button) const {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidButton(button)) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    return m_currentState[gamepadIndex].buttons[static_cast<std::size_t>(button)];
}

bool GamepadInputSystem::WasButtonPressedThisFrame(const std::size_t gamepadIndex,
                                                          const GamepadButton button) const {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidButton(button)) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    const std::size_t buttonIndex = static_cast<std::size_t>(button);
    return m_currentState[gamepadIndex].buttons[buttonIndex] && !m_previousState[gamepadIndex].buttons[buttonIndex];
}

bool GamepadInputSystem::WasButtonReleasedThisFrame(const std::size_t gamepadIndex,
                                                           const GamepadButton button) const {
    if (!IsValidGamepadIndex(gamepadIndex) || !IsValidButton(button)) {
        return false;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    const std::size_t buttonIndex = static_cast<std::size_t>(button);
    return !m_currentState[gamepadIndex].buttons[buttonIndex] && m_previousState[gamepadIndex].buttons[buttonIndex];
}

} // namespace UVE::Input
