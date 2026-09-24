// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// How a CharacterBody3D reads the world around it.
enum class CharacterMotionModeUVE : std::uint8_t {
    /// Walks: gravity pulls it down, it stands on floors, climbs steps and slides off walls.
    Grounded = 0,
    /// Flies or swims: no gravity, no floor, and every surface is a wall it slides along.
    Floating,
};

/// CharacterBody3D's own state: a body moved by its code (or by the built-in movement below)
/// rather than by forces, driven every fixed step by `EngineCoreUVE::SyncCharacterControllersUVE()`
/// through `Physics::CharacterControllerUVE::MoveWithToIUVE`. Its shape is the entity's collider.
///
/// Authored settings come first; the runtime state the controller writes back each step comes
/// last and is shown in the Inspector only while playing.
struct CharacterControllerComponentUVE final {
    CharacterMotionModeUVE motionMode = CharacterMotionModeUVE::Grounded;
    /// Multiplies the engine's gravity: 1 = normal, 0 = none. Ignored while Floating.
    float gravityScale = 1.0F;

    // ---- Built-in movement: move and jump from the keyboard with no script at all ------------------
    /// Reads movement and jump input itself. Off, the body moves by `velocity` alone, which is how
    /// a script or an AI drives it.
    bool builtInMovement = true;
    /// Top speed on the ground, in metres per second.
    float moveSpeed = 5.0F;
    /// How high a jump reaches, in metres, whatever the gravity.
    float jumpHeight = 1.5F;
    /// How much of the player's steering applies in the air: 1 is full control, 0 none.
    float airControl = 1.0F;
    /// A jump still counts this long after walking off a ledge, so a late press is not lost.
    float coyoteTimeSeconds = 0.1F;
    /// A jump pressed this long before landing happens on landing, instead of being ignored.
    float jumpBufferSeconds = 0.1F;

    // ---- Floor -------------------------------------------------------------------------------------
    // Collision is box against box today, so every floor is level and every wall upright: a
    // floor-angle limit and slope sliding have nothing to act on yet, and are not offered until
    // sloped shapes exist.
    /// Keeps the body on the floor walking down steps and ledges up to this far below it, instead
    /// of dropping off them. 0 lets it leave the floor at every edge.
    float floorSnapLength = 0.1F;
    /// Walks up steps and kerbs up to this height without jumping. 0 turns it off.
    float maxStepHeight = 0.3F;

    // ---- Ceiling -----------------------------------------------------------------------------------
    /// On hitting a ceiling, keep sliding along it. Off, the whole move stops there.
    bool slideOnCeiling = true;

    // ---- Pushing -----------------------------------------------------------------------------------
    /// Walking into a rigid body pushes it.
    bool pushRigidBodies = false;
    /// How hard it pushes, relative to its own speed.
    float pushStrength = 1.0F;
    /// The fastest a push may send a body, in metres per second.
    float maxPushSpeed = 5.0F;

    // ---- Collision ---------------------------------------------------------------------------------
    /// How many pieces a move is cut into to follow walls and corners; more is smoother and dearer.
    std::uint32_t maxSlides = 8U;

    // ---- Runtime state, written by the controller every step ---------------------------------------
    /// Metres per second. With built-in movement off, set this and the body goes there, sliding
    /// along walls. With it on and Floating, Space rises and Left Ctrl sinks.
    Math::Vector3UVE velocity{};
    bool isOnFloor = false;
    bool isOnCeiling = false;
    /// The floor's normal while on it; straight up otherwise.
    Math::Vector3UVE floorNormal{0.0F, 1.0F, 0.0F};
    /// Seconds since last on the floor (drives coyote time).
    float timeSinceOnFloor = 0.0F;
    /// Seconds a buffered jump has left to happen.
    float jumpBufferRemaining = 0.0F;
};

/// Every setting finite and in its range, and every state value finite.
[[nodiscard]] bool IsCharacterControllerComponentValidUVE(
    const CharacterControllerComponentUVE& characterController) noexcept;

} // namespace UVE::Scene
