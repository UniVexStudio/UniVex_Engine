// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/empty_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsEmptyNodeDefinitionValidUVE(const EmptyNodeDefinitionUVE& /*value*/) noexcept {
    // Empty carries no authored data — a definition with default-initialized (that is, absent)
    // fields is always valid. The validator exists so the kind keeps the same
    // validate-before-apply seam as every other node kind.
    return true;
}

void ApplyEmptyNodeDefinitionUVE(IEntityManagerUVE& /*entityManager*/, const EntityUVE /*entity*/,
                                 const EmptyNodeDefinitionUVE& /*value*/) noexcept {
    // Transform-only by design: the creation shell has already attached the entity's
    // TransformComponentUVE and NameComponentUVE, and Empty adds nothing on top.
}

} // namespace UVE::Scene
