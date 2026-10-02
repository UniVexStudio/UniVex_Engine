// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/abstract_animation_nodes_3d_uve.h"

#include "uve/component/animation_mixer_component_uve.h"
#include "uve/component/bone_modifier_component_uve.h"
#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/abstract_nodes_3d_uve.h"
#include "uve/nodes/3d/object_3d_uve.h"

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

void ApplyAnimationMixerBaseUVE(IEntityManagerUVE& entityManager, const EntityUVE entity,
                                const std::string_view nameFallback) {
    EnsureNodeBaselineUVE(entityManager, entity, nameFallback);
    if (entityManager.IsAliveUVE(entity) && !entityManager.HasComponentUVE<AnimationMixerComponentUVE>(entity)) {
        entityManager.AddComponentUVE<AnimationMixerComponentUVE>(entity, AnimationMixerComponentUVE{});
    }
}

} // namespace UVE::Scene
