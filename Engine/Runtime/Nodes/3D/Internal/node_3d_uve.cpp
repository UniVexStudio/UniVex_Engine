// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/node_3d_uve.h"

#include "uve/entity/i_entity_manager_uve.h"

namespace UVE::Scene {

bool IsNode3DNodeDefinitionValidUVE(const Node3DNodeDefinitionUVE& /*value*/) noexcept {
    // Node3D carries no authored data - a definition with default-initialized (that is,
    // absent) fields is always valid. The validator exists so the kind keeps the same
    // validate-before-apply seam as every other node kind.
    return true;
}

void ApplyNode3DNodeDefinitionUVE(IEntityManagerUVE& /*entityManager*/, const EntityUVE /*entity*/,
                                  const Node3DNodeDefinitionUVE& /*value*/) noexcept {
    // Recipe placeholder for the rename slice: the creation shell has already attached the
    // entity's Transform/WorldTransform/Hierarchy/Name. The next slice turns this into the
    // baseline guarantee the recipe actually promises.
}

} // namespace UVE::Scene
