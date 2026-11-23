// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "physical_bone_3d.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <span>
#include <vector>

#include "disabled_component.h"
#include "transform_component.h"
#include "world_transform_component.h"
#include "i_entity_manager.h"
#include "object_3d.h"
#include "skeleton_3d.h"
#include "collider_world_aabb_cache.h"
#include "ragdoll_solver.h"
#include "ragdoll_world.h"

namespace UVE::Scene {

bool IsPhysicalBone3DComponentValidUVE(const PhysicalBone3DComponentUVE& value) noexcept {
    return value.boneIndex >= 0 && std::isfinite(value.mass) && value.mass > 0.0F &&
           std::isfinite(value.linearDamping) && value.linearDamping >= 0.0F &&
           value.linearDamping <= 1.0F && IsFinite3DObjectVectorUVE(value.linearVelocity);
}

PhysicalBone3DComponentUVE SanitizePhysicalBone3DComponentUVE(
    const PhysicalBone3DComponentUVE& value) noexcept {
    PhysicalBone3DComponentUVE out = value;
    if (out.boneIndex < 0) {
        out.boneIndex = 0;
    }
    if (!std::isfinite(out.mass) || out.mass <= 0.0F) {
        out.mass = 5.0F;
    }
    if (!std::isfinite(out.linearDamping)) {
        out.linearDamping = 0.1F;
    } else {
        out.linearDamping = std::clamp(out.linearDamping, 0.0F, 1.0F);
    }
    if (!IsFinite3DObjectVectorUVE(out.linearVelocity)) {
        out.linearVelocity = Math::Vector3UVE{0.0F, 0.0F, 0.0F};
    }
    return out;
}

bool IsPhysicalBone3DObjectDefinitionValidUVE(
    const PhysicalBone3DObjectDefinitionUVE& value) noexcept {
    return IsPhysicalBone3DComponentValidUVE(value.physicalBone);
}

void ApplyPhysicalBone3DObjectDefinitionUVE(IEntityManagerUVE& entityManager,
                                            const EntityUVE entity,
                                            const PhysicalBone3DObjectDefinitionUVE& value) {
    EnsureObject3DBaselineUVE(entityManager, entity,
                              PhysicalBone3DObjectDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<PhysicalBone3DComponentUVE>(
        entity, SanitizePhysicalBone3DComponentUVE(value.physicalBone));
}

std::size_t UpdatePhysicalBone3DObjectsUVE(IEntityManagerUVE& entityManager,
                                           const Math::Vector3UVE& gravity,
                                           const float deltaTimeSeconds) {
    std::size_t updatedCount = 0U;
    const Math::Vector3UVE safeGravity = IsFinite3DObjectVectorUVE(gravity)
                                             ? gravity
                                             : Math::Vector3UVE{0.0F, -9.81F, 0.0F};
    const bool canStep = std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F;

    std::vector<EntityUVE> boneEntities;
    bool anySimulating = false;
    entityManager.ForEachUVE<PhysicalBone3DComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE entity, PhysicalBone3DComponentUVE& pb, TransformComponentUVE&) {
            if (!pb.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsPhysicalBone3DComponentValidUVE(pb)) {
                return;
            }
            boneEntities.push_back(entity);
            if (pb.simulatePhysics) anySimulating = true;
        });
    if (boneEntities.empty()) return 0U;

    std::vector<Physics::Detail::ColliderWorldAabbUVE> colliders;
    if (anySimulating && canStep) {
        colliders = Physics::Detail::BuildColliderWorldAabbCacheUVE(entityManager);
    }

