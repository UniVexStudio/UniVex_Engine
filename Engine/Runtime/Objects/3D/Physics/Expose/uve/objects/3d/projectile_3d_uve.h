// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"

namespace UVE::Scene {

/// What a projectile's own motion does when its swept sphere meets a collider.
///
/// This enum is the engine's half of the hit decision, and it is deliberately the *whole* of the
/// engine's half: damage, score, impact effects and despawning are gameplay, and they stay with
/// gameplay code that reads this component's result fields or the typed hit event the engine core
/// queues (`Physics::Projectile3DHitEventUVE`). What no game should have to write for itself is
/// the motion - a projectile that flies through a wall is an engine bug, not a design choice - so
/// the motion consequence is authored here and executed by `Physics::StepProjectile3DUVE()`.
enum class Projectile3DHitPolicyUVE : std::uint8_t {
    /// Halt at the contact point, clear the velocity and clear `active`. The default, because
    /// "it flew through the wall" is never the right answer to what a hit did.
    Stop = 0,
    /// Reflect the motion about the contact normal - `restitution` of the speed into the surface
    /// comes back out of it, `friction` of the speed along it is lost - and keep flying. A
    /// projectile that bounces is still bounded by `maxLifetime`, the one clock that ends it
    /// either way.
    Bounce,
};

/// True for a policy this engine knows how to execute. A value outside the enum is a malformed
/// component (the Inspector and the scene loader both refuse it), not a policy to guess at.
[[nodiscard]] bool IsKnownProjectile3DHitPolicyUVE(Projectile3DHitPolicyUVE policy) noexcept;

struct Projectile3DComponentUVE final {
    /// Where the projectile is going, in metres per second, in its own local axes - the convention
    /// Kinematic3D's `targetVelocity` and RayCast3D's `direction` also use.
    Math::Vector3UVE velocity{};
    /// Accumulated onto `velocity` every fixed step: gravity, drag, a wind volume.
    Math::Vector3UVE acceleration{};
    /// The sphere the projectile sweeps, in world metres. This is the size that decides whether it
    /// fits through a gap and how far its centre is held off a surface; a ray is infinitely thin
    /// and misses exactly the hits a fat projectile must not miss. Kept in world metres because
    /// that is what every other authored extent in this physics module means, parent scale
    /// included (the collider world cache reads half-extents the same way).
    float radius = 0.1F;
    /// Seconds of flight. An expired projectile clears `active` and stops being stepped.
    float maxLifetime = 10.0F;
    /// Runtime-only countdown to the same instant - `maxLifetime` is the authored number, this is
    /// the truth. Never serialized: the scene loader re-arms it from `maxLifetime`.
    float remainingLifetime = 10.0F;
    /// Which collision layers this projectile is allowed to hit, checked against the *obstacle's*
    /// layer, the same contract every other physics object uses.
    std::uint32_t collisionMask = 0xFFFFFFFFU;
    /// Off stops everything: no integration, no sweep, no countdown. Also cleared by a Stop hit and
    /// by expiry, which is why the Inspector shows it as authored state the engine writes.
    bool active = true;
    /// What this projectile's motion does on contact (see the enum).
    Projectile3DHitPolicyUVE hitPolicy = Projectile3DHitPolicyUVE::Stop;
    /// How much of the speed into a surface comes back out of it, 0..1 (1 = it leaves as fast as
    /// it arrived, 0 = it arrives and slides along). The projectile's own coefficient, not the
    /// surface's: this engine's colliders default to a restitution of 0, so combining the two
    /// would silently delete every default bounce. The surface's material is reported with the
    /// contact, and gameplay is free to react to it.
    float restitution = 0.5F;
    /// How much of the speed *along* a surface is lost on each bounce, 0..1 (0 = frictionless,
    /// 1 = it keeps none of its sideways motion). Only a bounce uses it.
    float friction = 0.2F;

    // ---------------------------------------------------------------------------------------------
    // Runtime-only result state, written by Physics::StepProjectile3DUVE() - never serialized (the
    // same authored/runtime split RayCast3D, Kinematic3D and CharacterController use).
    //
    // Unlike RayCast3D's per-frame result these are *sticky*. A ray answers "what is in front of
    // me right now", so a miss clears it; a projectile's last hit is a terminal fact about that
    // projectile, and gameplay reads it after the fact - where did it land, what did it hit, how
    // hard. These fields change only when a contact is resolved, and they survive the stop, the
    // expiry and every later step. `bounceCount` accumulates for the projectile's life.
    // ---------------------------------------------------------------------------------------------
    bool hit = false;
    EntityUVE hitEntity = kInvalidEntityUVE;
    /// World-space point on the obstacle's surface where the projectile's sphere first touched.
    Math::Vector3UVE hitPosition{};
    /// World-space surface normal at that point.
    Math::Vector3UVE hitNormal{};
    /// The speed *into* the surface at the moment of contact, in metres per second - the number a
    /// damage formula wants, not the speed the projectile happened to be travelling at.
    float impactSpeed = 0.0F;
    std::uint32_t bounceCount = 0U;
};

/// Reflects `velocity` off a surface: the component along the surface is kept and scaled by
/// `1 - friction`, the component into the surface is reversed and scaled by `restitution`.
/// Coefficients are clamped into 0..1, so no authored value can add energy. Returns `velocity`
/// unchanged when there is no usable direction to reflect about (a zero or non-finite normal) or
/// when an input is not finite - a malformed value never produces a NaN motion.
[[nodiscard]] Math::Vector3UVE ResolveProjectile3DBounceVelocityUVE(const Math::Vector3UVE& velocity,
                                                                    const Math::Vector3UVE& normal,
                                                                    float restitution,
                                                                    float friction) noexcept;

/// Authored-data rule: finite velocity/acceleration, a positive radius, a positive `maxLifetime`
/// with `remainingLifetime` inside it, a policy this engine executes and coefficients inside
/// 0..1. A claimed runtime hit must also name an entity and be finite, so a consumer can never
/// read a hit that points at nothing.
[[nodiscard]] bool IsProjectile3DObjectComponentValidUVE(const Projectile3DComponentUVE& value) noexcept;

} // namespace UVE::Scene
