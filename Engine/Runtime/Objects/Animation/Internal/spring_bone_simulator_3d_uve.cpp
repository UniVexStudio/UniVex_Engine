// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/spring_bone_simulator_3d_uve.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "uve/component/disabled_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/math/scalar_uve.h"
#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {
namespace {

/// Below this a link is a point, not a segment: the direction to its tail is undefined and the
/// chain cannot be posed. Bones that close to zero length are left at their animated rotation.
constexpr float kMinimumSpringLinkLengthUVE = 1.0e-4F;

constexpr float kSpringBoneDegreesToRadiansUVE = Math::kPiUVE / 180.0F;

[[nodiscard]] Math::Vector3UVE NormalizeOrUVE(const Math::Vector3UVE& vector,
                                              const Math::Vector3UVE& fallback) noexcept {
    const float lengthSquared = Math::LengthSquaredUVE(vector);
    if (!(lengthSquared > 1.0e-12F)) {
        return fallback;
    }
    return vector * (1.0F / std::sqrt(lengthSquared));
}

/// The shortest-arc rotation that turns `from` into `to`, with the angle it turns through.
///
/// The antiparallel case has no unique axis, so it picks a fixed perpendicular one instead of
/// returning garbage: which way a bone flips through 180 degrees is arbitrary either way, but it
/// must be arbitrary in the SAME way every frame or the chain jitters.
[[nodiscard]] Math::QuaternionUVE FromToRotationUVE(const Math::Vector3UVE& from,
                                                    const Math::Vector3UVE& to,
                                                    float& outRadians) noexcept {
    const float dot = std::clamp(Math::DotUVE(from, to), -1.0F, 1.0F);
    outRadians = std::acos(dot);
    const Math::Vector3UVE axis = Math::CrossUVE(from, to);
    const float axisLength = std::sqrt(Math::LengthSquaredUVE(axis));
    Math::QuaternionUVE rotation;
    if (axisLength > 1.0e-6F) {
        static_cast<void>(Math::TryMakeAxisAngleUVE(axis * (1.0F / axisLength), outRadians, rotation));
        return rotation;
    }
    if (dot >= 0.0F) {
        return Math::QuaternionUVE{};
    }
    const Math::Vector3UVE fallbackAxis = (std::fabs(from.x) < 0.9F) ? Math::Vector3UVE{1.0F, 0.0F, 0.0F}
                                                                     : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    static_cast<void>(Math::TryMakeAxisAngleUVE(
        NormalizeOrUVE(Math::CrossUVE(from, fallbackAxis), Math::Vector3UVE{0.0F, 1.0F, 0.0F}),
        outRadians, rotation));
    return rotation;
}

/// Grows `pose` until it covers every bone, seeding the new entries from the rest pose. Entries
/// animation has already written are left alone - a spring bone runs after the clip, not instead
/// of it.
void EnsureSkeletonPoseUVE(Skeleton3DComponentUVE& skeleton) {
    const std::size_t boneCount = skeleton.bones.size();
    if (skeleton.pose.size() >= boneCount) {
        return;
    }
    const std::size_t filled = skeleton.pose.size();
    skeleton.pose.resize(boneCount);
    for (std::size_t index = filled; index < boneCount; ++index) {
        skeleton.pose[index] = SkeletonBonePoseUVE{skeleton.bones[index].localPosition,
                                                   skeleton.bones[index].localRotation,
                                                   skeleton.bones[index].localScale};
    }
}

/// The chain a simulator drives: `root` and its first-child descendants, in storage order. A bone
/// with several children follows the first one, and the walk stops when a bone has none - so an
/// authored `chainLength` longer than the rig is harmless rather than an error.
[[nodiscard]] std::uint32_t ResolveSpringChainUVE(const Skeleton3DComponentUVE& skeleton,
                                                  const std::uint32_t root,
                                                  const std::uint32_t length,
                                                  std::vector<std::uint32_t>& outBones) {
    outBones.clear();
    if (root >= skeleton.bones.size() || length == 0U) {
        return 0U;
    }
    outBones.push_back(root);
    std::uint32_t current = root;
    while (outBones.size() < length) {
        std::uint32_t next = kInvalidSkeletonBoneIndexUVE;
        for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
            if (skeleton.bones[index].parentIndex == static_cast<std::int32_t>(current)) {
                next = static_cast<std::uint32_t>(index);
                break;
            }
        }
        if (next == kInvalidSkeletonBoneIndexUVE) {
            break;
        }
        outBones.push_back(next);
        current = next;
    }
    return static_cast<std::uint32_t>(outBones.size());
}

