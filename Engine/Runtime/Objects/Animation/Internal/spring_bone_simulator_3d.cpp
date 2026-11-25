// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "spring_bone_simulator_3d.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "disabled_component.h"
#include "i_entity_manager.h"
#include "scalar.h"
#include "abstract_animation_objects_3d.h"
#include "object_3d.h"

namespace UVE::Scene {
namespace {

/// Below this a link is a point, not a segment: the direction to its tail is undefined and the
/// chain cannot be posed. Bones that close to zero length are left at their animated rotation.
constexpr float kMinimumSpringLinkLength = 1.0e-4F;

constexpr float kSpringBoneDegreesToRadians = Math::kPi / 180.0F;

[[nodiscard]] Math::Vector3 NormalizeOr(const Math::Vector3& vector,
                                              const Math::Vector3& fallback) noexcept {
    const float lengthSquared = Math::LengthSquared(vector);
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
[[nodiscard]] Math::Quaternion FromToRotation(const Math::Vector3& from,
                                                    const Math::Vector3& to,
                                                    float& outRadians) noexcept {
    const float dot = std::clamp(Math::Dot(from, to), -1.0F, 1.0F);
    outRadians = std::acos(dot);
    const Math::Vector3 axis = Math::Cross(from, to);
    const float axisLength = std::sqrt(Math::LengthSquared(axis));
    Math::Quaternion rotation;
    if (axisLength > 1.0e-6F) {
        static_cast<void>(Math::TryMakeAxisAngle(axis * (1.0F / axisLength), outRadians, rotation));
        return rotation;
    }
    if (dot >= 0.0F) {
        return Math::Quaternion{};
    }
    const Math::Vector3 fallbackAxis = (std::fabs(from.x) < 0.9F) ? Math::Vector3{1.0F, 0.0F, 0.0F}
                                                                     : Math::Vector3{0.0F, 0.0F, 1.0F};
    static_cast<void>(Math::TryMakeAxisAngle(
        NormalizeOr(Math::Cross(from, fallbackAxis), Math::Vector3{0.0F, 1.0F, 0.0F}),
        outRadians, rotation));
    return rotation;
}

/// Grows `pose` until it covers every bone, seeding the new entries from the rest pose. Entries
/// animation has already written are left alone - a spring bone runs after the clip, not instead
/// of it.
void EnsureSkeletonPose(Skeleton3DComponent& skeleton) {
    const std::size_t boneCount = skeleton.bones.size();
    if (skeleton.pose.size() >= boneCount) {
        return;
    }
    const std::size_t filled = skeleton.pose.size();
    skeleton.pose.resize(boneCount);
    for (std::size_t index = filled; index < boneCount; ++index) {
        skeleton.pose[index] = SkeletonBonePose{skeleton.bones[index].localPosition,
                                                   skeleton.bones[index].localRotation,
                                                   skeleton.bones[index].localScale};
    }
}

/// The chain a simulator drives: `root` and its first-child descendants, in storage order. A bone
/// with several children follows the first one, and the walk stops when a bone has none - so an
/// authored `chainLength` longer than the rig is harmless rather than an error.
[[nodiscard]] std::uint32_t ResolveSpringChain(const Skeleton3DComponent& skeleton,
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
        std::uint32_t next = kInvalidSkeletonBoneIndex;
        for (std::size_t index = 0U; index < skeleton.bones.size(); ++index) {
            if (skeleton.bones[index].parentIndex == static_cast<std::int32_t>(current)) {
                next = static_cast<std::uint32_t>(index);
                break;
            }
        }
        if (next == kInvalidSkeletonBoneIndex) {
            break;
        }
        outBones.push_back(next);
        current = next;
    }
    return static_cast<std::uint32_t>(outBones.size());
}

/// The local tail offset of one chain segment: the next bone's rest position, or the simulator's
/// authored tip for the last segment.
[[nodiscard]] Math::Vector3 ChainTailLocal(const Skeleton3DComponent& skeleton,
                                                 const std::vector<std::uint32_t>& chain,
                                                 const std::size_t segment,
                                                 const Math::Vector3& finalTipLocal) {
    if (segment + 1U < chain.size()) {
        return skeleton.bones[chain[segment + 1U]].localPosition;
    }
    return finalTipLocal;
}

[[nodiscard]] Math::Vector3 ScaleOffset(const Math::Vector3& offset,
                                              const Math::Vector3& scale) noexcept {
    return Math::Vector3{offset.x * scale.x, offset.y * scale.y, offset.z * scale.z};
}

struct SpringBoneObstacle final {
    Math::Vector3 center{};
    float radius = 0.0F;
};

/// One fixed step of the whole chain against `skeleton`'s pose, which is written back as it goes.
/// Returns the number of segments solved, or 0 when nothing could be resolved.
std::uint32_t SolveSpringBoneChainStep(SpringBoneSimulator3DComponent& simulator,
                                          Skeleton3DComponent& skeleton,
                                          const ObjectWorldFrame& skeletonWorldFrame,
                                          const std::vector<std::uint32_t>& chain,
                                          const std::vector<Math::Quaternion>& animatedLocalRotations,
                                          const std::vector<SpringBoneObstacle>& obstacles,
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
        simulator.tips.assign(segmentCount, SpringBoneTipState{});
        for (std::size_t segment = 0U; segment < segmentCount; ++segment) {
            ObjectWorldFrame boneFrame;
            if (!TryResolveSkeletonBoneWorldFrame(skeleton, chain[segment], skeletonWorldFrame,
                                                     boneFrame)) {
                simulator.tips.clear();
                return 0U;
            }
            const Math::Vector3 tailLocal =
                ChainTailLocal(skeleton, chain, segment, simulator.tipOffsetLocal);
            simulator.tips[segment].position =
                boneFrame.position +
                Math::RotateVector(boneFrame.rotation, ScaleOffset(tailLocal, boneFrame.scale));
            simulator.tips[segment].velocity = Math::Vector3{0.0F, 0.0F, 0.0F};
        }
        simulator.initialized = true;
    }

