// Copyright (c) 2026 UniVex Studios. All Rights Reserved.

#pragma once

#include <string_view>

#include "uve/component/animation_tree_component_uve.h"
#include "uve/component/entity_uve.h"

namespace UVE::Asset {
struct AnimationClipAssetUVE;
} // namespace UVE::Asset

namespace UVE::Scene {

class IEntityManagerUVE;
struct TransformComponentUVE;

/// Authoring definition for the AnimationTree node: a pure Node - no transform, no visibility -
/// whose Inspector is its own section and then the Node section. It blends two clips on a target
/// node (`target`, or its parent) by a single Blend value.
struct AnimationTreeNodeDefinitionUVE final {
    static constexpr std::string_view defaultName = "AnimationTree";

    AnimationTreeComponentUVE tree{};
};

[[nodiscard]] bool IsAnimationTreeNodeDefinitionValidUVE(const AnimationTreeNodeDefinitionUVE& value) noexcept;

/// Makes `entity` a pure Node (EnsureNodeBaselineUVE) and adds the tree when it is missing.
void ApplyAnimationTreeNodeDefinitionUVE(IEntityManagerUVE& entityManager, EntityUVE entity,
                                         const AnimationTreeNodeDefinitionUVE& value);

/// Advances the tree by `deltaSeconds` and writes the blended pose into `target`. Either clip may be
/// null (not loaded, not set): the other then plays alone. Returns true when `target` was written;
/// an inactive tree, or one with neither clip usable, writes nothing.
[[nodiscard]] bool StepAnimationTreeUVE(AnimationTreeComponentUVE& tree, const Asset::AnimationClipAssetUVE* clipA,
                                        const Asset::AnimationClipAssetUVE* clipB, float deltaSeconds,
                                        TransformComponentUVE& target) noexcept;

} // namespace UVE::Scene
