// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "world_transform_component.h"
#include "object_3d_common.h"
#include "skeleton_3d.h"

namespace UVE::Scene {

class IEntityManager;

/// The most bones one chain may drive. Hair, tails and coats are 4-16 bones deep; anything longer
/// costs a solve per bone per substep and is better authored as a second simulator.
inline constexpr std::size_t kMaximumSpringBoneChainLength = 16U;

/// The simulation advances in fixed steps, so the same chain traces the same path whether the host
/// renders at 30, 60 or 144 fps: a frame accumulates time and spends it in whole steps. A variable
/// step would make the damping a function of the frame rate, which is exactly the bug this avoids.
inline constexpr float kSpringBoneFixedStepSeconds = 1.0F / 120.0F;

/// How many fixed steps one update may spend. A long hitch is not replayed as a burst - chasing it
/// would pay an unbounded cost in a frame that was already late - so the accumulator is clamped
/// instead and the chain runs at the wall-clock rate from the next update on.
inline constexpr std::uint32_t kMaximumSpringBoneSubsteps = 8U;

/// The rate per second at which `damping` bleeds velocity. Exponential rather than a per-frame
/// multiply so the decay is a property of elapsed time, not of how often the frame arrives.
inline constexpr float kSpringBoneDampingRate = 20.0F;

/// Spherical obstacle volume in world space (`worldPosition + Rotate(worldRotation, centerOffset)`)
/// that pushes simulated spring-bone tips outside its surface.
struct SpringBoneCollision3DComponent final {
    Math::Vector3 centerOffset{0.0F, 0.0F, 0.0F};
    float radius = 0.25F;
    bool enabled = true;

    [[nodiscard]] bool operator==(const SpringBoneCollision3DComponent&) const = default;
};

[[nodiscard]] bool IsSpringBoneCollision3DComponentValid(
    const SpringBoneCollision3DComponent& value) noexcept;

[[nodiscard]] SpringBoneCollision3DComponent SanitizeSpringBoneCollision3DComponent(
    const SpringBoneCollision3DComponent& value) noexcept;

struct SpringBoneCollision3DObjectDefinition final {
    static constexpr std::string_view defaultName = "SpringBoneCollision3D";
    SpringBoneCollision3DComponent collision{};
};

[[nodiscard]] bool IsSpringBoneCollision3DObjectDefinitionValid(
    const SpringBoneCollision3DObjectDefinition& value) noexcept;

void ApplySpringBoneCollision3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const SpringBoneCollision3DObjectDefinition& value);

/// One chain segment's simulated tail: its world position and the velocity it travels at. Runtime
/// only - the simulation is rebuilt from the animated pose whenever a simulator starts, so nothing
/// here is worth persisting.
struct SpringBoneTipState final {
    Math::Vector3 position{};
    Math::Vector3 velocity{};

    [[nodiscard]] bool operator==(const SpringBoneTipState&) const = default;
};

/// Secondary motion ("jiggle", hair, tail, coat) for a CHAIN of bones on a target skeleton.
///
/// The chain starts at `boneName` and follows each bone's first child, for at most `chainLength`
/// segments - the shape a DCC tool exports for a ponytail or a coat tail. Each segment simulates
/// the world position of its bone's tail: a spring pulls it toward where animation put it, gravity
/// and the collision spheres move it, the link length is enforced, and the resulting direction is
/// written back into the skeleton's runtime pose as a bone rotation. A `chainLength` of 1 is the
/// single-bone jiggle.
///
/// The whole chain is solved root to tip: a segment's pivot is the SIMULATED tail of its parent, so
/// displacing the root carries the rest of the chain with it instead of tearing it apart.
struct SpringBoneSimulator3DComponent final {
    Entity skeleton = kInvalidEntity;
    /// The authored handle, resolved against the skeleton every update. A DCC re-export that
    /// reorders bones cannot silently retarget a hair chain to a different bone.
    std::string boneName{};
    /// The runtime answer for `boneName`; `kInvalidSkeletonBoneIndex` until the first update.
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndex;
    /// Tail of the LAST segment, in that bone's local space. Every earlier segment uses the rest
    /// position of the next bone in the chain, which is what the skeleton already describes.
    Math::Vector3 tipOffsetLocal{0.0F, -0.25F, 0.0F};
    /// How many bones the chain drives, starting at `boneName` and following first children. A
    /// chain also stops early when a bone has no child, so authoring one too long is harmless.
    std::uint32_t chainLength = 1U;
    float stiffness = 15.0F;
    float damping = 0.35F;
    float gravityScale = 0.0F;
    Math::Vector3 gravityDirection{0.0F, -1.0F, 0.0F};
    float hitRadius = 0.05F;
    /// How far, in degrees, a segment may bend away from the direction animation gave it. A hair
    /// chain with no limit swings through the shoulders; 0 pins it to the animated pose.
    float limitAngleDegrees = 60.0F;
    bool enabled = true;