    const float limitRadians = simulator.limitAngleDegrees * kSpringBoneDegreesToRadians;
    for (std::size_t segment = 0U; segment < segmentCount; ++segment) {
        const std::uint32_t boneIndex = chain[segment];
        ObjectWorldFrame boneFrame;
        if (!TryResolveSkeletonBoneWorldFrame(skeleton, boneIndex, skeletonWorldFrame, boneFrame)) {
            return static_cast<std::uint32_t>(segment);
        }
        const Math::Vector3 tailLocal =
            ChainTailLocal(skeleton, chain, segment, simulator.tipOffsetLocal);
        const Math::Vector3 scaledTail = ScaleOffset(tailLocal, boneFrame.scale);
        const float linkLength = std::sqrt(Math::LengthSquared(scaledTail));
        if (!(linkLength > kMinimumSpringLinkLength)) {
            continue;
        }
        const Math::Vector3 restTailWorld =
            boneFrame.position + Math::RotateVector(boneFrame.rotation, scaledTail);
        // The pivot is the parent's SIMULATED tail, so the chain stays connected when the root
        // swings; only the first segment hangs off the bone that animation posed.
        const Math::Vector3 pivot =
            (segment == 0U) ? boneFrame.position : simulator.tips[segment - 1U].position;

        StepSpringBoneTip(simulator.tips[segment], simulator, pivot, restTailWorld, deltaTimeSeconds);
        for (const SpringBoneObstacle& obstacle : obstacles) {
            static_cast<void>(ResolveSpringBoneSphereCollision(
                simulator.tips[segment], pivot, obstacle.center, obstacle.radius, simulator.hitRadius));
        }

        const Math::Vector3 restDirection =
            NormalizeOr(restTailWorld - boneFrame.position, Math::Vector3{0.0F, -1.0F, 0.0F});
        Math::Vector3 simulatedDirection =
            NormalizeOr(simulator.tips[segment].position - pivot, restDirection);
        float bendRadians = 0.0F;
        const Math::Quaternion bend = FromToRotation(restDirection, simulatedDirection, bendRadians);
        if (bendRadians > limitRadians) {
            // Clamp inside the cone around the animated direction, then move the tip onto the
            // clamped direction: the state must agree with the pose we are about to write.
            const float alpha = (bendRadians > 0.0F) ? (limitRadians / bendRadians) : 0.0F;
            Math::Quaternion clamped;
            if (!Math::TrySlerp(Math::Quaternion{}, bend, alpha, clamped)) {
                return static_cast<std::uint32_t>(segment);
            }
            simulatedDirection = NormalizeOr(Math::RotateVector(clamped, restDirection), restDirection);
            simulator.tips[segment].position = pivot + simulatedDirection * linkLength;
        }

        float appliedRadians = 0.0F;
        const Math::Quaternion bendApplied =
            FromToRotation(restDirection, simulatedDirection, appliedRadians);
        ObjectWorldFrame parentFrame = skeletonWorldFrame;
        const std::int32_t parentIndex = skeleton.bones[boneIndex].parentIndex;
        if (parentIndex >= 0 &&
            !TryResolveSkeletonBoneWorldFrame(skeleton, static_cast<std::uint32_t>(parentIndex),
                                                 skeletonWorldFrame, parentFrame)) {
            return static_cast<std::uint32_t>(segment);
        }
        Math::Quaternion parentInverse;
        if (!Math::TryInverse(parentFrame.rotation, parentInverse)) {
            return static_cast<std::uint32_t>(segment);
        }
        const Math::Quaternion localRotation =
            Math::Multiply(parentInverse, Math::Multiply(bendApplied, boneFrame.rotation));
        Math::Quaternion normalized;
        if (!Math::TryNormalize(localRotation, normalized)) {
            return static_cast<std::uint32_t>(segment);
        }
        skeleton.pose[boneIndex].rotation = normalized;
    }
    simulator.resolvedBoneCount = segmentCount;
    return segmentCount;
}

} // namespace

