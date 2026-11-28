// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "entity.h"
#include "quaternion.h"
#include "vector3.h"
#include "object_3d_common.h"
#include "skeleton_3d.h"
#include "ragdoll_solver.h"
#include "ragdoll_world.h"

namespace UVE::Scene {

class IEntityManager;

inline constexpr std::uint32_t kMaximumRagdollBones = static_cast<std::uint32_t>(Physics::kMaximumRagdollBodies);

/// High-level simulation mode for a `Ragdoll3DComponent`.
enum class RagdollMode : std::uint8_t {
    /// Animation drives the bodies; per-bone linear & angular velocities are tracked for seamless activation.
    Kinematic = 0,
    /// Active Ragdoll / Physical Animation: PD joint & root motors track the live animated pose while
    /// physically reacting to gravity, projectiles, impacts, and world collisions.
    Active = 1,
    /// Full passive ragdoll driven by gravity, impacts, contacts, and anatomical joint limits.
    Simulating = 2,
    /// Smoothly blends out of a captured ragdoll pose snapshot back into live animation / Get-Up clip.
    BlendingOut = 3,
};

/// Performance / fidelity preset. `High` runs full substeps, self-collision and pushback;
/// `Balanced` runs fewer substeps; `Fast` runs the fewest substeps with self-collision off.
enum class RagdollQualityPreset : std::uint8_t {
    High = 0,
    Balanced = 1,
    Fast = 2,
    Custom = 3,
};

/// Result of inspecting a fallen ragdoll's pelvis/chest orientation for choosing a Get-Up animation.
enum class RagdollLyingOrientation : std::uint8_t {
    Unknown = 0,
    FaceDown = 1,
    FaceUp = 2,
    Upright = 3,
};

/// Captured bone pose snapshot used for pop-free ragdoll-to-animation (Get-Up) blending.
struct RagdollPoseSnapshot final {
    std::vector<SkeletonBonePose> bonePoses;
    Math::Vector3 rootWorldPosition{};
    Math::Quaternion rootWorldRotation{};
    bool valid = false;
};

/// Computed grounding & orientation data to align a character entity before playing a Get-Up clip.
struct RagdollGetUpState final {
    RagdollLyingOrientation orientation = RagdollLyingOrientation::Unknown;
    Math::Vector3 groundedRootPosition{};
    Math::Quaternion alignedRootRotation{};
    bool valid = false;
};

/// One simulated bone, as authored.
///
/// The shape is a capsule along the bone's own axis, and its LENGTH is taken from the rig at build
/// time rather than from this struct. `radius` is authored because a capsule's thickness is a
/// modelling choice; `halfHeight` is used for leaf bones that have no child to measure toward.
struct RagdollBoneSetup final {
    /// Exact, case-sensitive bone name. Resolution failure drops the bone rather than guessing.
    std::string boneName;
    /// The bone this one is jointed to. Empty means "nearest simulated ancestor in the skeleton",
    /// which is what a rig wants in almost every case and what the generator fills in.
    std::string parentBoneName;
    float mass = 1.0F;
    float radius = 0.05F;
    /// Half the cylindrical part, used only when the bone has no child to measure toward.
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    /// Distal limbs want more of this than proximal ones; see the humanoid profile below.
    float angularDamping = 0.5F;
    float friction = 0.8F;

    Physics::RagdollMotion swing1Motion = Physics::RagdollMotion::Limited;
    Physics::RagdollMotion swing2Motion = Physics::RagdollMotion::Limited;
    Physics::RagdollMotion twistMotion = Physics::RagdollMotion::Limited;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// One-way ranges for hinge joints (knees, elbows, ankles).
    bool twistAsymmetric = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;

    /// The joint frame's X axis, in the PARENT body's local space. Left zero, the builder derives it:
    /// along the bone for a cone joint, lateral for knees, and `Cross(armDir, forward)` for T/A-pose elbows.
    Math::Vector3 jointAxisLocal{0.0F, 0.0F, 0.0F};