/// The local tail offset of one chain segment: the next bone's rest position, or the simulator's
/// authored tip for the last segment.
[[nodiscard]] Math::Vector3UVE ChainTailLocalUVE(const Skeleton3DComponentUVE& skeleton,
                                                 const std::vector<std::uint32_t>& chain,
                                                 const std::size_t segment,
                                                 const Math::Vector3UVE& finalTipLocal) {
    if (segment + 1U < chain.size()) {
        return skeleton.bones[chain[segment + 1U]].localPosition;
    }
    return finalTipLocal;
}

[[nodiscard]] Math::Vector3UVE ScaleOffsetUVE(const Math::Vector3UVE& offset,
                                              const Math::Vector3UVE& scale) noexcept {
    return Math::Vector3UVE{offset.x * scale.x, offset.y * scale.y, offset.z * scale.z};
}

struct SpringBoneObstacleUVE final {
    Math::Vector3UVE center{};
    float radius = 0.0F;
};

/// One fixed step of the whole chain against `skeleton`'s pose, which is written back as it goes.
/// Returns the number of segments solved, or 0 when nothing could be resolved.
std::uint32_t SolveSpringBoneChainStepUVE(SpringBoneSimulator3DComponentUVE& simulator,
                                          Skeleton3DComponentUVE& skeleton,
                                          const ObjectWorldFrameUVE& skeletonWorldFrame,
                                          const std::vector<std::uint32_t>& chain,
                                          const std::vector<Math::QuaternionUVE>& animatedLocalRotations,
                                          const std::vector<SpringBoneObstacleUVE>& obstacles,
                                          const float deltaTimeSeconds) {
    const std::uint32_t segmentCount = static_cast<std::uint32_t>(chain.size());
    if (segmentCount == 0U) {
        return 0U;
    }
    // Start every step from the pose ANIMATION produced. The spring's own answer from the previous
    // step is not a reference: measuring the bend limit against it would let the cone creep a
    // little further each substep until the chain hung straight down.
    for (std::size_t segment = 0U; segment < chain.size(); ++segment) {
        skeleton.pose[chain[segment]].rotation = animatedLocalRotations[segment];
    }
    if (!simulator.initialized || simulator.tips.size() != segmentCount) {
        // Seed from the animated pose: the first frame must not pop, whatever the clip is doing.
        simulator.tips.assign(segmentCount, SpringBoneTipStateUVE{});
        for (std::size_t segment = 0U; segment < segmentCount; ++segment) {
            ObjectWorldFrameUVE boneFrame;
            if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, chain[segment], skeletonWorldFrame,
                                                     boneFrame)) {
                simulator.tips.clear();
                return 0U;
            }
            const Math::Vector3UVE tailLocal =
                ChainTailLocalUVE(skeleton, chain, segment, simulator.tipOffsetLocal);
            simulator.tips[segment].position =
                boneFrame.position +
                Math::RotateVectorUVE(boneFrame.rotation, ScaleOffsetUVE(tailLocal, boneFrame.scale));
            simulator.tips[segment].velocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
        }
        simulator.initialized = true;
    }

    const float limitRadians = simulator.limitAngleDegrees * kSpringBoneDegreesToRadiansUVE;
    for (std::size_t segment = 0U; segment < segmentCount; ++segment) {
        const std::uint32_t boneIndex = chain[segment];
        ObjectWorldFrameUVE boneFrame;
        if (!TryResolveSkeletonBoneWorldFrameUVE(skeleton, boneIndex, skeletonWorldFrame, boneFrame)) {
            return static_cast<std::uint32_t>(segment);
        }
        const Math::Vector3UVE tailLocal =
            ChainTailLocalUVE(skeleton, chain, segment, simulator.tipOffsetLocal);
        const Math::Vector3UVE scaledTail = ScaleOffsetUVE(tailLocal, boneFrame.scale);
        const float linkLength = std::sqrt(Math::LengthSquaredUVE(scaledTail));
        if (!(linkLength > kMinimumSpringLinkLengthUVE)) {
            continue;
        }
        const Math::Vector3UVE restTailWorld =
            boneFrame.position + Math::RotateVectorUVE(boneFrame.rotation, scaledTail);
        // The pivot is the parent's SIMULATED tail, so the chain stays connected when the root
        // swings; only the first segment hangs off the bone that animation posed.
        const Math::Vector3UVE pivot =
            (segment == 0U) ? boneFrame.position : simulator.tips[segment - 1U].position;

        StepSpringBoneTipUVE(simulator.tips[segment], simulator, pivot, restTailWorld, deltaTimeSeconds);
        for (const SpringBoneObstacleUVE& obstacle : obstacles) {
            static_cast<void>(ResolveSpringBoneSphereCollisionUVE(
                simulator.tips[segment], pivot, obstacle.center, obstacle.radius, simulator.hitRadius));
        }

        const Math::Vector3UVE restDirection =
            NormalizeOrUVE(restTailWorld - boneFrame.position, Math::Vector3UVE{0.0F, -1.0F, 0.0F});
        Math::Vector3UVE simulatedDirection =
            NormalizeOrUVE(simulator.tips[segment].position - pivot, restDirection);
        float bendRadians = 0.0F;
        const Math::QuaternionUVE bend = FromToRotationUVE(restDirection, simulatedDirection, bendRadians);
        if (bendRadians > limitRadians) {
            // Clamp inside the cone around the animated direction, then move the tip onto the
            // clamped direction: the state must agree with the pose we are about to write.
            const float alpha = (bendRadians > 0.0F) ? (limitRadians / bendRadians) : 0.0F;
            Math::QuaternionUVE clamped;
            if (!Math::TrySlerpUVE(Math::QuaternionUVE{}, bend, alpha, clamped)) {
                return static_cast<std::uint32_t>(segment);
            }
            simulatedDirection = NormalizeOrUVE(Math::RotateVectorUVE(clamped, restDirection), restDirection);
            simulator.tips[segment].position = pivot + simulatedDirection * linkLength;
        }

        float appliedRadians = 0.0F;
        const Math::QuaternionUVE bendApplied =
            FromToRotationUVE(restDirection, simulatedDirection, appliedRadians);
        ObjectWorldFrameUVE parentFrame = skeletonWorldFrame;
        const std::int32_t parentIndex = skeleton.bones[boneIndex].parentIndex;
        if (parentIndex >= 0 &&
            !TryResolveSkeletonBoneWorldFrameUVE(skeleton, static_cast<std::uint32_t>(parentIndex),
                                                 skeletonWorldFrame, parentFrame)) {
            return static_cast<std::uint32_t>(segment);
        }
        Math::QuaternionUVE parentInverse;
        if (!Math::TryInverseUVE(parentFrame.rotation, parentInverse)) {
            return static_cast<std::uint32_t>(segment);
        }
        const Math::QuaternionUVE localRotation =
            Math::MultiplyUVE(parentInverse, Math::MultiplyUVE(bendApplied, boneFrame.rotation));
        Math::QuaternionUVE normalized;
        if (!Math::TryNormalizeUVE(localRotation, normalized)) {
            return static_cast<std::uint32_t>(segment);
        }
        skeleton.pose[boneIndex].rotation = normalized;
    }
    simulator.resolvedBoneCount = segmentCount;
    return segmentCount;
}

} // namespace

