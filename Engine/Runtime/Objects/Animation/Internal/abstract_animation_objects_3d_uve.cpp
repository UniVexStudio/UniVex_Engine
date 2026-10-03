// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/objects/3d/abstract_animation_objects_3d_uve.h"

#include "uve/component/animated_object_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
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

void ApplyAnimatedObjectBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const std::string_view nameFallback) {
    EnsureObjectBaselineUVE(entityManager, entity, nameFallback);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimatedObjectComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimatedObjectComponentUVE>(entity, AnimatedObjectComponentUVE{});
    }
}

} // namespace UVE::Scene
