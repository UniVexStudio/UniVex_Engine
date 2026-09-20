// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#include "uve/nodes/3d/animation_tree_uve.h"

namespace UVE::Scene {

bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) noexcept {
    // An empty graph is the valid authored default for a node kind that cannot be created or
    // consumed yet (the registry marks AnimationTree libraryCreatable=false until the real
    // skeleton/skinning/clip-sampling pipeline exists — see SCENE_NODES_ROADMAP.md). Once a
    // graph IS authored, it must pass the animation module's own full validation.
    if (value.tree.nodes.empty()) {
        return true;
    }
    return Core::ValidateAnimationTreeUVE(value.tree).IsValidUVE();
}

} // namespace UVE::Scene
