// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#include "input_system.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

#include "input_action_triggered_event.h"

namespace UVE::Input {

InputSystem::InputSystem(Events::IEventSystem& eventSystem,
                               IGamepadInputSystem* gamepadInputSystem)
    : m_eventSystem(&eventSystem), m_gamepadInputSystem(gamepadInputSystem) {}

float InputSystem::GetBindingValue(const InputBinding& binding,
                                         const std::array<bool, kKeyCodeCount>& keyState,
                                         const std::array<bool, kMouseButtonCount>& mouseState,
                                         const GamepadSnapshotArray& currentGamepadState,
                                         const GamepadSnapshotArray& previousGamepadState,
                                         const bool usePreviousGamepadSnapshot) noexcept {
    switch (binding.source) {
    case InputBindingSource::Keyboard: {
        const std::size_t keyIndex = static_cast<std::size_t>(binding.key);
        return keyIndex < kKeyCodeCount && keyState[keyIndex] ? 1.0F : 0.0F;
    }
    case InputBindingSource::Mouse: {
        const std::size_t buttonIndex = static_cast<std::size_t>(binding.mouseButton);
        return buttonIndex < kMouseButtonCount && mouseState[buttonIndex] ? 1.0F : 0.0F;
    }
    case InputBindingSource::GamepadButton: {
        if (binding.gamepadIndex >= kMaximumGamepadCount) {
            return 0.0F;
        }
        const std::size_t buttonIndex = static_cast<std::size_t>(binding.gamepadButton);
        if (buttonIndex >= kGamepadButtonCount) {
            return 0.0F;
        }
        const GamepadStateSnapshot& snapshot =
            usePreviousGamepadSnapshot ? previousGamepadState[binding.gamepadIndex]
                                        : currentGamepadState[binding.gamepadIndex];
        return snapshot.buttons[buttonIndex] ? 1.0F : 0.0F;
    }
    case InputBindingSource::GamepadAxis: {
        if (binding.gamepadIndex >= kMaximumGamepadCount) {
            return 0.0F;
        }
        const std::size_t axisIndex = static_cast<std::size_t>(binding.gamepadAxis);
        if (axisIndex >= kGamepadAxisCount) {
            return 0.0F;
        }
        const GamepadStateSnapshot& snapshot =
            usePreviousGamepadSnapshot ? previousGamepadState[binding.gamepadIndex]
                                        : currentGamepadState[binding.gamepadIndex];
        const float value = snapshot.axes[axisIndex] * binding.scale;
        return std::isfinite(value) ? std::clamp(value, -1.0F, 1.0F) : 0.0F;
    }
    }
    return 0.0F;
}

bool InputSystem::AnyBindingDown(const std::vector<InputBinding>& bindings,
                                        const std::array<bool, kKeyCodeCount>& keyState,
                                        const std::array<bool, kMouseButtonCount>& mouseState,
                                        const GamepadSnapshotArray& currentGamepadState,
                                        const GamepadSnapshotArray& previousGamepadState,
                                        const bool usePreviousGamepadSnapshot) noexcept {
    for (const InputBinding& binding : bindings) {
        if (std::fabs(GetBindingValue(binding, keyState, mouseState, currentGamepadState,
                                         previousGamepadState, usePreviousGamepadSnapshot)) > 0.5F) {
            return true;
        }
    }
    return false;
}

void InputSystem::SetKeyState(KeyCode key, bool isDown) {
    const std::size_t index = static_cast<std::size_t>(key);
    if (index >= kKeyCodeCount) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_liveKeyState[index] = isDown;
}

void InputSystem::SetMouseButtonState(MouseButton button, bool isDown) {
    const std::size_t index = static_cast<std::size_t>(button);
    if (index >= kMouseButtonCount) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_liveMouseButtonState[index] = isDown;
}

void InputSystem::SetMousePosition(Math::Vector2 position) {
    if (!std::isfinite(position.x) || !std::isfinite(position.y)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    m_liveMousePosition = position;
}

void InputSystem::SetMouseScrollDelta(const float delta) {
    if (!std::isfinite(delta)) {
        return;
    }
    const std::lock_guard<std::mutex> lock(m_liveStateMutex);
    const float candidate = m_scrollDeltaAccumulator + delta;
    if (std::isfinite(candidate)) {
        m_scrollDeltaAccumulator = candidate;
    }
}

void InputSystem::Update() {
    {
        const std::lock_guard<std::mutex> lock(m_liveStateMutex);
        // Shift the old "current" (last frame's committed snapshot) into "previous" *before*
        // overwriting "current" with the live state — this ordering is what keeps
        // current/previous exactly one frame apart for the rest of this call and every query
        // made until the next Update().
        m_previousKeyState = m_currentKeyState;
        m_currentKeyState = m_liveKeyState;
        m_previousMouseButtonState = m_currentMouseButtonState;
        m_currentMouseButtonState = m_liveMouseButtonState;
        m_previousMousePosition = m_currentMousePosition;
        m_currentMousePosition = m_liveMousePosition;
        m_previousGamepadState = m_currentGamepadState;
        if (m_gamepadInputSystem == nullptr) {
            m_currentGamepadState.fill({});
        } else {
            for (std::size_t gamepadIndex = 0U; gamepadIndex < kMaximumGamepadCount; ++gamepadIndex) {
                m_currentGamepadState[gamepadIndex] = m_gamepadInputSystem->GetSnapshot(gamepadIndex);
            }
        }
        m_mouseScrollDelta = m_scrollDeltaAccumulator;
        m_scrollDeltaAccumulator = 0.0F;
    }
    const Math::Vector2 candidateMouseDelta = m_currentMousePosition - m_previousMousePosition;
    m_mouseDelta = std::isfinite(candidateMouseDelta.x) && std::isfinite(candidateMouseDelta.y)
                       ? candidateMouseDelta
                       : Math::Vector2{};

    const auto GetAxisSnapshotValue = [&](const InputAction& action, const bool previous) {
        const auto SumBindingValues = [&](const std::vector<InputBinding>& bindings) {
            float sum = 0.0F;
            for (const InputBinding& binding : bindings) {
                sum += GetBindingValue(
                    binding, previous ? m_previousKeyState : m_currentKeyState,
                    previous ? m_previousMouseButtonState : m_currentMouseButtonState,
                    m_currentGamepadState, m_previousGamepadState, previous);
            }
            return sum;
        };
        return std::clamp(SumBindingValues(action.positiveBindings) -
                              SumBindingValues(action.negativeBindings),
                          -1.0F, 1.0F);
    };

    for (const auto& [name, action] : m_actions) {
        if (action.type == InputActionType::Button) {
            const bool isDownNow = AnyBindingDown(action.positiveBindings, m_currentKeyState,
                                                     m_currentMouseButtonState, m_currentGamepadState,
                                                     m_previousGamepadState, false);
            const bool wasDownBefore =
                AnyBindingDown(action.positiveBindings, m_previousKeyState, m_previousMouseButtonState,
                                  m_currentGamepadState, m_previousGamepadState, true);
            if (isDownNow && !wasDownBefore) {
                m_eventSystem->QueueEvent(InputActionTriggeredEvent{name, InputActionType::Button, 0.0F});
            }
        } else {
            const float currentAxisValue = GetAxisSnapshotValue(action, false);
            const float previousAxisValue = GetAxisSnapshotValue(action, true);
            if (std::fabs(currentAxisValue) > kInputActionAxisTriggerThreshold &&
                std::fabs(previousAxisValue) <= kInputActionAxisTriggerThreshold) {
                m_eventSystem->QueueEvent(
                    InputActionTriggeredEvent{name, InputActionType::Axis1D, currentAxisValue});
            }
        }
    }
}

bool InputSystem::IsKeyDown(KeyCode key) const {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < kKeyCodeCount && m_currentKeyState[index];
}

bool InputSystem::WasKeyPressedThisFrame(KeyCode key) const {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < kKeyCodeCount && m_currentKeyState[index] && !m_previousKeyState[index];
}

bool InputSystem::WasKeyReleasedThisFrame(KeyCode key) const {
    const std::size_t index = static_cast<std::size_t>(key);
    return index < kKeyCodeCount && !m_currentKeyState[index] && m_previousKeyState[index];
}

bool InputSystem::IsMouseButtonDown(MouseButton button) const {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < kMouseButtonCount && m_currentMouseButtonState[index];
}

bool InputSystem::WasMouseButtonPressedThisFrame(MouseButton button) const {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < kMouseButtonCount && m_currentMouseButtonState[index] && !m_previousMouseButtonState[index];
}

bool InputSystem::WasMouseButtonReleasedThisFrame(MouseButton button) const {
    const std::size_t index = static_cast<std::size_t>(button);
    return index < kMouseButtonCount && !m_currentMouseButtonState[index] && m_previousMouseButtonState[index];
}

Math::Vector2 InputSystem::GetMousePosition() const {
    return m_currentMousePosition;
}

Math::Vector2 InputSystem::GetMouseDelta() const {
    return m_mouseDelta;
}

float InputSystem::GetMouseScrollDelta() const {
    return m_mouseScrollDelta;
}

void InputSystem::RegisterAction(InputAction&& action) {
    if (action.name.empty() || action.name.size() > kMaximumInputActionNameBytes ||
        action.name.contains('\0') ||
        (action.type != InputActionType::Button && action.type != InputActionType::Axis1D) ||
        !AreBindingsValid(action.positiveBindings) || !AreBindingsValid(action.negativeBindings)) {
        return;
    }
    const auto existing = m_actions.find(action.name);
    if (existing == m_actions.end() && m_actions.size() >= kMaximumInputActions) {
        return;
    }
    if (existing == m_actions.end()) {
        m_actions.emplace(action.name, std::move(action));
    } else {
        existing->second = std::move(action);
    }
}

bool InputSystem::UnregisterAction(std::string_view actionName) {
    return m_actions.erase(std::string(actionName)) > 0;
}

bool InputSystem::AreBindingsValid(
    const std::vector<InputBinding>& bindings) noexcept {
    if (bindings.size() > kMaximumBindingsPerSide) {
        return false;
    }
    for (const InputBinding& binding : bindings) {
        switch (binding.source) {
        case InputBindingSource::Keyboard:
            if (static_cast<std::size_t>(binding.key) >= kKeyCodeCount) {
                return false;
            }
            break;
        case InputBindingSource::Mouse:
            if (static_cast<std::size_t>(binding.mouseButton) >= kMouseButtonCount) {
                return false;
            }
            break;
        case InputBindingSource::GamepadButton:
            if (binding.gamepadIndex >= kMaximumGamepadCount ||
                static_cast<std::size_t>(binding.gamepadButton) >= kGamepadButtonCount) {
                return false;
            }
            break;
        case InputBindingSource::GamepadAxis:
            if (binding.gamepadIndex >= kMaximumGamepadCount ||
                static_cast<std::size_t>(binding.gamepadAxis) >= kGamepadAxisCount ||
                !std::isfinite(binding.scale)) {
                return false;
            }
            break;
        default:
            return false;
        }
    }
    return true;
}

bool InputSystem::RemapAction(std::string_view actionName,
                                    std::vector<InputBinding> positiveBindings,
                                    std::vector<InputBinding> negativeBindings) {
    const auto it = m_actions.find(std::string(actionName));
    if (it == m_actions.end() || !AreBindingsValid(positiveBindings) ||
        !AreBindingsValid(negativeBindings)) {
        return false;
    }
    it->second.positiveBindings = std::move(positiveBindings);
    it->second.negativeBindings = std::move(negativeBindings);
    return true;
}

bool InputSystem::IsActionTriggered(std::string_view actionName) const {
    const auto it = m_actions.find(std::string(actionName));
    if (it == m_actions.end()) {
        return false;
    }
    const bool isDownNow = AnyBindingDown(it->second.positiveBindings, m_currentKeyState, m_currentMouseButtonState,
                           m_currentGamepadState, m_previousGamepadState, false);
    const bool wasDownBefore =
        AnyBindingDown(it->second.positiveBindings, m_previousKeyState, m_previousMouseButtonState,
                           m_currentGamepadState, m_previousGamepadState, true);
    return isDownNow && !wasDownBefore;
}

bool InputSystem::IsActionHeld(std::string_view actionName) const {
    const auto it = m_actions.find(std::string(actionName));
    if (it == m_actions.end()) {
        return false;
    }
    return AnyBindingDown(it->second.positiveBindings, m_currentKeyState, m_currentMouseButtonState,
                           m_currentGamepadState, m_previousGamepadState, false);
}

bool InputSystem::IsActionReleased(std::string_view actionName) const {
    const auto it = m_actions.find(std::string(actionName));
    if (it == m_actions.end()) {
        return false;
    }
    const bool isDownNow = AnyBindingDown(it->second.positiveBindings, m_currentKeyState, m_currentMouseButtonState,
                           m_currentGamepadState, m_previousGamepadState, false);
    const bool wasDownBefore =
        AnyBindingDown(it->second.positiveBindings, m_previousKeyState, m_previousMouseButtonState,
                           m_currentGamepadState, m_previousGamepadState, true);
    return !isDownNow && wasDownBefore;
}

bool InputSystem::HasAction(std::string_view actionName) const {
    return m_actions.find(std::string(actionName)) != m_actions.end();
}

float InputSystem::GetAxisValue(std::string_view actionName) const {
    const auto it = m_actions.find(std::string(actionName));
    if (it == m_actions.end()) {
        return 0.0F;
    }
    const auto SumBindingValues = [&](const std::vector<InputBinding>& bindings) {
        float sum = 0.0F;
        for (const InputBinding& binding : bindings) {
            sum += GetBindingValue(binding, m_currentKeyState, m_currentMouseButtonState,
                                      m_currentGamepadState, m_previousGamepadState, false);
        }
        return sum;
    };
    return std::clamp(SumBindingValues(it->second.positiveBindings) -
                          SumBindingValues(it->second.negativeBindings),
                      -1.0F, 1.0F);
}

} // namespace UVE::Input
