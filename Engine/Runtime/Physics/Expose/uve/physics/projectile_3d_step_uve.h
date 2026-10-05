// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstdint>

#include "uve/component/entity_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/projectile_3d_uve.h"
#include "uve/scene/i_scene_graph_uve.h"

namespace UVE::Physics {

/// Why one fixed-step evaluation of a Projectile3D did what it did, or why it did nothing.
enum class Projectile3DStepCodeUVE : std::uint8_t {
    /// Flew the whole step; nothing was in the way.
    Stepped = 0,
    /// A contact reflected the motion (policy Bounce); still flying.
    Bounced,
    /// A contact ended the motion (policy Stop, or a bounce that left nothing to fly with).
    Stopped,
    /// The lifetime ran out this step; `active` cleared.
    Expired,
    /// `active` was already false: nothing was integrated, swept or counted down.
    Disabled,
    /// The step's duration was zero, negative or not finite.
    InvalidDeltaTime,
    /// No such entity.
    UnknownEntity,
    /// The entity is not a projectile: no Projectile3DComponentUVE.
    NotAProjectile,
    /// No TransformComponentUVE (where the motion is written) or no
    /// WorldTransformComponentUVE (the space the sweep runs in).
    MissingTransform,
    /// The component is past its own contract (an unknown policy, a coefficient outside 0..1, a
    /// non-finite field, a claimed hit that names nothing). Refused rather than clamped: a
    /// malformed projectile does not get to fly somewhere nobody could have meant.
    InvalidComponent,
};

/// One step's outcome, with the numbers that produced it.
///
/// Every field is defined for every code: a refusal reports zeroes and false, never a stale value.
/// `code` names the most consequential thing that happened, in the order Stopped, Expired,
/// Bounced, Stepped - while the flags are independent, so a step that bounced and then expired in
/// the same step reports `Expired` with both `bounced` and `expired` true.
struct Projectile3DStepResultUVE final {
    Projectile3DStepCodeUVE code = Projectile3DStepCodeUVE::UnknownEntity;
    bool hasHit = false;
    /// A contact ended the motion this step.
    bool stoppedOnHit = false;
    /// A contact reflected the motion this step.
    bool bounced = false;
    /// The lifetime reached zero this step.
    bool expired = false;
    Scene::EntityUVE hitEntity = Scene::kInvalidEntityUVE;
    /// World-space point on the obstacle's surface where the sphere first touched.
    Math::Vector3UVE hitPosition{};
    /// World-space surface normal at that point.
    Math::Vector3UVE hitNormal{};
    /// The motion this step ends with, in the projectile's own axes.
    Math::Vector3UVE velocity{};
    /// The speed into the surface at the moment of contact, in metres per second.
    float impactSpeed = 0.0F;
    /// What the projectile's policy was, and therefore what this step executed.
    Scene::Projectile3DHitPolicyUVE appliedPolicy = Scene::Projectile3DHitPolicyUVE::Stop;
    /// World metres the projectile actually advanced this step - less than `|velocity| * dt` when
    /// a contact stopped it short. The skin a bounce takes off the surface is not travel.
    float movedDistance = 0.0F;
    /// Bounces this projectile has taken, this one included.
    std::uint32_t bounceCount = 0U;
    /// `remainingLifetime` after this step.
    float remainingLifetime = 0.0F;