bool IsSpringBoneCollision3DComponentValid(
    const SpringBoneCollision3DComponent& value) noexcept {
    return IsFinite3DObjectVector(value.centerOffset) && std::isfinite(value.radius) &&
           value.radius > 0.0F;
}

SpringBoneCollision3DComponent SanitizeSpringBoneCollision3DComponent(
    const SpringBoneCollision3DComponent& value) noexcept {
    SpringBoneCollision3DComponent out = value;
    if (!IsFinite3DObjectVector(out.centerOffset)) {
        out.centerOffset = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(out.radius) || out.radius <= 0.0F) {
        out.radius = 0.25F;
    }
    return out;
}

bool IsSpringBoneCollision3DObjectDefinitionValid(
    const SpringBoneCollision3DObjectDefinition& value) noexcept {
    return IsSpringBoneCollision3DComponentValid(value.collision);
}

void ApplySpringBoneCollision3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const SpringBoneCollision3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              SpringBoneCollision3DObjectDefinition::defaultName);
    entityManager.AddComponent<SpringBoneCollision3DComponent>(
        entity, SanitizeSpringBoneCollision3DComponent(value.collision));
}

bool IsSpringBoneSimulator3DComponentValid(
    const SpringBoneSimulator3DComponent& value) noexcept {
    return IsBounded3DObjectString(value.boneName) &&
           IsFinite3DObjectVector(value.tipOffsetLocal) &&
           Math::LengthSquared(value.tipOffsetLocal) > 1.0e-8F &&
           value.chainLength >= 1U && value.chainLength <= kMaximumSpringBoneChainLength &&
           std::isfinite(value.stiffness) && value.stiffness >= 0.0F &&
           std::isfinite(value.damping) && value.damping >= 0.0F && value.damping <= 1.0F &&
           std::isfinite(value.gravityScale) &&
           IsFinite3DObjectVector(value.gravityDirection) &&
           Math::LengthSquared(value.gravityDirection) > 1.0e-8F &&
           std::isfinite(value.hitRadius) && value.hitRadius >= 0.0F &&
           std::isfinite(value.limitAngleDegrees) && value.limitAngleDegrees >= 0.0F &&
           value.limitAngleDegrees <= 180.0F;
}

