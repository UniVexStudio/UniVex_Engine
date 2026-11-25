// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "abstract_animation_objects_3d.h"

#include <algorithm>

#include "animation_driver_component.h"
#include "bone_modifier_component.h"
#include "hierarchy_component.h"
#include "i_entity_manager.h"
#include "abstract_objects_3d.h"
#include "object_3d.h"

namespace UVE::Scene {

void ApplyBoneModifier3DBase(IEntityManager& entityManager, const Entity entity,
                                const std::string_view nameFallback) {
    if (!entityManager.IsAlive(entity)) {
        return;
    }
    ApplyObject3DRecipe(entityManager, entity, nameFallback);
    if (!entityManager.HasComponent<BoneModifierComponent>(entity)) {
        entityManager.AddComponent<BoneModifierComponent>(entity, BoneModifierComponent{});
    }
}

void ApplyAnimationDriverBase(IEntityManager& entityManager, const Entity entity,
                                 const std::string_view nameFallback) {
    EnsureObjectBaseline(entityManager, entity, nameFallback);
    if (entityManager.IsAlive(entity) && !entityManager.HasComponent<AnimationDriverComponent>(entity)) {
        entityManager.AddComponent<AnimationDriverComponent>(entity, AnimationDriverComponent{});
    }
}

float ResolveBoneModifierInfluence(const IEntityManager& entityManager,
                                      const Entity entity) noexcept {
    if (!entityManager.IsAlive(entity)) {
        return 0.0F;
    }
    if (!entityManager.HasComponent<BoneModifierComponent>(entity)) {
        return 1.0F;
    }
    const BoneModifierComponent& modifier =
        entityManager.GetComponent<BoneModifierComponent>(entity);
    if (!IsBoneModifierComponentValid(modifier)) {
        return 0.0F;
    }
    return ResolveEffectiveBoneModifierWeight(&modifier);
}

bool IsBoneModifierActive(const IEntityManager& entityManager,
                             const Entity entity) noexcept {
    if (!entityManager.IsAlive(entity)) {
        return false;
    }
    if (!entityManager.HasComponent<BoneModifierComponent>(entity)) {
        return true;
    }
    return IsBoneModifierEffective(
        entityManager.GetComponent<BoneModifierComponent>(entity));
}

AnimationDriverComponent ResolveAnimationDriver(const IEntityManager& entityManager,
                                                      const Entity entity) noexcept {
    if (entityManager.IsAlive(entity) &&
        entityManager.HasComponent<AnimationDriverComponent>(entity)) {
        return entityManager.GetComponent<AnimationDriverComponent>(entity);
    }
    return AnimationDriverComponent{};
}

bool IsAnimationDriverRunningOnClock(const AnimationDriverComponent& driver,
                                        const AnimationProcessCallback callback) noexcept {
    return IsAnimationDriverComponentValid(driver) &&
           ShouldStepAnimationDriverOnClock(driver, callback);
}

Entity ResolveAnimationDriverTargetEntity(
    const IEntityManager& entityManager, const Entity driverEntity,
    const AnimationDriverComponent& driver) noexcept {
    const Entity parent =
        (entityManager.IsAlive(driverEntity) &&
         entityManager.HasComponent<HierarchyComponent>(driverEntity))
            ? entityManager.GetComponent<HierarchyComponent>(driverEntity).parent
            : kInvalidEntity;
    const Entity candidate = ResolveAnimationDriverTargetEntity(driver, parent);
    if (candidate != kInvalidEntity && entityManager.IsAlive(candidate)) {
        return candidate;
    }
    if (parent != kInvalidEntity && entityManager.IsAlive(parent)) {
        return parent;
    }
    return kInvalidEntity;
}

} // namespace UVE::Scene