    /// Per-bone Active Ragdoll motor strength scale in [0, 1].
    float motorStrengthScale = 1.0F;
    /// Per-bone physics-to-animation blend weight multiplier in [0, 1].
    float blendWeight = 1.0F;
    /// When false, this body stays kinematic (useful for per-bone partial ragdoll masks).
    bool simulate = true;
};

/// The runtime half of one simulated bone. Never persisted.
struct RagdollBoneBinding final {
    std::uint32_t bodyIndex = 0U;
    std::uint32_t parentBodyIndex = Physics::kInvalidRagdollBody;
    std::uint32_t boneIndex = kInvalidSkeletonBoneIndex;
    std::uint32_t parentBoneIndex = kInvalidSkeletonBoneIndex;
    std::size_t setupIndex = 0U;
    bool isRootBody = false;
    /// `conjugate(bodyBindRotation) * boneBindWorldRotation` - capsule's fixed orientation relative
    /// to the bone it represents.
    Math::Quaternion boneOffset{};
    /// Offset from body center of mass back to the bone's joint head in body-local space.
    /// Used on the root/pelvis body to write back exact world position without floating!
    Math::Vector3 rootAnchorLocal{};
    /// Distance from the root along the skeleton hierarchy.
    std::uint32_t depth = 0U;
    /// Cached previous-frame animated world pose for 2-frame linear & angular velocity inheritance.
    Math::Vector3 prevWorldPosition{};
    Math::Quaternion prevWorldRotation{};
    bool hasPreviousWorldPose = false;
};

/// Full ragdoll and physical animation component (passive, active PD motors, partial
/// subtree hit reactions, smooth pose-snapshot get-up blending, self-collision, dynamic Rigid3D
/// pushback, and Editor live Simulate Preview).
struct Ragdoll3DComponent final {
    Entity skeleton = kInvalidEntity;
    /// Empty means "generate the standard humanoid profile from the skeleton's bone names".
    std::vector<RagdollBoneSetup> bones;
    Math::Vector3 gravity{0.0F, -9.81F, 0.0F};
    /// Which collider layers the ragdoll body may hit, and what it must ignore.
    Physics::RagdollCollisionFilter filter{};
    Physics::RagdollStepSettings stepSettings{};
    Physics::RagdollSolver solver{};
    /// Extra initial velocity inherited when starting simulation (combined with animated bone velocities).
    Math::Vector3 initialVelocity{};

    // ---- Simulation & Authoring Settings ----
    RagdollMode mode = RagdollMode::Simulating;
    RagdollQualityPreset qualityPreset = RagdollQualityPreset::High;
    /// Overall blend between live animated pose (0) and simulated ragdoll pose (1).
    float physicsBlendWeight = 1.0F;
    /// Enables pairwise non-adjacent limb self-collision.
    bool enableSelfCollision = true;
    /// Applies equal-and-opposite contact impulses back onto dynamic `Rigid3DComponent` entities.
    bool enableDynamicPushback = true;
    /// When true, also moves the skeleton entity's own `TransformComponent` to follow the fallen pelvis.
    bool driveEntityTransform = false;
    /// Samples per-bone linear & angular velocity from animated pose before transitioning to ragdoll.
    bool inheritAnimatedVelocity = true;
    /// Maximum fixed substeps executed per frame (scaled down by the `Fast` preset on low-end hardware).
    std::uint32_t maxSubstepsPerFrame = Physics::kMaximumRagdollSubsteps;
    /// Active Ragdoll joint PD motor parameters: joint PD motors driving toward the animated pose.
    float motorStiffness = 160.0F;
    float motorDamping = 20.0F;
    float maxMotorTorque = 180.0F;
    /// Optional world-space root/pelvis balance drive during Active Ragdoll mode.
    float rootDriveLinearStiffness = 0.0F;
    float rootDriveLinearDamping = 0.0F;
    float rootDriveMaxLinearForce = 0.0F;
    float rootDriveAngularStiffness = 120.0F;
    float rootDriveAngularDamping = 16.0F;
    float rootDriveMaxAngularTorque = 160.0F;
    /// Optional bone name limiting dynamic simulation to that bone's subtree (empty = full body).
    std::string partialRootBoneName;
    bool enabled = true;

    // ---- Recovery (standing back up after a fall) ----
    /// When true, a body that has come to rest stands back up on its own: after `recoverDelaySeconds` of
    /// rest the pose blends from where it lies back to the pose it was simulated from.
    bool autoRecover = false;
    /// Seconds of rest before an automatic recovery begins.
    float recoverDelaySeconds = 2.0F;
    /// Seconds the stand-up blend takes.
    float recoverDurationSeconds = 1.6F;