    // Runtime state, never persisted:
    bool initialized = false;
    /// How many segments the last solve actually resolved (the chain may be shorter than asked).
    std::uint32_t resolvedBoneCount = 0U;
    /// One entry per resolved segment, root first.
    std::vector<SpringBoneTipState> tips;
    /// The local rotations animation gave the chain, and the ones the solver wrote last update.
    /// A pose still holding our own output is not a new reference - only a change made by someone
    /// else (the animation pass) refreshes it, which is what keeps the bend limit anchored to the
    /// performance instead of creeping outward frame after frame.
    std::vector<Math::Quaternion> referenceRotations;
    std::vector<Math::Quaternion> writtenRotations;
    /// Fixed-step accumulator, in seconds; the fraction a frame did not spend stays for next time.
    float pendingSeconds = 0.0F;

    /// Compares the AUTHORED fields only: a running simulator is not a different simulator.
    [[nodiscard]] bool operator==(const SpringBoneSimulator3DComponent& other) const noexcept {
        return skeleton == other.skeleton && boneName == other.boneName &&
               tipOffsetLocal == other.tipOffsetLocal && chainLength == other.chainLength &&
               stiffness == other.stiffness && damping == other.damping &&
               gravityScale == other.gravityScale &&
               gravityDirection == other.gravityDirection && hitRadius == other.hitRadius &&
               limitAngleDegrees == other.limitAngleDegrees && enabled == other.enabled;
    }
};

[[nodiscard]] bool IsSpringBoneSimulator3DComponentValid(
    const SpringBoneSimulator3DComponent& value) noexcept;

[[nodiscard]] SpringBoneSimulator3DComponent SanitizeSpringBoneSimulator3DComponent(
    const SpringBoneSimulator3DComponent& value) noexcept;

/// Integrates one tip a single step toward `restTipWorldPosition` while keeping the link length
/// `|restTipWorldPosition - pivotWorldPosition|` from the pivot.
///
/// Velocity decays as `exp(-damping * kSpringBoneDampingRate * dt)`, not as `1 - damping` per
/// call, so the same `dt` produces the same motion at any frame rate. The tip must already be
/// initialized (see `SpringBoneSimulator3DComponent::initialized`); a non-finite tip is reset to
/// the rest tip rather than propagated.
void StepSpringBoneTip(SpringBoneTipState& tip,
                          const SpringBoneSimulator3DComponent& simulator,
                          const Math::Vector3& pivotWorldPosition,
                          const Math::Vector3& restTipWorldPosition,
                          float deltaTimeSeconds) noexcept;

/// Pushes `tip.position` outside the sphere `(sphereCenterWorld, sphereRadius)` while maintaining
/// the link length from `pivotWorldPosition`, and removes the component of the tip's velocity that
/// pointed into the sphere so the next step does not drive it back in. Returns true if it pushed.
bool ResolveSpringBoneSphereCollision(SpringBoneTipState& tip,
                                         const Math::Vector3& pivotWorldPosition,
                                         const Math::Vector3& sphereCenterWorld,
                                         float sphereRadius, float hitRadius) noexcept;

struct SpringBoneSimulator3DObjectDefinition final {
    static constexpr std::string_view defaultName = "SpringBoneSimulator3D";
    SpringBoneSimulator3DComponent simulator{};
};

[[nodiscard]] bool IsSpringBoneSimulator3DObjectDefinitionValid(
    const SpringBoneSimulator3DObjectDefinition& value) noexcept;

void ApplySpringBoneSimulator3DObjectDefinition(
    IEntityManager& entityManager, Entity entity,
    const SpringBoneSimulator3DObjectDefinition& value);

/// Steps all enabled `SpringBoneSimulator3DComponent` chains against all enabled
/// `SpringBoneCollision3DComponent` obstacles and returns the number of simulators that
/// resolved a skeleton this update.
///
/// Time is consumed in whole `kSpringBoneFixedStepSeconds` steps, at most
/// `kMaximumSpringBoneSubsteps` per call, so a chain's motion depends on elapsed time and not on
/// the frame rate. A simulator whose skeleton, bone or world transform cannot be resolved is reset
/// (its tips are dropped) and re-seeded from the animated pose the next time it can be.
std::size_t UpdateSpringBoneSimulator3DObjects(IEntityManager& entityManager,
                                                  float deltaTimeSeconds);

} // namespace UVE::Scene
