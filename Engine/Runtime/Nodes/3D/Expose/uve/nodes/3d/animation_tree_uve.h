// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/animation/animation_tree_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Scene {

/// Authoring definition for the AnimationTree scene node. AnimationTree is the one registry kind
/// that is deliberately NOT library-creatable yet (SceneNodeDescriptorUVE::libraryCreatable ==
/// false): nothing consumes a tree at runtime until the skeleton/skinning/clip-sampling pipeline
/// exists (see SCENE_NODES_ROADMAP.md's Animation section). This file exists so the kind still
/// has the same per-file home as every other node kind — its authored default (an empty graph)
/// and its validation live here, ready for the day the pipeline lands. Per
/// Engine/Runtime/Scene/README.md's "one truth per concept" rule the graph itself is the
/// existing Core::AnimationTreeUVE, never a second copy.
struct AnimationTreeNodeDefinitionUVE final {
    /// Default document-entity name for a freshly created node of this kind.
    static constexpr std::string_view defaultName = "AnimationTree";

    /// Authored default: an empty graph — a valid placeholder, since authoring cannot start
    /// until the animation pipeline the registry entry is blocked on exists.
    Core::AnimationTreeUVE tree{};
};

[[nodiscard]] bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) noexcept;

// Deliberately no ApplyAnimationTreeNodeDefinitionUVE(): there is no ECS component to attach
// until the animation pipeline exists, and inventing a dead one would violate the registry's
// honest libraryCreatable=false claim. The editor rejects AnimationTree creation up front.

} // namespace UVE::Scene
