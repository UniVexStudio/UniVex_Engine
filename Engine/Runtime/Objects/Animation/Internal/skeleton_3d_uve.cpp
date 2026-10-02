// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/skeleton_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"

namespace UVE::Scene {

std::vector<SkeletonBonePoseUVE> GetSkeletonCurrentPoseUVE(const Skeleton3DComponentUVE& skeleton) {
    if (skeleton.pose.size() == skeleton.bones.size()) {
        return skeleton.pose;
    }
    std::vector<SkeletonBonePoseUVE> rest;
    rest.reserve(skeleton.bones.size());
    for (const SkeletonBoneUVE& bone : skeleton.bones) {
        rest.push_back(SkeletonBonePoseUVE{bone.localPosition, bone.localRotation, bone.localScale});
    }
    return rest;
}

bool IsSkeleton3DObjectComponentValidUVE(const Skeleton3DComponentUVE& value) noexcept {
    if (!IsBounded3DObjectStringUVE(value.skeletonAssetPath) || value.bones.size() > kMaximumSkeletonBonesUVE) {
        return false;
    }
    for (std::size_t index = 0U; index < value.bones.size(); ++index) {
        const SkeletonBoneUVE& bone = value.bones[index];
        if (!IsBounded3DObjectStringUVE(bone.name, false) || !IsFinite3DObjectVectorUVE(bone.localPosition) ||
            !IsFinite3DObjectQuaternionUVE(bone.localRotation) || !IsFinite3DObjectVectorUVE(bone.localScale) ||
            bone.localScale.x <= 0.0F || bone.localScale.y <= 0.0F || bone.localScale.z <= 0.0F ||
            (bone.parentIndex >= 0 && static_cast<std::size_t>(bone.parentIndex) >= index)) {
            return false;
        }
        for (std::size_t previous = 0U; previous < index; ++previous) {
            if (value.bones[previous].name == bone.name) {
                return false;
            }
        }
    }
    return true;
}

void ApplySkeleton3DObjectDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                      const Skeleton3DObjectDefinitionUVE& value) {
    ApplyObject3DRecipeUVE(entityManager, entity, Skeleton3DObjectDefinitionUVE::defaultName);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<Skeleton3DComponentUVE>(entity)) {
        entityManager.AddComponentUVE<Skeleton3DComponentUVE>(entity, value.skeleton);
    }
}

} // namespace UVE::Scene