bool IsSpringBoneCollision3DComponentValidUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept {
    return IsFinite3DObjectVectorUVE(value.centerOffset) && std::isfinite(value.radius) &&
           value.radius > 0.0F;
}

SpringBoneCollision3DComponentUVE SanitizeSpringBoneCollision3DComponentUVE(
    const SpringBoneCollision3DComponentUVE& value) noexcept {
    SpringBoneCollision3DComponentUVE out = value;
    if (!IsFinite3DObjectVectorUVE(out.centerOffset)) {
        out.centerOffset = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.25F;
    }
    return out;
}

bool IsSpringBoneCollision3DObjectDefinitionValidUVE(
    const SpringBoneCollision3DObjectDefinitionUVE& value) noexcept {
    return IsSpringBoneCollision3DComponentValidUVE(value.collision);
}

void ApplySpringBoneCollision3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const SpringBoneCollision3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              SpringBoneCollision3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SpringBoneCollision3DComponentUVE>(
        entity, SanitizeSpringBoneCollision3DComponentUVE(value.collision));
}

bool IsSpringBoneSimulator3DComponentValidUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept {
    return IsBounded3DObjectStringUVE(value.boneName) &&
           IsFinite3DObjectVectorUVE(value.tipOffsetLocal) &&
           Math::LengthSquaredUVE(value.tipOffsetLocal) > 1.0e-8F &&
           value.chainLength >= 1U && value.chainLength <= kMaximumSpringBoneChainLengthUVE &&
           std::isfinite(value.stiffness) && value.stiffness >= 0.0F &&
           std::isfinite(value.damping) && value.damping >= 0.0F && value.damping <= 1.0F &&
           std::isfinite(value.gravityScale) &&
           IsFinite3DObjectVectorUVE(value.gravityDirection) &&
           Math::LengthSquaredUVE(value.gravityDirection) > 1.0e-8F &&
           std::isfinite(value.hitRadius) && value.hitRadius >= 0.0F &&
           std::isfinite(value.limitAngleDegrees) && value.limitAngleDegrees >= 0.0F &&
           value.limitAngleDegrees <= 180.0F;
}

