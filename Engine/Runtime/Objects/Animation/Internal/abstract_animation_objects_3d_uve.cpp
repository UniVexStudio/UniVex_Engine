// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"

#include <algorithm>

#include "uve/component/animation_driver_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/component/hierarchy_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/objects/3d/abstract_objects_3d_uve.h"
#include "uve/objects/3d/object_3d_uve.h"

namespace UVE::Scene {

void ApplyBoneModifier3DBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const std::string_view nameFallback) {
    if (!entityManager.IsAliveUVE(entity)) {
        return;
    }
    ApplyObject3DRecipeUVE(entityManager, entity, nameFallback);
    if (!entityManager.HasComponentUVE<BoneModifierComponentUVE>(entity)) {
        entityManager.AddComponentUVE<BoneModifierComponentUVE>(entity, BoneModifierComponentUVE{});
    }
}

void ApplyAnimationDriverBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                 const std::string_view nameFallback) {
    EnsureObjectBaselineUVE(entityManager, entity, nameFallback);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationDriverComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationDriverComponentUVE>(entity, AnimationDriverComponentUVE{});
    }
}

float ResolveBoneModifierInfluenceUVE(const IEntityManagerUVE& entityManager,
                                      const EntityUVE entity) noexcept {
    if (!entityManager.IsAliveUVE(entity)) {
        return 0.0F;
    }
    if (!entityManager.HasComponentUVE<BoneModifierComponentUVE>(entity)) {
        return 1.0F;
    }
    const BoneModifierComponentUVE& modifier =
        entityManager.GetComponentUVE<BoneModifierComponentUVE>(entity);
    if (!IsBoneModifierComponentValidUVE(modifier)) {
        return 0.0F;
    }
    return ResolveEffectiveBoneModifierWeightUVE(&modifier);
}

bool IsBoneModifierActiveUVE(const IEntityManagerUVE& entityManager,
                             const EntityUVE entity) noexcept {
    if (!entityManager.IsAliveUVE(entity)) {
        return false;
    }
    if (!entityManager.HasComponentUVE<BoneModifierComponentUVE>(entity)) {
        return true;
    }
    return IsBoneModifierEffectiveUVE(
        entityManager.GetComponentUVE<BoneModifierComponentUVE>(entity));
}

AnimationDriverComponentUVE ResolveAnimationDriverUVE(const IEntityManagerUVE& entityManager,
                                                      const EntityUVE entity) noexcept {
    if (entityManager.IsAliveUVE(entity) &&
        entityManager.HasComponentUVE<AnimationDriverComponentUVE>(entity)) {
        return entityManager.GetComponentUVE<AnimationDriverComponentUVE>(entity);
    }
    return AnimationDriverComponentUVE{};
}

bool IsAnimationDriverRunningOnClockUVE(const AnimationDriverComponentUVE& driver,
                                        const AnimationProcessCallbackUVE callback) noexcept {
    return IsAnimationDriverComponentValidUVE(driver) &&
           ShouldStepAnimationDriverOnClockUVE(driver, callback);
}

EntityUVE ResolveAnimationDriverTargetEntityUVE(
    const IEntityManagerUVE& entityManager, const EntityUVE driverEntity,
    const AnimationDriverComponentUVE& driver) noexcept {
    const EntityUVE parent =
        (entityManager.IsAliveUVE(driverEntity) &&
         entityManager.HasComponentUVE<HierarchyComponentUVE>(driverEntity))
            ? entityManager.GetComponentUVE<HierarchyComponentUVE>(driverEntity).parent
            : kInvalidEntityUVE;
    const EntityUVE candidate = ResolveAnimationDriverTargetEntityUVE(driver, parent);
    if (candidate != kInvalidEntityUVE && entityManager.IsAliveUVE(candidate)) {
        return candidate;
    }
    if (parent != kInvalidEntityUVE && entityManager.IsAliveUVE(parent)) {
        return parent;
    }
    return kInvalidEntityUVE;
}

} // namespace UVE::Scene