    for (const EntityUVE entity : boneEntities) {
        if (!entityManager.IsAliveUVE(entity) ||
            !entityManager.HasComponentUVE<PhysicalBone3DComponentUVE>(entity) ||
            !entityManager.HasComponentUVE<TransformComponentUVE>(entity)) {
            continue;
        }
        PhysicalBone3DComponentUVE& pb = entityManager.GetComponentUVE<PhysicalBone3DComponentUVE>(entity);
        TransformComponentUVE& localTransform = entityManager.GetComponentUVE<TransformComponentUVE>(entity);

        Skeleton3DComponentUVE* skeleton = nullptr;
        if (pb.skeletonEntity != kInvalidEntityUVE &&
            entityManager.IsAliveUVE(pb.skeletonEntity) &&
            entityManager.HasComponentUVE<Skeleton3DComponentUVE>(pb.skeletonEntity)) {
            skeleton = &entityManager.GetComponentUVE<Skeleton3DComponentUVE>(pb.skeletonEntity);
        }
        const std::size_t idx = static_cast<std::size_t>(pb.boneIndex);
        const bool validBone = skeleton != nullptr && idx < skeleton->bones.size();

        if (!pb.simulatePhysics) {
            if (validBone) {
                const SkeletonBoneUVE& bone = skeleton->bones[idx];
                const bool hasRuntimePose = idx < skeleton->pose.size();
                localTransform.localPosition =
                    hasRuntimePose ? skeleton->pose[idx].position : bone.localPosition;
                localTransform.localRotation =
                    hasRuntimePose ? skeleton->pose[idx].rotation : bone.localRotation;
                if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
                }
                ++updatedCount;
            }
            continue;
        }

        if (!canStep) continue;

        // Delegate physical bone stepping internally to the AAA RagdollSolverUVE + world contact pipeline.
        Physics::RagdollBodyUVE body;
        body.mass = pb.mass;
        body.radius = 0.06F;
        body.halfHeight = 0.08F;
        body.linearDamping = pb.linearDamping;
        body.angularDamping = 0.5F;
        body.friction = 0.8F;
        body.restitution = 0.0F;
        body.position = localTransform.localPosition;
        body.rotation = localTransform.localRotation;
        body.linearVelocity = pb.linearVelocity;
        body.angularVelocity = Math::Vector3UVE{};

        Physics::RagdollSolverUVE solver;
        const std::array<Physics::RagdollBodyUVE, 1U> singleBody{body};
        if (!solver.ResetUVE(singleBody)) continue;

        Physics::RagdollCollisionFilterUVE filter;
        filter.ignoreEntity = entity;
        filter.alsoIgnoreEntity = pb.skeletonEntity;

        Physics::RagdollStepSettingsUVE stepSettings;
        stepSettings.velocityIterations = 4U;
        stepSettings.positionIterations = 2U;
        stepSettings.sleepDelaySeconds = 0.0F;
        stepSettings.enableSelfCollision = false;

        constexpr std::size_t kContactCap = 16U;
        std::array<Physics::RagdollContactUVE, kContactCap> contacts{};
        const float clampedDt = std::clamp(deltaTimeSeconds, 1.0e-5F, 0.1F);
        const std::uint32_t substeps = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::ceil(clampedDt / Physics::kRagdollFixedStepSecondsUVE)), 1U, 4U);
        const float subDt = clampedDt / static_cast<float>(substeps);

        for (std::uint32_t s = 0U; s < substeps; ++s) {
            Physics::RagdollContactStatsUVE stats{};
            if (!colliders.empty()) {
                stats = Physics::BuildRagdollContactsUVE(solver, colliders, filter,
                                                         std::span<Physics::RagdollContactUVE>{contacts});
            }
            static_cast<void>(solver.StepUVE(
                safeGravity, subDt,
                std::span<const Physics::RagdollContactUVE>{contacts.data(), stats.generatedContactCount},
                stepSettings));
        }

        const Physics::RagdollBodyUVE& solved = solver.GetBodyUVE(0U);
        pb.linearVelocity = solved.linearVelocity;
        localTransform.localPosition = solved.position;
        localTransform.localRotation = solved.rotation;
        if (validBone) {
            if (skeleton->pose.size() < skeleton->bones.size()) {
                skeleton->pose = GetSkeletonCurrentPoseUVE(*skeleton);
            }
            skeleton->pose[idx].position = solved.position;
            skeleton->pose[idx].rotation = solved.rotation;
        }
        if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
            entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