SpringBoneSimulator3DComponentUVE SanitizeSpringBoneSimulator3DComponentUVE(
    const SpringBoneSimulator3DComponentUVE& value) noexcept {
    SpringBoneSimulator3DComponentUVE out = value;
    if (!IsBounded3DObjectStringUVE(out.boneName)) {
        out.boneName.resize(kMaximum3DObjectStringLengthUVE);
    }
    if (!IsFinite3DObjectVectorUVE(out.tipOffsetLocal) ||
        Math::LengthSquaredUVE(out.tipOffsetLocal) <= 1.0e-8F) {
        out.tipOffsetLocal = Math::Vector3UVE{0.0F, -0.25F, 0.0F};
    }
    if (out.chainLength < 1U) {
        out.chainLength = 1U;
    } else if (out.chainLength > kMaximumSpringBoneChainLengthUVE) {
        out.chainLength = static_cast<std::uint32_t>(kMaximumSpringBoneChainLengthUVE);
    }
    if (!std::isfinite(out.stiffness) || out.stiffness < 0.0F) {
        out.stiffness = 15.0F;
    }
    if (!std::isfinite(out.damping)) {
        out.damping = 0.35F;
    } else {
        out.damping = std::clamp(out.damping, 0.0F, 1.0F);
    }
    if (!std::isfinite(out.gravityScale)) {
        out.gravityScale = 0.0F;
    }
    if (!IsFinite3DObjectVectorUVE(out.gravityDirection) ||
        Math::LengthSquaredUVE(out.gravityDirection) <= 1.0e-8F) {
        out.gravityDirection = Math::Vector3UVE{0.0F, -1.0F, 0.0F};
    }
    if (!std::isfinite(out.hitRadius) || out.hitRadius < 0.0F) {
        out.hitRadius = 0.05F;
    }
    if (!std::isfinite(out.limitAngleDegrees)) {
        out.limitAngleDegrees = 60.0F;
    } else {
        out.limitAngleDegrees = std::clamp(out.limitAngleDegrees, 0.0F, 180.0F);
    }
    return out;
}

void StepSpringBoneTipUVE(SpringBoneTipStateUVE& tip,
                          const SpringBoneSimulator3DComponentUVE& simulator,
                          const Math::Vector3UVE& pivotWorldPosition,
                          const Math::Vector3UVE& restTipWorldPosition,
                          const float deltaTimeSeconds) noexcept {
    if (!IsFinite3DObjectVectorUVE(pivotWorldPosition) ||
        !IsFinite3DObjectVectorUVE(restTipWorldPosition)) {
        return;
    }
    if (!IsFinite3DObjectVectorUVE(tip.position) || !IsFinite3DObjectVectorUVE(tip.velocity)) {
        tip.position = restTipWorldPosition;
        tip.velocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return;
    }
    const float dt = std::min(deltaTimeSeconds, 0.1F);
    const Math::Vector3UVE gravDir =
        NormalizeOrUVE(simulator.gravityDirection, Math::Vector3UVE{0.0F, -1.0F, 0.0F});
    const Math::Vector3UVE springForce =
        (restTipWorldPosition - tip.position) * simulator.stiffness + gravDir * simulator.gravityScale;
    const float decay =
        std::exp(-std::clamp(simulator.damping, 0.0F, 1.0F) * kSpringBoneDampingRateUVE * dt);
    tip.velocity = tip.velocity * decay + springForce * dt;
    Math::Vector3UVE nextTip = tip.position + tip.velocity * dt;

    const float linkLength = Math::LengthUVE(restTipWorldPosition - pivotWorldPosition);
    if (linkLength > kMinimumSpringLinkLengthUVE) {
        const Math::Vector3UVE fromPivot = nextTip - pivotWorldPosition;
        const float distance = std::sqrt(Math::LengthSquaredUVE(fromPivot));
        if (distance > kMinimumSpringLinkLengthUVE) {
            nextTip = pivotWorldPosition + fromPivot * (linkLength / distance);
        } else {
            nextTip = restTipWorldPosition;
        }
    }
    tip.position = nextTip;
}

