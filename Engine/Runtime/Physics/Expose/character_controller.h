// Copyright (c) 2026 UniVex Studios. All Rights Reserved.
#pragma once

#include <cstddef>
#include <cstdint>

#include "vector3.h"
#include "i_collision_system.h"
#include "i_entity_manager.h"
#include "i_scene_graph.h"

namespace UVE::Physics {

/// Caller-owned kinematic movement request. The entity must have a valid transform, collider,
/// and either no rigid body or a valid kinematic rigid body; CharacterController never adds,
/// removes, or serializes components.
enum class CcdEligibilityStatus : std::uint8_t { Invalid = 0, Disabled, Enabled };

/// Classifies whether a finite displacement warrants a caller-owned continuous sweep.
[[nodiscard]] CcdEligibilityStatus EvaluateCcdEligibility(
    Math::Vector3 displacement, float minimumSweepDistance) noexcept;

struct CharacterControllerInput final {
    Scene::Entity entity;
    Math::Vector3 desiredDisplacement{};
    std::size_t maximumSubsteps = 8U;
    float maximumSubstepDistance = 0.25F;
    float maximumGroundSlopeDegrees = 45.0F;
    /// Opt-in maximum vertical step height for MoveWithToI; zero preserves the no-step behavior.
    float maximumStepHeight = 0.0F;
    /// Opt-in controller-driven push for non-kinematic positive-mass contact targets.
    bool pushDynamicBodies = false;
    /// Multiplier for the controller's contact displacement when deriving target push speed.
    float dynamicBodyPushStrength = 1.0F;
    /// Per-contact cap for pushed target normal speed, preventing unbounded velocity mutation.
    float maximumDynamicBodyPushSpeed = 5.0F;
    /// Fixed-step duration used to derive target push speed from controller displacement.
    float dynamicBodyPushDeltaTimeSeconds = 1.0F / 60.0F;
};

enum class CharacterControllerMoveCode : std::uint8_t {
    Moved = 0,
    InvalidEntity,
    MissingTransform,
    MissingCollider,
    NonKinematicBody,
    InvalidInput,
};

struct CharacterControllerMoveResult final {
    CharacterControllerMoveCode code = CharacterControllerMoveCode::InvalidInput;
    Math::Vector3 requestedDisplacement{};
    Math::Vector3 appliedDisplacement{};
    Math::Vector3 remainingDisplacement{};
    std::size_t substeps = 0U;
    std::size_t contactCount = 0U;
    bool blocked = false;
    bool inputClamped = false;
    bool contactsTruncated = false;
    bool toiUsed = false;
    bool stepUpUsed = false;
    bool dynamicBodyPushClamped = false;
    std::size_t pushedBodyCount = 0U;
    bool grounded = false;
    Math::Vector3 groundNormal{};
    float earliestImpactTime = 1.0F;

    [[nodiscard]] bool IsAccepted() const noexcept {
        return code == CharacterControllerMoveCode::Moved;
    }
};

/// Bounded main-thread kinematic movement over the existing AABB collision/layer contracts.
/// The default Move path advances in capped discrete substeps, then existing minimum-translation-
/// vector contacts are applied and remaining displacement is projected away from contacted normals.
/// The opt-in MoveWithToI path adds capped conservative swept-AABB TOI and, when
/// `maximumStepHeight` is positive, one failure-atomic vertical lift/clearance/drop attempt for a
/// wall-like impact. Neither path owns exact non-box narrow phases, dynamic-body push policy,
/// locomotion state, or persistent controller history.
class CharacterController final {
public:
    static constexpr std::size_t kMaximumSubsteps = 32U;
    static constexpr std::size_t kMaximumContacts = 64U;
    static constexpr float kMinimumMovementDistance = 1.0e-5F;
    static constexpr float kDefaultMaximumSubstepDistance = 0.25F;
    static constexpr float kDefaultMaximumGroundSlopeDegrees = 45.0F;
    static constexpr float kMaximumGroundSlopeDegrees = 89.0F;
    static constexpr std::size_t kMaximumToIIterations = 8U;
    static constexpr std::size_t kMaximumToITargets = 256U;
    static constexpr std::size_t kMaximumStepSearchIterations = 8U;
    static constexpr float kToIEpsilon = 1.0e-4F;

    [[nodiscard]] static CharacterControllerMoveResult Move(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        ICollisionSystem& collisionSystem, const CharacterControllerInput& input);

    /// Opt-in bounded TOI-assisted movement. It scans the shared conservative world-AABB cache,
    /// advances to the earliest swept-AABB impact, removes the contacted normal from remaining
    /// displacement, and repeats for a capped number of iterations. It does not replace the
    /// overlap resolver or claim backend-wide CCD, exact non-box narrow phases, or backend-wide
    /// dynamic-body continuous response. When enabled in the input, a bounded per-contact target
    /// velocity push is applied to validated non-kinematic positive-mass bodies.
    [[nodiscard]] static CharacterControllerMoveResult MoveWithToI(
        Scene::IEntityManager& entityManager, Scene::ISceneGraph& sceneGraph,
        ICollisionSystem& collisionSystem, const CharacterControllerInput& input);
};

} // namespace UVE::Physics
