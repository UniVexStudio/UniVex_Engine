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

bool IsPhysicalBone3DComponentValid(const PhysicalBone3DComponent& value) noexcept {
    return value.boneIndex >= 0 && std::isfinite(value.mass) && value.mass > 0.0F &&
           std::isfinite(value.linearDamping) && value.linearDamping >= 0.0F &&
           value.linearDamping <= 1.0F && IsFinite3DObjectVector(value.linearVelocity);
}

PhysicalBone3DComponent SanitizePhysicalBone3DComponent(
    const PhysicalBone3DComponent& value) noexcept {
    PhysicalBone3DComponent out = value;
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
    if (!IsFinite3DObjectVector(out.linearVelocity)) {
        out.linearVelocity = Math::Vector3{0.0F, 0.0F, 0.0F};
    }
    return out;
}

bool IsPhysicalBone3DObjectDefinitionValid(
    const PhysicalBone3DObjectDefinition& value) noexcept {
    return IsPhysicalBone3DComponentValid(value.physicalBone);
}

void ApplyPhysicalBone3DObjectDefinition(IEntityManager& entityManager,
                                            const Entity entity,
                                            const PhysicalBone3DObjectDefinition& value) {
    EnsureObject3DBaseline(entityManager, entity,
                              PhysicalBone3DObjectDefinition::defaultName);
    entityManager.AddComponent<PhysicalBone3DComponent>(
        entity, SanitizePhysicalBone3DComponent(value.physicalBone));
}

std::size_t UpdatePhysicalBone3DObjects(IEntityManager& entityManager,
                                           const Math::Vector3& gravity,
                                           const float deltaTimeSeconds) {
    std::size_t updatedCount = 0U;
    const Math::Vector3 safeGravity = IsFinite3DObjectVector(gravity)
                                             ? gravity
                                             : Math::Vector3{0.0F, -9.81F, 0.0F};
    const bool canStep = std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F;

    std::vector<Entity> boneEntities;
    bool anySimulating = false;
    entityManager.ForEach<PhysicalBone3DComponent, TransformComponent>(
        [&](const Entity entity, PhysicalBone3DComponent& pb, TransformComponent&) {
            if (!pb.enabled || entityManager.HasComponent<DisabledComponent>(entity) ||
                !IsPhysicalBone3DComponentValid(pb)) {
                return;
            }
            boneEntities.push_back(entity);
            if (pb.simulatePhysics) anySimulating = true;
        });
    if (boneEntities.empty()) return 0U;

    std::vector<Physics::Detail::ColliderWorldAabb> colliders;
    if (anySimulating && canStep) {
        colliders = Physics::Detail::BuildColliderWorldAabbCache(entityManager);
    }

    for (const Entity entity : boneEntities) {
        if (!entityManager.IsAlive(entity) ||
            !entityManager.HasComponent<PhysicalBone3DComponent>(entity) ||
            !entityManager.HasComponent<TransformComponent>(entity)) {
            continue;
        }
        PhysicalBone3DComponent& pb = entityManager.GetComponent<PhysicalBone3DComponent>(entity);
        TransformComponent& localTransform = entityManager.GetComponent<TransformComponent>(entity);

        Skeleton3DComponent* skeleton = nullptr;
        if (pb.skeletonEntity != kInvalidEntity &&
            entityManager.IsAlive(pb.skeletonEntity) &&
            entityManager.HasComponent<Skeleton3DComponent>(pb.skeletonEntity)) {
            skeleton = &entityManager.GetComponent<Skeleton3DComponent>(pb.skeletonEntity);
        }
        const std::size_t idx = static_cast<std::size_t>(pb.boneIndex);
        const bool validBone = skeleton != nullptr && idx < skeleton->bones.size();

        if (!pb.simulatePhysics) {
            if (validBone) {
                const SkeletonBone& bone = skeleton->bones[idx];
                const bool hasRuntimePose = idx < skeleton->pose.size();
                localTransform.localPosition =
                    hasRuntimePose ? skeleton->pose[idx].position : bone.localPosition;
                localTransform.localRotation =
                    hasRuntimePose ? skeleton->pose[idx].rotation : bone.localRotation;
                if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
                    entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
                }
                ++updatedCount;
            }
            continue;
        }

        if (!canStep) continue;

        // Delegate physical bone stepping internally to the RagdollSolver + world contact pipeline.
        Physics::RagdollBody body;
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
        body.angularVelocity = Math::Vector3{};

        Physics::RagdollSolver solver;
        const std::array<Physics::RagdollBody, 1U> singleBody{body};
        if (!solver.Reset(singleBody)) continue;

        Physics::RagdollCollisionFilter filter;
        filter.ignoreEntity = entity;
        filter.alsoIgnoreEntity = pb.skeletonEntity;

        Physics::RagdollStepSettings stepSettings;
        stepSettings.velocityIterations = 4U;
        stepSettings.positionIterations = 2U;
        stepSettings.sleepDelaySeconds = 0.0F;
        stepSettings.enableSelfCollision = false;

        constexpr std::size_t kContactCap = 16U;
        std::array<Physics::RagdollContact, kContactCap> contacts{};
        const float clampedDt = std::clamp(deltaTimeSeconds, 1.0e-5F, 0.1F);
        const std::uint32_t substeps = std::clamp<std::uint32_t>(
            static_cast<std::uint32_t>(std::ceil(clampedDt / Physics::kRagdollFixedStepSeconds)), 1U, 4U);
        const float subDt = clampedDt / static_cast<float>(substeps);

        for (std::uint32_t s = 0U; s < substeps; ++s) {
            Physics::RagdollContactStats stats{};
            if (!colliders.empty()) {
                stats = Physics::BuildRagdollContacts(solver, colliders, filter,
                                                         std::span<Physics::RagdollContact>{contacts});
            }
            static_cast<void>(solver.Step(
                safeGravity, subDt,
                std::span<const Physics::RagdollContact>{contacts.data(), stats.generatedContactCount},
                stepSettings));
        }

        const Physics::RagdollBody& solved = solver.GetBody(0U);
        pb.linearVelocity = solved.linearVelocity;
        localTransform.localPosition = solved.position;
        localTransform.localRotation = solved.rotation;
        if (validBone) {
            if (skeleton->pose.size() < skeleton->bones.size()) {
                skeleton->pose = GetSkeletonCurrentPose(*skeleton);
            }
            skeleton->pose[idx].position = solved.position;
            skeleton->pose[idx].rotation = solved.rotation;
        }
        if (entityManager.HasComponent<WorldTransformComponent>(entity)) {
            entityManager.GetComponent<WorldTransformComponent>(entity).dirty = true;
        }
        ++updatedCount;
    }
    return updatedCount;
}

} // namespace UVE::Scene
