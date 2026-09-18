// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_player_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsAnimationPlayerNodeDefinitionValidUVE(const AnimationPlayerNodeDefinitionUVE& value) noexcept {
    return IsAnimationPlayerComponentValidUVE(value.player);
}

void ApplyAnimationPlayerNodeDefinitionUVE(IEntityManagerUVE& entityManager, const EntityUVE entity, const AnimationPlayerNodeDefinitionUVE& value) {
    entityManager.AddComponentUVE<AnimationPlayerComponentUVE>(entity, value.player);
}

} // namespace UVE::Scene
