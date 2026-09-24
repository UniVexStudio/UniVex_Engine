// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <functional>
#include <string_view>

#include "uve/asset/asset_guid_uve.h"
#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManagerUVE;
struct TransformComponentUVE;

/// Authoring definition for the AnimationTree node: a pure Node - no transform, no visibility -
/// whose Inspector is its own section and then the Node section. It evaluates an animation graph
/// onto a target node (`target`, or its parent).
struct AnimationTreeNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimationTree";

    AnimationTreeComponentUVE tree{};
};

[[nodiscard]] bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value);

/// Makes `entity` a pure Node (EnsureNodeBaselineUVE) and adds the tree when it is missing.
void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value);

/// The loaded clip for a guid, or null when it is not set, not loaded yet or failed.
using AnimationClipResolverUVE = std::function<const Asset::AnimationClipAssetUVE*(Asset::AssetGuidUVE)>;

/// Advances the graph by `deltaSeconds` and writes the Output node's pose into `target` through the
/// channel masks. Node state lives in `tree.nodeStates` and is rebuilt (every node back to its
/// start) whenever the graph's shape changes. Triggers are consumed by the node or transition that
/// uses them. Returns true when `target` was written: an inactive tree, an invalid graph, or one
/// whose clips are all missing writes nothing.
[[nodiscard]] bool StepAnimationTreeUVE(AnimationTreeComponentUVE& tree, const AnimationClipResolverUVE& clips,
                                        float deltaSeconds, TransformComponentUVE& target);

/// Puts every node back to its start: clips at 0, state machines in their entry state.
void ResetAnimationTreeUVE(AnimationTreeComponentUVE& tree);

/// Sets a parameter by name. Returns false when the tree has none by that name.
[[nodiscard]] bool SetAnimationTreeParameterUVE(AnimationTreeComponentUVE& tree, std::string_view name, float value);

} // namespace UVE::Scene