    // ---- Runtime, never persisted ----
    bool initialized = false;
    std::vector<RagdollBoneBinding> bindings;
    /// Fixed-step accumulator: the fraction of a frame that did not fill a whole step stays here.
    float pendingSeconds = 0.0F;
    Physics::RagdollStepReport lastReport{};
    RagdollPoseSnapshot blendOutSnapshot{};
    float blendOutElapsedSeconds = 0.0F;
    float blendOutDurationSeconds = 0.35F;
    bool hadFloorSupport = false;
    bool editorPreviewSimulating = false;
    std::vector<SkeletonBonePose> editorPreviewBackupPose;
    RagdollMode editorPreviewBackupMode = RagdollMode::Simulating;
    /// Why the last `InitializeRagdoll3D` did not build, in words an author can act on. Empty after
    /// a successful build. Shown by the editor instead of a generic "could not start".
    std::string lastSetupError;

    /// The pose the skeleton had when the simulation took over (the animated or rest pose): what a recovery
    /// stands back up into when there is no animation driving the skeleton (the editor preview).
    std::vector<SkeletonBonePose> recoverTargetPose;
    /// True when a recovery blends to `recoverTargetPose`; false when it blends to the live animation.
    bool recoverToCapturedPose = false;
    /// How long the body has been at rest, for `autoRecover`.
    float restSeconds = 0.0F;

