// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "uve/component/entity_uve.h"
#include "uve/math/quaternion_uve.h"
#include "uve/math/vector3_uve.h"
#include "uve/objects/3d/object_3d_common_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"
#include "uve/physics/active_ragdoll_uve.h"
#include "uve/physics/airborne_body_control_uve.h"
#include "uve/physics/landing_system_uve.h"
#include "uve/physics/motion_trajectory_uve.h"
#include "uve/physics/procedural_reaction_uve.h"
#include "uve/physics/ragdoll_solver_uve.h"
#include "uve/physics/ragdoll_world_uve.h"

namespace UVE::Scene {

class IEntityManagerUVE;

inline constexpr std::uint32_t kMaximumRagdollBonesUVE = static_cast<std::uint32_t>(Physics::kMaximumRagdollBodiesUVE);

/// Supported physical collision shapes for ragdoll bone colliders.
enum class RagdollShapeTypeUVE : std::uint8_t {
    Capsule = 0,
    Box,
    Cylinder,
};

/// High-level ragdoll simulation mode.
enum class RagdollSimulationModeUVE : std::uint8_t {
    ActiveRagdoll = 0, // Hybrid balance, procedural stepping, adaptive joint drives
    FullRagdoll,       // Pure physics passive ragdoll
    Kinematic,         // Driven by animation, colliders act as kinematic hitboxes
    Disabled,          // Solver inactive
};

/// One simulated bone, as authored.
struct RagdollBoneSetupUVE final {
    /// Exact, case-sensitive bone name. Resolution failure drops the bone rather than guessing.
    std::string boneName;
    /// The bone this one is jointed to. Empty means "nearest simulated ancestor in the skeleton".
    std::string parentBoneName;
    RagdollShapeTypeUVE shapeType = RagdollShapeTypeUVE::Capsule;
    Math::Vector3UVE boxExtents{0.08F, 0.16F, 0.08F};
    float mass = 1.0F;
    float radius = 0.05F;
    /// Half the cylindrical part, used only when the bone has no child to measure toward.
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    /// Distal limbs want more of this than proximal ones; see the humanoid profile below.
    float angularDamping = 0.5F;
    /// How much of the step's motion is bled off before the solver sees it.
    float friction = 0.8F;

    Physics::RagdollMotionUVE swing1Motion = Physics::RagdollMotionUVE::Limited;
    Physics::RagdollMotionUVE swing2Motion = Physics::RagdollMotionUVE::Limited;
    Physics::RagdollMotionUVE twistMotion = Physics::RagdollMotionUVE::Limited;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// One-way ranges for the two joints that only bend one way.
    bool twistAsymmetric = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;

    /// The joint frame's X axis, in the PARENT body's local space.
    Math::Vector3UVE jointAxisLocal{0.0F, 0.0F, 0.0F};

    [[nodiscard]] bool operator==(const RagdollBoneSetupUVE&) const noexcept = default;
};

/// The runtime half of one simulated bone. Never persisted.
struct RagdollBoneBindingUVE final {
    std::uint32_t bodyIndex = 0U;
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndexUVE;
    std::uint32_t parentBoneIndex = kInvalidSkeletonBoneIndexUVE;
    Math::QuaternionUVE boneOffset{};
    std::uint32_t depth = 0U;
};

/// A ragdoll: the simulated body a skeleton becomes when animation stops driving it.
struct Ragdoll3DComponentUVE final {
    EntityUVE skeleton = kInvalidEntityUVE;
    std::vector<RagdollBoneSetupUVE> bones;
    Math::Vector3UVE gravity{0.0F, -9.81F, 0.0F};
    Physics::RagdollCollisionFilterUVE filter{};
    Physics::RagdollStepSettingsUVE stepSettings{};
    Physics::RagdollSolverUVE solver{};
    Math::Vector3UVE initialVelocity{};
    bool enabled = true;

