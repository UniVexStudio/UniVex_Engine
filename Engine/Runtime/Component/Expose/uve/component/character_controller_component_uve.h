// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"

namespace UVE::Scene {

/// How a Character3D reads the world around it.
enum class CharacterMotionModeUVE : std::uint8_t {
    /// Walks: gravity pulls it down, it stands on floors, climbs steps and slides off walls.
    Grounded = 0,
    /// Flies or swims: no gravity, no floor, and every surface is a wall it slides along.
    Floating,
};

/// What a moving platform does to a body standing on it when it leaves the body behind. The three
/// names are the three answers a platformer ever wants: nothing (the body keeps its own velocity),
/// the platform's whole velocity, or only the part of it that pushes the body up.
enum class CharacterPlatformLeaveModeUVE : std::uint8_t {
    /// The body keeps only the velocity it had of its own - a lift that drops away leaves the
    /// rider hanging in the air with no inherited side motion.
    KeepVelocity = 0,
    /// The body keeps the platform's velocity as it leaves, so a jump off a moving train carries
    /// the train's motion.
    AddVelocity,
    /// Only the platform's upward motion is added, never its sideways motion - a spring platform
    /// throws the body straight up rather than sideways. A platform moving down hands over nothing,
    /// so a falling lift never drags a jump down with it.
    AddUpwardVelocity,
};

/// Character3D's own state: a body moved by its code (or by the built-in movement below)
/// rather than by forces, driven every fixed step by `EngineCoreUVE::SyncCharacterControllersUVE()`.
/// Its shape is the entity's collider.
///
/// Authored settings come first; the runtime state the controller writes back each step comes
/// last and is shown in the Inspector only while playing.
struct CharacterControllerComponentUVE final {
    CharacterMotionModeUVE motionMode = CharacterMotionModeUVE::Grounded;
    /// The body's own up. Floors, jumps, gravity and ceilings are measured from this. Defaults to
    /// world +Y because the engine's gravity is along -Y; a body that walks on a wall or a planet
    /// writes a different up here and the mover, the jump and the floor probe all follow it.
    Math::Vector3UVE upDirection{0.0F, 1.0F, 0.0F};
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
    // Collision is exact for boxes, spheres and capsules today, so the surface the body stands on
    // is classified by its own normal: everything below is measured from that normal against the
    // body's up direction.
    /// Steepest surface still walked on, in degrees from up. Steeper surfaces are walls.
    float floorMaxAngleDegrees = 45.0F;
    /// How far past the floor limit a surface may lean and still be slid along. Between the two the
    /// body stops on the ramp instead of creeping up it. 0 slides on every wall.
    float wallMinSlideAngleDegrees = 15.0F;
    /// The skin the body keeps against surfaces, in metres. Motion stops this far short of them and
    /// a body pushed out of geometry is pushed out this far past it, which is what keeps a resting
    /// body's next collision query well-posed instead of exactly touching.
    float safeMargin = 0.001F;
    /// Standing still on a slope stays standing still instead of creeping downhill.
    bool floorStopOnSlope = true;
    /// Walking up or down a slope keeps the horizontal speed the walk started with.
    bool floorConstantSpeed = false;
    /// Keeps the body on the floor walking down steps and ledges up to this far below it, instead
    /// of dropping off them. 0 lets it leave the floor at every edge.
    float floorSnapLength = 0.1F;
    /// Walks up steps and kerbs up to this height without jumping. 0 turns it off.
    float maxStepHeight = 0.3F;
    /// How much room a step needs on top to be worth climbing, in metres: a ledge narrower than
    /// this is a wall with a decoration, not somewhere to stand.
    float minStepWidth = 0.02F;

    // ---- Ceiling -----------------------------------------------------------------------------------
    /// On hitting a ceiling, keep sliding along it. Off, the whole move stops there.
    bool slideOnCeiling = true;
    /// On the floor, stop at a wall instead of sliding along it - no wall-running, no corner
    /// slipping. Off, a body walking into a wall keeps its sideways motion.
    bool floorBlockOnWall = false;

    // ---- Platforms ---------------------------------------------------------------------------------
    /// What the platform does to this body's velocity when the body leaves it behind.
    CharacterPlatformLeaveModeUVE platformOnLeave = CharacterPlatformLeaveModeUVE::AddVelocity;
    /// The fastest a platform's motion may carry the body, in metres per second. A platform that
    /// teleported - a script writing a transform - would otherwise fling its rider across the
    /// level; past this, the step treats it as standing still.
    float maximumPlatformSpeed = 50.0F;

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
    /// The most contacts one move reports, biggest first. Past this the extras are dropped, which
    /// `CharacterMotionResultUVE::contactsTruncated` says out loud.
    std::uint32_t maximumContacts = 64U;

    // ---- Runtime state, written by the controller every step ---------------------------------------
    /// Metres per second. With built-in movement off, set this and the body goes there, sliding
    /// along walls. With it on and Floating, Space rises and Left Ctrl sinks.
    Math::Vector3UVE velocity{};
    bool grounded = false;
    bool isOnCeiling = false;
    /// Touching a wall after the last step.
    bool onWall = false;
    /// The floor's normal while on it; straight up otherwise.
    Math::Vector3UVE floorNormal{0.0F, 1.0F, 0.0F};
    /// The last wall's normal, zero when no wall was met.
    Math::Vector3UVE wallNormal{};
    /// The last ceiling's normal, zero when no ceiling was met.
    Math::Vector3UVE ceilingNormal{};
    /// Seconds since last on the floor (drives coyote time).
    float timeSinceOnFloor = 0.0F;
    /// Seconds a buffered jump has left to happen.
    float jumpBufferRemaining = 0.0F;
    /// What the last step actually moved the body by, after slides, steps and snapping.
    Math::Vector3UVE lastMotion{};
    /// lastMotion divided by that step's duration: the body's real speed, which is not `velocity`
    /// whenever a wall or a ceiling stopped part of the motion.
    Math::Vector3UVE realVelocity{};
    /// The platform the body is standing on, or kInvalidEntityUVE. Runtime scaffolding: it is
    /// re-established by the first step that stands on something, and a stale one is never trusted
    /// without a fresh query against the live entity.
    EntityUVE platform{};
    /// That platform's world position at the end of the last step - the reference the next step
    /// measures its motion against, because what matters about a moving platform is how far it
    /// moved, not a velocity number it happens to publish.
    Math::Vector3UVE platformWorldPosition{};
    /// The platform velocity measured over the last step. Zero while not riding one.
    Math::Vector3UVE platformVelocity{};
    /// Whether `platformWorldPosition` is a position a previous step actually observed. A platform
    /// sitting at the world origin is a real platform, so "no reference yet" cannot be spelled as
    /// a zero vector - it needs its own bit.
    bool hasPlatformWorldPosition = false;
};

/// Every setting finite and in its range, and every state value finite.
[[nodiscard]] bool IsCharacterControllerComponentValidUVE(
    const CharacterControllerComponentUVE& characterController) noexcept;

} // namespace UVE::Scene