    /// Compares authored fields only: a running ragdoll is not a different ragdoll.
    [[nodiscard]] bool operator==(const Ragdoll3DComponent& other) const noexcept {
        if (skeleton != other.skeleton || gravity != other.gravity || enabled != other.enabled ||
            initialVelocity != other.initialVelocity || mode != other.mode ||
            qualityPreset != other.qualityPreset || physicsBlendWeight != other.physicsBlendWeight ||
            enableSelfCollision != other.enableSelfCollision ||
            enableDynamicPushback != other.enableDynamicPushback ||
            driveEntityTransform != other.driveEntityTransform ||
            inheritAnimatedVelocity != other.inheritAnimatedVelocity ||
            maxSubstepsPerFrame != other.maxSubstepsPerFrame ||
            motorStiffness != other.motorStiffness || motorDamping != other.motorDamping ||
            maxMotorTorque != other.maxMotorTorque ||
            rootDriveLinearStiffness != other.rootDriveLinearStiffness ||
            rootDriveLinearDamping != other.rootDriveLinearDamping ||
            rootDriveMaxLinearForce != other.rootDriveMaxLinearForce ||
            rootDriveAngularStiffness != other.rootDriveAngularStiffness ||
            rootDriveAngularDamping != other.rootDriveAngularDamping ||
            rootDriveMaxAngularTorque != other.rootDriveMaxAngularTorque ||
            partialRootBoneName != other.partialRootBoneName || autoRecover != other.autoRecover ||
            recoverDelaySeconds != other.recoverDelaySeconds ||
            recoverDurationSeconds != other.recoverDurationSeconds ||
            bones.size() != other.bones.size()) {
            return false;
        }
        for (std::size_t index = 0U; index < bones.size(); ++index) {
            const RagdollBoneSetup& lhs = bones[index];
            const RagdollBoneSetup& rhs = other.bones[index];
            if (lhs.boneName != rhs.boneName || lhs.parentBoneName != rhs.parentBoneName ||
                lhs.mass != rhs.mass || lhs.radius != rhs.radius || lhs.halfHeight != rhs.halfHeight ||
                lhs.linearDamping != rhs.linearDamping || lhs.angularDamping != rhs.angularDamping ||
                lhs.friction != rhs.friction || lhs.swing1Motion != rhs.swing1Motion ||
                lhs.swing2Motion != rhs.swing2Motion || lhs.twistMotion != rhs.twistMotion ||
                lhs.swing1LimitDegrees != rhs.swing1LimitDegrees ||
                lhs.swing2LimitDegrees != rhs.swing2LimitDegrees ||
                lhs.twistLimitDegrees != rhs.twistLimitDegrees || lhs.twistAsymmetric != rhs.twistAsymmetric ||
                lhs.twistLowerLimitDegrees != rhs.twistLowerLimitDegrees ||
                lhs.twistUpperLimitDegrees != rhs.twistUpperLimitDegrees ||
                lhs.jointAxisLocal != rhs.jointAxisLocal ||
                lhs.motorStrengthScale != rhs.motorStrengthScale ||
                lhs.blendWeight != rhs.blendWeight || lhs.simulate != rhs.simulate) {
                return false;
            }
        }
        return true;
    }
};

/// Validates an authored `Ragdoll3DComponent`, allowing `skeleton == kInvalidEntity` for
/// freshly created scene objects before a skeleton is linked in the Editor or during serialization.
[[nodiscard]] bool IsRagdoll3DComponentAuthoredValid(const Ragdoll3DComponent& value) noexcept;

/// Validates that `value` is both well-authored AND linked to a skeleton entity ready for simulation.
[[nodiscard]] bool IsRagdoll3DComponentValid(const Ragdoll3DComponent& value) noexcept;

struct Ragdoll3DObjectDefinition final {
    static constexpr std::string_view defaultName = "Ragdoll3D";
    Ragdoll3DComponent ragdoll{};
};

[[nodiscard]] bool IsRagdoll3DObjectDefinitionValid(const Ragdoll3DObjectDefinition& value) noexcept;

void ApplyRagdoll3DObjectDefinition(IEntityManager& entityManager, Entity entity,
                                       const Ragdoll3DObjectDefinition& value);

/// Applies a performance / fidelity preset (`High`, `Balanced`, `Fast`) onto
/// `ragdoll`'s substep cap, solver iteration counts, self-collision, and dynamic pushback flags.
void ApplyRagdollQualityPreset(Ragdoll3DComponent& ragdoll, RagdollQualityPreset preset) noexcept;

/// Standard humanoid profile entry matched against lowercased bone names (Mixamo, VRM,
/// Blender, and generic DCC conventions).
struct HumanoidRagdollProfileEntry final {
    std::string_view pattern;
    float mass = 1.0F;
    float radius = 0.05F;
    float halfHeight = 0.08F;
    float linearDamping = 0.05F;
    float angularDamping = 0.5F;
    float swing1LimitDegrees = 40.0F;
    float swing2LimitDegrees = 40.0F;
    float twistLimitDegrees = 25.0F;
    /// A hinge locks both swing axes and limits the twist instead - a knee, an elbow or an ankle.
    bool hinge = false;
    float twistLowerLimitDegrees = -45.0F;
    float twistUpperLimitDegrees = 45.0F;
};

[[nodiscard]] std::span<const HumanoidRagdollProfileEntry> GetHumanoidRagdollProfile() noexcept;

/// Resolves the humanoid profile against `skeleton`'s bone names, producing one setup per matched
/// bone in hierarchy order.
[[nodiscard]] std::vector<RagdollBoneSetup> BuildHumanoidRagdollBones(
    const std::vector<SkeletonBone>& skeletonBones);

/// Generic structural fallback for non-humanoid, custom-named, or creature rigs where the humanoid
/// name table finds fewer than two bones. Filters out finger/toe/face/IK helper bones and caps the
/// result at `kMaximumRagdollBones`.
[[nodiscard]] std::vector<RagdollBoneSetup> BuildGenericRagdollBones(
    const std::vector<SkeletonBone>& skeletonBones);

/// The generator the editor runs when a ragdoll has no bone list yet: the humanoid profile, falling
/// back to the generic structural builder when fewer than two humanoid bones match. Never returns
/// more than `kMaximumRagdollBones` setups.
[[nodiscard]] std::vector<RagdollBoneSetup> BuildAutoRagdollBones(const std::vector<SkeletonBone>& skeletonBones);

/// Shrinks `bones` to at most `kMaximumRagdollBones`, keeping the heaviest (most structural) bodies
/// and dropping digit and helper bones first. Order of the survivors is preserved. Returns true when
/// anything was removed.
bool TrimRagdollBonesToCap(std::vector<RagdollBoneSetup>& bones);

/// The Skeleton3D a ragdoll on `from` most plausibly belongs to: `from` itself, then its own subtree,
/// then each ancestor's subtree outward (so a sibling skeleton under the same character wins over
/// one on another character). The scene root's subtree is only searched when the whole scene holds
/// exactly one Skeleton3D, so a ragdoll is never silently bound to an unrelated character.
[[nodiscard]] Entity FindNearestSkeleton3D(IEntityManager& entityManager, Entity from);

/// Gets a ragdoll ready to build: links the nearest skeleton when none is linked, and generates the
/// bone list when it is empty or `regenerateBones` is set. On failure `lastSetupError` says why.
[[nodiscard]] bool PrepareRagdoll3D(IEntityManager& entityManager, Entity entity, bool regenerateBones = false);

/// Builds the solver from the skeleton's current pose, applies inertia stabilization
/// and non-adjacent self-collision pair filtering, and computes exact T/A-pose hinge axes.
[[nodiscard]] bool InitializeRagdoll3D(IEntityManager& entityManager, Entity entity);

/// Repositions a non-simulating ragdoll's debug/preview capsules to match its skeleton's current
/// world transform and bone pose (used by Edit-mode collider visualization).
[[nodiscard]] bool SyncRagdollBodiesToSkeletonPose(IEntityManager& entityManager, Entity entity);

/// Seamlessly activates `mode` (`Simulating` or `Active`), initializing if needed, waking the
/// solver, and inheriting per-bone linear + angular velocities from the last animated frames plus
/// any optional `extraVelocity`.
[[nodiscard]] bool ActivateRagdoll(IEntityManager& entityManager, Entity entity,
                                      RagdollMode mode = RagdollMode::Simulating,
                                      const Math::Vector3& extraVelocity = {});

/// Captures the ragdoll's current skeleton pose and root world transform for smooth Get-Up blending.
[[nodiscard]] bool CaptureRagdollPoseSnapshot(IEntityManager& entityManager, Entity entity,
                                                 RagdollPoseSnapshot& outSnapshot);

/// Classifies whether the fallen ragdoll is lying FaceDown (prone), FaceUp (supine), or Upright,
/// and computes a grounded root position and yaw-aligned upright facing rotation for Get-Up clips.
[[nodiscard]] bool EvaluateRagdollGetUpState(IEntityManager& entityManager, Entity entity,
                                                RagdollGetUpState& outState, float floorYOffset = 0.0F);

/// Starts standing the body back up: the current fallen pose blends into the pose it was simulated from (or
/// into the live animation when the simulation was started by gameplay) over `recoverDurationSeconds`, with
/// the bodies following the pose so the colliders stand up with the mesh. The ragdoll returns to
/// `Kinematic` when it is done (an editor preview ends there). False when nothing is simulating.
[[nodiscard]] bool BeginRagdollRecovery(IEntityManager& entityManager, Entity entity);

/// True while the ragdoll is simulating or blending out, i.e. there is a fall to recover from or a recovery
/// in progress.
[[nodiscard]] bool IsRagdollActive(const IEntityManager& entityManager, Entity entity);

/// Captures a pose snapshot and enters `RagdollMode::BlendingOut`, smoothly blending from the
/// simulated ragdoll pose back into the live animated / Get-Up pose over `blendDurationSeconds`.
[[nodiscard]] bool BeginBlendOutFromRagdoll(IEntityManager& entityManager, Entity entity,
                                               float blendDurationSeconds = 0.35F);

/// Editor-safe live preview: backs up the skeleton's current pose and starts simulating in Edit Mode.
[[nodiscard]] bool StartRagdollEditorPreview(IEntityManager& entityManager, Entity entity);

/// Stops Editor live preview and restores the exact skeleton pose and ragdoll mode saved on start.
[[nodiscard]] bool StopRagdollEditorPreview(IEntityManager& entityManager, Entity entity);

/// Applies one world-space hit to the ragdoll's body nearest `worldPoint`, waking the ragdoll first.
[[nodiscard]] bool ApplyRagdollImpact(IEntityManager& entityManager, Entity entity,
                                         const Math::Vector3& worldPoint,
                                         const Math::Vector3& impulse, float searchRadius = 0.5F);

/// Steps enabled ragdolls against world colliders (with AABB broadphase, per-substep contact refresh,
/// self-collision, dynamic Rigid3D pushback, PD Active Ragdoll motors, smart sleep, root position +
/// rotation write-back, and smooth pose blending).
/// When `editorPreviewOnly` is true, only ragdolls with `editorPreviewSimulating == true` advance.
/// When `fallbackFloorPlaneY` is provided, an infinite studio floor plane at that Y height catches
/// falling limbs even when no floor collider entity is present in the scene.
std::size_t UpdateRagdoll3DObjects(IEntityManager& entityManager, float deltaTimeSeconds,
                                      bool editorPreviewOnly = false,
                                      std::optional<float> fallbackFloorPlaneY = std::nullopt);

} // namespace UVE::Scene