    // ---- Ragdoll-Specific Physics & Gravity Modes ----
    RagdollSimulationModeUVE simulationMode = RagdollSimulationModeUVE::ActiveRagdoll;
    bool drivesRootTransform = true;
    bool enablePhysicsInEditor = true;
    bool enableGravityInEditor = false; // Default OFF in editor so floating bodies don't fall into void
    bool enableGravityInGame = true;    // Full gravity in game/play mode

    // ---- Trajectory & Viewport Gizmo Settings ----
    bool enableTrajectory = true;
    float trajectoryHorizonSeconds = 2.0F;
    bool showTrajectoryGizmo = true;
    bool showBoneColliders = true;
    bool showCenterOfMass = true;
    bool showSupportPolygon = true;

    // ---- Active Balance & Airborne Controls ----
    bool enableAirborneControl = true;
    bool enableLandingPreparation = true;
    Physics::ActiveRagdollConfigUVE balanceConfig{};

    // ---- Procedural Idle & Hit Reactions ----
    bool enableProceduralIdle = true;
    float proceduralIdleIntensity = 1.0F;

    // ---- Runtime State, never persisted ----
    bool initialized = false;
    std::vector<RagdollBoneBindingUVE> bindings;
    float pendingSeconds = 0.0F;
    Physics::RagdollStepReportUVE lastReport{};
    Physics::ActiveRagdollStateUVE balanceState{};
    Physics::MotionTrajectoryPredictionUVE trajectory{};
    float accumulatedIdleTime = 0.0F;
    Physics::ProceduralReactionTypeUVE activeReactionType = Physics::ProceduralReactionTypeUVE::None;
    float reactionTimer = 0.0F;
    float reactionIntensity = 1.0F;
    Math::Vector3UVE initialRootWorldPosition{};

    /// Compares authored fields only: a running ragdoll is not a different ragdoll.
    [[nodiscard]] bool operator==(const Ragdoll3DComponentUVE& other) const noexcept {
        if (skeleton != other.skeleton || gravity != other.gravity || enabled != other.enabled ||
            initialVelocity != other.initialVelocity || bones.size() != other.bones.size() ||
            simulationMode != other.simulationMode || drivesRootTransform != other.drivesRootTransform ||
            enablePhysicsInEditor != other.enablePhysicsInEditor ||
            enableGravityInEditor != other.enableGravityInEditor ||
            enableGravityInGame != other.enableGravityInGame ||
            enableTrajectory != other.enableTrajectory ||
            showTrajectoryGizmo != other.showTrajectoryGizmo ||
            showBoneColliders != other.showBoneColliders ||
            showCenterOfMass != other.showCenterOfMass ||
            showSupportPolygon != other.showSupportPolygon ||
            enableAirborneControl != other.enableAirborneControl ||
            enableLandingPreparation != other.enableLandingPreparation ||
            enableProceduralIdle != other.enableProceduralIdle ||
            trajectoryHorizonSeconds != other.trajectoryHorizonSeconds ||
            proceduralIdleIntensity != other.proceduralIdleIntensity) {
            return false;
        }
        for (std::size_t index = 0U; index < bones.size(); ++index) {
            if (bones[index] != other.bones[index]) return false;
        }
        return true;
    }
};

[[nodiscard]] bool IsRagdoll3DComponentValidUVE(const Ragdoll3DComponentUVE& value) noexcept;

struct Ragdoll3DObjectDefinitionUVE final {
    static constexpr std::string_view defaultName = "Ragdoll3D";
    Ragdoll3DComponentUVE ragdoll{};
};

[[nodiscard]] bool IsRagdoll3DObjectDefinitionValidUVE(const Ragdoll3DObjectDefinitionUVE& value) noexcept;

void ApplyRagdoll3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                       const Ragdoll3DObjectDefinitionUVE& value);

