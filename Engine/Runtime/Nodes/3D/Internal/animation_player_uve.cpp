// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_player_uve.h"

#include "uve/entity/i_entity_manager_uve.h"
#include "uve/nodes/3d/node_3d_uve.h"

namespace UVE::Scene {

bool IsAnimationPlayerNodeDefinitionValidUVE(const AnimationPlayerNodeDefinitionUVE& value) noexcept {
    return IsAnimationPlayerComponentValidUVE(value.player);
}

void ApplyAnimationPlayerNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const AnimationPlayerNodeDefinitionUVE& value) {
    // AnimationPlayer is Node3D plus its own components: the shared baseline guarantee comes first,
    // then this kind's part goes on top.
    EnsureNode3DBaselineUVE(entityManager, entity, AnimationPlayerNodeDefinitionUVE::defaultName);
    entityManager.AddComponentUVE<AnimationPlayerComponentUVE>(entity, value.player);
}

} // namespace UVE::Scene