bool ResolveSpringBoneSphereCollisionUVE(SpringBoneTipStateUVE& tip,
                                         const Math::Vector3UVE& pivotWorldPosition,
                                         const Math::Vector3UVE& sphereCenterWorld,
                                         const float sphereRadius, const float hitRadius) noexcept {
    if (!IsFinite3DObjectVectorUVE(tip.position) || !std::isfinite(sphereRadius) ||
        sphereRadius <= 0.0F) {
        return false;
    }
    const float combinedRadius = sphereRadius + std::max(0.0F, hitRadius);
    const Math::Vector3UVE delta = tip.position - sphereCenterWorld;
    const float distanceSquared = Math::LengthSquaredUVE(delta);
    if (distanceSquared >= combinedRadius * combinedRadius) {
        return false;
    }
    const Math::Vector3UVE boneDirection = tip.position - pivotWorldPosition;
    const float linkLength = std::sqrt(Math::LengthSquaredUVE(boneDirection));
    Math::Vector3UVE pushNormal = (distanceSquared > 1.0e-8F)
                                      ? delta * (1.0F / std::sqrt(distanceSquared))
                                      : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
    if (linkLength > kMinimumSpringLinkLengthUVE) {
        const Math::Vector3UVE unitBone = boneDirection * (1.0F / linkLength);
        if (std::fabs(Math::DotUVE(pushNormal, unitBone)) > 0.99F) {
            const Math::Vector3UVE fallbackAxis =
                (std::fabs(unitBone.x) < 0.9F) ? Math::Vector3UVE{1.0F, 0.0F, 0.0F}
                                               : Math::Vector3UVE{0.0F, 0.0F, 1.0F};
            pushNormal = NormalizeOrUVE(Math::CrossUVE(unitBone, fallbackAxis),
                                        Math::Vector3UVE{0.0F, 1.0F, 0.0F});
        }
    }
    Math::Vector3UVE pushed = sphereCenterWorld + pushNormal * combinedRadius;
    if (linkLength > kMinimumSpringLinkLengthUVE) {
        const Math::Vector3UVE fromPivot = pushed - pivotWorldPosition;
        const float fromPivotSquared = Math::LengthSquaredUVE(fromPivot);
        if (fromPivotSquared > 1.0e-8F) {
            pushed = pivotWorldPosition + fromPivot * (linkLength / std::sqrt(fromPivotSquared));
        }
    }
    tip.position = pushed;
    // Bleed the velocity that was driving the tip in: without this the spring keeps pushing the
    // tip through the sphere every step and the chain buzzes against it.
    const float intoSurface = Math::DotUVE(tip.velocity, pushNormal);
    if (intoSurface < 0.0F) {
        tip.velocity = tip.velocity - pushNormal * intoSurface;
    }
    return true;
}

bool IsSpringBoneSimulator3DObjectDefinitionValidUVE(
    const SpringBoneSimulator3DObjectDefinitionUVE& value) noexcept {
    return IsSpringBoneSimulator3DComponentValidUVE(value.simulator);
}