/// The standard humanoid profile: which bone names become bodies, and with what mass, size and
/// limits. Exposed because the generator is a heuristic over names, and a caller that disagrees with
/// one row should be able to inspect the table rather than reverse-engineer it from behaviour.
///
/// The names are matched as lowercase substrings, most specific first, so `lowerarm_l`,
/// `Forearm.R` and `forearmL` all resolve. Masses sum to roughly 75 kg for a rig with three spine
/// bones - the point is not anatomical precision but that the ratios are right, because a solver
/// cares about ratios far more than about absolutes.
struct HumanoidRagdollProfileEntryUVE final {
    std::string_view pattern;
    float mass = 1.0F;
    float radius = 0.05F;
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    float angularDamping = 0.5F;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// A hinge locks both swing axes and limits the twist instead - a knee or an elbow. The signed
    /// range is what keeps a shin from folding through the thigh.
    bool hinge = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;
};

[[nodiscard]] std::span<const HumanoidRagdollProfileEntryUVE> GetHumanoidRagdollProfileUVE() noexcept;

/// Resolves the humanoid profile against `skeleton`'s bone names, producing one setup per matched
/// bone in hierarchy order. Bones with no matching pattern are skipped - a rig with extra twist
/// bones in the forearm keeps them, and they simply follow the parent.
///
/// Returns an empty vector when nothing matched, which is the honest answer for a rig whose bones
/// are named in a language this table does not know; the caller then authors `bones` explicitly.
[[nodiscard]] std::vector<RagdollBoneSetupUVE> BuildHumanoidRagdollBonesUVE(
    const std::vector<SkeletonBoneUVE>& skeletonBones);

/// Builds the solver from the skeleton's current pose. Called automatically on the first update; a
/// caller may call it earlier to inspect the rig before it starts falling.
///
/// Refused (false, component untouched) for an unknown or uninitialised skeleton entity, a skeleton
/// with no bones, or a resolved body list that is empty - all cases where simulating something would
/// mean simulating the wrong thing.
[[nodiscard]] bool InitializeRagdoll3DUVE(IEntityManagerUVE& entityManager, EntityUVE entity);

/// Applies one world-space hit to the ragdoll's body nearest `worldPoint`, waking the ragdoll
/// first.
///
/// This is the impulse half of "the body reacts when it is struck": the world layer stops a
/// ragdoll from sinking into geometry, but a hit that never becomes a contact - a hitscan leg shot,
/// a blast radius, a scripted shove - has to enter as momentum. The impulse is applied at the
/// closest point on that body's capsule rather than at its centre, so a hit on the shoulder turns
/// the torso instead of only sliding it.
///
/// A hit further than `searchRadius` metres from every body is refused rather than snapped to the
/// nearest one: a blast 30 m away should not move a corpse, and silently doing so is how "the body
/// reacted to something that never touched it" bugs are born.
[[nodiscard]] bool ApplyRagdollImpactUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const Math::Vector3UVE& worldPoint,
                                         const Math::Vector3UVE& impulse, float searchRadius = 0.5F);

/// Steps every enabled ragdoll against the world's colliders and writes the result back into each
/// skeleton's runtime pose.
///
/// Time is consumed in whole `Physics::kRagdollFixedStepSecondsUVE` steps, at most
/// `Physics::kMaximumRagdollSubstepsUVE` per call, so the same fall traces the same arc at 30, 60 or
/// 144 fps. The collider snapshot is built once per call and shared by every ragdoll in the scene -
/// one entity scan, not one per corpse.
///
/// A sleeping ragdoll still costs one hash lookup and nothing else; it wakes the moment a contact
/// exists, which is what makes "the body reacts when it hits the mesh" true for a corpse that has
/// already come to rest and is then stepped on.
///
/// Returns the number of ragdolls whose simulation advanced this call.
std::size_t UpdateRagdoll3DObjectsUVE(IEntityManagerUVE& entityManager, float deltaTimeSeconds, bool isPlayMode = true);

} // namespace UVE::Scene
