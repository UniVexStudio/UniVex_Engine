// Copyright (c) 2026 UniVex Studios. All Rights Reserved.


#pragma once

#include <string_view>

#include "input_action.h"
#include "key_code.h"
#include "mouse_button.h"
#include "vector2.h"

namespace UVE::Input {

/// IInputSystem is the spec's `InputSystem` (Part 7.7): abstract keyboard/mouse input plus
/// a named action-mapping layer (`InputAction`/`InputBinding`). The legacy action layer remains
/// keyboard/mouse-only for compatibility; bounded gamepad device snapshots are provided separately
/// by `GamepadInputSystem`, while backend-neutral touch/gyroscope snapshots are provided by
/// `MobileInputSystem`. Android/iOS polling, lifecycle, coordinate-system mapping, and gesture
/// interpretation remain outside this desktop-safe interface.
/// Unlike ICollisionSystem/IRaycastSystem, an implementation is expected to be stateful
/// (current/previous device state for edge detection) and driven once per frame via Update() —
/// closer in shape to IEventSystem's own "self-contained stateful service" than to
/// RaycastSystem's pure-stateless-query shape. There is deliberately no second "Null"-style
/// implementation: no real backend (WindowManager) exists yet to abstract away, so the single
/// concrete InputSystem — which never touches real hardware, only consumes explicitly pushed
/// state — already is the sandbox-appropriate implementation. A future real backend calls the
/// same Set*State() methods below directly, rather than implementing a second variant of this
/// interface.
/// Thread-safety: the injection methods (Set*State()) are safe to call from any thread,
/// matching IEventSystem::QueueEvent()'s contract — a future real backend would call them from
/// a platform/window thread. Update() and every query method are main-thread-only, matching
/// IEventSystem::DispatchQueued()'s contract.
class IInputSystem {
public:
    virtual ~IInputSystem() = default;

    // --- Injection: how device state gets in. ---
    // Reflects "current state," not a queued event log: a key press-and-release faster than one
    // frame apart can be missed — a documented, standard polling-input simplification, not an
    // oversight (see docs/CODING_STANDARDS.md).
    /// `KeyCode::Count` is a sentinel, not a physical key; passing it is inert and never indexes
    /// device state. The same fail-closed rule applies to `MouseButton::Count` below and to raw
    /// key/mouse edge queries.
    virtual void SetKeyState(KeyCode key, bool isDown) = 0;
    virtual void SetMouseButtonState(MouseButton button, bool isDown) = 0;
    virtual void SetMousePosition(Math::Vector2 position) = 0;
    /// Accumulates finite deltas until the next Update() call drains it — a real scroll wheel
    /// can report several small deltas between frames. Non-finite deltas and additions that would
    /// make the accumulator non-finite are ignored, preserving the last valid accumulated value.
    virtual void SetMouseScrollDelta(float delta) = 0;

    /// Advances current->previous state for edge detection, computes mouse delta, drains the
    /// scroll accumulator, and queues an InputActionTriggeredEvent for every Button action
    /// that newly transitioned to triggered plus every Axis1D action that crossed the documented
    /// absolute threshold this frame. Must be called exactly once per frame,
    /// from the main thread, before anything reads this frame's input state.
    virtual void Update() = 0;

    // --- Raw device-state queries. ---
    [[nodiscard]] virtual bool IsKeyDown(KeyCode key) const = 0;
    [[nodiscard]] virtual bool WasKeyPressedThisFrame(KeyCode key) const = 0;
    [[nodiscard]] virtual bool WasKeyReleasedThisFrame(KeyCode key) const = 0;
    [[nodiscard]] virtual bool IsMouseButtonDown(MouseButton button) const = 0;
    [[nodiscard]] virtual bool WasMouseButtonPressedThisFrame(MouseButton button) const = 0;
    [[nodiscard]] virtual bool WasMouseButtonReleasedThisFrame(MouseButton button) const = 0;
    [[nodiscard]] virtual Math::Vector2 GetMousePosition() const = 0;
    [[nodiscard]] virtual Math::Vector2 GetMouseDelta() const = 0;
    [[nodiscard]] virtual float GetMouseScrollDelta() const = 0;

    // --- Action layer. ---
    /// Registers (or replaces, by name) an action. Takes an explicit-move sink parameter — the
    /// ownership transfer is visible at the call site
    /// (`inputSystem.RegisterAction(std::move(action))`, or a temporary) — and the
    /// implementation moves it straight into its internal registry.
    /// Invalid names, types, sentinel/malformed bindings, or over-cap binding vectors are ignored
    /// without publishing a new action or replacing an existing one; empty vectors remain valid.
    virtual void RegisterAction(InputAction&& action) = 0;
    /// Returns true if an action with this name existed and was removed, false otherwise — no
    /// assertion for an unknown name. Needed for future editor workflows, hot reload, and
    /// runtime action rebuilding without forcing the registry to only ever grow.
    virtual bool UnregisterAction(std::string_view actionName) = 0;
    /// Atomically replaces the positive/negative binding vectors for an existing action. The
    /// vectors are copied/moved into the action registry only after every binding is validated;
    /// a missing action or invalid candidate leaves the existing mapping and frame snapshots unchanged.
    /// Remapping takes effect for subsequent queries without resetting current/previous device state
    /// or queueing a synthetic trigger event.
    virtual bool RemapAction(std::string_view actionName, std::vector<InputBinding> positiveBindings,
                                std::vector<InputBinding> negativeBindings) = 0;
    // An unregistered/typo'd action name returns false/0.0F rather than asserting — matches
    // IConfigManager's "keyed lookup that may legitimately miss" precedent, not a programmer
    // precondition violation.
    [[nodiscard]] virtual bool IsActionTriggered(std::string_view actionName) const = 0;
    [[nodiscard]] virtual bool IsActionHeld(std::string_view actionName) const = 0;
    [[nodiscard]] virtual bool IsActionReleased(std::string_view actionName) const = 0;
    [[nodiscard]] virtual float GetAxisValue(std::string_view actionName) const = 0;
    [[nodiscard]] virtual bool HasAction(std::string_view actionName) const = 0;
};

} // namespace UVE::Input