void ApplySpringBoneSimulator3DObjectDefinitionUVE(
    IEntityManagerUVE& entityManager, const EntityUVE entity,
    const SpringBoneSimulator3DObjectDefinitionUVE& value) {
    ApplyBoneModifier3DBaseUVE(entityManager, entity,
                               SpringBoneSimulator3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<SpringBoneSimulator3DComponentUVE>(
        entity, SanitizeSpringBoneSimulator3DComponentUVE(value.simulator));
}

std::size_t UpdateSpringBoneSimulator3DObjectsUVE(IEntityManagerUVE& entityManager,
                                                  const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::vector<SpringBoneObstacleUVE> obstacles;
    entityManager.ForEachUVE<SpringBoneCollision3DComponentUVE, WorldTransformComponentUVE>(
        [&](const EntityUVE entity, const SpringBoneCollision3DComponentUVE& collision,
            const WorldTransformComponentUVE& world) {
            if (!collision.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsSpringBoneCollision3DComponentValidUVE(collision) ||
                !IsWorldTransformComponentValidUVE(world)) {
                return;
            }
            obstacles.push_back(SpringBoneObstacleUVE{
                TransformWorldPointUVE(world, collision.centerOffset), collision.radius});
        });

    std::size_t steppedCount = 0U;
    entityManager.ForEachUVE<SpringBoneSimulator3DComponentUVE>(
        [&](const EntityUVE entity, SpringBoneSimulator3DComponentUVE& simulator) {
            const auto resetRuntimeState = [&simulator]() {
                simulator.initialized = false;
                simulator.resolvedBoneCount = 0U;
                simulator.tips.clear();
                simulator.referenceRotations.clear();
                simulator.writtenRotations.clear();
                simulator.pendingSeconds = 0.0F;
            };
            if (!simulator.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsSpringBoneSimulator3DComponentValidUVE(simulator) ||
                !entityManager.IsAliveUVE(simulator.skeleton) ||
                !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(simulator.skeleton) ||
                !entityManager.HasComponentUVE<WorldTransformComponentUVE>(simulator.skeleton)) {
                resetRuntimeState();
                return;
            }
            Skeleton3DComponentUVE& skeleton =
                entityManager.GetComponentUVE<Skeleton3DComponentUVE>(simulator.skeleton);
            const WorldTransformComponentUVE& skeletonWorld =
                entityManager.GetComponentUVE<WorldTransformComponentUVE>(simulator.skeleton);
            if (!skeleton.enabled || !IsSkeleton3DObjectComponentValidUVE(skeleton) ||
                !IsWorldTransformComponentValidUVE(skeletonWorld)) {
                resetRuntimeState();
                return;
            }
            std::uint32_t resolvedBone = kInvalidSkeletonBoneIndexUVE;
            if (!TryFindSkeletonBoneIndexUVE(skeleton, simulator.boneName, resolvedBone)) {
                resetRuntimeState();
                return;
            }
            simulator.boneIndex = resolvedBone;
            std::vector<std::uint32_t> chain;
            if (ResolveSpringChainUVE(skeleton, resolvedBone, simulator.chainLength, chain) == 0U) {
                resetRuntimeState();
                return;
            }
            EnsureSkeletonPoseUVE(skeleton);
            // The pose as animation left it, captured once for the whole update: every fixed step
            // re-applies it, so the simulation is the only state carried between steps. The pose
            // still holds OUR last answer when no animation is running, so only a rotation that
            // differs from what the solver wrote is treated as a fresh reference.
            simulator.referenceRotations.resize(chain.size());
            simulator.writtenRotations.resize(chain.size());
            for (std::size_t segment = 0U; segment < chain.size(); ++segment) {
                const Math::QuaternionUVE current = skeleton.pose[chain[segment]].rotation;
                if (!simulator.initialized || simulator.writtenRotations[segment] != current) {
                    simulator.referenceRotations[segment] = current;
                }
            }
            const ObjectWorldFrameUVE skeletonFrame{skeletonWorld.worldPosition,
                                                    skeletonWorld.worldRotation,
                                                    skeletonWorld.worldScale};

            // Whole steps only, and a bounded number of them: the leftover fraction is kept for
            // the next frame instead of being spent as an irregular step, which is what makes the
            // motion depend on elapsed time rather than on the frame rate.
            simulator.pendingSeconds += deltaTimeSeconds;
            simulator.pendingSeconds =
                std::min(simulator.pendingSeconds,
                         kSpringBoneFixedStepSecondsUVE *
                             static_cast<float>(kMaximumSpringBoneSubstepsUVE));
            std::uint32_t stepsTaken = 0U;
            while (simulator.pendingSeconds >= kSpringBoneFixedStepSecondsUVE &&
                   stepsTaken < kMaximumSpringBoneSubstepsUVE) {
                if (SolveSpringBoneChainStepUVE(simulator, skeleton, skeletonFrame, chain,
                                                simulator.referenceRotations, obstacles,
                                                kSpringBoneFixedStepSecondsUVE) == 0U) {
                    resetRuntimeState();
                    return;
                }
                simulator.pendingSeconds -= kSpringBoneFixedStepSecondsUVE;
                ++stepsTaken;
            }
            for (std::size_t segment = 0U; segment < chain.size(); ++segment) {
                simulator.writtenRotations[segment] = skeleton.pose[chain[segment]].rotation;
            }
            ++steppedCount;
        });
    return steppedCount;
}

} // namespace UVE::Scene
