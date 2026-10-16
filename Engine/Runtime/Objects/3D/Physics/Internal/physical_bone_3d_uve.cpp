// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/physical_bone_3d_uve.h"

#include <algorithm>
#include <cmath>

#include "uve/component/disabled_component_uve.h"
#include "uve/component/transform_component_uve.h"
#include "uve/component/world_transform_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/object_3d_uve.h"
#include "uve/objects/3d/skeleton_3d_uve.h"

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
    entityManager.ForEachUVE<PhysicalBone3DComponentUVE, TransformComponentUVE>(
        [&](const EntityUVE entity, PhysicalBone3DComponentUVE& pb,
            TransformComponentUVE& localTransform) {
            if (!pb.enabled || entityManager.HasComponentUVE<DisabledComponentUVE>(entity) ||
                !IsPhysicalBone3DComponentValidUVE(pb)) {
                return;
            }
            Skeleton3DComponentUVE* skeleton = nullptr;
            if (pb.skeletonEntity != kInvalidEntityUVE &&
                entityManager.IsAliveUVE(pb.skeletonEntity) &&
                entityManager.HasComponentUVE<Skeleton3DComponentUVE>(pb.skeletonEntity)) {
                skeleton =
                    &entityManager.GetComponentUVE<Skeleton3DComponentUVE>(pb.skeletonEntity);
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
                        entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty =
                            true;
                    }
                    ++updatedCount;
                }
                return;
            }

            if (std::isfinite(deltaTimeSeconds) && deltaTimeSeconds > 0.0F) {
                const float dampFactor = std::clamp(1.0F - pb.linearDamping * deltaTimeSeconds,
                                                    0.0F, 1.0F);
                pb.linearVelocity =
                    (pb.linearVelocity + safeGravity * deltaTimeSeconds) * dampFactor;
                localTransform.localPosition =
                    localTransform.localPosition + pb.linearVelocity * deltaTimeSeconds;
                if (validBone) {
                    if (skeleton->pose.size() < skeleton->bones.size()) {
                        skeleton->pose = GetSkeletonCurrentPoseUVE(*skeleton);
                    }
                    skeleton->pose[idx].position = localTransform.localPosition;
                }
                if (entityManager.HasComponentUVE<WorldTransformComponentUVE>(entity)) {
                    entityManager.GetComponentUVE<WorldTransformComponentUVE>(entity).dirty = true;
                }
                ++updatedCount;
            }
        });
    return updatedCount;
}

} // namespace UVE::Scene
