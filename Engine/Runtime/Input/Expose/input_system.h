// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <array>
#include <cstddef>
#include <mutex>
#include <string>
#include <unordered_map>

#include "i_event_system.h"
#include "i_gamepad_input_system.h"
#include "i_input_system.h"

namespace UVE::Input {

inline constexpr std::size_t kMaximumInputActionNameBytes = 128U;
inline constexpr std::size_t kMaximumInputActions = 256U;

/// InputSystem is the concrete, engine-standard implementation of IInputSystem. Composes an
/// Events::IEventSystem& (dependency injection, matching Renderer3D/PhysicsSystem's
/// precedent for a service that needs a constructor-injected collaborator) to queue
/// InputActionTriggeredEvent.
///
/// Uses a triple-buffered state model per device field: a "live" value Set*State() writes to
/// immediately, and a "current"/"previous" pair that only Update() ever touches. Update()
/// shifts current->previous *before* snapshotting live->current, so current/previous always
/// differ by exactly one full frame throughout the frame that follows — the frame-stability
/// WasKeyPressedThisFrame()-style edge queries need (comparing live directly against a
/// same-call snapshot would make "pressed this frame" false immediately after the very Update()
/// call that should report it true).
///
/// Thread-safety: an internal mutex guards only the "live" fields Set*State() writes and
/// Update() reads from — matching EventSystem's own mutex-guarded-state precedent, and
/// backing the documented "Set*State() safe from any thread" contract. The "current"/
/// "previous" frame-snapshot fields (and the action registry) are written and read exclusively by
/// Update()/the query methods/RegisterAction()/UnregisterAction() — all main-thread-only
/// by contract — so they need no synchronization of their own.
class InputSystem final : public IInputSystem {
public:
    /// `eventSystem` must outlive this InputSystem.
    explicit InputSystem(Events::IEventSystem& eventSystem,
                            IGamepadInputSystem* gamepadInputSystem = nullptr);

    void SetKeyState(KeyCode key, bool isDown) override;
    void SetMouseButtonState(MouseButton button, bool isDown) override;
    void SetMousePosition(Math::Vector2 position) override;
    void SetMouseScrollDelta(float delta) override;
    void Update() override;

    [[nodiscard]] bool IsKeyDown(KeyCode key) const override;
    [[nodiscard]] bool WasKeyPressedThisFrame(KeyCode key) const override;
    [[nodiscard]] bool WasKeyReleasedThisFrame(KeyCode key) const override;
    [[nodiscard]] bool IsMouseButtonDown(MouseButton button) const override;
    [[nodiscard]] bool WasMouseButtonPressedThisFrame(MouseButton button) const override;
    [[nodiscard]] bool WasMouseButtonReleasedThisFrame(MouseButton button) const override;
    [[nodiscard]] Math::Vector2 GetMousePosition() const override;
    [[nodiscard]] Math::Vector2 GetMouseDelta() const override;
    [[nodiscard]] float GetMouseScrollDelta() const override;

    /// Rejects malformed or over-cap binding vectors before registration or replacement; empty
    /// vectors remain valid for actions that intentionally start unbound.
    void RegisterAction(InputAction&& action) override;
    bool UnregisterAction(std::string_view actionName) override;
    bool RemapAction(std::string_view actionName, std::vector<InputBinding> positiveBindings,
                        std::vector<InputBinding> negativeBindings) override;
    [[nodiscard]] bool IsActionTriggered(std::string_view actionName) const override;
    [[nodiscard]] bool IsActionHeld(std::string_view actionName) const override;
    [[nodiscard]] bool IsActionReleased(std::string_view actionName) const override;
    [[nodiscard]] float GetAxisValue(std::string_view actionName) const override;
    [[nodiscard]] bool HasAction(std::string_view actionName) const override;

private:
    static constexpr std::size_t kKeyCodeCount = static_cast<std::size_t>(KeyCode::Count);
    static constexpr std::size_t kMouseButtonCount = static_cast<std::size_t>(MouseButton::Count);
    using GamepadSnapshotArray = std::array<GamepadStateSnapshot, kMaximumGamepadCount>;
    static constexpr std::size_t kMaximumBindingsPerSide = 32U;

    [[nodiscard]] static bool AreBindingsValid(
        const std::vector<InputBinding>& bindings) noexcept;

    /// True iff any binding in `bindings` is down in the given state arrays.
    [[nodiscard]] static bool AnyBindingDown(const std::vector<InputBinding>& bindings,
                                                 const std::array<bool, kKeyCodeCount>& keyState,
                                                 const std::array<bool, kMouseButtonCount>& mouseState,
                                                 const GamepadSnapshotArray& currentGamepadState,
                                                 const GamepadSnapshotArray& previousGamepadState,
                                                 bool usePreviousGamepadSnapshot) noexcept;
    [[nodiscard]] static float GetBindingValue(const InputBinding& binding,
                                                   const std::array<bool, kKeyCodeCount>& keyState,
                                                   const std::array<bool, kMouseButtonCount>& mouseState,
                                                   const GamepadSnapshotArray& currentGamepadState,
                                                   const GamepadSnapshotArray& previousGamepadState,
                                                   bool usePreviousGamepadSnapshot) noexcept;

    Events::IEventSystem* m_eventSystem;
    IGamepadInputSystem* m_gamepadInputSystem;

    // "Live" state — written by Set*State() (any thread), read only by Update() under
    // m_liveStateMutex.
    mutable std::mutex m_liveStateMutex;
    std::array<bool, kKeyCodeCount> m_liveKeyState{};
    std::array<bool, kMouseButtonCount> m_liveMouseButtonState{};
    Math::Vector2 m_liveMousePosition{};
    float m_scrollDeltaAccumulator = 0.0F;

    // Frame-snapshot state — written only by Update(), read only by the query methods; both
    // main-thread-only by contract, so no synchronization is needed for these fields.
    std::array<bool, kKeyCodeCount> m_currentKeyState{};
    std::array<bool, kKeyCodeCount> m_previousKeyState{};
    std::array<bool, kMouseButtonCount> m_currentMouseButtonState{};
    std::array<bool, kMouseButtonCount> m_previousMouseButtonState{};
    GamepadSnapshotArray m_currentGamepadState{};
    GamepadSnapshotArray m_previousGamepadState{};
    Math::Vector2 m_currentMousePosition{};
    Math::Vector2 m_previousMousePosition{};
    Math::Vector2 m_mouseDelta{};
    float m_mouseScrollDelta = 0.0F;

    std::unordered_map<std::string, InputAction> m_actions;
};

} // namespace UVE::Input