SpringBoneSimulator3DComponent SanitizeSpringBoneSimulator3DComponent(
    const SpringBoneSimulator3DComponent& value) noexcept {
    SpringBoneSimulator3DComponent out = value;
    if (!IsBounded3DObjectString(out.boneName)) {
        out.boneName.resize(kMaximum3DObjectStringLength);
    }
    if (!IsFinite3DObjectVector(out.tipOffsetLocal) ||
        Math::LengthSquared(out.tipOffsetLocal) <= 1.0e-8F) {
        out.tipOffsetLocal = Math::Vector3{0.0F, -0.25F, 0.0F};
    }
    if (out.chainLength < 1U) {
        out.chainLength = 1U;
    } else if (out.chainLength > kMaximumSpringBoneChainLength) {
        out.chainLength = static_cast<std::uint32_t>(kMaximumSpringBoneChainLength);
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
    if (!IsFinite3DObjectVector(out.gravityDirection) ||
        Math::LengthSquared(out.gravityDirection) <= 1.0e-8F) {
        out.gravityDirection = Math::Vector3{0.0F, -1.0F, 0.0F};
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

void StepSpringBoneTip(SpringBoneTipState& tip,
                          const SpringBoneSimulator3DComponent& simulator,
                          const Math::Vector3& pivotWorldPosition,
                          const Math::Vector3& restTipWorldPosition,
                          const float deltaTimeSeconds) noexcept {
    if (!IsFinite3DObjectVector(pivotWorldPosition) ||
        !IsFinite3DObjectVector(restTipWorldPosition)) {
        return;
    }
    if (!IsFinite3DObjectVector(tip.position) || !IsFinite3DObjectVector(tip.velocity)) {
        tip.position = restTipWorldPosition;
        tip.velocity = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return;
    }
    const float dt = std::min(deltaTimeSeconds, 0.1F);
    const Math::Vector3 gravDir =
        NormalizeOr(simulator.gravityDirection, Math::Vector3{0.0F, -1.0F, 0.0F});
    const Math::Vector3 springForce =
        (restTipWorldPosition - tip.position) * simulator.stiffness + gravDir * simulator.gravityScale;
    const float decay =
        std::exp(-std::clamp(simulator.damping, 0.0F, 1.0F) * kSpringBoneDampingRate * dt);
    tip.velocity = tip.velocity * decay + springForce * dt;
    Math::Vector3 nextTip = tip.position + tip.velocity * dt;

    const float linkLength = Math::Length(restTipWorldPosition - pivotWorldPosition);
    if (linkLength > kMinimumSpringLinkLength) {
        const Math::Vector3 fromPivot = nextTip - pivotWorldPosition;
        const float distance = std::sqrt(Math::LengthSquared(fromPivot));
        if (distance > kMinimumSpringLinkLength) {
            nextTip = pivotWorldPosition + fromPivot * (linkLength / distance);
        } else {
            nextTip = restTipWorldPosition;
        }
    }
    tip.position = nextTip;
}

bool ResolveSpringBoneSphereCollision(SpringBoneTipState& tip,
                                         const Math::Vector3& pivotWorldPosition,
                                         const Math::Vector3& sphereCenterWorld,
                                         const float sphereRadius, const float hitRadius) noexcept {
    if (!IsFinite3DObjectVector(tip.position) || !std::isfinite(sphereRadius) ||
        sphereRadius <= 0.0F) {
        return false;
    }
    const float combinedRadius = sphereRadius + std::max(0.0F, hitRadius);
    const Math::Vector3 delta = tip.position - sphereCenterWorld;
    const float distanceSquared = Math::LengthSquared(delta);
    if (distanceSquared >= combinedRadius * combinedRadius) {
        return false;
    }
    const Math::Vector3 boneDirection = tip.position - pivotWorldPosition;
    const float linkLength = std::sqrt(Math::LengthSquared(boneDirection));
    Math::Vector3 pushNormal = (distanceSquared > 1.0e-8F)
                                      ? delta * (1.0F / std::sqrt(distanceSquared))
                                      : Math::Vector3{0.0F, 0.0F, 1.0F};
    if (linkLength > kMinimumSpringLinkLength) {
        const Math::Vector3 unitBone = boneDirection * (1.0F / linkLength);
        if (std::fabs(Math::Dot(pushNormal, unitBone)) > 0.99F) {
            const Math::Vector3 fallbackAxis =
                (std::fabs(unitBone.x) < 0.9F) ? Math::Vector3{1.0F, 0.0F, 0.0F}
                                               : Math::Vector3{0.0F, 0.0F, 1.0F};
            pushNormal = NormalizeOr(Math::Cross(unitBone, fallbackAxis),
                                        Math::Vector3{0.0F, 1.0F, 0.0F});
        }
    }
    Math::Vector3 pushed = sphereCenterWorld + pushNormal * combinedRadius;
    if (linkLength > kMinimumSpringLinkLength) {
        const Math::Vector3 fromPivot = pushed - pivotWorldPosition;
        const float fromPivotSquared = Math::LengthSquared(fromPivot);
        if (fromPivotSquared > 1.0e-8F) {
            pushed = pivotWorldPosition + fromPivot * (linkLength / std::sqrt(fromPivotSquared));
        }
    }
    tip.position = pushed;
    // Bleed the velocity that was driving the tip in: without this the spring keeps pushing the
    // tip through the sphere every step and the chain buzzes against it.
    const float intoSurface = Math::Dot(tip.velocity, pushNormal);
    if (intoSurface < 0.0F) {
        tip.velocity = tip.velocity - pushNormal * intoSurface;
    }
    return true;
}

bool IsSpringBoneSimulator3DObjectDefinitionValid(
    const SpringBoneSimulator3DObjectDefinition& value) noexcept {
    return IsSpringBoneSimulator3DComponentValid(value.simulator);
}

void ApplySpringBoneSimulator3DObjectDefinition(
    IEntityManager& entityManager, const Entity entity,
    const SpringBoneSimulator3DObjectDefinition& value) {
    ApplyBoneModifier3DBase(entityManager, entity,
                               SpringBoneSimulator3DObjectDefinition::defaultName);
    entityManager.AddComponent<SpringBoneSimulator3DComponent>(
        entity, SanitizeSpringBoneSimulator3DComponent(value.simulator));
}

std::size_t UpdateSpringBoneSimulator3DObjects(IEntityManager& entityManager,
                                                  const float deltaTimeSeconds) {
    if (!std::isfinite(deltaTimeSeconds) || deltaTimeSeconds <= 0.0F) {
        return 0U;
    }
    std::vector<SpringBoneObstacle> obstacles;
    entityManager.ForEach<SpringBoneCollision3DComponent, WorldTransformComponent>(
        [&](const Entity entity, const SpringBoneCollision3DComponent& collision,
            const WorldTransformComponent& world) {
            if (!collision.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsSpringBoneCollision3DComponentValid(collision) ||
                !IsWorldTransformComponentValid(world)) {
                return;
            }
            obstacles.push_back(SpringBoneObstacle{
                TransformWorldPoint(world, collision.centerOffset), collision.radius});
        });

    std::size_t steppedCount = 0U;
    entityManager.ForEach<SpringBoneSimulator3DComponent>(
        [&](const Entity entity, SpringBoneSimulator3DComponent& simulator) {
            const auto resetRuntimeState = [&simulator]() {
                simulator.initialized = false;
                simulator.resolvedBoneCount = 0U;
                simulator.tips.clear();
                simulator.referenceRotations.clear();
                simulator.writtenRotations.clear();
                simulator.pendingSeconds = 0.0F;
            };
            if (!simulator.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsSpringBoneSimulator3DComponentValid(simulator) ||
                !entityManager.IsAlive(simulator.skeleton) ||
                !entityManager.HasComponent<Skeleton3DComponent>(simulator.skeleton) ||
                !entityManager.HasComponent<WorldTransformComponent>(simulator.skeleton)) {
                resetRuntimeState();
                return;
            }
            Skeleton3DComponent& skeleton =
                entityManager.GetComponent<Skeleton3DComponent>(simulator.skeleton);
            const WorldTransformComponent& skeletonWorld =
                entityManager.GetComponent<WorldTransformComponent>(simulator.skeleton);
            if (!skeleton.enabled || !IsSkeleton3DObjectComponentValid(skeleton) ||
                !IsWorldTransformComponentValid(skeletonWorld)) {
                resetRuntimeState();
                return;
            }
            std::uint32_t resolvedBone = kInvalidSkeletonBoneIndex;
            if (!TryFindSkeletonBoneIndex(skeleton, simulator.boneName, resolvedBone)) {
                resetRuntimeState();
                return;
            }
            simulator.boneIndex = resolvedBone;
            std::vector<std::uint32_t> chain;
            if (ResolveSpringChain(skeleton, resolvedBone, simulator.chainLength, chain) == 0U) {
                resetRuntimeState();
                return;
            }
            EnsureSkeletonPose(skeleton);
            // The pose as animation left it, captured once for the whole update: every fixed step
            // re-applies it, so the simulation is the only state carried between steps. The pose
            // still holds OUR last answer when no animation is running, so only a rotation that
            // differs from what the solver wrote is treated as a fresh reference.
            simulator.referenceRotations.resize(chain.size());
            simulator.writtenRotations.resize(chain.size());
            for (std::size_t segment = 0U; segment < chain.size(); ++segment) {
                const Math::Quaternion current = skeleton.pose[chain[segment]].rotation;
                if (!simulator.initialized || simulator.writtenRotations[segment] != current) {
                    simulator.referenceRotations[segment] = current;
                }
            }
            const ObjectWorldFrame skeletonFrame{skeletonWorld.worldPosition,
                                                    skeletonWorld.worldRotation,
                                                    skeletonWorld.worldScale};

            // Whole steps only, and a bounded number of them: the leftover fraction is kept for
            // the next frame instead of being spent as an irregular step, which is what makes the
            // motion depend on elapsed time rather than on the frame rate.
            simulator.pendingSeconds += deltaTimeSeconds;
            simulator.pendingSeconds =
                std::min(simulator.pendingSeconds,
                         kSpringBoneFixedStepSeconds *
                             static_cast<float>(kMaximumSpringBoneSubsteps));
            std::uint32_t stepsTaken = 0U;
            while (simulator.pendingSeconds >= kSpringBoneFixedStepSeconds &&
                   stepsTaken < kMaximumSpringBoneSubsteps) {
                if (SolveSpringBoneChainStep(simulator, skeleton, skeletonFrame, chain,
                                                simulator.referenceRotations, obstacles,
                                                kSpringBoneFixedStepSeconds) == 0U) {
                    resetRuntimeState();
                    return;
                }
                simulator.pendingSeconds -= kSpringBoneFixedStepSeconds;
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