    [[nodiscard]] bool IsSteppedUVE() const noexcept { return code == Projectile3DStepCodeUVE::Stepped; }
    /// A bounce happened this step - independent of `code`, which expiry can overshadow.
    [[nodiscard]] bool BouncedUVE() const noexcept { return bounced; }
    /// A contact ended the motion this step.
    [[nodiscard]] bool StoppedUVE() const noexcept { return stoppedOnHit; }
    /// The lifetime ran out this step, whatever else also happened.
    [[nodiscard]] bool ExpiredUVE() const noexcept { return expired; }
    [[nodiscard]] bool IsDisabledUVE() const noexcept { return code == Projectile3DStepCodeUVE::Disabled; }
    /// True for every code that means the step was refused rather than performed: the component
    /// and its transform were not touched.
    [[nodiscard]] bool IsRefusedUVE() const noexcept {
        switch (code) {
            case Projectile3DStepCodeUVE::InvalidDeltaTime:
            case Projectile3DStepCodeUVE::UnknownEntity:
            case Projectile3DStepCodeUVE::NotAProjectile:
            case Projectile3DStepCodeUVE::MissingTransform:
            case Projectile3DStepCodeUVE::InvalidComponent:
                return true;
            case Projectile3DStepCodeUVE::Stepped:
            case Projectile3DStepCodeUVE::Bounced:
            case Projectile3DStepCodeUVE::Stopped:
            case Projectile3DStepCodeUVE::Expired:
            case Projectile3DStepCodeUVE::Disabled:
                return false;
        }
        return true;
    }
};

/// Queued once per resolved contact. The engine owns the *motion* decision and so reports it here;
/// the consequences (damage, effects, despawn) are gameplay's, and it decides them from this
/// evidence. Delivery is owned by IEventSystemUVE.
struct Projectile3DHitEventUVE final {
    Scene::EntityUVE projectile = Scene::kInvalidEntityUVE;
    Scene::EntityUVE hitEntity = Scene::kInvalidEntityUVE;
    Math::Vector3UVE position{};
    Math::Vector3UVE normal{};
    float impactSpeed = 0.0F;
    Scene::Projectile3DHitPolicyUVE policy = Scene::Projectile3DHitPolicyUVE::Stop;
    /// What the policy did this step: the projectile is stopped (and inactive) when true, still
    /// flying when false.
    bool stopped = false;
    std::uint32_t bounceCount = 0U;

    [[nodiscard]] bool operator==(const Projectile3DHitEventUVE&) const noexcept = default;
};

/// Runs one fixed step of `entity`'s projectile:
///
///   1. refuses a projectile it cannot drive (see the codes above) rather than half-moving one;
///   2. integrates `velocity += acceleration * dt` (the authored velocity is the state that
///      accumulates, exactly as the pre-sweep engine-core tick documented);
///   3. sweeps the sphere of `radius` along this step's motion - in *world* space, from the
///      world position along the world-normalized velocity, because that is the space colliders
///      live in. `collisionMask` selects the obstacle layers; the projectile's own entity and
///      `ignoreEntity` are skipped;
///   4. resolves the contact through `Scene::Projectile3DUVE`: Stop halts at the contact, Bounce
///      reflects and leaves the sphere one contact-skin off the surface so the next sweep starts
///      outside it. A bounce that leaves no motion at all is a stop;
///   5. counts `remainingLifetime` down and clears `active` when it reaches zero.
///
/// A sweep that starts already overlapping an obstacle reports distance zero and no normal. That
/// is not a contact: there is no direction to resolve and no surface to have touched, so the
/// projectile is allowed to leave rather than die on the spot or bounce off a normal that does
/// not exist. This is what lets a projectile be fired from inside a launcher's own volume.
///
/// The committed motion is the projectile's own local: `velocity` is authored in the object's
/// local axes (like Kinematic3D's target velocity), the sweep runs in world space, and the
/// contact normal is rotated back into local space for the reflection. Rotation is handled
/// exactly; parent scale is ignored, which is this physics module's standing contract for every
/// authored extent (see Detail::BuildColliderWorldAabbCacheUVE).
///
/// The world pose read here is the one the scene graph last resolved; the engine updates it every
/// frame after its fixed steps, and a caller driving this by hand should do the same.
[[nodiscard]] Projectile3DStepResultUVE StepProjectile3DUVE(Scene::IEntityManagerUVE& entityManager,
                                                             Scene::ISceneGraphUVE& sceneGraph,
                                                             Scene::EntityUVE entity,
                                                             float deltaTimeSeconds);

} // namespace UVE::Physics
